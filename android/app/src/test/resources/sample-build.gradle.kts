// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Fixture read by VersionMappingTest. A trimmed copy of the version-relevant
// lines from app/build.gradle.kts, kept in the test resources rather than
// reading the real file (the production script is a Gradle .kts, not a
// classpath resource). If you change the production script's version block,
// mirror the change here — the test will fail loudly otherwise.
//
// The versionName here is a literal "0.3.0" (not the
// "${arrowVersion.major}..." template the production script expands to)
// because the test reads this file as plain text. The production script
// and the fixture therefore agree on what `arrowVersion.versionCode`
// means for the same literal SemVer, which is the property under test.

plugins {
    alias(libs.plugins.android.application)
}

val arrowVersion: SemVer =
    parseSemVer(
        releaseVersionOverride
            ?: (project.property("arrow.version") as String),
    )

android {
    namespace = "io.github.arrowplayer.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.arrowplayer.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 300
        versionName = "0.3.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
}
