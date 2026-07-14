#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "launchcore/bootloader_update.h"
#include "launchcore/metadata.h"

#define LAUNCHCORE_BUNDLE_MAGIC 0x4C43424Eu
#define LAUNCHCORE_BUNDLE_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t bootloader_offset;
    uint32_t bootloader_size;
    uint32_t bootloader_version;
    uint32_t bootloader_crc32;
    uint8_t bootloader_sha256[32];
    uint8_t bootloader_signature[LAUNCHCORE_BOOTLOADER_SIGNATURE_MAX_SIZE];
    uint32_t bootloader_signature_size;
    uint32_t app_offset;
    uint32_t app_size;
    uint8_t app_sha256[32];
    uint32_t header_crc32;
} launchcore_bundle_header_t;

uint32_t launchcore_bundle_header_crc(const launchcore_bundle_header_t *header);
bool launchcore_bundle_header_is_valid(const launchcore_bundle_header_t *header, uint32_t available_size);
bool launchcore_process_staged_bundle(launchcore_metadata_t *md);
