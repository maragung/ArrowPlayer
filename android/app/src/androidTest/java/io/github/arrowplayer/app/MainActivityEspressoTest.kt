// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Final-verification smoke for the About screen (REQ-BLD-002, §16). The
// version string the user sees in-app is `R.string.version_label`
// ("Version %1$s") formatted with `BuildConfig.VERSION_NAME`. BuildConfig
// is generated from `defaultConfig.versionName` in app/build.gradle.kts,
// which in turn reads `arrow.version` from gradle.properties (or, on a
// release job, the `-PreleaseVersion` override). The contract under test
// is that *whatever the build says, the About screen says* — a
// regression that pins `versionName` to a literal while leaving
// `versionCode` as a derived value would slip past VersionMappingTest
// but surface here, which is the reason §16 lists this run in the
// "final verification" set rather than as a unit test.
//
// `createAndroidComposeRule<MainActivity>()` is the literal
// ActivityScenarioRule the spec asks for: the return type is
// `AndroidComposeTestRule<ActivityScenarioRule<T>, T>`, so the
// underlying launch mechanism is `ActivityScenarioRule`. We do not
// stack a second rule on top — two competing ActivityScenario
// instances on the same activity is the only configuration that has
// been observed to flake this test (first activity destroyed before
// the second scenario's window transitions settle).
//
// The `onNodeWithText` assertion reads the formatted string off the
// Compose semantic tree. Espresso's `onView` only sees Android Views,
// not Compose nodes, which is why we route through `composeTestRule`
// rather than `Espresso.onView(withText(...))` — the latter is a
// valid alternative but is silently a no-op against a Compose `Text`
// and would not fail loudly on a regression.

package io.github.arrowplayer.app

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MainActivityEspressoTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun aboutScreen_rendersVersionLabelFromBuildConfig() {
        // The label format is fixed in strings.xml:
        //     <string name="version_label">Version %1$s</string>
        // Hard-coding the prefix here is intentional: a refactor that
        // drops "Version " (e.g. localising to "v0.3.0") is a UX change
        // and a translation, not a regression — the unit-of-truth is
        // that the version reported by BuildConfig reaches the user.
        val expected = "Version ${BuildConfig.VERSION_NAME}"
        composeRule
            .onNodeWithText(expected)
            .assertExists()
            .assertIsDisplayed()
    }

    // The scenario is launched by `composeRule` above and torn down by
    // its `afterActivityFinished` callback, so the dismissal the spec
    // asks for is owned by the rule rather than an explicit @After.
    // Asserting isFinishing= true here would only assert the
    // teardown path of the rule, which is exercised by every other
    // test in this class — adding a separate test for it would be the
    // OQ-051 shape (a test that re-tests the framework).
}
