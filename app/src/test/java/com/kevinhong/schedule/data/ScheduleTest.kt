package com.kevinhong.schedule.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.time.LocalDate
import java.time.ZoneOffset

class ScheduleTest {
    private val date = LocalDate.of(2026, 10, 12)
    private val utc = ZoneOffset.UTC

    private fun millis(day: LocalDate, hour: Int, minute: Int) =
        day.atTime(hour, minute).toInstant(utc).toEpochMilli()

    @Test
    fun noReminderReturnsNull() {
        val s = Schedule(title = "회의", epochDay = date.toEpochDay(), startMinute = 600)
        assertNull(s.reminderTriggerAt(utc))
    }

    @Test
    fun reminderIsOffsetFromStart() {
        val s = Schedule(
            title = "회의", epochDay = date.toEpochDay(),
            startMinute = 10 * 60 + 30, reminderMinutesBefore = 30,
        )
        assertEquals(millis(date, 10, 0), s.reminderTriggerAt(utc))
    }

    @Test
    fun allDayReminderAnchorsAtNineAndCanCrossMidnight() {
        val s = Schedule(title = "생일", epochDay = date.toEpochDay(), reminderMinutesBefore = 24 * 60)
        assertEquals(millis(date.minusDays(1), 9, 0), s.reminderTriggerAt(utc))
    }

    @Test
    fun formatMinutePadsHoursAndMinutes() {
        assertEquals("09:05", formatMinute(9 * 60 + 5))
        assertEquals("23:59", formatMinute(23 * 60 + 59))
    }
}
