package com.kevinhong.schedule

import android.app.Application
import com.kevinhong.schedule.data.ScheduleDatabase
import com.kevinhong.schedule.data.ScheduleRepository
import com.kevinhong.schedule.reminder.ReminderScheduler

class ScheduleApp : Application() {
    lateinit var repository: ScheduleRepository
        private set

    override fun onCreate() {
        super.onCreate()
        val reminders = ReminderScheduler(this)
        reminders.createNotificationChannel()
        repository = ScheduleRepository(ScheduleDatabase.create(this).scheduleDao(), reminders)
    }
}
