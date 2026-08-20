// The 3D pane. A software rasterizer on QPainter, not OpenGL.
//
// That choice is about buildability, not aesthetics. At screenshot size a
// flat-shaded prism with a dark edge overlay is indistinguishable from a GL
// render, and in exchange:
//   * no extra system packages -- no libgl1-mesa-dev, and none of the cryptic
//     "Could NOT find OpenGL (missing: OPENGL_glx_LIBRARY)" that costs an
//     evaluator twenty minutes;
//   * the CI smoke test is one env var (QT_QPA_PLATFORM=offscreen) rather than
//     xvfb plus LIBGL_ALWAYS_SOFTWARE=1;
//   * screenshots are deterministic, so the marketing PNGs can be generated and
//     pixel-diffed in CI, which is impossible with a GPU in the loop;
//   * there is no GL-context-creation failure mode at all, on any machine,
//     including over RDP and ThinLinc.
//
// The honest cost: no depth buffer. Triangles are painter's-algorithm sorted by
// depth, which is exact for a convex prism and can misorder interpenetrating
// geometry. For extruded profiles that is not a case we can reach.
//
// Escape hatch: swapping this for a QOpenGLWidget is a one-file change.
// Qt6::OpenGLWidgets is LGPLv3, so that is a scope decision, never a licence one.

#pragma once

#include <QVector>
#include <QWidget>

#include "../core/document.h"
#include "../core/mesh.h"

namespace ui3d {

class Viewport : public QWidget {
    Q_OBJECT

public:
    explicit Viewport(core::Document* doc, QWidget* parent = nullptr);

    // Rebuild the solid from the document's first closed loop. Cheap enough to
    // call on every document change; a profile has tens of points, not millions.
    void rebuild();

    const core::Mesh& mesh() const { return m_mesh; }
    bool hasMesh() const { return !m_mesh.isEmpty(); }
    QString lastError() const { return m_error; }
    // NOT named height(): that would shadow QWidget::height() and silently
    // feed the extrusion height into anything expecting widget pixels.
    double extrudeHeight() const { return m_height; }

public slots:
    void setHeight(double h);
    void resetView();
    void setShaded(bool on);

signals:
    // Carries the extrusion outcome so the status bar can report it without the
    // viewport needing to know a status bar exists.
    void statusChanged(QString text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    QSize sizeHint() const override { return QSize(460, 560); }

private:
    struct Camera {
        core::Vec3 target{0, 0, 0};
        double azimuth = 0.7;    // radians
        double elevation = 0.6;  // radians, clamped away from the poles
        double distance = 400.0;
    };

    core::Vec3 eye() const;
    // World -> camera space. z is depth along the view direction.
    core::Vec3 toCamera(const core::Vec3& w) const;
    // Camera space -> widget pixels. Returns false when behind the near plane.
    bool project(const core::Vec3& cam, QPointF* out) const;
    bool projectWorld(const core::Vec3& w, QPointF* out) const;

    void frameMesh();
    void paintEmptyState(QPainter& p) const;
    void paintGround(QPainter& p) const;
    void paintAxes(QPainter& p) const;
    void paintMesh(QPainter& p) const;

    core::Document* m_doc;
    core::Mesh m_mesh;
    QString m_error;

    Camera m_cam;
    double m_height = 40.0;
    bool m_shaded = true;

    QPoint m_dragFrom;
    bool m_orbiting = false;
    bool m_panning = false;
};

}  // namespace ui3d
