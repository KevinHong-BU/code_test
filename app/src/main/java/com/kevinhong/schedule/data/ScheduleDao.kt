package com.kevinhong.schedule.data

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Query
import androidx.room.Upsert
import kotlinx.coroutines.flow.Flow

@Dao
interface ScheduleDao {
    @Query(
        "SELECT * FROM schedules WHERE epochDay = :epochDay " +
            "ORDER BY startMinute IS NOT NULL, startMinute, id"
    )
    fun observeDay(epochDay: Long): Flow<List<Schedule>>

    @Query("SELECT * FROM schedules WHERE epochDay BETWEEN :from AND :to")
    fun observeRange(from: Long, to: Long): Flow<List<Schedule>>

    @Query("SELECT * FROM schedules WHERE id = :id")
    suspend fun get(id: Long): Schedule?

    @Query(
        "SELECT * FROM schedules WHERE reminderMinutesBefore IS NOT NULL " +
            "AND isDone = 0 AND epochDay >= :fromEpochDay"
    )
    suspend fun pendingReminders(fromEpochDay: Long): List<Schedule>

    @Upsert
    suspend fun upsert(schedule: Schedule): Long

    @Delete
    suspend fun delete(schedule: Schedule)
}
