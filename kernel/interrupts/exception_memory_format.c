/* SPDX-License-Identifier: MIT */
#include <utamo/interrupts.h>
#include <utamo/vmm.h>

void exception_format_memory(format_emit_fn emit, void *context,
                             bool available, const struct vmm_mapping *mapping)
{
    if (emit == NULL) {
        return;
    }
    kformat(emit, context, "\nVirtual memory context\n");
    if (!available || mapping == NULL) {
        kformat(emit, context, "VMM query: unavailable\n");
        return;
    }
    kformat(emit, context, "Mapped: %s\n",
            (const char *)(mapping->mapped ? "Yes" : "No"));
    if (!mapping->mapped) {
        return;
    }
    kformat(emit, context,
            "Physical: 0x%llx\nPage size: %llu bytes\nEffective flags: 0x%llx\n",
            (unsigned long long)mapping->physical,
            (unsigned long long)mapping->page_size,
            (unsigned long long)mapping->flags);
    kformat(emit, context, "Writable: %s\nUser: %s\nNX: %s\n",
            (const char *)((mapping->flags & VMM_WRITABLE) != 0u ? "Yes" : "No"),
            (const char *)((mapping->flags & VMM_USER) != 0u ? "Yes" : "No"),
            (const char *)((mapping->flags & VMM_NX) != 0u ? "Yes" : "No"));
}
