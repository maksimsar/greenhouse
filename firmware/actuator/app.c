#include "app.h"
#include "wire.h"

#include <stdio.h>
#include <string.h>

enum Action { IDLE, SERVO, STEP, PUMP };
enum { MAX_TRAVEL = 200, STEP_HALF_PERIOD_MS = 2, SERVO_SETTLE_MS = 400 };

static struct {
    enum Action action;
    int angle;
    int position;
    int remaining;
    int direction;
    uint32_t deadline;
    bool step_high;
} actuator;

static void report_state(void)
{
    char line[32];
    snprintf(line, sizeof(line), "STATE:%d,%d,%d,%d", actuator.angle,
             actuator.position, actuator.action == PUMP, actuator.action != IDLE);
    io_send(line);
}

static void complete(void)
{
    actuator.action = IDLE;
    report_state();
    io_send("OK");
}

void app_init(void)
{
    memset(&actuator, 0, sizeof(actuator));
    io_servo(0);
    io_step(false);
    io_direction(false);
    io_pump(false);
}

static bool argument(const char *input, int *value)
{
    return *input >= '0' && *input <= '9' &&
           wire_integer(&input, value) && strcmp(input, ";") == 0;
}

void app_receive(const char *line, uint32_t now)
{
    if (strcmp(line, "STATUS?") == 0) {
        report_state();
        return;
    }
    if (actuator.action != IDLE) {
        io_send("ERROR");
        return;
    }

    int value;
    const char *arguments = line;
    if (wire_prefix(&arguments, "SERV:") && argument(arguments, &value) && value <= 180) {
        actuator.angle = value;
        actuator.action = SERVO;
        actuator.deadline = now + SERVO_SETTLE_MS;
        io_servo(value);
        return;
    }

    int direction = 0;
    arguments = line;
    if (wire_prefix(&arguments, "STEP:FWD,")) {
        direction = 1;
    } else if (wire_prefix(&arguments, "STEP:REV,")) {
        direction = -1;
    }
    if (direction && argument(arguments, &value) && value > 0) {
        const int target = actuator.position + direction * value;
        if (target >= 0 && target <= MAX_TRAVEL) {
            actuator.action = STEP;
            actuator.direction = direction;
            actuator.remaining = value;
            actuator.deadline = now + STEP_HALF_PERIOD_MS;
            io_direction(direction > 0);
            return;
        }
    }

    if (strcmp(line, "PUMP:003;") == 0) {
        actuator.action = PUMP;
        actuator.deadline = now + 3000;
        io_pump(true);
        report_state();
        return;
    }
    io_send("ERROR");
}

void app_tick(uint32_t now)
{
    if (actuator.action == IDLE || !due(now, actuator.deadline)) {
        return;
    }
    switch (actuator.action) {
    case STEP:
        actuator.step_high = !actuator.step_high;
        io_step(actuator.step_high);
        actuator.deadline = now + STEP_HALF_PERIOD_MS;
        if (actuator.step_high) {
            actuator.position += actuator.direction;
        } else if (--actuator.remaining == 0) {
            complete();
        }
        break;
    case PUMP:
        io_pump(false);
        complete();
        break;
    case SERVO:
        complete();
        break;
    case IDLE:
        break;
    }
}
