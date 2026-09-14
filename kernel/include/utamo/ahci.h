/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_AHCI_H
#define UTAMO_AHCI_H
#include <utamo/block.h>
/* Pure command construction; only IDENTIFY and READ DMA EXT are accepted. */
bool ahci_build_read(void *header, void *table, uint64_t table_phys,
                     uint64_t buffer_phys, uint8_t command, uint64_t lba, size_t count);
bool ahci_identify(const unsigned char data[512], uint64_t *sectors);
/* Init once, boot thread IF=1. One ATA disk, polling, readonly, 512-byte sectors.
 * 0=no ATA device, 1=ready, -1=controller/device rejected. */
int ahci_init(void);
const struct block_device *ahci_device(void);
void ahci_status(void);
#endif
