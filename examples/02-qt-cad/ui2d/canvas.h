// The 2D drafting view.
//
// A plain QWidget with QPainter rather than QGraphicsView. For three entity kinds
// and a few hundred entities, a scene graph is more machinery than the job needs:
// we would still hand-write the grid, the snap markers, the rubber preview and the
// hit-testing, and we would additionally fight the scene's coordinate system and
// item lifetimes. One paintEvent is easier to read, which matters more here than
// scaling to a hundred thousand items.
//
// World units are millimetres. Y points UP in world space, as a drafter expects,
// and the view flips it -- see worldToScreen.

#pragma once

#include <QPointF>
#include <QSize>
#include <QUndoStack>
#include <QWidget>

#include "../core/document.h"

namespace ui2d {

enum class Tool { Select, Line, Polyline, Circle };

class Canvas : public QWidget {
    Q_OBJECT

public:
    explicit Canvas(core::Document* doc, QUndoStack* undo, QWidget* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return m_tool; }

    bool snapEnabled() const { return m_snap; }
    bool orthoEnabled() const { return m_ortho; }
    int selectedId() const { return m_selected; }

    void deleteSelected();
    void zoomToFit();
    // Called when the document changed underneath us (load, undo, redo).
    void documentReset();

public slots:
    void setSnapEnabled(bool on);
    void setOrthoEnabled(bool on);

signals:
    // World coordinates under the cursor, plus the live measurement while
    // drawing. The status bar shows these; a CAD tool without a coordinate
    // readout does not read as a CAD tool.
    void cursorMoved(QPointF world, bool snapped);
    void measurementChanged(QString text);
    void selectionChanged(int id);
    void documentChanged();
    void toolChanged(Tool t);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    QSize sizeHint() const override { return QSize(720, 560); }

private:
    // --- transforms
    QPointF worldToScreen(const QPointF& w) const;
    QPointF screenToWorld(const QPointF& s) const;
    double pixelsPerUnit() const { return m_scale; }

    // --- snapping
    // Returns the snapped point, and reports whether it latched onto geometry (as
    // opposed to the grid) so the UI can draw a different marker.
    QPointF snap(const QPointF& world, bool* onGeometry) const;
    QPointF applyOrtho(const QPointF& from, const QPointF& to) const;
    double gridStep() const;

    // --- drawing state machine
    // Single place that reacts to the document changing under us, so a stale
    // selection cannot survive an undo. Used as the callback in every command.
    void onDocumentChanged();
    void commitPending();
    void cancelPending();
    void pushEntity(const core::Entity& e);

    // --- painting helpers
    void paintGrid(QPainter& p) const;
    void paintEntities(QPainter& p) const;
    void paintPending(QPainter& p) const;
    void paintSnapMarker(QPainter& p) const;

    core::Document* m_doc;
    QUndoStack* m_undo;

    Tool m_tool = Tool::Select;
    bool m_snap = true;
    bool m_ortho = false;

    // View: world point at the widget centre, plus pixels-per-unit.
    QPointF m_centre{0, 0};
    double m_scale = 2.0;

    // In-progress geometry. Empty when not drawing.
    QVector<QPointF> m_pending;
    QPointF m_cursorWorld{0, 0};
    bool m_cursorOnGeometry = false;
    bool m_haveCursor = false;

    int m_selected = -1;
};

}  // namespace ui2d
