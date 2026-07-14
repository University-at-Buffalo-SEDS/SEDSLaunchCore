#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "launchcore/metadata.h"

#define LAUNCHCORE_DELTA_MAGIC 0x4C434450u
#define LAUNCHCORE_DELTA_VERSION 1u

typedef enum {
    LAUNCHCORE_DELTA_CODEC_RAW = 0,
    LAUNCHCORE_DELTA_CODEC_RLE = 1,
} launchcore_delta_codec_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t erase_size;
    uint32_t base_image_size;
    uint32_t target_image_size;
    uint32_t record_count;
    uint32_t records_offset;
    uint8_t base_sha256[32];
    uint8_t target_sha256[32];
    uint32_t header_crc32;
} launchcore_delta_header_t;

typedef struct __attribute__((packed)) {
    uint32_t target_offset;
    uint32_t output_size;
    uint32_t old_data_size;
    uint32_t new_data_size;
    uint32_t old_crc32;
    uint32_t new_crc32;
    uint8_t old_codec;
    uint8_t new_codec;
    uint16_t reserved;
    uint32_t header_crc32;
} launchcore_delta_record_t;

typedef enum {
    LAUNCHCORE_DELTA_NONE = 0,
    LAUNCHCORE_DELTA_READY,
    LAUNCHCORE_DELTA_ROLLED_BACK,
    LAUNCHCORE_DELTA_REJECTED,
    LAUNCHCORE_DELTA_RETRY,
} launchcore_delta_status_t;

uint32_t launchcore_delta_header_crc(const launchcore_delta_header_t *header);
uint32_t launchcore_delta_record_crc(const launchcore_delta_record_t *record);
bool launchcore_delta_header_is_valid(const launchcore_delta_header_t *header, uint32_t available_size);
launchcore_delta_status_t launchcore_process_delta_update(launchcore_metadata_t *md);
