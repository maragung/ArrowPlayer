// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// The Qt shell — spec §7.1 layer 5 (PRESENTATION), §28 Phase 0 exit gates 1
// ("window opens") and 7 ("version string shown in About").
//
// This is the only file that may construct the QApplication, because
// QApplication must live on the thread that runs `main` (§1.3 rule 4) and
// because main.cpp handed control here precisely so the application layer
// never has to name a Qt type (see shell.hpp).
//
// The ARROW_SMOKE_TEST hook at the bottom is the Phase 0 answer to a
// headless CI: exit gate 1 says "window opens", and on a runner with no
// display the only honest way to assert that is to create the window, process
// events, and quit. With the environment variable set, the event loop quits
// right after the window has been shown and painted, so the binary exits 0
// exactly when a window *could* open. It is a test seam and is documented as
// such; the real CLI owns the proper flags in Phase 4 (OQ-054).

#include "arrow/ui/shell.hpp"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QString>
#include <QTimer>
#include <QTranslator>

#include "arrow/ui/main_window.hpp"

namespace arrow::ui {

namespace {

// Installs the application's own translations plus Qt's stock translations for
// the active locale, and returns true when anything was installed.
//
// The .qm files are compiled from the committed .ts sources in
// desktop/resources/i18n/ (en + id, the two locales Phase 0 ships) and
// embedded in the binary via the arrow-i18n resource (ui/CMakeLists.txt).
// Qt's own strings come from the Qt installation's translation directory when
// it carries the locale — a missing file there is not an error, because the
// application's own strings are the ones this project controls.
bool install_translators(QCoreApplication& app, const QString& locale) {
    QTranslator* app_translator = new QTranslator(&app);
    if (app_translator->load(QLocale(locale), QStringLiteral("arrow"), QStringLiteral("_"),
                             QStringLiteral(":/i18n"))) {
        app.installTranslator(app_translator);
    } else {
        delete app_translator;
        app_translator = nullptr;
    }

    QTranslator* qt_translator = new QTranslator(&app);
    const QString qt_dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qt_translator->load(QLocale(locale), QStringLiteral("qtbase"), QStringLiteral("_"),
                            qt_dir)) {
        app.installTranslator(qt_translator);
    } else {
        delete qt_translator;
        qt_translator = nullptr;
    }
    return app_translator != nullptr || qt_translator != nullptr;
}

}  // namespace

int run_shell(int argc, char** argv, const ShellInfo& info) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Arrow Player"));
    app.setApplicationVersion(QString::fromUtf8(info.version.data(),
                                                static_cast<qsizetype>(info.version.size())));

    // The locale Qt derives from the environment is the one a translator
    // reordered the sentences for; substituting another would be the §12.7
    // pipeline misbehaving by hand.
    install_translators(app, QLocale::system().name());

    MainWindow window(info);
    window.show();

    if (qEnvironmentVariableIsSet("ARROW_SMOKE_TEST")) {
        // Quit once the shown window has been exposed and the event loop has
        // actually run, so a headless CI run can assert "a window opens"
        // without a display and without hanging.
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}

}  // namespace arrow::ui
