package com.kevinhong.schedule.reminder

import android.app.AlarmManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import com.kevinhong.schedule.R
import com.kevinhong.schedule.data.Schedule
import com.kevinhong.schedule.data.formatMinute

/** Schedules and cancels reminder alarms for [Schedule]s. */
class ReminderScheduler(private val context: Context) {
    private val alarmManager = context.getSystemService(AlarmManager::class.java)

    fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            context.getString(R.string.channel_name),
            NotificationManager.IMPORTANCE_HIGH,
        ).apply { description = context.getString(R.string.channel_description) }
        context.getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    /** Arms the reminder for [schedule], or cancels it if it no longer applies. */
    fun sync(schedule: Schedule) {
        val triggerAt = schedule.reminderTriggerAt()
        if (triggerAt == null || schedule.isDone || triggerAt <= System.currentTimeMillis()) {
            cancel(schedule.id)
            return
        }
        val text = if (schedule.isAllDay) {
            "종일"
        } else {
            formatMinute(schedule.startMinute!!) +
                (schedule.endMinute?.let { " - " + formatMinute(it) } ?: "")
        }
        val pending = pendingIntent(
            schedule.id,
            Intent(context, ReminderReceiver::class.java)
                .putExtra(ReminderReceiver.EXTRA_ID, schedule.id)
                .putExtra(ReminderReceiver.EXTRA_TITLE, schedule.title)
                .putExtra(ReminderReceiver.EXTRA_TEXT, text),
        )
        val canExact = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || alarmManager.canScheduleExactAlarms()
        if (canExact) {
            alarmManager.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAt, pending)
        } else {
            alarmManager.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAt, pending)
        }
    }

    fun cancel(id: Long) {
        alarmManager.cancel(pendingIntent(id, Intent(context, ReminderReceiver::class.java)))
    }

    private fun pendingIntent(id: Long, intent: Intent): PendingIntent =
        PendingIntent.getBroadcast(
            context,
            id.toInt(),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )

    companion object {
        const val CHANNEL_ID = "schedule_reminders"
    }
}
