#define _POSIX_C_SOURCE 200809L

#include "wire.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static uint64_t time_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void io_send(const char *line)
{
    puts(line);
    fflush(stdout);
}

void io_servo(int angle) { (void)angle; }
void io_direction(bool forward) { (void)forward; }
void io_step(bool high) { (void)high; }
void io_pump(bool active) { (void)active; }

int main(void)
{
    FrameReader reader = {0};
    struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
    const uint64_t started = time_ms();
    app_init();
    for (;;) {
        app_tick((uint32_t)(time_ms() - started));
        const int available = poll(&input, 1, 1);
        if (available == 0 || (available < 0 && errno == EINTR)) {
            continue;
        }
        if (available < 0) {
            return 1;
        }
        uint8_t chunk[128];
        const ssize_t count = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return count == 0 ? 0 : 1;
        }
        for (ssize_t i = 0; i < count; ++i) {
            frame_byte(&reader, chunk[i], (uint32_t)(time_ms() - started));
        }
    }
}
