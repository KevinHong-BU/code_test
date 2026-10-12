package com.kevinhong.schedule.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.kevinhong.schedule.ui.theme.MyScheduleTheme
import java.time.LocalDate

class MainActivity : ComponentActivity() {
    private val notificationPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        setContent {
            MyScheduleTheme { ScheduleNavHost() }
        }
    }
}

@Composable
private fun ScheduleNavHost() {
    val navController = rememberNavController()
    val viewModel: ScheduleViewModel = viewModel()

    NavHost(navController, startDestination = "calendar") {
        composable("calendar") {
            CalendarScreen(
                viewModel = viewModel,
                onAdd = { date -> navController.navigate("edit?date=${date.toEpochDay()}") },
                onOpen = { schedule -> navController.navigate("edit?id=${schedule.id}") },
            )
        }
        composable(
            "edit?id={id}&date={date}",
            arguments = listOf(
                navArgument("id") { type = NavType.LongType; defaultValue = 0L },
                navArgument("date") { type = NavType.LongType; defaultValue = LocalDate.now().toEpochDay() },
            ),
        ) { entry ->
            val args = requireNotNull(entry.arguments)
            EditScheduleScreen(
                viewModel = viewModel,
                scheduleId = args.getLong("id"),
                initialEpochDay = args.getLong("date"),
                onDone = { navController.popBackStack() },
            )
        }
    }
}
