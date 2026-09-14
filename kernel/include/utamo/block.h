/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_BLOCK_H
#define UTAMO_BLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct block_device {
    uint64_t sectors;
    uint32_t sector_size;
    void *context;
    bool (*read)(void *context, uint64_t lba, size_t count, void *buffer);
};
/* Caller provides count*sector_size bytes of trusted kernel storage.
 * Zero sectors is rejected; overflow/bounds are checked before the driver. */
bool block_read(const struct block_device *device, uint64_t lba, size_t count, void *buffer);
#endif
