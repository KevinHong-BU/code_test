package com.kevinhong.schedule.data

import androidx.room.Entity
import androidx.room.PrimaryKey
import java.time.LocalDate
import java.time.LocalTime
import java.time.ZoneId

/**
 * A single schedule entry.
 *
 * Dates are stored as epoch days and times as minutes since midnight so the
 * table needs no type converters and range queries stay simple.
 */
@Entity(tableName = "schedules")
data class Schedule(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val title: String,
    val memo: String = "",
    val epochDay: Long,
    /** Start minute of day, or null for an all-day schedule. */
    val startMinute: Int? = null,
    /** End minute of day, or null when there is no end time. */
    val endMinute: Int? = null,
    val colorIndex: Int = 0,
    /** Minutes before the start to remind, or null for no reminder. */
    val reminderMinutesBefore: Int? = null,
    val isDone: Boolean = false,
) {
    val date: LocalDate get() = LocalDate.ofEpochDay(epochDay)
    val isAllDay: Boolean get() = startMinute == null

    /**
     * Epoch millis at which the reminder should fire, or null if this schedule
     * has no reminder. All-day schedules are anchored at [ALL_DAY_ANCHOR_MINUTE].
     */
    fun reminderTriggerAt(zone: ZoneId = ZoneId.systemDefault()): Long? {
        val before = reminderMinutesBefore ?: return null
        val minute = startMinute ?: ALL_DAY_ANCHOR_MINUTE
        return date.atTime(LocalTime.of(minute / 60, minute % 60))
            .minusMinutes(before.toLong())
            .atZone(zone)
            .toInstant()
            .toEpochMilli()
    }

    companion object {
        /** All-day schedules remind relative to 09:00. */
        const val ALL_DAY_ANCHOR_MINUTE = 9 * 60
    }
}

fun formatMinute(minute: Int): String = "%02d:%02d".format(minute / 60, minute % 60)
