"""Deterministic demonstration model. Percentages are normalized sensor readings."""

from math import log

SENSORS = ("air", "humidity", "outside_light", "inside_light", "soil", "water", "glass")
SCENARIOS = {
    "normal": (24, 64, 48, 60, 75, 23, 22, 55),
    "heat": (31, 74, 65, 60, 75, 25, 21, 50),
    "sun": (24, 61, 95, 60, 75, 24, 22, 55),
    "dry": (24, 64, 48, 24, 75, 23, 22, 55),
    "empty": (24, 64, 48, 24, 5, 23, 22, 55),
    "morning": (24, 84, 36, 60, 75, 14, 16, 48),
    "fault": (24, 64, 48, 24, 75, 23, 22, 55),
}


class Greenhouse:
    def __init__(self):
        self.scenario("normal")

    def scenario(self, name):
        (self.air, self.humidity, self.outside_light, self.soil, self.water,
         self.glass, self.outside_temperature, self.outside_humidity) = SCENARIOS[name]
        self.name = name
        self.inside_light = self.outside_light * 0.9

    def tick(self, state, pump_seconds=None):
        angle, steps, pump, _busy = state
        vent = angle / 180
        shade = steps / 200
        self.inside_light = self.outside_light * (0.9 - 0.65 * shade)
        self.air += 0.11 * self.inside_light / 100 - (0.03 + 0.30 * vent) * (
            self.air - self.outside_temperature
        )
        self.humidity += 0.14 - (0.015 + 0.32 * vent) * (
            self.humidity - self.outside_humidity
        )
        self.glass += 0.045 * (
            0.65 * self.outside_temperature + 0.35 * self.air - self.glass
        )
        self.soil = max(0, self.soil - 0.018 - 0.035 * self.inside_light / 100)
        if pump_seconds is None:
            pump_seconds = int(pump)
        if pump_seconds > 0 and self.water > 0:
            used = min(self.water, 1.5 * pump_seconds)
            self.water -= used
            self.soil = min(100, self.soil + 3.6 * used)
            self.humidity = min(100, self.humidity + 0.25 * pump_seconds)

    @property
    def dew_point(self):
        gamma = log(self.humidity / 100) + (17.62 * self.air) / (243.12 + self.air)
        return 243.12 * gamma / (17.62 - gamma)

    @property
    def condensation(self):
        return self.glass <= self.dew_point

    def readings(self):
        return {
            name: None if name == "soil" and self.name == "fault" else round(getattr(self, name), 1)
            for name in SENSORS
        }

    def frame(self):
        values = self.readings()
        fields = (
            "NA" if values[name] is None else str(round(values[name] * scale))
            for name, scale in zip(SENSORS, (10, 1, 1, 1, 1, 1, 10))
        )
        return ("SENS:" + ",".join(fields) + "\n").encode("ascii")
