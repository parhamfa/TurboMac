#include "SMCReader.h"

#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    SMC_USER_CLIENT_SELECTOR = 2,
    SMC_COMMAND_READ_BYTES = 5,
    SMC_COMMAND_READ_KEY_INFO = 9,
};

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t build;
    uint8_t reserved;
    uint16_t release;
} SMCVersion;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpu_limit;
    uint32_t gpu_limit;
    uint32_t memory_limit;
} SMCPLimitData;

typedef struct {
    uint32_t data_size;
    uint32_t data_type;
    uint8_t data_attributes;
} SMCKeyInfo;

typedef struct {
    uint32_t key;
    SMCVersion version;
    SMCPLimitData power_limits;
    SMCKeyInfo key_info;
    uint8_t result;
    uint8_t status;
    uint8_t command;
    uint32_t data32;
    uint8_t bytes[SMC_MAX_DATA_SIZE];
} SMCKeyData;

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static void set_kern_error(
    char *error,
    size_t error_size,
    const char *operation,
    kern_return_t result
) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s failed: 0x%08x", operation, result);
    }
}

static uint32_t key_to_u32(const char key[SMC_KEY_LENGTH + 1]) {
    return ((uint32_t)(uint8_t)key[0] << 24)
        | ((uint32_t)(uint8_t)key[1] << 16)
        | ((uint32_t)(uint8_t)key[2] << 8)
        | (uint32_t)(uint8_t)key[3];
}

static void u32_to_key(uint32_t value, char key[SMC_KEY_LENGTH + 1]) {
    key[0] = (char)((value >> 24) & 0xff);
    key[1] = (char)((value >> 16) & 0xff);
    key[2] = (char)((value >> 8) & 0xff);
    key[3] = (char)(value & 0xff);
    key[4] = '\0';
}

static bool smc_call(
    io_connect_t connection,
    const SMCKeyData *input,
    SMCKeyData *output,
    char *error,
    size_t error_size
) {
    size_t output_size = sizeof(*output);
    kern_return_t result = IOConnectCallStructMethod(
        connection,
        SMC_USER_CLIENT_SELECTOR,
        input,
        sizeof(*input),
        output,
        &output_size
    );
    if (result != KERN_SUCCESS) {
        set_kern_error(error, error_size, "AppleSMC read", result);
        return false;
    }
    if (output_size != sizeof(*output)) {
        set_error(error, error_size, "AppleSMC returned an unexpected payload size");
        return false;
    }
    if (output->result != 0) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "AppleSMC returned result 0x%02x", output->result);
        }
        return false;
    }
    return true;
}

bool smc_reader_open(SMCReader *reader, char *error, size_t error_size) {
    if (reader == NULL) {
        set_error(error, error_size, "reader is null");
        return false;
    }
    reader->connection = IO_OBJECT_NULL;
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching("AppleSMC")
    );
    if (service == IO_OBJECT_NULL) {
        set_error(error, error_size, "AppleSMC service was not found");
        return false;
    }
    kern_return_t result = IOServiceOpen(service, mach_task_self(), 0, &reader->connection);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS) {
        reader->connection = IO_OBJECT_NULL;
        set_kern_error(error, error_size, "IOServiceOpen", result);
        return false;
    }
    return true;
}

void smc_reader_close(SMCReader *reader) {
    if (reader == NULL || reader->connection == IO_OBJECT_NULL) {
        return;
    }
    IOServiceClose(reader->connection);
    reader->connection = IO_OBJECT_NULL;
}

bool smc_reader_read(
    SMCReader *reader,
    const char key[SMC_KEY_LENGTH + 1],
    SMCRawValue *value,
    char *error,
    size_t error_size
) {
    if (reader == NULL
        || reader->connection == IO_OBJECT_NULL
        || key == NULL
        || strlen(key) != SMC_KEY_LENGTH
        || value == NULL) {
        set_error(error, error_size, "invalid AppleSMC read argument");
        return false;
    }

    SMCKeyData input = {};
    SMCKeyData output = {};
    input.key = key_to_u32(key);
    input.command = SMC_COMMAND_READ_KEY_INFO;
    if (!smc_call(reader->connection, &input, &output, error, error_size)) {
        return false;
    }
    if (output.key_info.data_size == 0 || output.key_info.data_size > SMC_MAX_DATA_SIZE) {
        set_error(error, error_size, "AppleSMC key has an invalid data size");
        return false;
    }

    SMCKeyInfo key_info = output.key_info;
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.key = key_to_u32(key);
    input.key_info.data_size = key_info.data_size;
    input.command = SMC_COMMAND_READ_BYTES;
    if (!smc_call(reader->connection, &input, &output, error, error_size)) {
        return false;
    }

    memset(value, 0, sizeof(*value));
    memcpy(value->key, key, SMC_KEY_LENGTH);
    value->key[SMC_KEY_LENGTH] = '\0';
    u32_to_key(key_info.data_type, value->type);
    value->size = key_info.data_size;
    memcpy(value->bytes, output.bytes, value->size);
    return true;
}

static uint16_t read_be16(const uint8_t bytes[2]) {
    uint16_t raw;
    memcpy(&raw, bytes, sizeof(raw));
    return ntohs(raw);
}

static uint32_t read_be32(const uint8_t bytes[4]) {
    uint32_t raw;
    memcpy(&raw, bytes, sizeof(raw));
    return ntohl(raw);
}

static int hexadecimal_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool smc_decode_numeric(const SMCRawValue *value, double *decoded) {
    if (value == NULL || decoded == NULL) {
        return false;
    }
    if (memcmp(value->type, "flt ", 4) == 0 && value->size == 4) {
        float raw;
        memcpy(&raw, value->bytes, sizeof(raw));
        if (!isfinite(raw)) {
            return false;
        }
        *decoded = raw;
        return true;
    }
    if (memcmp(value->type, "ui8 ", 4) == 0 && value->size == 1) {
        *decoded = value->bytes[0];
        return true;
    }
    if (memcmp(value->type, "ui16", 4) == 0 && value->size == 2) {
        *decoded = read_be16(value->bytes);
        return true;
    }
    if (memcmp(value->type, "ui32", 4) == 0 && value->size == 4) {
        *decoded = read_be32(value->bytes);
        return true;
    }
    if (memcmp(value->type, "si8 ", 4) == 0 && value->size == 1) {
        *decoded = (int8_t)value->bytes[0];
        return true;
    }
    if (memcmp(value->type, "si16", 4) == 0 && value->size == 2) {
        *decoded = (int16_t)read_be16(value->bytes);
        return true;
    }
    if (memcmp(value->type, "flag", 4) == 0 && value->size == 1) {
        *decoded = value->bytes[0] == 0 ? 0.0 : 1.0;
        return true;
    }

    const bool signed_fixed = memcmp(value->type, "sp", 2) == 0;
    const bool unsigned_fixed = memcmp(value->type, "fp", 2) == 0;
    const int fractional_bits = hexadecimal_digit(value->type[3]);
    if (value->size == 2
        && fractional_bits >= 0
        && (signed_fixed || unsigned_fixed)) {
        const uint16_t raw = read_be16(value->bytes);
        const double scale = (double)(1U << fractional_bits);
        *decoded = signed_fixed
            ? (double)(int16_t)raw / scale
            : (double)raw / scale;
        return true;
    }
    return false;
}
