#include "viewport.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ui3d {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNear = 1.0;
// Vertical field of view. 35 degrees reads as a mild telephoto, which flatters
// mechanical shapes; a wide angle makes a small prism look like a fisheye shot.
constexpr double kFovY = 35.0 * kPi / 180.0;

const QColor kBg(0x17, 0x1a, 0x1f);
const QColor kGround(0x25, 0x2b, 0x33);
const QColor kGroundAxis(0x3a, 0x44, 0x50);
const QColor kEdge(0x10, 0x13, 0x17);
const QColor kFaceLit(0x7f, 0xb2, 0xe0);
const QColor kFaceDark(0x22, 0x33, 0x45);
const QColor kWire(0x8d, 0xc8, 0xf0);
const QColor kHint(0x76, 0x84, 0x94);
const QColor kAxisX(0xe0, 0x6c, 0x6c);
const QColor kAxisY(0x7d, 0xd0, 0x87);
const QColor kAxisZ(0x6f, 0xa8, 0xe8);

// A single directional light, from over the viewer's left shoulder. Fixed in
// WORLD space rather than camera space so orbiting reveals shape instead of
// dragging the highlight around with you.
const core::Vec3 kLight = core::normalized(core::Vec3{-0.45, -0.65, 0.62});

}  // namespace

Viewport::Viewport(core::Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
    rebuild();
}

// --- camera ------------------------------------------------------------------

core::Vec3 Viewport::eye() const {
    const double ce = std::cos(m_cam.elevation);
    return core::Vec3{m_cam.target.x + m_cam.distance * ce * std::cos(m_cam.azimuth),
                      m_cam.target.y + m_cam.distance * ce * std::sin(m_cam.azimuth),
                      m_cam.target.z + m_cam.distance * std::sin(m_cam.elevation)};
}

core::Vec3 Viewport::toCamera(const core::Vec3& w) const {
    const core::Vec3 e = eye();
    const core::Vec3 fwd = core::normalized(m_cam.target - e);
    // World Z is up. Near the poles fwd and up go parallel and the cross product
    // collapses, which is why elevation is clamped in the event handlers rather
    // than special-cased here.
    const core::Vec3 worldUp{0, 0, 1};
    const core::Vec3 right = core::normalized(core::cross(fwd, worldUp));
    const core::Vec3 up = core::cross(right, fwd);
    const core::Vec3 d = w - e;
    return core::Vec3{core::dot(d, right), core::dot(d, up), core::dot(d, fwd)};
}

bool Viewport::project(const core::Vec3& cam, QPointF* out) const {
    if (cam.z < kNear) return false;  // behind the eye, or in the near plane
    // QWidget::height() explicitly: the focal length is in PIXELS.
    const double px = QWidget::height() > 0 ? static_cast<double>(QWidget::height()) : 1.0;
    const double f = px / (2.0 * std::tan(kFovY * 0.5));
    out->setX(width() * 0.5 + cam.x * f / cam.z);
    // Screen Y grows downward; camera Y grows up.
    out->setY(QWidget::height() * 0.5 - cam.y * f / cam.z);
    return true;
}

bool Viewport::projectWorld(const core::Vec3& w, QPointF* out) const {
    return project(toCamera(w), out);
}

// --- state -------------------------------------------------------------------

void Viewport::rebuild() {
    m_error.clear();
    const QVector<QPointF> loop = m_doc->firstClosedLoop();
    if (loop.isEmpty()) {
        m_mesh = core::Mesh{};
        emit statusChanged(QString());
        update();
        return;
    }

    const core::ExtrudeResult r = core::extrudeProfile(loop, m_height);
    if (!r) {
        m_mesh = core::Mesh{};
        m_error = r.error;
        emit statusChanged(r.error);
        update();
        return;
    }

    const bool firstSolid = m_mesh.isEmpty();
    m_mesh = r.mesh;
    if (firstSolid) frameMesh();  // only auto-frame when a solid first appears
    emit statusChanged(QStringLiteral("Extruded %1 triangles, height %2")
                           .arg(m_mesh.tris.size())
                           .arg(m_height, 0, 'f', 1));
    update();
}

void Viewport::frameMesh() {
    if (m_mesh.isEmpty()) return;
    core::Vec3 lo, hi;
    m_mesh.bounds(&lo, &hi);
    m_cam.target = core::Vec3{(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5, (lo.z + hi.z) * 0.5};
    const double span = std::fmax(std::fmax(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
    // 2.2x the span keeps the solid comfortably inside the frame at this FOV.
    m_cam.distance = std::fmax(span * 2.2, 20.0);
}

void Viewport::setHeight(double h) {
    const double clamped = std::fmax(0.1, std::fmin(h, 100000.0));
    if (std::fabs(clamped - m_height) < 1e-9) return;
    m_height = clamped;
    rebuild();
}

void Viewport::resetView() {
    m_cam.azimuth = 0.7;
    m_cam.elevation = 0.6;
    frameMesh();
    update();
}

void Viewport::setShaded(bool on) {
    m_shaded = on;
    update();
}

// --- events ------------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent* ev) {
    m_dragFrom = ev->pos();
    if (ev->button() == Qt::LeftButton) {
        m_orbiting = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (ev->button() == Qt::MiddleButton) {
        m_panning = true;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* ev) {
    const QPoint d = ev->pos() - m_dragFrom;
    m_dragFrom = ev->pos();

    if (m_orbiting) {
        m_cam.azimuth -= d.x() * 0.01;
        m_cam.elevation += d.y() * 0.01;
        // Clamp short of the poles. At exactly +/-90 degrees the view direction
        // is parallel to world up, cross() degenerates, and the whole basis
        // collapses -- the picture would snap to nothing.
        const double lim = kPi * 0.5 - 0.02;
        m_cam.elevation = std::fmax(-lim, std::fmin(m_cam.elevation, lim));
        update();
    } else if (m_panning) {
        // Pan in the camera's own plane, scaled by distance so the solid tracks
        // the pointer at any zoom.
        const core::Vec3 e = eye();
        const core::Vec3 fwd = core::normalized(m_cam.target - e);
        const core::Vec3 right = core::normalized(core::cross(fwd, core::Vec3{0, 0, 1}));
        const core::Vec3 up = core::cross(right, fwd);
        const double k = m_cam.distance * 0.002;
        m_cam.target.x -= (right.x * d.x() - up.x * d.y()) * k;
        m_cam.target.y -= (right.y * d.x() - up.y * d.y()) * k;
        m_cam.target.z -= (right.z * d.x() - up.z * d.y()) * k;
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent*) {
    m_orbiting = false;
    m_panning = false;
    setCursor(Qt::OpenHandCursor);
}

void Viewport::wheelEvent(QWheelEvent* ev) {
    const double steps = ev->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    m_cam.distance = std::fmax(5.0, std::fmin(m_cam.distance * std::pow(0.88, steps), 500000.0));
    update();
}

void Viewport::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
        case Qt::Key_BracketRight: setHeight(m_height * 1.25); return;
        case Qt::Key_BracketLeft: setHeight(m_height / 1.25); return;
        case Qt::Key_W: setShaded(!m_shaded); return;
        case Qt::Key_R: resetView(); return;
        default: break;
    }
    QWidget::keyPressEvent(ev);
}

// --- painting ----------------------------------------------------------------

void Viewport::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), kBg);

    paintGround(p);
    paintAxes(p);

    if (m_mesh.isEmpty()) {
        paintEmptyState(p);
        return;
    }
    paintMesh(p);

    p.setPen(kHint);
    p.drawText(10, QWidget::height() - 10,
               QStringLiteral("height %1  [ ]  resize   W wireframe   R reset   drag orbit")
                   .arg(m_height, 0, 'f', 1));
}

void Viewport::paintEmptyState(QPainter& p) const {
    p.setPen(m_error.isEmpty() ? kHint : QColor(0xff, 0x8a, 0x7a));
    const QString msg =
        m_error.isEmpty()
            ? QStringLiteral("Draw a CLOSED polyline in the 2D view, then it extrudes here.\n\n"
                             "Polyline tool, click a few points, then click the first point again "
                             "to close.")
            : m_error;
    p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
               msg);
}

void Viewport::paintGround(QPainter& p) const {
    // A ground plane at z=0 does most of the work of conveying "this is 3D".
    // Without it an orbiting solid reads as a rotating flat shape.
    const double step = 20.0;
    const int n = 10;
    const double ext = step * n;
    p.setPen(QPen(kGround, 1.0));
    for (int i = -n; i <= n; ++i) {
        const double t = i * step;
        QPointF a, b;
        if (projectWorld(core::Vec3{t, -ext, 0}, &a) && projectWorld(core::Vec3{t, ext, 0}, &b)) {
            p.setPen(QPen(i == 0 ? kGroundAxis : kGround, i == 0 ? 1.4 : 1.0));
            p.drawLine(a, b);
        }
        if (projectWorld(core::Vec3{-ext, t, 0}, &a) && projectWorld(core::Vec3{ext, t, 0}, &b)) {
            p.setPen(QPen(i == 0 ? kGroundAxis : kGround, i == 0 ? 1.4 : 1.0));
            p.drawLine(a, b);
        }
    }
}

void Viewport::paintAxes(QPainter& p) const {
    const double len = 26.0;
    const struct {
        core::Vec3 dir;
        QColor col;
        const char* label;
    } axes[] = {
        {{len, 0, 0}, kAxisX, "X"},
        {{0, len, 0}, kAxisY, "Y"},
        {{0, 0, len}, kAxisZ, "Z"},
    };
    QPointF o;
    if (!projectWorld(core::Vec3{0, 0, 0}, &o)) return;
    for (const auto& a : axes) {
        QPointF tip;
        if (!projectWorld(a.dir, &tip)) continue;
        p.setPen(QPen(a.col, 2.0));
        p.drawLine(o, tip);
        p.drawText(tip + QPointF(4, -4), QString::fromLatin1(a.label));
    }
}

void Viewport::paintMesh(QPainter& p) const {
    struct Face {
        QPolygonF poly;
        double depth = 0.0;
        double shade = 0.0;
    };

    QVector<Face> faces;
    faces.reserve(m_mesh.tris.size());

    for (const core::Tri& t : m_mesh.tris) {
        const core::Vec3 nrm = m_mesh.faceNormal(t);
        const core::Vec3 ca = toCamera(m_mesh.verts.at(t.a));
        const core::Vec3 cb = toCamera(m_mesh.verts.at(t.b));
        const core::Vec3 cc = toCamera(m_mesh.verts.at(t.c));

        // Backface cull in CAMERA space: a face whose outward normal points away
        // from the eye is interior and must not be drawn, or the far side of the
        // solid paints over the near side.
        const core::Vec3 toEye = core::normalized(core::Vec3{-ca.x, -ca.y, -ca.z});
        const core::Vec3 camNrm = core::normalized(core::cross(cb - ca, cc - ca));
        if (core::dot(camNrm, toEye) <= 0.0) continue;

        QPointF pa, pb, pc;
        if (!project(ca, &pa) || !project(cb, &pb) || !project(cc, &pc)) continue;

        Face f;
        f.poly << pa << pb << pc;
        // Sort key is the FARTHEST vertex: painter's algorithm draws back to
        // front. Exact for a convex prism, which is all extrusion produces.
        f.depth = std::fmax(std::fmax(ca.z, cb.z), cc.z);
        // Flat Lambert with a generous ambient term, so faces turned away from
        // the light stay readable instead of going black.
        f.shade = 0.32 + 0.68 * std::fmax(0.0, core::dot(nrm, kLight));
        faces.append(f);
    }

    std::sort(faces.begin(), faces.end(),
              [](const Face& a, const Face& b) { return a.depth > b.depth; });

    for (const Face& f : faces) {
        if (m_shaded) {
            const QColor c(
                static_cast<int>(kFaceDark.red() + (kFaceLit.red() - kFaceDark.red()) * f.shade),
                static_cast<int>(kFaceDark.green() + (kFaceLit.green() - kFaceDark.green()) * f.shade),
                static_cast<int>(kFaceDark.blue() + (kFaceLit.blue() - kFaceDark.blue()) * f.shade));
            p.setBrush(c);
            // A hairline edge in the face colour's shadow reads as a crease and
            // hides the seams between adjacent triangles of one flat face.
            p.setPen(QPen(kEdge, 0.8));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWire, 1.0));
        }
        p.drawPolygon(f.poly);
    }
}

}  // namespace ui3d
