plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

// Explicit API mode: all public API must be declared.
kotlin {
    explicitApi()
}

android {
    namespace = "io.github.arrowplayer.core.library"
    compileSdk = 35

    defaultConfig {
        minSdk = 26
        // Room schema export: stored at ../schemas/ relative to this module.
        // The path must match across both platforms (REQ-LIB-001).
        ksp {
            arg("room.schemaLocation", "$projectDir/schemas")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        // Explicit API mode
        optIn("kotlin.ExperimentalExplicitApi")
    }
}

dependencies {
    // Room: Android's compile-time SQLite wrapper (spec §9.4, ADR 0007)
    // Pinned in the version catalog (§5 layout, REQ-SEC-013).
    implementation(libs.androidx.room.runtime)
    implementation(libs.androidx.room.ktx)
    ksp(libs.androidx.room.compiler)

    // Coroutines for structured concurrency
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.kotlinx.coroutines.core)

    // Core
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)

    testImplementation(libs.junit)
}
