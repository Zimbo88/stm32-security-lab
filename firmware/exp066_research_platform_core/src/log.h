#ifndef LOG_H
#define LOG_H
#include <stdint.h>
typedef enum { LOG_DEBUG=0, LOG_INFO=1, LOG_WARNING=2, LOG_ERROR=3, LOG_SECURITY=4 } log_severity_t;
void log_init(void);
void log_write(log_severity_t severity, uint16_t source, uint16_t event, const void *payload, uint8_t length);
void log_show(void);
void log_clear(void);
uint32_t log_count(void);
#endif
