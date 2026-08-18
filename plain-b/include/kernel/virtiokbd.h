#ifndef QVOS_VIRTIOKBD_H
#define QVOS_VIRTIOKBD_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/syscall.h>

/* virtio-keyboard event driver, exposed as /dev/kbd. */
int      kbd_init(void);
int      kbd_active(void);
int      kbd_read_events(struct qvos_input_event *dst, size_t max_events);
uintptr_t kbd_base_get(void);   /* virtio-mmio base, 0 when absent */

#endif
