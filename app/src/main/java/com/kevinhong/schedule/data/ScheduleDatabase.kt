package com.kevinhong.schedule.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

@Database(entities = [Schedule::class], version = 1, exportSchema = false)
abstract class ScheduleDatabase : RoomDatabase() {
    abstract fun scheduleDao(): ScheduleDao

    companion object {
        fun create(context: Context): ScheduleDatabase =
            Room.databaseBuilder(context, ScheduleDatabase::class.java, "schedule.db").build()
    }
}
