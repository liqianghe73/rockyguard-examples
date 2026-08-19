// rgcad -- a small 2D CAD tool where 3D is a paid feature.
//
// The application deliberately does NOT refuse to start when unlicensed. Draft
// tier is a product: 2D drafting, unlimited, no time limit. A licensing demo that
// greets you with a lockout modal teaches the wrong lesson and, in a real product,
// loses the customer before they have seen anything work.

#include <QApplication>
#include <QTimer>

#include <cstdlib>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("rgcad"));
    app.setOrganizationName(QStringLiteral("Rocky Software Inc."));

    MainWindow w;
    w.show();

    // RGCAD_SMOKE lets CI prove the app constructs, lays out, paints once and
    // shuts down cleanly, without a display and without hanging in exec(). A GUI
    // test that needs a human is a GUI test that never runs.
    if (const char* smoke = std::getenv("RGCAD_SMOKE")) {
        if (*smoke && *smoke != '0') {
            QTimer::singleShot(0, &app, [&app] {
                // One event-loop turn happens before this fires, so the window has
                // been shown and painted by now.
                app.quit();
            });
        }
    }

    return app.exec();
}
