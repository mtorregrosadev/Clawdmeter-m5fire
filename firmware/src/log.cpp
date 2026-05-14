#include "log.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>

static char log_buffer[LOG_BUFFER_SIZE][LOG_MESSAGE_LEN];
static int log_index = 0;
static int log_count = 0;
static bool log_dirty = false;

void log_init(void) {
    log_index = 0;
    log_count = 0;
    log_dirty = false;
    memset(log_buffer, 0, sizeof(log_buffer));
}

void log_add(const char* message) {
    if (!message) return;
    strncpy(log_buffer[log_index], message, LOG_MESSAGE_LEN - 1);
    log_buffer[log_index][LOG_MESSAGE_LEN - 1] = '\0';
    log_index = (log_index + 1) % LOG_BUFFER_SIZE;
    if (log_count < LOG_BUFFER_SIZE) log_count++;
    log_dirty = true;
}

void log_format(const char* fmt, ...) {
    if (!fmt) return;
    char buf[LOG_MESSAGE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_add(buf);
}

const char* log_get_line(int index) {
    if (index < 0 || index >= log_count) return "";
    int pos = (log_index - log_count + index) % LOG_BUFFER_SIZE;
    if (pos < 0) pos += LOG_BUFFER_SIZE;
    return log_buffer[pos];
}

int log_get_count(void) {
    return log_count;
}

bool log_consume_dirty(void) {
    bool was_dirty = log_dirty;
    log_dirty = false;
    return was_dirty;
}
