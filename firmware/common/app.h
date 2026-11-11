#ifndef GREENHOUSE_APP_H
#define GREENHOUSE_APP_H

#include <stdbool.h>
#include <stdint.h>

void app_init(void);
void app_tick(uint32_t now);
void app_receive(const char *line, uint32_t now);

void io_send(const char *line);
void io_servo(int angle);
void io_direction(bool forward);
void io_step(bool high);
void io_pump(bool active);

static inline bool due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

#endif
