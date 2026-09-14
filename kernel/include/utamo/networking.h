/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_NETWORKING_H
#define UTAMO_NETWORKING_H
#include <stdbool.h>
#include <stdint.h>
void networking_init(void);
void networking_status(void);
bool networking_dhcp(void);
void networking_ping(const char *target);
void networking_resolve(const char *name, const char *server, uint16_t port);
bool networking_selftest(uint16_t fixture_port);
#endif
