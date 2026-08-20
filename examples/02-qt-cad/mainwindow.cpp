#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoView>
#include <QVBoxLayout>

#include <cstdio>

#include "core/mesh.h"
#include "licensing/gate.h"

namespace {

// The two tier names. Draft is a PRODUCT, not an error state -- the wording
// everywhere in this app is chosen to reflect that.
const char* kDraft = "Draft";
const char* kPro = "Pro";

QString tierName() { return lic::has(lic::k3D) ? QString(kPro) : QString(kDraft); }

// addAction(text, shortcut, receiver, method) was introduced in Qt 6.3. The
// floor here is 6.2 (Ubuntu 22.04 ships 6.2.4, and that is the required CI job),
// so build actions the long way. This also sidesteps the 6.4 deprecation of the
// old shortcut-last overloads: text, then setShortcut, then connect compiles
// unchanged on every Qt 6.
template <typename Receiver, typename Method>
QAction* addAct(QMenu* menu, const QString& text, const QKeySequence& shortcut,
                Receiver* receiver, Method method) {
    QAction* a = menu->addAction(text);
    if (!shortcut.isEmpty()) a->setShortcut(shortcut);
    QObject::connect(a, &QAction::triggered, receiver, method);
    return a;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("rgcad"));

    m_canvas = new ui2d::Canvas(&m_doc, &m_undo, this);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_canvas);
    split->addWidget(buildRightPane());
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);

    buildActions();
    buildToolBar();
    buildStatusBar();
    buildDocks();

    connect(m_canvas, &ui2d::Canvas::cursorMoved, this, [this](QPointF w, bool snapped) {
        m_coords->setText(QStringLiteral("X %1  Y %2%3")
                              .arg(w.x(), 0, 'f', 2)
                              .arg(w.y(), 0, 'f', 2)
                              .arg(snapped ? QStringLiteral("  [snap]") : QString()));
    });
    connect(m_canvas, &ui2d::Canvas::measurementChanged, this,
            [this](const QString& t) { m_measure->setText(t); });
    connect(m_canvas, &ui2d::Canvas::documentChanged, this, &MainWindow::updateStatus);
    connect(m_canvas, &ui2d::Canvas::documentChanged, this, &MainWindow::rebuild3D);
    connect(m_canvas, &ui2d::Canvas::selectionChanged, this, &MainWindow::updateStatus);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &MainWindow::refreshTitle);

    // The viewport reports extrusion outcomes -- triangle counts, and the
    // self-intersection refusal -- straight into the status bar.
    if (m_viewport) {
        connect(m_viewport, &ui3d::Viewport::statusChanged, this,
                [this](const QString& t) { m_solid->setText(t); });
        m_viewport->rebuild();
    }

    updateStatus();
    refreshTitle();
    resize(1180, 760);
}

MainWindow::~MainWindow() {
    // ~QUndoStack calls clear(), and clear() on a DIRTY stack emits
    // cleanChanged(true). The connection to refreshTitle() is still live at that
    // point (~QObject for MainWindow has not run yet), and refreshTitle() reads
    // m_path -- which is destroyed BEFORE m_undo, since members die in reverse
    // declaration order. Repro: draw one line, close the window.
    m_undo.blockSignals(true);
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    if (confirmDiscard()) {
        ev->accept();
    } else {
        ev->ignore();
    }
}

// --- right-hand pane: where licensing becomes visible ------------------------

QWidget* MainWindow::buildRightPane() {
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(18, 18, 18, 18);

    const lic::Status& s = lic::status();
    const bool unlocked = lic::has(lic::k3D);

    auto* title = new QLabel(pane);
    title->setTextFormat(Qt::RichText);
    title->setWordWrap(true);

    if (unlocked) {
        // Construct the viewport ONLY here. Building it and hiding it would leave
        // a working 3D view one setVisible() away, which is not a gate.
        m_viewport = new ui3d::Viewport(&m_doc, pane);
        title->setText(QStringLiteral(
            "<b>3D &mdash; unlocked</b> &nbsp; <span style='color:#7a8a9b'>"
            "cad_3d present. Drag to orbit, wheel to zoom, <code>[</code> / "
            "<code>]</code> height, <code>W</code> wireframe, <code>R</code> reset."
            "</span>"));
        layout->addWidget(title);
        layout->addWidget(m_viewport, 1);
        // The layout is finished for the unlocked case: the viewport IS the pane.
        layout->setContentsMargins(8, 8, 8, 8);
        return pane;
    } else {
        // Name the flag AND the file. A developer evaluating RockyGuard wants to
        // know exactly where the decision is made, and telling them is more
        // persuasive than hiding it.
        title->setText(QStringLiteral(
            "<h2>3D &mdash; Pro feature</h2>"
            "<p>You are on the <b>Draft</b> tier: 2D drafting, unlimited, no "
            "time limit. 3D extrusion requires the <code>cad_3d</code> feature.</p>"
            "<p>The decision is made in one place: "
            "<code>licensing/gate.cpp</code>.</p>"));
    }
    layout->addWidget(title);

    auto* detail = new QLabel(pane);
    detail->setWordWrap(true);
    detail->setTextFormat(Qt::RichText);

    QString body;
    switch (s.state) {
        case lic::State::Valid:
            body = QStringLiteral("<p><b>License:</b> %1<br><b>Expires:</b> %2</p>")
                       .arg(s.licensee.empty() ? QStringLiteral("(unnamed)")
                                               : QString::fromStdString(s.licensee),
                            QString::fromStdString(s.expires));
            if (s.inGracePeriod) {
                body += QStringLiteral(
                            "<p style='color:#ffb14a'><b>In grace period</b> &mdash; "
                            "%1 day(s) remaining. Your work is not at risk; renew "
                            "when convenient.</p>")
                            .arg(s.graceDaysRemaining);
            }
            break;

        case lic::State::NoLicense:
            if (s.stub) {
                // Say what is actually true: this binary has no SDK, so it never
                // even opened the file. Claiming "no license file found" over a
                // file the user can see with ls is the fastest way to lose their
                // trust in everything else the pane says.
                body = QStringLiteral(
                           "<p><b>This binary cannot verify licenses.</b> It was built "
                           "against the RockyGuard API stub, not the SDK, so no license "
                           "file is ever opened and Pro is unreachable by "
                           "construction.</p>"
                           "<p>License path: <code>%1</code><br>"
                           "A file %2 there.</p>"
                           "<p>Rebuild against the SDK:<br>"
                           "<code>cmake -B build -S . "
                           "-DROCKYGUARD_ROOT=/path/to/customer-bundle</code></p>")
                           .arg(QString::fromStdString(lic::licensePath()),
                                s.filePresent ? QStringLiteral("<b>is</b>")
                                              : QStringLiteral("is <b>not</b>"));
            } else if (s.keyError) {
                // Our bug, not theirs. Say so plainly rather than reporting
                // "unlicensed", which would send them hunting for a license file
                // that was never the problem.
                body = QStringLiteral(
                           "<p style='color:#ff6b6b'><b>Build error:</b> %1</p>"
                           "<p>This is a problem with this binary, not with your "
                           "license.</p>")
                           .arg(QString::fromStdString(s.message));
            } else {
                body = QStringLiteral(
                           "<p>No license file found at "
                           "<code>%1</code>.</p>"
                           "<p>Try one of the samples:<br>"
                           "<code>RGCAD_LICENSE=examples/licenses/valid.lic</code></p>")
                           .arg(QString::fromStdString(lic::licensePath()));
            }
            break;

        case lic::State::Invalid:
            // Surface the library's own words verbatim. It already wrote the
            // sentence, and paraphrasing it loses the detail a support ticket
            // needs.
            body = QStringLiteral(
                       "<p style='color:#ff6b6b'><b>License rejected.</b></p>"
                       "<p><code>%1</code></p>"
                       "<p>2D drafting continues to work.</p>")
                       .arg(QString::fromStdString(s.message));
            break;
    }

    // The stub already explains itself in full above, so do not repeat it.
    if (s.stub && s.state != lic::State::NoLicense) {
        body += QStringLiteral(
            "<hr><p style='color:#7a8a9b'>Built against the RockyGuard <b>API "
            "stub</b>, so no license can verify and Pro is unreachable by "
            "construction. Configure with <code>-DROCKYGUARD_ROOT=...</code> "
            "against the real SDK.</p>");
    }

    detail->setText(body);
    layout->addWidget(detail);
    layout->addStretch(1);
    return pane;
}

// --- actions, toolbar, status ------------------------------------------------

void MainWindow::buildActions() {
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    // Qt does not show action tooltips in menus unless this is set; it defaults to
    // false and only toolbars show them. Without it the one string that names the
    // cad_stl_export flag and licensing/gate.cpp is never displayed, and the user
    // just sees a greyed-out item with no explanation.
    file->setToolTipsVisible(true);

    addAct(file, QStringLiteral("&New"), QKeySequence::New, this, &MainWindow::newDrawing);
    addAct(file, QStringLiteral("&Open..."), QKeySequence::Open, this,
           &MainWindow::openDrawing);
    addAct(file, QStringLiteral("&Save"), QKeySequence::Save, this, &MainWindow::saveDrawing);
    addAct(file, QStringLiteral("Save &As..."), QKeySequence::SaveAs, this,
           &MainWindow::saveDrawingAs);
    file->addSeparator();

    const bool canStl = lic::has(lic::kStlExport);
    // Name the tier in the label as well as the tooltip: a greyed item with a
    // hidden reason is a feature nobody buys.
    m_actExportStl = file->addAction(canStl ? QStringLiteral("Export &STL...")
                                           : QStringLiteral("Export &STL... (Pro)"));
    // A SECOND gated feature, and the reason it is here: two SKUs prove tiering,
    // but two independent flags prove PER-FEATURE gating, which is what RockyGuard
    // actually sells. Highest value per line in this repo.
    m_actExportStl->setEnabled(canStl);
    if (!canStl) {
        m_actExportStl->setToolTip(
            QStringLiteral("Requires the cad_stl_export feature (gate: licensing/gate.cpp)"));
    }
    connect(m_actExportStl, &QAction::triggered, this, &MainWindow::exportStl);

    file->addSeparator();
    addAct(file, QStringLiteral("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* undoAct = m_undo.createUndoAction(this, QStringLiteral("&Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    QAction* redoAct = m_undo.createRedoAction(this, QStringLiteral("&Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    edit->addAction(undoAct);
    edit->addAction(redoAct);
    edit->addSeparator();
    addAct(edit, QStringLiteral("&Delete"), QKeySequence::Delete, m_canvas,
           &ui2d::Canvas::deleteSelected);

    auto* view = menuBar()->addMenu(QStringLiteral("&View"));
    addAct(view, QStringLiteral("Zoom to &Fit"), QKeySequence(Qt::Key_F), m_canvas,
           &ui2d::Canvas::zoomToFit);

    auto* help = menuBar()->addMenu(QStringLiteral("&Help"));
    addAct(help, QStringLiteral("&License status..."), QKeySequence(), this,
           &MainWindow::showLicenseInfo);
}

void MainWindow::buildToolBar() {
    auto* tb = addToolBar(QStringLiteral("Tools"));
    tb->setMovable(false);

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    struct ToolDef {
        const char* label;
        ui2d::Tool tool;
        Qt::Key key;
    };
    const ToolDef defs[] = {
        {"Select", ui2d::Tool::Select, Qt::Key_S},
        {"Line", ui2d::Tool::Line, Qt::Key_L},
        {"Polyline", ui2d::Tool::Polyline, Qt::Key_P},
        {"Circle", ui2d::Tool::Circle, Qt::Key_C},
    };

    for (const ToolDef& d : defs) {
        QAction* a = tb->addAction(QString::fromLatin1(d.label));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(d.key));
        a->setChecked(d.tool == ui2d::Tool::Select);
        group->addAction(a);
        const ui2d::Tool t = d.tool;
        connect(a, &QAction::triggered, this, [this, t] { m_canvas->setTool(t); });
    }

    tb->addSeparator();
    m_actSnap = tb->addAction(QStringLiteral("Snap"));
    m_actSnap->setCheckable(true);
    m_actSnap->setChecked(true);
    m_actSnap->setShortcut(QKeySequence(Qt::Key_F9));
    connect(m_actSnap, &QAction::toggled, m_canvas, &ui2d::Canvas::setSnapEnabled);
    connect(m_actSnap, &QAction::toggled, this, &MainWindow::updateStatus);

    m_actOrtho = tb->addAction(QStringLiteral("Ortho"));
    m_actOrtho->setCheckable(true);
    m_actOrtho->setShortcut(QKeySequence(Qt::Key_F8));
    connect(m_actOrtho, &QAction::toggled, m_canvas, &ui2d::Canvas::setOrthoEnabled);
    connect(m_actOrtho, &QAction::toggled, this, &MainWindow::updateStatus);
}

void MainWindow::buildStatusBar() {
    m_coords = new QLabel(QStringLiteral("X 0.00  Y 0.00"), this);
    m_solid = new QLabel(this);
    m_measure = new QLabel(this);
    m_modes = new QLabel(this);
    m_counts = new QLabel(this);
    m_tier = new QLabel(this);

    m_coords->setMinimumWidth(200);
    m_measure->setMinimumWidth(170);

    statusBar()->addWidget(m_coords);
    statusBar()->addWidget(m_measure);
    statusBar()->addWidget(m_modes);
    statusBar()->addWidget(m_solid, 1);
    statusBar()->addPermanentWidget(m_counts);
    statusBar()->addPermanentWidget(m_tier);
}

void MainWindow::buildDocks() {
    auto* dock = new QDockWidget(QStringLiteral("History"), this);
    auto* view = new QUndoView(&m_undo, dock);
    view->setEmptyLabel(QStringLiteral("<empty drawing>"));
    dock->setWidget(view);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();  // available from the dock context menu, off by default
}

// Null-safe by construction: with cad_3d absent there is no viewport to talk to,
// and every caller goes through here rather than touching m_viewport directly.
void MainWindow::rebuild3D() {
    if (m_viewport) m_viewport->rebuild();
}

void MainWindow::exportStl() {
    // Defensive second check. The menu item is already disabled without the
    // feature, but a disabled QAction can still be triggered programmatically,
    // and this is the capability boundary rather than the UI.
    if (!lic::has(lic::kStlExport)) {
        QMessageBox::information(
            this, QStringLiteral("Export STL"),
            QStringLiteral("STL export requires the cad_stl_export feature.\n\n"
                           "Gate: licensing/gate.cpp"));
        return;
    }
    if (!m_viewport || !m_viewport->hasMesh()) {
        QMessageBox::information(
            this, QStringLiteral("Export STL"),
            QStringLiteral("There is no solid to export yet. Draw a closed polyline "
                           "in the 2D view first."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export STL"),
                                               QString(),
                                               QStringLiteral("STL meshes (*.stl)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".stl"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".stl");
    }
    QString err;
    if (!core::writeStlBinary(path, m_viewport->mesh(), &err)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), err);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Wrote %1 triangles to %2")
            .arg(m_viewport->mesh().tris.size())
            .arg(QFileInfo(path).fileName()),
        5000);
}

void MainWindow::updateStatus() {
    m_modes->setText(QStringLiteral("%1  %2")
                         .arg(m_canvas->snapEnabled() ? QStringLiteral("SNAP") : QStringLiteral("snap"))
                         .arg(m_canvas->orthoEnabled() ? QStringLiteral("ORTHO")
                                                       : QStringLiteral("ortho")));
    m_counts->setText(QStringLiteral("%1 entit%2%3")
                          .arg(m_doc.count())
                          .arg(m_doc.count() == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                          .arg(m_doc.hasClosedLoop() ? QStringLiteral("  closed loop OK")
                                                     : QString()));

    const lic::Status& s = lic::status();
    QString badge = tierName();
    if (s.state == lic::State::Invalid) badge += QStringLiteral("  (license rejected)");
    if (s.inGracePeriod) badge += QStringLiteral("  (grace)");
    if (s.stub) badge += QStringLiteral("  (stub)");
    m_tier->setText(badge);
}

void MainWindow::refreshTitle() {
    const QString name = m_path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1%2 - rgcad [%3]")
                       .arg(name, m_undo.isClean() ? QString() : QStringLiteral("*"), tierName()));
}

// --- file handling -----------------------------------------------------------

bool MainWindow::confirmDiscard() {
    if (m_undo.isClean()) return true;
    const auto choice = QMessageBox::warning(
        this, QStringLiteral("Unsaved changes"),
        QStringLiteral("This drawing has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Save) return saveDrawing();
    return choice == QMessageBox::Discard;
}

void MainWindow::newDrawing() {
    if (!confirmDiscard()) return;
    m_doc.clear();
    m_undo.clear();
    m_path.clear();
    m_canvas->documentReset();
    updateStatus();
    refreshTitle();
}

bool MainWindow::openPath(const QString& path) {
    const core::LoadResult r = m_doc.load(path);
    if (!r) {
        // stderr, not QMessageBox: a modal here would hang any unattended run,
        // which is exactly the trap a GUI smoke test must not contain.
        std::fprintf(stderr,
                     "rgcad: cannot open %s\n"
                     "  %s\n",
                     path.toLocal8Bit().constData(), r.error.toLocal8Bit().constData());
        return false;
    }
    m_path = path;
    m_undo.clear();
    m_canvas->documentReset();
    m_canvas->zoomToFit();
    rebuild3D();
    updateStatus();
    refreshTitle();
    return true;
}

void MainWindow::printSmokeState() const {
    const int tris = m_viewport ? m_viewport->mesh().tris.size() : 0;
    std::printf("RGCAD_STATE tier=%s entities=%d closed_loop=%s viewport=%s triangles=%d\n",
                lic::has(lic::k3D) ? "Pro" : "Draft", m_doc.count(),
                m_doc.hasClosedLoop() ? "yes" : "no",
                m_viewport ? "built" : "absent", tris);
    std::fflush(stdout);
}

void MainWindow::openDrawing() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open drawing"), QString(),
        QStringLiteral("rgcad drawings (*.rgcad);;All files (*)"));
    if (path.isEmpty()) return;

    const core::LoadResult r = m_doc.load(path);
    if (!r) {
        QMessageBox::critical(this, QStringLiteral("Cannot open drawing"), r.error);
        return;
    }
    m_path = path;
    m_undo.clear();
    m_canvas->documentReset();
    m_canvas->zoomToFit();
    rebuild3D();
    updateStatus();
    refreshTitle();
}

bool MainWindow::saveDrawing() {
    if (m_path.isEmpty()) return saveDrawingAs();
    const core::LoadResult r = m_doc.save(m_path);
    if (!r) {
        QMessageBox::critical(this, QStringLiteral("Cannot save drawing"), r.error);
        return false;
    }
    m_undo.setClean();
    refreshTitle();
    return true;
}

bool MainWindow::saveDrawingAs() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save drawing"), QString(),
                                                QStringLiteral("rgcad drawings (*.rgcad)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".rgcad"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".rgcad");
    }
    m_path = path;
    return saveDrawing();
}

void MainWindow::showLicenseInfo() {
    const lic::Status& s = lic::status();
    QString text = QStringLiteral("Tier: %1\nLicense file: %2\n")
                       .arg(tierName(), QString::fromStdString(lic::licensePath()));
    text += QStringLiteral("cad_3d: %1\ncad_stl_export: %2\n")
                .arg(lic::has(lic::k3D) ? QStringLiteral("unlocked") : QStringLiteral("locked"),
                     lic::has(lic::kStlExport) ? QStringLiteral("unlocked")
                                               : QStringLiteral("locked"));
    if (!s.message.empty()) {
        text += QStringLiteral("\n%1").arg(QString::fromStdString(s.message));
    }
    QMessageBox::information(this, QStringLiteral("License status"), text);
}
