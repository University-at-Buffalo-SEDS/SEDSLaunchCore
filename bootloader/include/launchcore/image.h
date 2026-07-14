#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAUNCHCORE_IMAGE_MAGIC 0x4C43494Du
#define LAUNCHCORE_IMAGE_HEADER_VERSION 1u
#define LAUNCHCORE_HASH_SHA256_SIZE 32u
#define LAUNCHCORE_SIGNATURE_MAX_SIZE 64u
#ifndef LAUNCHCORE_VALIDATE_CHUNK_SIZE
#define LAUNCHCORE_VALIDATE_CHUNK_SIZE 256u
#endif

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t block[64];
    size_t used;
} launchcore_sha256_ctx_t;

typedef enum {
    LAUNCHCORE_IMAGE_FLAG_NONE = 0u,
    LAUNCHCORE_IMAGE_FLAG_XIP = 1u << 0,
    LAUNCHCORE_IMAGE_FLAG_COPY_RAM = 1u << 1,
    LAUNCHCORE_IMAGE_FLAG_SIGNED = 1u << 2,
    LAUNCHCORE_IMAGE_FLAG_BOOTLOADER = 1u << 3,
    LAUNCHCORE_IMAGE_FLAG_APPLICATION = 1u << 4,
} launchcore_image_flags_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t image_sha256[LAUNCHCORE_HASH_SHA256_SIZE];
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t version_build;
    uint32_t image_flags;
    uint32_t slot_base_addr;
    uint32_t vector_table_addr;
    uint32_t load_addr;
    uint32_t entry_addr;
    uint32_t min_bootloader_version;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t signature[LAUNCHCORE_SIGNATURE_MAX_SIZE];
    uint32_t header_crc32;
} launchcore_image_header_t;

typedef enum {
    LAUNCHCORE_IMAGE_OK = 0,
    LAUNCHCORE_IMAGE_ERR_MAGIC,
    LAUNCHCORE_IMAGE_ERR_VERSION,
    LAUNCHCORE_IMAGE_ERR_SIZE,
    LAUNCHCORE_IMAGE_ERR_HEADER_CRC,
    LAUNCHCORE_IMAGE_ERR_IMAGE_CRC,
    LAUNCHCORE_IMAGE_ERR_IMAGE_HASH,
    LAUNCHCORE_IMAGE_ERR_VECTOR,
    LAUNCHCORE_IMAGE_ERR_STORAGE,
} launchcore_image_status_t;

uint32_t launchcore_crc32(const void *data, size_t len);
uint32_t launchcore_crc32_update(uint32_t crc, const void *data, size_t len);
void launchcore_sha256_init(launchcore_sha256_ctx_t *ctx);
void launchcore_sha256_update(launchcore_sha256_ctx_t *ctx, const void *data, size_t len);
void launchcore_sha256_final(launchcore_sha256_ctx_t *ctx, uint8_t out[32]);
void launchcore_sha256(const void *data, size_t len, uint8_t out[32]);
uint32_t launchcore_image_header_crc(const launchcore_image_header_t *header);
launchcore_image_status_t launchcore_image_validate_header(const launchcore_image_header_t *header);
launchcore_image_status_t launchcore_image_validate(const launchcore_image_header_t *header, const void *payload);
launchcore_image_status_t launchcore_image_validate_stored(const launchcore_image_header_t *header, uint32_t payload_addr);
uint32_t launchcore_image_packed_version(const launchcore_image_header_t *header);
