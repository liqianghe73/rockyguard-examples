#pragma once

#include <QMainWindow>
#include <QUndoStack>

#include "core/document.h"
#include "ui2d/canvas.h"

class QLabel;
class QAction;
class QCloseEvent;

// The shell: toolbar, status bar, undo dock, and the split between the 2D editor
// and the 3D pane.
//
// The 3D pane is where the licensing shows up, and it is a SPLITTER pane rather
// than a hidden menu item on purpose. A locked feature the user cannot see is a
// feature they will never buy; a locked feature they can see, with the flag name
// and the file that gates it, is both honest and a sales surface.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

protected:
    // Without this, File>Exit and the window-manager close button discard
    // unsaved work silently, while File>New on identical state prompts.
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void newDrawing();
    void openDrawing();
    bool saveDrawing();
    bool saveDrawingAs();
    void showLicenseInfo();
    void updateStatus();

private:
    void buildActions();
    void buildToolBar();
    void buildStatusBar();
    void buildDocks();
    QWidget* buildRightPane();
    bool confirmDiscard();
    void refreshTitle();

    core::Document m_doc;
    QUndoStack m_undo;
    ui2d::Canvas* m_canvas = nullptr;
    QString m_path;

    QLabel* m_coords = nullptr;
    QLabel* m_measure = nullptr;
    QLabel* m_modes = nullptr;
    QLabel* m_counts = nullptr;
    QLabel* m_tier = nullptr;

    QAction* m_actSnap = nullptr;
    QAction* m_actOrtho = nullptr;
    QAction* m_actExportStl = nullptr;
};
