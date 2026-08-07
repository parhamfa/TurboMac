#ifndef TURBOMAC_SMC_READER_H
#define TURBOMAC_SMC_READER_H

#include <IOKit/IOKitLib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SMC_MAX_DATA_SIZE = 32,
    SMC_KEY_LENGTH = 4,
};

typedef struct {
    io_connect_t connection;
} SMCReader;

typedef struct {
    char key[SMC_KEY_LENGTH + 1];
    char type[SMC_KEY_LENGTH + 1];
    uint32_t size;
    uint8_t bytes[SMC_MAX_DATA_SIZE];
} SMCRawValue;

bool smc_reader_open(SMCReader *reader, char *error, size_t error_size);
void smc_reader_close(SMCReader *reader);
bool smc_reader_read(
    SMCReader *reader,
    const char key[SMC_KEY_LENGTH + 1],
    SMCRawValue *value,
    char *error,
    size_t error_size
);
bool smc_decode_numeric(const SMCRawValue *value, double *decoded);

#ifdef __cplusplus
}
#endif

#endif
