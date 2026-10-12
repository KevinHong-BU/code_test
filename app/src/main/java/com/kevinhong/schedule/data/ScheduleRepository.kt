package com.kevinhong.schedule.data

import com.kevinhong.schedule.reminder.ReminderScheduler
import kotlinx.coroutines.flow.Flow
import java.time.LocalDate

/** Single entry point for schedule data; keeps reminders in sync with the database. */
class ScheduleRepository(
    private val dao: ScheduleDao,
    private val reminders: ReminderScheduler,
) {
    fun observeDay(date: LocalDate): Flow<List<Schedule>> = dao.observeDay(date.toEpochDay())

    fun observeRange(from: LocalDate, to: LocalDate): Flow<List<Schedule>> =
        dao.observeRange(from.toEpochDay(), to.toEpochDay())

    suspend fun get(id: Long): Schedule? = dao.get(id)

    suspend fun save(schedule: Schedule) {
        val rowId = dao.upsert(schedule)
        // @Upsert returns -1 when it updated an existing row.
        val saved = if (schedule.id == 0L) schedule.copy(id = rowId) else schedule
        reminders.sync(saved)
    }

    suspend fun delete(schedule: Schedule) {
        dao.delete(schedule)
        reminders.cancel(schedule.id)
    }

    suspend fun rescheduleAllReminders() {
        dao.pendingReminders(LocalDate.now().minusDays(1).toEpochDay()).forEach(reminders::sync)
    }
}
