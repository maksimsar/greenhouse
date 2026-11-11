#include "wire.h"

#include <string.h>

void frame_byte(FrameReader *reader, uint8_t byte, uint32_t now)
{
    if (byte == '\n') {
        if (reader->discard) {
            app_receive("", now);
        } else {
            if (reader->length && reader->line[reader->length - 1] == '\r') {
                --reader->length;
            }
            reader->line[reader->length] = '\0';
            app_receive(reader->line, now);
        }
        reader->length = 0;
        reader->discard = false;
    } else if (reader->length + 1 >= sizeof(reader->line) ||
               (byte != '\r' && (byte < ' ' || byte > '~'))) {
        reader->discard = true;
    } else if (!reader->discard) {
        reader->line[reader->length++] = (char)byte;
    }
}

bool wire_integer(const char **input, int *value)
{
    const char *cursor = *input;
    const bool negative = *cursor == '-';
    if (negative) {
        ++cursor;
    }
    int number = 0;
    unsigned digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        if (++digits > 4) {
            return false;
        }
        number = number * 10 + (*cursor++ - '0');
    }
    if (!digits) {
        return false;
    }
    *value = negative ? -number : number;
    *input = cursor;
    return true;
}

bool wire_prefix(const char **input, const char *prefix)
{
    const size_t length = strlen(prefix);
    if (strncmp(*input, prefix, length) != 0) {
        return false;
    }
    *input += length;
    return true;
}
