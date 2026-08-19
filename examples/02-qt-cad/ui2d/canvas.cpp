#include "canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <cmath>

#include "commands.h"

namespace ui2d {
namespace {

// Picking tolerance in PIXELS, converted to world units at the current zoom.
// Fixing it in world units instead would make picking harder the further you zoom
// in, which feels broken.
constexpr double kPickPixels = 7.0;
constexpr double kSnapPixels = 12.0;

// Not M_PI. M_PI is not standard C++, and on MSVC it only exists if
// _USE_MATH_DEFINES is defined before the FIRST include of <cmath> -- which is
// unachievable here, because Qt's own headers pull <cmath> in first. Ordering a
// #define correctly is also fragile: any future include reshuffle breaks it
// again, and only on Windows.
constexpr double kPi = 3.14159265358979323846;

const QColor kBackground(0x1e, 0x22, 0x28);
const QColor kGridMinor(0x2b, 0x31, 0x39);
const QColor kGridMajor(0x39, 0x41, 0x4c);
const QColor kAxis(0x5a, 0x6b, 0x7d);
const QColor kEntity(0xd6, 0xdd, 0xe6);
const QColor kEntityPro(0x8d, 0xc8, 0xf0);
const QColor kSelected(0xff, 0xb1, 0x4a);
const QColor kPreview(0x6f, 0xd3, 0x8c);
const QColor kSnapGeom(0xff, 0xb1, 0x4a);
const QColor kSnapGrid(0x7a, 0x8a, 0x9b);

double dist(const QPointF& a, const QPointF& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }

}  // namespace

Canvas::Canvas(core::Document* doc, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_undo(undo) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // needed for the live rubber preview
    setAutoFillBackground(false);
    setCursor(Qt::CrossCursor);
}

// --- transforms --------------------------------------------------------------

QPointF Canvas::worldToScreen(const QPointF& w) const {
    // Y is negated: world Y points up, screen Y points down.
    return QPointF(width() / 2.0 + (w.x() - m_centre.x()) * m_scale,
                   height() / 2.0 - (w.y() - m_centre.y()) * m_scale);
}

QPointF Canvas::screenToWorld(const QPointF& s) const {
    return QPointF(m_centre.x() + (s.x() - width() / 2.0) / m_scale,
                   m_centre.y() - (s.y() - height() / 2.0) / m_scale);
}

// --- grid --------------------------------------------------------------------

double Canvas::gridStep() const {
    // Adaptive: keep the minor spacing near 12 px by stepping through a 1-2-5
    // sequence, so the grid stays useful at every zoom instead of turning into
    // either a solid wash or a blank field.
    const double target = 12.0 / m_scale;
    const double mag = std::pow(10.0, std::floor(std::log10(std::fmax(target, 1e-9))));
    const double n = target / mag;
    double mult = 1.0;
    if (n > 5.0) {
        mult = 10.0;
    } else if (n > 2.0) {
        mult = 5.0;
    } else if (n > 1.0) {
        mult = 2.0;
    }
    return mag * mult;
}

// --- snapping ----------------------------------------------------------------

QPointF Canvas::snap(const QPointF& world, bool* onGeometry) const {
    if (onGeometry) *onGeometry = false;
    if (!m_snap) return world;

    // Geometry endpoints win over the grid: latching onto an existing vertex is
    // what makes closed loops possible, and a loop that is a pixel short is not
    // closed and will not extrude.
    const double tol = kSnapPixels / m_scale;
    double best = tol;
    QPointF bestPt = world;
    bool found = false;
    QVector<QPointF> candidates = m_doc->snapCandidates();
    // The in-progress run's OWN vertices must be candidates too. Without this,
    // closing a polyline worked only when the first vertex and the closing click
    // happened to quantise to the same grid point -- not because of endpoint
    // snapping at all.
    candidates += m_pending;
    for (const QPointF& c : candidates) {
        const double d = dist(world, c);
        if (d <= best) {
            best = d;
            bestPt = c;
            found = true;
        }
    }
    if (found) {
        if (onGeometry) *onGeometry = true;
        return bestPt;
    }

    const double step = gridStep();
    return QPointF(std::round(world.x() / step) * step, std::round(world.y() / step) * step);
}

QPointF Canvas::applyOrtho(const QPointF& from, const QPointF& to) const {
    if (!m_ortho) return to;
    const double dx = std::fabs(to.x() - from.x());
    const double dy = std::fabs(to.y() - from.y());
    return (dx >= dy) ? QPointF(to.x(), from.y()) : QPointF(from.x(), to.y());
}

// --- tools -------------------------------------------------------------------

void Canvas::setTool(Tool t) {
    if (m_tool == t) return;
    cancelPending();
    m_tool = t;
    m_selected = -1;
    emit selectionChanged(m_selected);
    emit toolChanged(t);
    update();
}

void Canvas::setSnapEnabled(bool on) {
    m_snap = on;
    update();
}

void Canvas::setOrthoEnabled(bool on) {
    m_ortho = on;
    update();
}

// The other half of the phantom-entity bug. m_selected used to survive an undo
// that removed the selected entity, so a following Delete built a DeleteEntity
// against an id no longer present -- which then injected a default entity whose
// saved file could never be reopened. Drop the selection the moment its entity
// stops existing.
void Canvas::onDocumentChanged() {
    if (m_selected >= 0 && !m_doc->find(m_selected)) {
        m_selected = -1;
        emit selectionChanged(m_selected);
    }
    emit documentChanged();
    update();
}

void Canvas::pushEntity(const core::Entity& e) {
    m_undo->push(new AddEntity(m_doc, e, [this] { onDocumentChanged(); }));
}

void Canvas::cancelPending() {
    if (m_pending.isEmpty()) return;
    m_pending.clear();
    emit measurementChanged(QString());
    update();
}

void Canvas::commitPending() {
    // Polyline is the only tool that commits on demand rather than on a fixed
    // point count -- Enter or a double-click ends an open run.
    if (m_tool == Tool::Polyline && m_pending.size() >= 2) {
        core::Entity e;
        e.kind = core::Kind::Polyline;
        e.pts = m_pending;
        e.closed = false;
        pushEntity(e);
    }
    cancelPending();
}

void Canvas::deleteSelected() {
    if (m_selected < 0) return;
    // Belt and braces alongside DeleteEntity's own m_valid guard: never build a
    // command against an entity that is already gone.
    if (!m_doc->find(m_selected)) {
        m_selected = -1;
        emit selectionChanged(m_selected);
        update();
        return;
    }
    const int id = m_selected;
    m_selected = -1;
    m_undo->push(new DeleteEntity(m_doc, id, [this] { onDocumentChanged(); }));
    emit selectionChanged(m_selected);
    update();
}

void Canvas::documentReset() {
    m_selected = -1;
    cancelPending();
    emit selectionChanged(m_selected);
    update();
}

void Canvas::zoomToFit() {
    if (m_doc->isEmpty()) {
        m_centre = QPointF(0, 0);
        m_scale = 2.0;
        update();
        return;
    }
    // Accumulate min/max by hand rather than leaning on QRectF. A QRectF built
    // from a single point has zero width and height, which makes isNull() TRUE,
    // and united() returns the other operand whenever either side is null -- so
    // the old accumulation never grew for a lines-only drawing and this function
    // silently did nothing. Opening such a file left the previous viewport.
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool have = false;
    const auto include = [&](double x, double y) {
        if (!have) {
            minX = maxX = x;
            minY = maxY = y;
            have = true;
            return;
        }
        minX = std::fmin(minX, x);
        maxX = std::fmax(maxX, x);
        minY = std::fmin(minY, y);
        maxY = std::fmax(maxY, y);
    };

    for (const core::Entity& e : m_doc->entities()) {
        if (e.kind == core::Kind::Circle && !e.pts.isEmpty()) {
            const QPointF c = e.pts.at(0);
            include(c.x() - e.radius, c.y() - e.radius);
            include(c.x() + e.radius, c.y() + e.radius);
        } else {
            for (const QPointF& p : e.pts) include(p.x(), p.y());
        }
    }
    if (!have) return;
    const QRectF box(QPointF(minX, minY), QPointF(maxX, maxY));
    // 12% margin so geometry never touches the frame edge.
    const double margin = 1.12;
    const double sx = width() / std::fmax(box.width() * margin, 1e-6);
    const double sy = height() / std::fmax(box.height() * margin, 1e-6);
    m_scale = std::fmax(0.02, std::fmin(std::fmin(sx, sy), 400.0));
    m_centre = box.center();
    update();
}

// --- events ------------------------------------------------------------------

void Canvas::mousePressEvent(QMouseEvent* ev) {
    const QPointF raw = screenToWorld(ev->position());

    if (ev->button() == Qt::MiddleButton) {
        // Middle-click recentres. Cheap panning without a drag state machine.
        m_centre = raw;
        update();
        return;
    }

    if (ev->button() == Qt::RightButton) {
        // Right-click ends a polyline, matching the convention in most CAD tools.
        if (!m_pending.isEmpty()) {
            commitPending();
        }
        return;
    }

    if (ev->button() != Qt::LeftButton) return;

    bool onGeom = false;
    const QPointF pSnapped = snap(raw, &onGeom);

    // Decide closure on the PRE-ortho point. Ortho used to be applied first,
    // which rewrote the closing click onto an axis and made
    // dist(p, first) the full off-axis offset -- so with Ortho on, a polyline
    // could never be closed except by luck, silently. Closing is the app's
    // headline gesture and the only way to author an extrudable profile.
    const bool closing = (m_tool == Tool::Polyline) && m_pending.size() >= 3 &&
                         dist(pSnapped, m_pending.first()) <= kSnapPixels / m_scale;

    QPointF p = pSnapped;
    if (!closing && !m_pending.isEmpty()) {
        const QPointF constrained = applyOrtho(m_pending.last(), pSnapped);
        // Ortho moved the point off the thing we latched onto, so the marker and
        // the [snap] badge would otherwise be lying about where we are.
        if (constrained != pSnapped) onGeom = false;
        p = constrained;
    }

    switch (m_tool) {
        case Tool::Select: {
            m_selected = m_doc->hitTest(raw, kPickPixels / m_scale);
            emit selectionChanged(m_selected);
            update();
            break;
        }

        case Tool::Line: {
            m_pending.append(p);
            if (m_pending.size() == 2) {
                core::Entity e;
                e.kind = core::Kind::Line;
                e.pts = m_pending;
                pushEntity(e);
                cancelPending();
            }
            update();
            break;
        }

        case Tool::Polyline: {
            // Clicking the first vertex again closes the loop -- `closing` was
            // computed above from the pre-ortho point.
            if (closing) {
                core::Entity e;
                e.kind = core::Kind::Polyline;
                e.pts = m_pending;
                e.closed = true;
                pushEntity(e);
                cancelPending();
            } else if (!m_pending.isEmpty() && dist(p, m_pending.last()) < 1e-9) {
                // Reject a coincident vertex. A zero-length edge makes
                // polygonSelfIntersects() blind and would break ear clipping when
                // the extruder lands.
            } else {
                m_pending.append(p);
            }
            update();
            break;
        }

        case Tool::Circle: {
            m_pending.append(p);
            if (m_pending.size() == 2) {
                const double r = dist(m_pending.at(0), m_pending.at(1));
                if (r > 1e-9) {
                    core::Entity e;
                    e.kind = core::Kind::Circle;
                    e.pts = {m_pending.at(0)};
                    e.radius = r;
                    pushEntity(e);
                }
                cancelPending();
            }
            update();
            break;
        }
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* ev) {
    const QPointF raw = screenToWorld(ev->position());
    bool onGeom = false;
    QPointF p = snap(raw, &onGeom);
    if (!m_pending.isEmpty()) {
        const QPointF constrained = applyOrtho(m_pending.last(), p);
        if (constrained != p) onGeom = false;  // do not claim a snap we moved off
        p = constrained;
    }

    m_cursorWorld = p;
    m_cursorOnGeometry = onGeom;
    m_haveCursor = true;
    emit cursorMoved(p, onGeom);

    // Live length/angle while drawing. This, the grid and the coordinate readout
    // are most of what makes the app read as CAD rather than as a paint program.
    if (!m_pending.isEmpty()) {
        const QPointF a = m_pending.last();
        const double len = dist(a, p);
        if (m_tool == Tool::Circle) {
            emit measurementChanged(QStringLiteral("R %1").arg(len, 0, 'f', 2));
        } else {
            const double angle = std::atan2(p.y() - a.y(), p.x() - a.x()) * 180.0 / kPi;
            emit measurementChanged(
                QStringLiteral("L %1  A %2 deg").arg(len, 0, 'f', 2).arg(angle, 0, 'f', 1));
        }
    }
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (m_tool == Tool::Polyline) {
        commitPending();
        return;
    }
    // Do NOT fall through to QWidget's default, which forwards to
    // mousePressEvent and would place a second vertex on top of the first.
    ev->accept();
}

void Canvas::leaveEvent(QEvent* ev) {
    // Otherwise the snap marker stays painted where the pointer left.
    m_haveCursor = false;
    update();
    QWidget::leaveEvent(ev);
}

void Canvas::wheelEvent(QWheelEvent* ev) {
    const double steps = ev->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    // Zoom about the cursor, not the widget centre, so the point under the mouse
    // stays put. Getting this wrong is the single most annoying thing a CAD view
    // can do.
    const QPointF anchorWorld = screenToWorld(ev->position());
    const double factor = std::pow(1.15, steps);
    m_scale = std::fmax(0.02, std::fmin(m_scale * factor, 400.0));
    const QPointF afterWorld = screenToWorld(ev->position());
    m_centre += anchorWorld - afterWorld;
    update();
}

void Canvas::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
        case Qt::Key_Escape:
            if (!m_pending.isEmpty()) {
                cancelPending();
            } else if (m_selected >= 0) {
                m_selected = -1;
                emit selectionChanged(m_selected);
                update();
            }
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitPending();
            return;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelected();
            return;
        // No F8/F9 here on purpose. The toolbar QActions hold those shortcuts, and
        // shortcut dispatch beats key delivery to the focus widget, so these cases
        // were dead -- and had they run they would have flipped the flag without
        // toggling the QAction, leaving the button and the SNAP/ORTHO badge stale.
        default:
            break;
    }
    QWidget::keyPressEvent(ev);
}

// --- painting ----------------------------------------------------------------

void Canvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), kBackground);
    paintGrid(p);
    paintEntities(p);
    paintPending(p);
    paintSnapMarker(p);
}

void Canvas::paintGrid(QPainter& p) const {
    const double step = gridStep();
    const QPointF tl = screenToWorld(QPointF(0, 0));
    const QPointF br = screenToWorld(QPointF(width(), height()));

    const double x0 = std::floor(tl.x() / step) * step;
    const double x1 = std::ceil(br.x() / step) * step;
    const double y0 = std::floor(br.y() / step) * step;
    const double y1 = std::ceil(tl.y() / step) * step;

    // Guard against a pathological zoom asking for a million lines.
    const int maxLines = 4000;
    if ((x1 - x0) / step > maxLines || (y1 - y0) / step > maxLines) return;

    QPen minor(kGridMinor, 1.0);
    QPen major(kGridMajor, 1.0);

    for (double x = x0; x <= x1; x += step) {
        // Every tenth line is emphasised, which is what gives the grid a sense of
        // scale without needing labels.
        const bool isMajor = std::fabs(std::fmod(x / step, 10.0)) < 1e-6;
        p.setPen(isMajor ? major : minor);
        const double sx = worldToScreen(QPointF(x, 0)).x();
        p.drawLine(QPointF(sx, 0), QPointF(sx, height()));
    }
    for (double y = y0; y <= y1; y += step) {
        const bool isMajor = std::fabs(std::fmod(y / step, 10.0)) < 1e-6;
        p.setPen(isMajor ? major : minor);
        const double sy = worldToScreen(QPointF(0, y)).y();
        p.drawLine(QPointF(0, sy), QPointF(width(), sy));
    }

    p.setPen(QPen(kAxis, 1.4));
    const QPointF origin = worldToScreen(QPointF(0, 0));
    p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
}

void Canvas::paintEntities(QPainter& p) const {
    for (const core::Entity& e : m_doc->entities()) {
        const bool sel = (e.id == m_selected);
        // Pro-authored geometry is tinted, so a Draft-tier session can SEE that it
        // is carrying data it cannot create. That is the honest way to present
        // "fail closed on capability, fail open on data".
        QColor c = e.proAuthored ? kEntityPro : kEntity;
        if (sel) c = kSelected;
        p.setPen(QPen(c, sel ? 2.4 : 1.6));
        p.setBrush(Qt::NoBrush);

        switch (e.kind) {
            case core::Kind::Line:
                if (e.pts.size() >= 2) {
                    p.drawLine(worldToScreen(e.pts.at(0)), worldToScreen(e.pts.at(1)));
                }
                break;
            case core::Kind::Polyline: {
                if (e.pts.size() < 2) break;
                QPolygonF poly;
                for (const QPointF& w : e.pts) poly << worldToScreen(w);
                if (e.closed) {
                    p.drawPolygon(poly);
                } else {
                    p.drawPolyline(poly);
                }
                break;
            }
            case core::Kind::Circle: {
                if (e.pts.isEmpty()) break;
                const QPointF c0 = worldToScreen(e.pts.at(0));
                const double r = e.radius * m_scale;
                p.drawEllipse(c0, r, r);
                break;
            }
        }
    }
}

void Canvas::paintPending(QPainter& p) const {
    if (m_pending.isEmpty()) return;
    p.setPen(QPen(kPreview, 1.6, Qt::DashLine));
    p.setBrush(Qt::NoBrush);

    if (m_tool == Tool::Circle) {
        const double r = m_haveCursor ? dist(m_pending.at(0), m_cursorWorld) : 0.0;
        p.drawEllipse(worldToScreen(m_pending.at(0)), r * m_scale, r * m_scale);
    } else {
        QPolygonF poly;
        for (const QPointF& w : m_pending) poly << worldToScreen(w);
        if (m_haveCursor) poly << worldToScreen(m_cursorWorld);
        p.drawPolyline(poly);
    }

    // Mark the placed vertices so it is obvious where the run has been.
    p.setPen(QPen(kPreview, 1.0));
    p.setBrush(kPreview);
    for (const QPointF& w : m_pending) {
        p.drawEllipse(worldToScreen(w), 2.5, 2.5);
    }
}

void Canvas::paintSnapMarker(QPainter& p) const {
    if (!m_haveCursor || !m_snap) return;
    const QPointF s = worldToScreen(m_cursorWorld);
    p.setBrush(Qt::NoBrush);
    if (m_cursorOnGeometry) {
        // A square for a geometry snap, a small cross for the grid. Distinct
        // glyphs, because "am I actually latched onto that vertex" is the question
        // a drafter asks constantly.
        p.setPen(QPen(kSnapGeom, 1.8));
        p.drawRect(QRectF(s.x() - 5, s.y() - 5, 10, 10));
    } else {
        p.setPen(QPen(kSnapGrid, 1.2));
        p.drawLine(QPointF(s.x() - 4, s.y()), QPointF(s.x() + 4, s.y()));
        p.drawLine(QPointF(s.x(), s.y() - 4), QPointF(s.x(), s.y() + 4));
    }
}

}  // namespace ui2d
