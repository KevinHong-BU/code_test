package com.kevinhong.schedule.reminder

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.kevinhong.schedule.ScheduleApp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/** Alarms are cleared on reboot (and when exact-alarm permission changes), so re-arm them. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED &&
            intent.action != "android.app.action.SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED"
        ) {
            return
        }
        val app = context.applicationContext as ScheduleApp
        val pending = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                app.repository.rescheduleAllReminders()
            } finally {
                pending.finish()
            }
        }
    }
}
