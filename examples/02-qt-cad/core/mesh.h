// Extrusion: a closed 2D profile becomes a solid prism.
//
// QtCore only, like the rest of core/. No GUI, no licensing, no RockyGuard. The
// whole thing is testable headless, which matters because the interesting bugs
// here are geometric rather than visual -- a wrong winding order looks fine until
// you notice the solid is inside out.
//
// Deliberately NOT using a geometry kernel. OpenCASCADE would do this properly
// and correctly, and it is a 500 MB+ build that would dwarf the thing it is
// demonstrating. Ear clipping in 150 lines is the right trade for an example.

#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace core {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator-(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
double dot(const Vec3& a, const Vec3& b);
Vec3 normalized(const Vec3& v);

struct Tri {
    int a = 0;
    int b = 0;
    int c = 0;
};

struct Mesh {
    QVector<Vec3> verts;
    QVector<Tri> tris;

    bool isEmpty() const { return tris.isEmpty(); }
    Vec3 faceNormal(const Tri& t) const;
    Vec3 centroid() const;
    // Axis-aligned bounds, for framing the camera.
    void bounds(Vec3* lo, Vec3* hi) const;
};

// Why a result type rather than an empty Mesh on failure: "this loop crosses
// itself" and "this loop has fewer than three points" want different messages,
// and ear clipping on a self-intersecting loop produces confident garbage rather
// than an error. One wrong-looking 3D screenshot is worse for this repo than no
// 3D at all.
struct ExtrudeResult {
    Mesh mesh;
    bool ok = false;
    QString error;
    explicit operator bool() const { return ok; }
};

// Extrude a closed profile along +Z by `height`.
//
// Winding is normalised internally, so a clockwise profile extrudes to the same
// solid as a counter-clockwise one -- the user drew a shape, not a winding order,
// and making them care would be a bug rather than a lesson.
ExtrudeResult extrudeProfile(const QVector<QPointF>& loop, double height);

// Triangulate a simple polygon by ear clipping. Exposed because it is the part
// worth unit-testing on its own. Expects counter-clockwise input; returns
// triangles as indices into `poly`.
QVector<Tri> earClip(const QVector<QPointF>& poly);

// Binary STL. Gated behind cad_stl_export in the app -- see licensing/gate.h.
// Binary rather than ASCII because every slicer and mesh tool reads it, it is a
// tenth the size, and there is no float-formatting ambiguity to get wrong.
bool writeStlBinary(const QString& path, const Mesh& mesh, QString* error);

}  // namespace core
