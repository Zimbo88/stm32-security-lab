#ifndef BYTE_READER_H
#define BYTE_READER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BYTE_READER_OK = 0,
    BYTE_READER_NO_DATA = 1,
    BYTE_READER_TIMEOUT = 2,
    BYTE_READER_ERR_INVALID_ARGUMENT = 3,
    BYTE_READER_ERR_IO = 4
} byte_reader_status_t;

typedef byte_reader_status_t (*byte_reader_getc_fn_t)(
    void *context,
    uint8_t *byte
);

typedef struct {
    void *context;
    byte_reader_getc_fn_t getc;
} byte_reader_t;

void byte_reader_init(
    byte_reader_t *reader,
    byte_reader_getc_fn_t getc,
    void *context
);
byte_reader_status_t byte_reader_getc_nonblocking(
    const byte_reader_t *reader,
    uint8_t *byte
);
byte_reader_status_t byte_reader_getc_timeout(
    const byte_reader_t *reader,
    uint8_t *byte,
    uint32_t timeout_polls
);
byte_reader_status_t byte_reader_read_timeout(
    const byte_reader_t *reader,
    uint8_t *buffer,
    size_t length,
    uint32_t per_byte_timeout_polls,
    size_t *bytes_read
);
const char *byte_reader_status_text(byte_reader_status_t status);

#endif
