package com.kevinhong.schedule.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

/** Colors a user can tag a schedule with; [com.kevinhong.schedule.data.Schedule.colorIndex] indexes this. */
val ScheduleColors = listOf(
    Color(0xFF3F51B5), // 인디고
    Color(0xFFE53935), // 빨강
    Color(0xFF43A047), // 초록
    Color(0xFFFB8C00), // 주황
    Color(0xFF8E24AA), // 보라
    Color(0xFF00897B), // 청록
    Color(0xFF6D4C41), // 갈색
)

fun scheduleColor(index: Int): Color = ScheduleColors[index.coerceIn(ScheduleColors.indices)]

val SundayColor = Color(0xFFE53935)
val SaturdayColor = Color(0xFF1E88E5)

@Composable
fun MyScheduleTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val colors = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        dark -> darkColorScheme(primary = Color(0xFF9FA8DA))
        else -> lightColorScheme(primary = Color(0xFF3F51B5))
    }
    MaterialTheme(colorScheme = colors, content = content)
}
