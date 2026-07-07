#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LAUNCHCORE_BOOTLOADER_UPDATE_MAGIC 0x4C434255u
#define LAUNCHCORE_BOOTLOADER_UPDATE_VERSION 1u
#define LAUNCHCORE_BOOTLOADER_SIGNATURE_MAX_SIZE 64u

typedef enum {
    LAUNCHCORE_BOOTLOADER_UPDATE_OK = 0,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_MAGIC,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_VERSION,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIZE,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_MANIFEST_CRC,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_CRC,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_HASH,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIGNATURE,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE,
    LAUNCHCORE_BOOTLOADER_UPDATE_ERR_METADATA,
} launchcore_bootloader_update_status_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;

    uint32_t bootloader_image_addr;
    uint32_t bootloader_image_size;
    uint32_t bootloader_crc32;
    uint8_t bootloader_sha256[32];
    uint8_t bootloader_signature[LAUNCHCORE_BOOTLOADER_SIGNATURE_MAX_SIZE];
    uint32_t bootloader_signature_size;

    uint32_t app_image_addr;
    uint32_t app_image_size;
    uint8_t app_sha256[32];

    uint32_t required_current_bootloader_version;
    uint32_t required_new_bootloader_version;
    uint32_t reserved0;
    uint32_t manifest_crc32;
} launchcore_bootloader_update_manifest_t;

uint32_t launchcore_bootloader_update_manifest_crc(const launchcore_bootloader_update_manifest_t *manifest);
launchcore_bootloader_update_status_t launchcore_bootloader_update_validate_manifest(const launchcore_bootloader_update_manifest_t *manifest);
launchcore_bootloader_update_status_t launchcore_bootloader_update_verify_staged(const launchcore_bootloader_update_manifest_t *manifest);
launchcore_bootloader_update_status_t launchcore_bootloader_update_apply_staged(const launchcore_bootloader_update_manifest_t *manifest);
launchcore_bootloader_update_status_t launchcore_bootloader_update_continue_app_install(const launchcore_bootloader_update_manifest_t *manifest);
