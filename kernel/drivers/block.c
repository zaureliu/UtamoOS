/* SPDX-License-Identifier: MIT */
#include <utamo/block.h>
bool block_read(const struct block_device *device, uint64_t lba, size_t count, void *buffer)
{
    return device != NULL && device->read != NULL && buffer != NULL && count != 0u &&
        device->sector_size != 0u && count <= SIZE_MAX / device->sector_size &&
        lba < device->sectors && (uint64_t)count <= device->sectors - lba &&
        device->read(device->context, lba, count, buffer);
}
