// Geometry tests for extrusion. No Qt GUI, no SDK, no display -- core/ is
// QtCore-only precisely so this can run anywhere, including on a fork PR with no
// licence secret available.
//
// These are the tests worth having, because the bugs in this code are invisible
// rather than loud: a reversed winding order produces a solid that renders inside
// out, and ear clipping on a self-intersecting loop emits confident garbage rather
// than an error.

#include <QPointF>
#include <QVector>

#include <cmath>
#include <cstdio>

#include "../examples/02-qt-cad/core/document.h"
#include "../examples/02-qt-cad/core/mesh.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

QVector<QPointF> square(double w, double h) {
    return {QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h)};
}

// An outward normal must point AWAY from the interior. Test it by stepping from a
// face's centroid along its normal and confirming that leaves the solid's bounds.
bool normalsPointOutward(const core::Mesh& m) {
    core::Vec3 lo, hi;
    m.bounds(&lo, &hi);
    const core::Vec3 mid{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    for (const core::Tri& t : m.tris) {
        const core::Vec3& a = m.verts.at(t.a);
        const core::Vec3& b = m.verts.at(t.b);
        const core::Vec3& c = m.verts.at(t.c);
        const core::Vec3 fc{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        const core::Vec3 n = m.faceNormal(t);
        // Vector from the solid's centre to this face should agree with the normal.
        if (core::dot(n, fc - mid) <= 0.0) return false;
    }
    return true;
}

}  // namespace

int main() {
    std::printf("mesh_tests\n");

    // --- a square extrudes to a box -----------------------------------------
    {
        const core::ExtrudeResult r = core::extrudeProfile(square(60, 40), 25);
        check(static_cast<bool>(r), "square extrudes");
        // 4-point profile: 2 cap triangles x 2 caps + 2 per side x 4 sides = 12.
        check(r.mesh.tris.size() == 12, "box has 12 triangles");
        check(r.mesh.verts.size() == 8, "box has 8 vertices");
        check(normalsPointOutward(r.mesh), "every face normal points outward");

        core::Vec3 lo, hi;
        r.mesh.bounds(&lo, &hi);
        check(std::fabs(hi.z - lo.z - 25.0) < 1e-9, "height matches the requested 25");
        check(std::fabs(hi.x - lo.x - 60.0) < 1e-9, "width preserved");
    }

    // --- winding must not matter to the user --------------------------------
    {
        QVector<QPointF> ccw = square(60, 40);
        QVector<QPointF> cw = ccw;
        std::reverse(cw.begin(), cw.end());
        const core::ExtrudeResult a = core::extrudeProfile(ccw, 25);
        const core::ExtrudeResult b = core::extrudeProfile(cw, 25);
        check(static_cast<bool>(a) && static_cast<bool>(b), "both windings extrude");
        check(a.mesh.tris.size() == b.mesh.tris.size(), "same triangle count either way");
        // The real assertion: a clockwise profile must not produce an inside-out
        // solid. The user drew a shape, not a winding order.
        check(normalsPointOutward(b.mesh), "clockwise profile is NOT inside out");
    }

    // --- self-intersection is refused, not mis-meshed -----------------------
    {
        const QVector<QPointF> bowtie = {QPointF(0, 0), QPointF(60, 40), QPointF(60, 0),
                                         QPointF(0, 40)};
        check(core::polygonSelfIntersects(bowtie), "bowtie detected as self-intersecting");
        const core::ExtrudeResult r = core::extrudeProfile(bowtie, 25);
        check(!static_cast<bool>(r), "bowtie extrusion REFUSED");
        check(r.mesh.isEmpty(), "no partial mesh left behind on refusal");
        check(!r.error.isEmpty(), "refusal carries a message");
    }

    // --- degenerate input ---------------------------------------------------
    {
        check(!core::extrudeProfile({QPointF(0, 0), QPointF(1, 1)}, 10).ok, "2 points refused");
        check(!core::extrudeProfile(square(60, 40), 0.0).ok, "zero height refused");
        const QVector<QPointF> collinear = {QPointF(0, 0), QPointF(10, 0), QPointF(20, 0)};
        check(!core::extrudeProfile(collinear, 10).ok, "zero-area profile refused");
    }

    // --- an L-shape: concave, so ear clipping actually has to work -----------
    {
        const QVector<QPointF> ell = {QPointF(0, 0),  QPointF(60, 0),  QPointF(60, 20),
                                      QPointF(20, 20), QPointF(20, 60), QPointF(0, 60)};
        const core::ExtrudeResult r = core::extrudeProfile(ell, 15);
        check(static_cast<bool>(r), "concave L-shape extrudes");
        // n=6: ear clipping yields n-2 = 4 triangles per cap, x2 caps, + 2x6 sides.
        check(r.mesh.tris.size() == 4 * 2 + 12, "L-shape has 20 triangles");
        check(core::earClip(ell).size() == 4, "ear clip yields n-2 triangles");
    }

    // --- ear clipping never emits a degenerate index ------------------------
    {
        const QVector<QPointF> ell = {QPointF(0, 0),  QPointF(60, 0),  QPointF(60, 20),
                                      QPointF(20, 20), QPointF(20, 60), QPointF(0, 60)};
        bool sane = true;
        for (const core::Tri& t : core::earClip(ell)) {
            if (t.a == t.b || t.b == t.c || t.a == t.c) sane = false;
            if (t.a < 0 || t.a >= ell.size()) sane = false;
        }
        check(sane, "no duplicate or out-of-range indices");
    }

    std::printf("%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
