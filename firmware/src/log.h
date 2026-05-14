#pragma once
#include <stdint.h>

#define LOG_BUFFER_SIZE 128
#define LOG_MESSAGE_LEN 64

void log_init(void);
void log_add(const char* message);
const char* log_get_line(int index);  // 0 = oldest, LOG_BUFFER_SIZE-1 = newest
int log_get_count(void);
