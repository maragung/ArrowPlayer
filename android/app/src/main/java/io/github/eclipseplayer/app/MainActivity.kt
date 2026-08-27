// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// MainActivity — Phase 0 scaffold (ADR 0012). One Compose screen that states
// the build identity, mirroring the desktop exit gate 7 ("version shown in
// About") on this platform. The version comes from BuildConfig.VERSION_NAME,
// which Gradle reads from gradle.properties (the single Android-side source),
// so the About screen and the APK metadata cannot disagree.

package io.github.eclipseplayer.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.eclipseplayer.app.ui.theme.EclipseTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            EclipseTheme {
                AboutScreen()
            }
        }
    }
}

@Composable
fun AboutScreen() {
    Scaffold { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = stringResource(R.string.app_name),
                style = MaterialTheme.typography.headlineMedium,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.version_label, BuildConfig.VERSION_NAME),
                style = MaterialTheme.typography.bodyLarge,
            )
            Spacer(Modifier.height(16.dp))
            Text(
                text = stringResource(R.string.about_hint),
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}
