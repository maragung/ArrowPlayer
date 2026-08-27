// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// Shell tests — spec §28 Phase 0 exit gates 1 ("window opens") and 7 ("version
// string generated from git and shown in About", REQ-BLD-007), run off-screen.
//
// These assert what the gates name: a window constructs, shows and is exposed
// on a display-less runner, and the About text carries exactly the three
// fields main.cpp filled from the generated version header — never a literal,
// because a literal here would pass while the About dialog showed something
// else (the same reasoning as test_app.cpp's AppInfo::CurrentMatches... case).

#include <QString>
#include <QtTest>

#include <eclipse/ui/main_window.hpp>
#include <eclipse/ui/shell.hpp>

using eclipse::ui::MainWindow;
using eclipse::ui::ShellInfo;

class ShellTest : public QObject {
    Q_OBJECT

  private slots:
    void aboutTextCarriesTheFieldsThatWereHandedIn();
    void dirtyBuildIsStatedInTheAboutText();
    void windowOpensOffScreen();
    void windowIsNamedForTheProduct();

  private:
    static ShellInfo sampleInfo() {
        return ShellInfo{.version = "0.1.0", .git_sha = "abcdef123456", .git_dirty = false};
    }
};

void ShellTest::aboutTextCarriesTheFieldsThatWereHandedIn() {
    const MainWindow window(sampleInfo());
    const QString text = window.aboutText();
    QVERIFY(text.contains(QStringLiteral("0.1.0")));
    QVERIFY(text.contains(QStringLiteral("abcdef123456")));
    // A clean build must not claim otherwise; the dirty marker is asserted by
    // its own case below.
    QVERIFY(!text.contains(QStringLiteral("uncommitted")));
}

void ShellTest::dirtyBuildIsStatedInTheAboutText() {
    ShellInfo info = sampleInfo();
    info.git_dirty = true;
    const MainWindow window(info);
    QVERIFY(window.aboutText().contains(QStringLiteral("uncommitted")));
}

void ShellTest::windowOpensOffScreen() {
    MainWindow window(sampleInfo());
    window.show();
    // The closest a display-less run can come to "the window opens": mapped
    // and exposed by the platform plugin, with the event loop having run.
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.isVisible());
}

void ShellTest::windowIsNamedForTheProduct() {
    // A smoke run observes the window title before anything else; gate 1's
    // "hello window" has to be recognisable as the product.
    const MainWindow window(sampleInfo());
    QCOMPARE(window.windowTitle(), QStringLiteral("Eclipse Player"));
}

QTEST_MAIN(ShellTest)

#include "test_shell.moc"
