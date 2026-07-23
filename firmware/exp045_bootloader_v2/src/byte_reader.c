#include "byte_reader.h"

void byte_reader_init(
    byte_reader_t *reader,
    byte_reader_getc_fn_t getc,
    void *context
)
{
    if (reader != NULL) {
        reader->context = context;
        reader->getc = getc;
    }
}

byte_reader_status_t byte_reader_getc_nonblocking(
    const byte_reader_t *reader,
    uint8_t *byte
)
{
    if ((reader == NULL) || (reader->getc == NULL) || (byte == NULL)) {
        return BYTE_READER_ERR_INVALID_ARGUMENT;
    }

    return reader->getc(reader->context, byte);
}

byte_reader_status_t byte_reader_getc_timeout(
    const byte_reader_t *reader,
    uint8_t *byte,
    uint32_t timeout_polls
)
{
    if ((reader == NULL) || (reader->getc == NULL) || (byte == NULL)) {
        return BYTE_READER_ERR_INVALID_ARGUMENT;
    }

    while (timeout_polls != 0U) {
        const byte_reader_status_t status =
            byte_reader_getc_nonblocking(reader, byte);

        if (status != BYTE_READER_NO_DATA) {
            return status;
        }

        --timeout_polls;
    }

    return BYTE_READER_TIMEOUT;
}

byte_reader_status_t byte_reader_read_timeout(
    const byte_reader_t *reader,
    uint8_t *buffer,
    size_t length,
    uint32_t per_byte_timeout_polls,
    size_t *bytes_read
)
{
    size_t count = 0U;

    if (bytes_read != NULL) {
        *bytes_read = 0U;
    }

    if ((reader == NULL) ||
        (reader->getc == NULL) ||
        ((length != 0U) && (buffer == NULL))) {
        return BYTE_READER_ERR_INVALID_ARGUMENT;
    }

    while (count < length) {
        const byte_reader_status_t status = byte_reader_getc_timeout(
            reader,
            &buffer[count],
            per_byte_timeout_polls
        );

        if (status != BYTE_READER_OK) {
            if (bytes_read != NULL) {
                *bytes_read = count;
            }
            return status;
        }

        ++count;
    }

    if (bytes_read != NULL) {
        *bytes_read = count;
    }
    return BYTE_READER_OK;
}

const char *byte_reader_status_text(byte_reader_status_t status)
{
    switch (status) {
    case BYTE_READER_OK:                   return "OK";
    case BYTE_READER_NO_DATA:              return "NO DATA";
    case BYTE_READER_TIMEOUT:              return "TIMEOUT";
    case BYTE_READER_ERR_INVALID_ARGUMENT: return "INVALID ARGUMENT";
    case BYTE_READER_ERR_IO:               return "IO";
    default:                               return "UNKNOWN";
    }
}
