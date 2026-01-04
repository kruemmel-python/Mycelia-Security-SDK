package com.mycelia.security.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val LightColors = lightColorScheme(
    primary = Color(0xFF005B9A),
    secondary = Color(0xFF3A7CA5),
    tertiary = Color(0xFF7A4DCC)
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF6DB6FF),
    secondary = Color(0xFF9FC4E0),
    tertiary = Color(0xFFD0BCFF)
)

@Composable
fun MyceliaTheme(
    darkTheme: Boolean,
    content: @Composable () -> Unit
) {
    val colors = if (darkTheme) DarkColors else LightColors
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as? android.app.Activity)?.window
            if (window != null) {
                WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = !darkTheme
            }
        }
    }
    MaterialTheme(
        colorScheme = colors,
        content = content
    )
}
