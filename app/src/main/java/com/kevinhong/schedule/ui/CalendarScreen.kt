package com.kevinhong.schedule.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.kevinhong.schedule.data.Schedule
import com.kevinhong.schedule.data.formatMinute
import com.kevinhong.schedule.ui.theme.SaturdayColor
import com.kevinhong.schedule.ui.theme.SundayColor
import com.kevinhong.schedule.ui.theme.scheduleColor
import java.time.LocalDate
import java.time.YearMonth
import java.time.format.TextStyle
import java.util.Locale

private val WeekdayLabels = listOf("일", "월", "화", "수", "목", "금", "토")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CalendarScreen(
    viewModel: ScheduleViewModel,
    onAdd: (LocalDate) -> Unit,
    onOpen: (Schedule) -> Unit,
) {
    val month by viewModel.month.collectAsStateWithLifecycle()
    val selected by viewModel.selectedDate.collectAsStateWithLifecycle()
    val monthSchedules by viewModel.monthSchedules.collectAsStateWithLifecycle()
    val daySchedules by viewModel.daySchedules.collectAsStateWithLifecycle()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("내 스케줄") },
                actions = { TextButton(onClick = viewModel::goToday) { Text("오늘") } },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { onAdd(selected) }) {
                Icon(Icons.Default.Add, contentDescription = "일정 추가")
            }
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            MonthHeader(
                month = month,
                onPrev = { viewModel.showMonth(month.minusMonths(1)) },
                onNext = { viewModel.showMonth(month.plusMonths(1)) },
            )
            MonthGrid(
                month = month,
                selected = selected,
                schedulesByDate = monthSchedules,
                onSelect = viewModel::selectDate,
                onSwipe = { forward -> viewModel.showMonth(if (forward) month.plusMonths(1) else month.minusMonths(1)) },
            )
            HorizontalDivider(Modifier.padding(top = 8.dp))
            DayHeader(selected, daySchedules)
            if (daySchedules.isEmpty()) {
                Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                    Text(
                        "일정이 없습니다.\n+ 버튼으로 추가해 보세요.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 88.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(daySchedules, key = { it.id }) { schedule ->
                        ScheduleItem(
                            schedule = schedule,
                            onClick = { onOpen(schedule) },
                            onToggleDone = { viewModel.toggleDone(schedule) },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun MonthHeader(month: YearMonth, onPrev: () -> Unit, onNext: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onPrev) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = "이전 달")
        }
        Text(
            "${month.year}년 ${month.monthValue}월",
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
        )
        IconButton(onClick = onNext) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = "다음 달")
        }
    }
}

@Composable
private fun MonthGrid(
    month: YearMonth,
    selected: LocalDate,
    schedulesByDate: Map<LocalDate, List<Schedule>>,
    onSelect: (LocalDate) -> Unit,
    onSwipe: (forward: Boolean) -> Unit,
) {
    val today = remember { LocalDate.now() }
    // Sunday-first grid: Sunday is DayOfWeek.value 7, so value % 7 gives 0 for Sunday.
    val offset = month.atDay(1).dayOfWeek.value % 7
    val cellCount = offset + month.lengthOfMonth()
    val rows = (cellCount + 6) / 7

    Column(
        Modifier
            .padding(horizontal = 8.dp)
            .pointerInput(month) {
                var total = 0f
                detectHorizontalDragGestures(
                    onDragStart = { total = 0f },
                    onDragEnd = {
                        if (total < -120f) onSwipe(true) else if (total > 120f) onSwipe(false)
                    },
                    onHorizontalDrag = { _, amount -> total += amount },
                )
            },
    ) {
        Row(Modifier.fillMaxWidth()) {
            WeekdayLabels.forEachIndexed { i, label ->
                Text(
                    label,
                    modifier = Modifier.weight(1f).padding(vertical = 4.dp),
                    textAlign = TextAlign.Center,
                    style = MaterialTheme.typography.labelMedium,
                    color = weekdayColor(i) ?: MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        for (row in 0 until rows) {
            Row(Modifier.fillMaxWidth()) {
                for (col in 0 until 7) {
                    val day = row * 7 + col - offset + 1
                    Box(Modifier.weight(1f)) {
                        if (day in 1..month.lengthOfMonth()) {
                            val date = month.atDay(day)
                            DayCell(
                                date = date,
                                isSelected = date == selected,
                                isToday = date == today,
                                schedules = schedulesByDate[date].orEmpty(),
                                onClick = { onSelect(date) },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun DayCell(
    date: LocalDate,
    isSelected: Boolean,
    isToday: Boolean,
    schedules: List<Schedule>,
    onClick: () -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    Column(
        Modifier
            .fillMaxWidth()
            .aspectRatio(1f)
            .padding(2.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(if (isSelected) colors.primaryContainer else Color.Transparent)
            .then(
                if (isToday && !isSelected) Modifier.border(1.dp, colors.primary, RoundedCornerShape(10.dp))
                else Modifier
            )
            .clickable(onClick = onClick)
            .padding(top = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            date.dayOfMonth.toString(),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = if (isToday) FontWeight.Bold else FontWeight.Normal,
            color = when {
                isSelected -> colors.onPrimaryContainer
                else -> weekdayColor(date.dayOfWeek.value % 7) ?: colors.onSurface
            },
        )
        Spacer(Modifier.height(4.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            schedules.take(3).forEach { s ->
                Box(
                    Modifier
                        .size(6.dp)
                        .clip(CircleShape)
                        .background(scheduleColor(s.colorIndex).copy(alpha = if (s.isDone) 0.35f else 1f))
                )
            }
        }
        if (schedules.size > 3) {
            Text(
                "+${schedules.size - 3}",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun DayHeader(date: LocalDate, schedules: List<Schedule>) {
    val weekday = date.dayOfWeek.getDisplayName(TextStyle.FULL, Locale.KOREAN)
    val remaining = schedules.count { !it.isDone }
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "${date.monthValue}월 ${date.dayOfMonth}일 $weekday",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.weight(1f),
        )
        if (schedules.isNotEmpty()) {
            Text(
                "남은 일정 $remaining / ${schedules.size}",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun ScheduleItem(schedule: Schedule, onClick: () -> Unit, onToggleDone: () -> Unit) {
    val dim = if (schedule.isDone) 0.5f else 1f
    Card(onClick = onClick, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.heightIn(min = 64.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .width(6.dp)
                    .height(64.dp)
                    .background(scheduleColor(schedule.colorIndex).copy(alpha = dim))
            )
            Column(Modifier.weight(1f).padding(horizontal = 12.dp, vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        timeLabel(schedule),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary.copy(alpha = dim),
                    )
                    if (schedule.reminderMinutesBefore != null) {
                        Icon(
                            Icons.Default.Notifications,
                            contentDescription = "알림 설정됨",
                            modifier = Modifier.padding(start = 4.dp).size(14.dp),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = dim),
                        )
                    }
                }
                Text(
                    schedule.title,
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Medium,
                    textDecoration = if (schedule.isDone) TextDecoration.LineThrough else null,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = dim),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (schedule.memo.isNotBlank()) {
                    Text(
                        schedule.memo,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = dim),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Checkbox(checked = schedule.isDone, onCheckedChange = { onToggleDone() })
        }
    }
}

private fun timeLabel(schedule: Schedule): String {
    val start = schedule.startMinute ?: return "종일"
    val end = schedule.endMinute ?: return formatMinute(start)
    return "${formatMinute(start)} - ${formatMinute(end)}"
}

/** Red for Sunday (index 0), blue for Saturday (index 6), null otherwise. */
private fun weekdayColor(sundayFirstIndex: Int): Color? = when (sundayFirstIndex) {
    0 -> SundayColor
    6 -> SaturdayColor
    else -> null
}
