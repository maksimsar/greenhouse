#include "app.h"
#include "wire.h"

#include <stdio.h>
#include <string.h>

enum Reading {
    AIR, HUMIDITY, OUTSIDE_LIGHT, INSIDE_LIGHT, SOIL, WATER, GLASS, READING_COUNT
};
enum Request { NONE, STATUS, MOTION, WATERING };
enum IrrigationGuard { IRRIGATION_READY, IRRIGATION_LOW_WATER, IRRIGATION_SENSOR };
enum {
    TELEMETRY_TTL_MS = 3500, STATUS_INTERVAL_MS = 2000, RESPONSE_TIMEOUT_MS = 6000,
    PUMP_PAUSE_MS = 15000, PUMP_DURATION_MS = 3000, MAX_TRAVEL = 200, MISSING = -10000
};

static struct {
    int sensors[READING_COUNT];
    bool have_sensors;
    uint32_t sensors_at;
    int angle;
    int position;
    bool pump_active;
    bool busy;
    bool synchronized;
    bool angle_diagnostic;
    enum Request request;
    bool have_completion;
    uint32_t deadline;
    uint32_t status_at;
    enum IrrigationGuard irrigation_guard;
    bool cooling_down;
    uint32_t water_at;
} controller;

static void update_irrigation_guard(void)
{
    enum IrrigationGuard guard = IRRIGATION_READY;
    if (!controller.have_sensors || controller.sensors[SOIL] == MISSING ||
        controller.sensors[WATER] == MISSING) {
        guard = IRRIGATION_SENSOR;
    } else if (controller.sensors[WATER] <= 10) {
        guard = IRRIGATION_LOW_WATER;
    }
    if (guard != controller.irrigation_guard) {
        static const char *const messages[] = {
            [IRRIGATION_READY] = "INFO:IRRIGATION,READY",
            [IRRIGATION_LOW_WATER] = "INFO:IRRIGATION,WATER",
            [IRRIGATION_SENSOR] = "INFO:IRRIGATION,SENSOR"
        };
        controller.irrigation_guard = guard;
        io_send(messages[guard]);
    }
}

static bool fields(const char *input, int *values, size_t count, bool allow_missing)
{
    for (size_t i = 0; i < count; ++i) {
        if (allow_missing && wire_prefix(&input, "NA")) {
            values[i] = MISSING;
        } else if (!wire_integer(&input, &values[i])) {
            return false;
        }
        if (i + 1 < count && *input++ != ',') {
            return false;
        }
    }
    return *input == '\0';
}

static void read_sensors(const char *line, uint32_t now)
{
    int values[READING_COUNT];
    controller.have_sensors = fields(line, values, READING_COUNT, true);
    if (!controller.have_sensors) {
        return;
    }
    for (unsigned i = 0; i < READING_COUNT; ++i) {
        const bool temperature = i == AIR || i == GLASS;
        const int minimum = temperature ? -450 : 0;
        const int maximum = temperature ? 600 : 100;
        controller.sensors[i] =
            values[i] >= minimum && values[i] <= maximum ? values[i] : MISSING;
    }
    controller.sensors_at = now;
}

static void read_state(const char *line, uint32_t now)
{
    int state[4];
    if (!fields(line, state, 4, false) ||
        state[0] < 0 || state[0] > 180 || state[1] < 0 || state[1] > MAX_TRAVEL ||
        state[2] < 0 || state[2] > 1 || state[3] < 0 || state[3] > 1 ||
        (state[2] && !state[3])) {
        return;
    }
    if (controller.pump_active && !state[2]) {
        controller.cooling_down = true;
        controller.water_at = now + PUMP_PAUSE_MS;
    }
    controller.angle = state[0];
    controller.position = state[1];
    controller.pump_active = state[2] != 0;
    controller.busy = state[3] != 0;
    if (controller.request == STATUS) {
        controller.request = NONE;
        controller.synchronized = !controller.busy;
        controller.status_at = now + STATUS_INTERVAL_MS;
    } else if (controller.request == MOTION || controller.request == WATERING) {
        controller.have_completion = !controller.busy;
    }
}

void app_init(void)
{
    memset(&controller, 0, sizeof(controller));
}

void app_receive(const char *line, uint32_t now)
{
    if (wire_prefix(&line, "SENS:")) {
        read_sensors(line, now);
        update_irrigation_guard();
    } else if (wire_prefix(&line, "STATE:")) {
        read_state(line, now);
    } else if (strcmp(line, "DIAG:ANGLE") == 0) {
        controller.angle_diagnostic = true;
    } else if (controller.request == MOTION || controller.request == WATERING) {
        if (strcmp(line, "OK") == 0 && controller.have_completion) {
            if (controller.request == WATERING) {
                controller.water_at = now + PUMP_PAUSE_MS;
            }
            controller.request = NONE;
            controller.status_at = now + STATUS_INTERVAL_MS;
        } else if (strcmp(line, "ERROR") == 0) {
            controller.request = NONE;
            controller.synchronized = false;
            controller.status_at = now;
        }
    }
}

static void request(enum Request request_type, const char *line, uint32_t now)
{
    controller.request = request_type;
    controller.deadline = now + RESPONSE_TIMEOUT_MS;
    controller.have_completion = false;
    io_send(line);
}

void app_tick(uint32_t now)
{
    if (controller.request != NONE) {
        if (!due(now, controller.deadline)) {
            return;
        }
        controller.request = NONE;
        controller.synchronized = false;
        controller.status_at = now;
    }
    if (!controller.synchronized || due(now, controller.status_at)) {
        if (due(now, controller.status_at)) {
            request(STATUS, "STATUS?", now);
        }
        return;
    }
    if (controller.busy) {
        return;
    }
    if (controller.angle_diagnostic) {
        controller.angle_diagnostic = false;
        request(MOTION, "SERV:181;", now);
        return;
    }
    if (!controller.have_sensors || now - controller.sensors_at >= TELEMETRY_TTL_MS) {
        return;
    }
    const int air = controller.sensors[AIR];
    const int humidity = controller.sensors[HUMIDITY];
    if ((air >= 280 || humidity >= 80) && controller.angle != 90) {
        request(MOTION, "SERV:090;", now);
        return;
    }
    if (air != MISSING && humidity != MISSING && air <= 250 && humidity <= 70 &&
        controller.angle != 0) {
        request(MOTION, "SERV:000;", now);
        return;
    }
    const int light = controller.sensors[OUTSIDE_LIGHT];
    if (light != MISSING && ((light >= 75 && controller.position < MAX_TRAVEL) ||
                             (light <= 55 && controller.position > 0))) {
        char line[24];
        const bool forward = light >= 75;
        const int count = forward ? MAX_TRAVEL - controller.position : controller.position;
        snprintf(line, sizeof(line), "STEP:%s,%d;", forward ? "FWD" : "REV", count);
        request(MOTION, line, now);
        return;
    }
    if (controller.cooling_down && due(now, controller.water_at)) {
        controller.cooling_down = false;
    }
    if (controller.irrigation_guard == IRRIGATION_READY &&
        controller.sensors[SOIL] < 35 && !controller.cooling_down) {
        controller.cooling_down = true;
        controller.water_at = now + PUMP_DURATION_MS + PUMP_PAUSE_MS;
        request(WATERING, "PUMP:003;", now);
    }
}
