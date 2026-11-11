#ifndef GREENHOUSE_WIRE_H
#define GREENHOUSE_WIRE_H

#include <stddef.h>

#include "app.h"

typedef struct {
    char line[80];
    size_t length;
    bool discard;
} FrameReader;

void frame_byte(FrameReader *reader, uint8_t byte, uint32_t now);
bool wire_integer(const char **input, int *value);
bool wire_prefix(const char **input, const char *prefix);

#endif
