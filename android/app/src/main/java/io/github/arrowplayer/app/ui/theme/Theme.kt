// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Phase 0 theme. §12.1's canonical design tokens live in shared-spec/
// design-system/tokens.json and are consumed by the real theme engine (§11);
// until that engine exists this is a plain Material3 light/dark pair, so the
// scaffold is not pretending to ship the token system. OQ-001's conflict
// (extends vs. required groups) is decided in the schema, not here.

package io.github.arrowplayer.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

@Composable
@Suppress("FunctionNaming")
fun ArrowTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) darkColorScheme() else lightColorScheme(),
        content = content,
    )
}
