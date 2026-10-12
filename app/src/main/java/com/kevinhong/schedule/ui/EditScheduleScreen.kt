package com.kevinhong.schedule.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.kevinhong.schedule.data.Schedule
import com.kevinhong.schedule.data.formatMinute
import com.kevinhong.schedule.ui.theme.ScheduleColors
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset
import java.time.format.TextStyle
import java.util.Locale

/** Reminder choices: minutes before start (null = no reminder) to label. */
private val ReminderOptions: List<Pair<Int?, String>> = listOf(
    null to "알림 없음",
    0 to "정시",
    10 to "10분 전",
    30 to "30분 전",
    60 to "1시간 전",
    24 * 60 to "1일 전",
)

private const val NO_REMINDER = -1

private enum class PickerTarget { Date, Start, End }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditScheduleScreen(
    viewModel: ScheduleViewModel,
    scheduleId: Long,
    initialEpochDay: Long,
    onDone: () -> Unit,
) {
    val isNew = scheduleId == 0L

    // Primitive, saveable form state so edits survive rotation.
    var loaded by rememberSaveable { mutableStateOf(isNew) }
    var title by rememberSaveable { mutableStateOf("") }
    var memo by rememberSaveable { mutableStateOf("") }
    var epochDay by rememberSaveable { mutableLongStateOf(initialEpochDay) }
    var allDay by rememberSaveable { mutableStateOf(false) }
    var startMinute by rememberSaveable { mutableIntStateOf(defaultStartMinute()) }
    var endMinute by rememberSaveable { mutableIntStateOf((defaultStartMinute() + 60).coerceAtMost(23 * 60 + 59)) }
    var colorIndex by rememberSaveable { mutableIntStateOf(0) }
    var reminder by rememberSaveable { mutableIntStateOf(NO_REMINDER) }
    var isDone by rememberSaveable { mutableStateOf(false) }
    var titleError by rememberSaveable { mutableStateOf(false) }

    var existing by remember { mutableStateOf<Schedule?>(null) }
    var picker by remember { mutableStateOf<PickerTarget?>(null) }
    var confirmDelete by remember { mutableStateOf(false) }

    LaunchedEffect(scheduleId) {
        if (isNew) return@LaunchedEffect
        val s = viewModel.load(scheduleId) ?: run { onDone(); return@LaunchedEffect }
        existing = s
        if (!loaded) {
            title = s.title
            memo = s.memo
            epochDay = s.epochDay
            allDay = s.isAllDay
            s.startMinute?.let { startMinute = it }
            s.endMinute?.let { endMinute = it }
            colorIndex = s.colorIndex
            reminder = s.reminderMinutesBefore ?: NO_REMINDER
            isDone = s.isDone
            loaded = true
        }
    }

    fun save() {
        if (title.isBlank()) {
            titleError = true
            return
        }
        viewModel.save(
            Schedule(
                id = if (isNew) 0 else scheduleId,
                title = title.trim(),
                memo = memo.trim(),
                epochDay = epochDay,
                startMinute = if (allDay) null else startMinute,
                endMinute = if (allDay) null else endMinute,
                colorIndex = colorIndex,
                reminderMinutesBefore = reminder.takeIf { it != NO_REMINDER },
                isDone = isDone,
            )
        )
        onDone()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (isNew) "새 일정" else "일정 수정") },
                navigationIcon = {
                    IconButton(onClick = onDone) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "뒤로")
                    }
                },
                actions = {
                    if (!isNew) {
                        IconButton(onClick = { confirmDelete = true }) {
                            Icon(Icons.Default.Delete, contentDescription = "삭제")
                        }
                    }
                    IconButton(onClick = { save() }) {
                        Icon(Icons.Default.Check, contentDescription = "저장")
                    }
                },
            )
        },
    ) { padding ->
        if (!loaded) return@Scaffold
        Column(
            Modifier
                .padding(padding)
                .fillMaxSize()
                .imePadding()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            OutlinedTextField(
                value = title,
                onValueChange = { title = it; titleError = false },
                label = { Text("제목") },
                isError = titleError,
                supportingText = { if (titleError) Text("제목을 입력하세요.") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )

            FieldRow("날짜", dateLabel(LocalDate.ofEpochDay(epochDay))) { picker = PickerTarget.Date }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("종일", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                Switch(checked = allDay, onCheckedChange = { allDay = it })
            }

            if (!allDay) {
                FieldRow("시작", formatMinute(startMinute)) { picker = PickerTarget.Start }
                FieldRow("종료", formatMinute(endMinute)) { picker = PickerTarget.End }
            }

            ReminderDropdown(selected = reminder, onSelect = { reminder = it })

            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("색상", style = MaterialTheme.typography.bodyLarge)
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ScheduleColors.forEachIndexed { i, color ->
                        ColorDot(color, selected = i == colorIndex) { colorIndex = i }
                    }
                }
            }

            OutlinedTextField(
                value = memo,
                onValueChange = { memo = it },
                label = { Text("메모") },
                modifier = Modifier.fillMaxWidth().heightIn(min = 120.dp),
            )

            if (!isNew) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("완료", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                    Switch(checked = isDone, onCheckedChange = { isDone = it })
                }
            }

            Button(onClick = { save() }, modifier = Modifier.fillMaxWidth()) { Text("저장") }
        }
    }

    when (picker) {
        PickerTarget.Date -> DateDialog(
            initial = LocalDate.ofEpochDay(epochDay),
            onDismiss = { picker = null },
            onPick = { epochDay = it.toEpochDay(); picker = null },
        )
        PickerTarget.Start -> TimeDialog(
            initialMinute = startMinute,
            onDismiss = { picker = null },
            onPick = { m ->
                // Keep the duration when moving the start time.
                val duration = (endMinute - startMinute).coerceAtLeast(0)
                startMinute = m
                endMinute = (m + duration).coerceAtMost(23 * 60 + 59)
                picker = null
            },
        )
        PickerTarget.End -> TimeDialog(
            initialMinute = endMinute,
            onDismiss = { picker = null },
            onPick = { m ->
                endMinute = m.coerceAtLeast(startMinute)
                picker = null
            },
        )
        null -> Unit
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("일정 삭제") },
            text = { Text("이 일정을 삭제할까요?") },
            confirmButton = {
                TextButton(onClick = {
                    existing?.let(viewModel::delete)
                    confirmDelete = false
                    onDone()
                }) { Text("삭제") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("취소") } },
        )
    }
}

@Composable
private fun FieldRow(label: String, value: String, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
        Text(value, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.primary)
    }
}

@Composable
private fun ColorDot(color: Color, selected: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .size(32.dp)
            .background(color, CircleShape)
            .then(
                if (selected) Modifier.border(3.dp, MaterialTheme.colorScheme.onSurface, CircleShape)
                else Modifier
            )
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (selected) Icon(Icons.Default.Check, contentDescription = "선택됨", tint = Color.White)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ReminderDropdown(selected: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val label = ReminderOptions.first { (it.first ?: NO_REMINDER) == selected }.second
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = label,
            onValueChange = {},
            readOnly = true,
            label = { Text("알림") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.fillMaxWidth().menuAnchor(MenuAnchorType.PrimaryNotEditable),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            ReminderOptions.forEach { (minutes, text) ->
                DropdownMenuItem(
                    text = { Text(text) },
                    onClick = {
                        onSelect(minutes ?: NO_REMINDER)
                        expanded = false
                    },
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DateDialog(initial: LocalDate, onDismiss: () -> Unit, onPick: (LocalDate) -> Unit) {
    // DatePicker works in UTC millis.
    val state = rememberDatePickerState(
        initialSelectedDateMillis = initial.atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli()
    )
    DatePickerDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = {
                state.selectedDateMillis?.let {
                    onPick(Instant.ofEpochMilli(it).atZone(ZoneOffset.UTC).toLocalDate())
                } ?: onDismiss()
            }) { Text("확인") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("취소") } },
    ) {
        DatePicker(state = state)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TimeDialog(initialMinute: Int, onDismiss: () -> Unit, onPick: (Int) -> Unit) {
    val state = rememberTimePickerState(
        initialHour = initialMinute / 60,
        initialMinute = initialMinute % 60,
        is24Hour = true,
    )
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = { onPick(state.hour * 60 + state.minute) }) { Text("확인") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("취소") } },
        text = { TimePicker(state = state) },
    )
}

private fun dateLabel(date: LocalDate): String =
    "${date.year}년 ${date.monthValue}월 ${date.dayOfMonth}일 (" +
        date.dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.KOREAN) + ")"

/** Next full hour from now, capped so the default end time still fits in the day. */
private fun defaultStartMinute(): Int =
    ((java.time.LocalTime.now().hour + 1) * 60).coerceAtMost(22 * 60)
