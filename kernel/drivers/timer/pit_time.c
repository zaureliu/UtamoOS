/* SPDX-License-Identifier: MIT */
#include <utamo/pit.h>
uint64_t pit_ticks_to_seconds(uint64_t ticks)
{
    return ticks / UTAMO_PIT_HZ;
}
uint64_t pit_ticks_to_milliseconds(uint64_t ticks)
{
    return ticks > UINT64_MAX / 10u ? UINT64_MAX : ticks * 10u;
}
