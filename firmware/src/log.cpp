#include "log.h"
#include <cstring>

static char log_buffer[LOG_BUFFER_SIZE][LOG_MESSAGE_LEN];
static int log_index = 0;
static int log_count = 0;

void log_init(void) {
    log_index = 0;
    log_count = 0;
    memset(log_buffer, 0, sizeof(log_buffer));
}

void log_add(const char* message) {
    if (!message) return;
    strncpy(log_buffer[log_index], message, LOG_MESSAGE_LEN - 1);
    log_buffer[log_index][LOG_MESSAGE_LEN - 1] = '\0';
    log_index = (log_index + 1) % LOG_BUFFER_SIZE;
    if (log_count < LOG_BUFFER_SIZE) log_count++;
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
