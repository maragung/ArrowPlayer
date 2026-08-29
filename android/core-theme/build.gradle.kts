// SPDX-License-Identifier: MPL-2.0
//
// build.gradle.kts — Android core-theme module.
//
// Spec: eclipse-player.md §5 (android/core-theme/ entry).
//
// This module contains the theme and skin engine for Android.
// It is consumed by feature modules and must NOT depend on any other
// feature module (§7.2 REQ-GEN-050).

plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin-kapt)
    alias(libs.plugins.hilt)
    alias(libs.plugins.ktlint)
    alias(libs.plugins.detekt)
}

android {
    namespace = "io.github.arrowplayer.core.theme"
    compileSdk = 35

    defaultConfig {
        minSdk = 26
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        // All theme token values come from shared-spec/design-system/tokens.json.
        // This module does NOT contain any hard-coded values — the tokens are
        // loaded from that file at build time by the gen-tokens Gradle task.
    }

    buildFeatures {
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        // Explicit API mode per §1.2: all public APIs must have documentation.
        freeCompilerArgs += listOf(
            "-Xexplicit-api=warning",
        )
    }
}

dependencies {
    // AndroidX Core
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)

    // Dagger Hilt DI
    implementation(libs.dagger.hilt.android)
    kapt(libs.dagger.hilt.compiler)

    // Coroutines (structured concurrency only, no GlobalScope)
    implementation(libs.kotlinx-coroutines-android)

    // Testing
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
