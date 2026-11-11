"""Track one demonstration using frames observed at the two UART endpoints."""

COMMANDS = {
    "heat": "SERV:090;",
    "sun": "STEP:FWD,200;",
    "dry": "PUMP:003;",
    "morning": "SERV:090;",
    "invalid": "SERV:181;",
}
GUARDS = {"empty": "INFO:IRRIGATION,WATER", "fault": "INFO:IRRIGATION,SENSOR"}
TITLES = {
    "heat": "Жара", "sun": "Яркое солнце", "dry": "Сухой грунт",
    "morning": "Холодное утро", "empty": "Пустой бак", "fault": "Сбой датчика",
    "invalid": "Ошибочный угол",
}
LABELS = {
    "heat": "Проверяем, как форточка реагирует на перегрев.",
    "sun": "Сравниваем освещённость снаружи и под шторой.",
    "dry": "Следим за помпой, влажностью грунта и уровнем воды.",
    "morning": "Сравниваем температуру стекла с точкой росы до и после проветривания.",
    "empty": "Проверяем блокировку полива при уровне воды 10% и ниже.",
    "fault": "Проверяем реакцию на потерю показаний влажности грунта.",
    "invalid": "Передаём недопустимый угол 181° и проверяем ответ исполнительной платы.",
}


class Experiment:
    def __init__(self, name, now):
        self.name = name
        self.phase = "preparing"
        self.deadline = now + 35
        self.message = "Возвращаем приводы в исходное положение."
        self.input_frame = None
        self.command = None
        self.response = None
        self.result = None
        self.initial = None
        self.saw_pump = False

    def begin(self, readings, now):
        self.initial = readings
        self.phase = "running"
        self.deadline = now + 28
        self.message = LABELS[self.name]

    def fail(self, message):
        self.phase = "failed"
        self.message = message

    def frame(self, route, frame):
        if self.phase != "running":
            return
        if route == "Модель → МК 1" and self.input_frame is None:
            self.input_frame = frame
        if self.name == "invalid" and route == "Стенд → МК 1":
            self.input_frame = frame
        if route == "МК 1" and frame == GUARDS.get(self.name):
            self.command = frame
            self.response = "Полив не запрошен"
        if route == "МК 1 → МК 2":
            if frame == COMMANDS.get(self.name):
                self.command = frame
            elif self.name in GUARDS and frame.startswith("PUMP:"):
                self.fail("МК 1 запросил полив при активной блокировке.")
        if route == "МК 2 → МК 1" and self.command and self.name not in GUARDS and frame in ("OK", "ERROR"):
            self.response = frame
            if (frame == "ERROR") != (self.name == "invalid"):
                self.fail("Ответ МК 2 отличается от ожидаемого.")

    def check(self, house, state):
        if self.phase != "running" or state is None:
            return
        angle, steps, pump, busy = state
        self.saw_pump |= bool(pump)
        if not self.response or busy:
            return
        values = house.readings()
        initial = self.initial
        name = self.name
        if name == "heat" and values["air"] < initial["air"] - 0.5:
            self.result = f'Форточка открылась. Температура {initial["air"]} → {values["air"]} °C.'
        elif name == "sun" and steps == 200 and values["inside_light"] < initial["inside_light"]:
            self.result = f'Штора выдвинута на 200 шагов. Свет над грядкой {initial["inside_light"]} → {values["inside_light"]}%.'
        elif name == "dry" and self.saw_pump and values["soil"] > initial["soil"] and values["water"] < initial["water"]:
            self.result = f'Полив завершён. Влажность грунта {initial["soil"]} → {values["soil"]}%.'
        elif name == "morning" and not house.condensation:
            self.result = f'После проветривания стекло {values["glass"]} °C, точка росы {house.dew_point:.1f} °C. Конденсат исчез.'
        elif name in GUARDS and not pump:
            self.result = "Помпа осталась выключенной."
        elif name == "invalid" and angle == 0 and steps == 0 and not pump:
            self.result = "МК 2 отклонил угол 181°. Приводы остались в исходном положении."
        if self.result is not None:
            self.phase = "passed"
            self.message = "Результат подтверждён показаниями и сообщениями контроллеров."

    def snapshot(self):
        return {
            "name": self.name, "title": TITLES[self.name], "phase": self.phase,
            "message": self.message, "input": self.input_frame,
            "command": self.command, "response": self.response, "result": self.result,
        }
