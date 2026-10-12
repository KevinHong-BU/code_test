package com.kevinhong.schedule.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.kevinhong.schedule.ScheduleApp
import com.kevinhong.schedule.data.Schedule
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.time.YearMonth

@OptIn(ExperimentalCoroutinesApi::class)
class ScheduleViewModel(app: Application) : AndroidViewModel(app) {
    private val repository = (app as ScheduleApp).repository

    private val _month = MutableStateFlow(YearMonth.now())
    val month: StateFlow<YearMonth> = _month.asStateFlow()

    private val _selectedDate = MutableStateFlow(LocalDate.now())
    val selectedDate: StateFlow<LocalDate> = _selectedDate.asStateFlow()

    /** Schedules in the visible month grouped by date, for the calendar dots. */
    val monthSchedules: StateFlow<Map<LocalDate, List<Schedule>>> = _month
        .flatMapLatest { repository.observeRange(it.atDay(1), it.atEndOfMonth()) }
        .map { list -> list.groupBy { it.date } }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyMap())

    val daySchedules: StateFlow<List<Schedule>> = _selectedDate
        .flatMapLatest { repository.observeDay(it) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    fun selectDate(date: LocalDate) {
        _selectedDate.value = date
        _month.value = YearMonth.from(date)
    }

    fun showMonth(month: YearMonth) {
        _month.value = month
        // Keep the selection inside the visible month.
        val day = _selectedDate.value.dayOfMonth.coerceAtMost(month.lengthOfMonth())
        _selectedDate.value = month.atDay(day)
    }

    fun goToday() = selectDate(LocalDate.now())

    fun toggleDone(schedule: Schedule) = viewModelScope.launch {
        repository.save(schedule.copy(isDone = !schedule.isDone))
    }

    fun save(schedule: Schedule) = viewModelScope.launch { repository.save(schedule) }

    fun delete(schedule: Schedule) = viewModelScope.launch { repository.delete(schedule) }

    suspend fun load(id: Long): Schedule? = repository.get(id)
}
