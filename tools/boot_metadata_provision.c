#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_metadata_provision_core.h"
#include "stm32f429_memory_layout.h"

#define EXIT_OK 0
#define EXIT_FAILED 1

typedef struct {
    const char *slot;
    const char *candidate_slot;
    const char *image_version;
    const char *candidate_image_version;
    const char *copy_a_output;
    const char *copy_b_output;
    const char *confirmed_output;
    const char *writing_output;
    const char *candidate_ready_output;
    const char *json_output;
    uint8_t sector_image;
} options_t;

static void print_usage(FILE *stream)
{
    fprintf(
        stream,
        "Usage:\n"
        "  boot_metadata_provision create-confirmed --slot a|b "
        "--image-version N --copy-a-output PATH --copy-b-output PATH "
        "[--sector-image] [--json-output PATH]\n"
        "  boot_metadata_provision create-update-sequence --active-slot a|b "
        "--active-version N --candidate-slot a|b --candidate-version N "
        "--confirmed-output PATH --writing-output PATH "
        "--candidate-ready-output PATH [--json-output PATH]\n"
    );
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return 0;
    }

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if ((errno != 0) || (end == text) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        return 0;
    }

    *value = (uint32_t)parsed;
    return 1;
}

static int parse_slot(const char *text, uint32_t *slot)
{
    if ((text == NULL) || (slot == NULL)) {
        return 0;
    }
    if ((strcmp(text, "a") == 0) || (strcmp(text, "A") == 0)) {
        *slot = (uint32_t)BOOT_SLOT_A;
        return 1;
    }
    if ((strcmp(text, "b") == 0) || (strcmp(text, "B") == 0)) {
        *slot = (uint32_t)BOOT_SLOT_B;
        return 1;
    }
    return 0;
}

static const char *slot_name(uint32_t slot)
{
    return (slot == (uint32_t)BOOT_SLOT_A) ? "a" : "b";
}

static int write_all(const char *path, const uint8_t *data, size_t length)
{
    FILE *file;

    if ((path == NULL) || (data == NULL)) {
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to open output '%s': %s\n", path, strerror(errno));
        return 0;
    }

    if (length != 0U && fwrite(data, 1U, length, file) != length) {
        fprintf(stderr, "failed to write output '%s': %s\n", path, strerror(errno));
        (void)fclose(file);
        return 0;
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "failed to close output '%s': %s\n", path, strerror(errno));
        return 0;
    }

    return 1;
}

static void fill_sector_image(
    uint8_t *sector,
    size_t sector_size,
    const uint8_t record[STM32F429_BOOT_METADATA_RECORD_SIZE]
)
{
    memset(sector, 0xFF, sector_size);
    memcpy(sector, record, STM32F429_BOOT_METADATA_RECORD_SIZE);
}

static int write_json_confirmed(
    const char *path,
    uint32_t slot,
    uint32_t image_version,
    size_t image_size
)
{
    FILE *file;

    if (path == NULL) {
        return 1;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to open JSON output '%s': %s\n", path, strerror(errno));
        return 0;
    }

    fprintf(
        file,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"ok\",\n"
        "  \"operation\": \"create-confirmed\",\n"
        "  \"mcu\": \"STM32F429IGT6\",\n"
        "  \"layout_profile\": \"%s\",\n"
        "  \"flash_base\": %lu,\n"
        "  \"flash_end\": %lu,\n"
        "  \"metadata_state\": \"CONFIRMED\",\n"
        "  \"sequence\": 1,\n"
        "  \"active_slot\": \"%s\",\n"
        "  \"active_slot_id\": %lu,\n"
        "  \"image_version\": %lu,\n"
        "  \"copy_a_address\": %lu,\n"
        "  \"copy_b_address\": %lu,\n"
        "  \"record_size\": %lu,\n"
        "  \"output_size\": %lu\n"
        "}\n",
        STM32F429_LAYOUT_PROFILE_NAME,
        (unsigned long)STM32F429_FLASH_BASE,
        (unsigned long)STM32F429_FLASH_END,
        slot_name(slot),
        (unsigned long)slot,
        (unsigned long)image_version,
        (unsigned long)STM32F429_BOOT_METADATA_A_BASE,
        (unsigned long)STM32F429_BOOT_METADATA_B_BASE,
        (unsigned long)STM32F429_BOOT_METADATA_RECORD_SIZE,
        (unsigned long)image_size
    );

    if (fclose(file) != 0) {
        fprintf(stderr, "failed to close JSON output '%s': %s\n", path, strerror(errno));
        return 0;
    }
    return 1;
}

static int write_json_sequence(
    const char *path,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t active_version,
    uint32_t candidate_version
)
{
    FILE *file;

    if (path == NULL) {
        return 1;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to open JSON output '%s': %s\n", path, strerror(errno));
        return 0;
    }

    fprintf(
        file,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"ok\",\n"
        "  \"operation\": \"create-update-sequence\",\n"
        "  \"layout_profile\": \"%s\",\n"
        "  \"active_slot\": \"%s\",\n"
        "  \"candidate_slot\": \"%s\",\n"
        "  \"active_version\": %lu,\n"
        "  \"candidate_version\": %lu,\n"
        "  \"record_size\": %lu,\n"
        "  \"states\": [\"CONFIRMED\", \"WRITING\", \"CANDIDATE_READY\"]\n"
        "}\n",
        STM32F429_LAYOUT_PROFILE_NAME,
        slot_name(active_slot),
        slot_name(candidate_slot),
        (unsigned long)active_version,
        (unsigned long)candidate_version,
        (unsigned long)STM32F429_BOOT_METADATA_RECORD_SIZE
    );

    if (fclose(file) != 0) {
        fprintf(stderr, "failed to close JSON output '%s': %s\n", path, strerror(errno));
        return 0;
    }
    return 1;
}

static int parse_create_confirmed(int argc, char **argv, options_t *options)
{
    for (int i = 2; i < argc; ++i) {
        if ((strcmp(argv[i], "--slot") == 0) && ((i + 1) < argc)) {
            options->slot = argv[++i];
        } else if ((strcmp(argv[i], "--image-version") == 0) && ((i + 1) < argc)) {
            options->image_version = argv[++i];
        } else if ((strcmp(argv[i], "--copy-a-output") == 0) && ((i + 1) < argc)) {
            options->copy_a_output = argv[++i];
        } else if ((strcmp(argv[i], "--copy-b-output") == 0) && ((i + 1) < argc)) {
            options->copy_b_output = argv[++i];
        } else if ((strcmp(argv[i], "--json-output") == 0) && ((i + 1) < argc)) {
            options->json_output = argv[++i];
        } else if (strcmp(argv[i], "--sector-image") == 0) {
            options->sector_image = 1U;
        } else {
            fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]);
            return 0;
        }
    }

    return (options->slot != NULL) &&
           (options->image_version != NULL) &&
           (options->copy_a_output != NULL) &&
           (options->copy_b_output != NULL);
}

static int parse_create_update_sequence(int argc, char **argv, options_t *options)
{
    for (int i = 2; i < argc; ++i) {
        if ((strcmp(argv[i], "--active-slot") == 0) && ((i + 1) < argc)) {
            options->slot = argv[++i];
        } else if ((strcmp(argv[i], "--active-version") == 0) && ((i + 1) < argc)) {
            options->image_version = argv[++i];
        } else if ((strcmp(argv[i], "--candidate-slot") == 0) && ((i + 1) < argc)) {
            options->candidate_slot = argv[++i];
        } else if ((strcmp(argv[i], "--candidate-version") == 0) && ((i + 1) < argc)) {
            options->candidate_image_version = argv[++i];
        } else if ((strcmp(argv[i], "--confirmed-output") == 0) && ((i + 1) < argc)) {
            options->confirmed_output = argv[++i];
        } else if ((strcmp(argv[i], "--writing-output") == 0) && ((i + 1) < argc)) {
            options->writing_output = argv[++i];
        } else if ((strcmp(argv[i], "--candidate-ready-output") == 0) && ((i + 1) < argc)) {
            options->candidate_ready_output = argv[++i];
        } else if ((strcmp(argv[i], "--json-output") == 0) && ((i + 1) < argc)) {
            options->json_output = argv[++i];
        } else {
            fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]);
            return 0;
        }
    }

    return (options->slot != NULL) &&
           (options->image_version != NULL) &&
           (options->candidate_slot != NULL) &&
           (options->candidate_image_version != NULL) &&
           (options->confirmed_output != NULL) &&
           (options->writing_output != NULL) &&
           (options->candidate_ready_output != NULL);
}

static int run_create_confirmed(const options_t *options)
{
    uint8_t record[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_a_sector[STM32F429_BOOT_METADATA_A_SIZE];
    uint8_t copy_b_sector[STM32F429_BOOT_METADATA_B_SIZE];
    const uint8_t *copy_a = record;
    const uint8_t *copy_b = record;
    size_t copy_a_size = sizeof(record);
    size_t copy_b_size = sizeof(record);
    uint32_t active_slot;
    uint32_t image_version;
    boot_metadata_status_t status;

    if ((parse_slot(options->slot, &active_slot) == 0) ||
        (parse_u32(options->image_version, &image_version) == 0)) {
        fprintf(stderr, "invalid confirmed metadata arguments\n");
        return EXIT_FAILED;
    }

    status = boot_metadata_provision_confirmed_image(
        active_slot,
        image_version,
        record
    );
    if (status != BOOT_METADATA_OK) {
        fprintf(stderr, "failed to create confirmed metadata: %s\n",
                boot_metadata_status_text(status));
        return EXIT_FAILED;
    }

    if (options->sector_image != 0U) {
        fill_sector_image(copy_a_sector, sizeof(copy_a_sector), record);
        fill_sector_image(copy_b_sector, sizeof(copy_b_sector), record);
        copy_a = copy_a_sector;
        copy_b = copy_b_sector;
        copy_a_size = sizeof(copy_a_sector);
        copy_b_size = sizeof(copy_b_sector);
    }

    if ((write_all(options->copy_a_output, copy_a, copy_a_size) == 0) ||
        (write_all(options->copy_b_output, copy_b, copy_b_size) == 0) ||
        (write_json_confirmed(
            options->json_output,
            active_slot,
            image_version,
            copy_a_size
        ) == 0)) {
        return EXIT_FAILED;
    }

    return (copy_a_size == copy_b_size) ? EXIT_OK : EXIT_FAILED;
}

static int run_create_update_sequence(const options_t *options)
{
    uint8_t confirmed[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t writing[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t ready[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint32_t active_slot;
    uint32_t candidate_slot;
    uint32_t active_version;
    uint32_t candidate_version;
    boot_metadata_status_t status;

    if ((parse_slot(options->slot, &active_slot) == 0) ||
        (parse_slot(options->candidate_slot, &candidate_slot) == 0) ||
        (parse_u32(options->image_version, &active_version) == 0) ||
        (parse_u32(options->candidate_image_version, &candidate_version) == 0)) {
        fprintf(stderr, "invalid update sequence arguments\n");
        return EXIT_FAILED;
    }

    status = boot_metadata_provision_update_sequence(
        active_slot,
        candidate_slot,
        active_version,
        candidate_version,
        confirmed,
        writing,
        ready
    );
    if (status != BOOT_METADATA_OK) {
        fprintf(stderr, "failed to create update metadata sequence: %s\n",
                boot_metadata_status_text(status));
        return EXIT_FAILED;
    }

    if ((write_all(options->confirmed_output, confirmed, sizeof(confirmed)) == 0) ||
        (write_all(options->writing_output, writing, sizeof(writing)) == 0) ||
        (write_all(options->candidate_ready_output, ready, sizeof(ready)) == 0) ||
        (write_json_sequence(
            options->json_output,
            active_slot,
            candidate_slot,
            active_version,
            candidate_version
        ) == 0)) {
        return EXIT_FAILED;
    }

    return EXIT_OK;
}

int main(int argc, char **argv)
{
    options_t options;

    memset(&options, 0, sizeof(options));
    if (argc < 2) {
        print_usage(stderr);
        return EXIT_FAILED;
    }
    if ((strcmp(argv[1], "--help") == 0) || (strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return EXIT_OK;
    }

    if (strcmp(argv[1], "create-confirmed") == 0) {
        if (parse_create_confirmed(argc, argv, &options) == 0) {
            print_usage(stderr);
            return EXIT_FAILED;
        }
        return run_create_confirmed(&options);
    }

    if (strcmp(argv[1], "create-update-sequence") == 0) {
        if (parse_create_update_sequence(argc, argv, &options) == 0) {
            print_usage(stderr);
            return EXIT_FAILED;
        }
        return run_create_update_sequence(&options);
    }

    fprintf(stderr, "unknown command: %s\n", argv[1]);
    print_usage(stderr);
    return EXIT_FAILED;
}
