#include "mesh.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstring>  // std::memcpy for the STL header
#include <limits>

#include "document.h"  // polygonSignedArea, polygonSelfIntersects

namespace core {
namespace {

constexpr double kEps = 1e-9;

double cross2(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

// Strictly inside, edges excluded. An ear test that counts boundary points as
// inside rejects every valid ear on a polygon with collinear-ish vertices.
bool pointInTriangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) {
    const double d1 = cross2(a, b, p);
    const double d2 = cross2(b, c, p);
    const double d3 = cross2(c, a, p);
    return (d1 > kEps && d2 > kEps && d3 > kEps) || (d1 < -kEps && d2 < -kEps && d3 < -kEps);
}

}  // namespace

// --- small vector maths ------------------------------------------------------

Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 normalized(const Vec3& v) {
    const double len = std::sqrt(dot(v, v));
    if (len < kEps) return Vec3{0, 0, 1};
    return Vec3{v.x / len, v.y / len, v.z / len};
}

// --- Mesh --------------------------------------------------------------------

Vec3 Mesh::faceNormal(const Tri& t) const {
    if (t.a >= verts.size() || t.b >= verts.size() || t.c >= verts.size()) return Vec3{0, 0, 1};
    return normalized(cross(verts.at(t.b) - verts.at(t.a), verts.at(t.c) - verts.at(t.a)));
}

Vec3 Mesh::centroid() const {
    if (verts.isEmpty()) return Vec3{};
    Vec3 s{};
    for (const Vec3& v : verts) {
        s.x += v.x;
        s.y += v.y;
        s.z += v.z;
    }
    const double n = static_cast<double>(verts.size());
    return Vec3{s.x / n, s.y / n, s.z / n};
}

void Mesh::bounds(Vec3* lo, Vec3* hi) const {
    if (!lo || !hi) return;
    if (verts.isEmpty()) {
        *lo = *hi = Vec3{};
        return;
    }
    *lo = *hi = verts.at(0);
    for (const Vec3& v : verts) {
        lo->x = std::fmin(lo->x, v.x);
        lo->y = std::fmin(lo->y, v.y);
        lo->z = std::fmin(lo->z, v.z);
        hi->x = std::fmax(hi->x, v.x);
        hi->y = std::fmax(hi->y, v.y);
        hi->z = std::fmax(hi->z, v.z);
    }
}

// --- ear clipping ------------------------------------------------------------

QVector<Tri> earClip(const QVector<QPointF>& poly) {
    QVector<Tri> out;
    const int n = poly.size();
    if (n < 3) return out;

    // Work on an index list so the triangles returned refer to the CALLER's
    // vertex numbering. Returning positions instead would duplicate vertices and
    // break the shared-vertex assumption the renderer relies on.
    QVector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i) idx.append(i);

    int guard = 0;
    const int maxIterations = n * n + 16;  // O(n^2) worst case; bail rather than hang

    while (idx.size() > 3 && guard++ < maxIterations) {
        bool clipped = false;
        const int m = idx.size();
        for (int i = 0; i < m; ++i) {
            const int i0 = idx.at((i + m - 1) % m);
            const int i1 = idx.at(i);
            const int i2 = idx.at((i + 1) % m);
            const QPointF& a = poly.at(i0);
            const QPointF& b = poly.at(i1);
            const QPointF& c = poly.at(i2);

            // Convex corner? (CCW input, so a left turn.)
            if (cross2(a, b, c) <= kEps) continue;

            // No other vertex inside the candidate ear.
            bool contains = false;
            for (int j = 0; j < m && !contains; ++j) {
                const int ij = idx.at(j);
                if (ij == i0 || ij == i1 || ij == i2) continue;
                contains = pointInTriangle(poly.at(ij), a, b, c);
            }
            if (contains) continue;

            out.append(Tri{i0, i1, i2});
            idx.remove(i);
            clipped = true;
            break;
        }
        // No ear found on a full pass: the polygon is degenerate or
        // self-intersecting. Stop rather than spin, and let the caller's
        // self-intersection check produce the message.
        if (!clipped) break;
    }

    if (idx.size() == 3) out.append(Tri{idx.at(0), idx.at(1), idx.at(2)});
    return out;
}

// --- extrusion ---------------------------------------------------------------

ExtrudeResult extrudeProfile(const QVector<QPointF>& loop, double height) {
    ExtrudeResult r;

    if (loop.size() < 3) {
        r.error = QStringLiteral("An extrusion profile needs at least three points.");
        return r;
    }
    if (!(std::fabs(height) > kEps)) {
        r.error = QStringLiteral("Extrusion height must be non-zero.");
        return r;
    }
    if (polygonSelfIntersects(loop)) {
        // Checked BEFORE triangulating. Ear clipping does not fail on a
        // self-intersecting loop, it silently emits a wrong solid.
        r.error = QStringLiteral(
            "This profile crosses itself, so it does not enclose a single region. "
            "Redraw it without crossings.");
        return r;
    }

    // Normalise winding to counter-clockwise. The user drew a shape; which
    // direction they happened to go round it is not something they should have to
    // think about, and ear clipping needs CCW.
    QVector<QPointF> poly = loop;
    if (polygonSignedArea(poly) < 0.0) {
        std::reverse(poly.begin(), poly.end());
    }
    if (std::fabs(polygonSignedArea(poly)) < kEps) {
        r.error = QStringLiteral("This profile encloses no area.");
        return r;
    }

    const QVector<Tri> capTris = earClip(poly);
    if (capTris.isEmpty()) {
        r.error = QStringLiteral("Could not triangulate this profile.");
        return r;
    }

    const int n = poly.size();
    Mesh m;
    m.verts.reserve(n * 2);
    // Bottom ring 0..n-1 at z=0, top ring n..2n-1 at z=height.
    for (const QPointF& p : poly) m.verts.append(Vec3{p.x(), p.y(), 0.0});
    for (const QPointF& p : poly) m.verts.append(Vec3{p.x(), p.y(), height});

    // Caps. Every triangle is wound so its normal points OUT of the solid:
    // bottom reversed to face -Z, top as-is to face +Z. Get this wrong and
    // backface culling erases the faces you meant to see.
    for (const Tri& t : capTris) m.tris.append(Tri{t.a, t.c, t.b});
    for (const Tri& t : capTris) m.tris.append(Tri{t.a + n, t.b + n, t.c + n});

    // Sides, two triangles per edge, outward for a CCW profile.
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        m.tris.append(Tri{i, j, j + n});
        m.tris.append(Tri{i, j + n, i + n});
    }

    r.mesh = m;
    r.ok = true;
    return r;
}

// --- STL ---------------------------------------------------------------------

bool writeStlBinary(const QString& path, const Mesh& mesh, QString* error) {
    const auto fail = [error](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    if (mesh.tris.isEmpty()) return fail(QStringLiteral("Nothing to export: the mesh is empty."));

    // QSaveFile: a crash or full disk mid-write leaves any previous file intact
    // rather than truncated.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("Cannot write %1: %2").arg(path, f.errorString()));
    }

    QDataStream out(&f);
    // Binary STL is little-endian with IEEE-754 32-bit floats, always. Qt
    // defaults to big-endian and double precision, so both must be set or the
    // file is silently unreadable by every slicer.
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);

    char header[80] = {};
    const QByteArray tag = QByteArrayLiteral("rgcad binary STL");
    std::memcpy(header, tag.constData(), static_cast<size_t>(qMin(tag.size(), 79)));
    out.writeRawData(header, 80);
    out << static_cast<quint32>(mesh.tris.size());

    for (const Tri& t : mesh.tris) {
        const Vec3 nrm = mesh.faceNormal(t);
        const Vec3& a = mesh.verts.at(t.a);
        const Vec3& b = mesh.verts.at(t.b);
        const Vec3& c = mesh.verts.at(t.c);
        for (const Vec3& v : {nrm, a, b, c}) {
            out << static_cast<float>(v.x) << static_cast<float>(v.y) << static_cast<float>(v.z);
        }
        out << static_cast<quint16>(0);  // attribute byte count, unused
    }

    if (out.status() != QDataStream::Ok) {
        return fail(QStringLiteral("Write failed part-way through %1.").arg(path));
    }
    if (!f.commit()) {
        return fail(QStringLiteral("Cannot commit %1: %2").arg(path, f.errorString()));
    }
    return true;
}

}  // namespace core
