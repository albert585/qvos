#ifndef QVOS_VIRTIOINPUT_H
#define QVOS_VIRTIOINPUT_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/syscall.h>

/* virtio-input (tablet/mouse) event driver, exposed as /dev/mouse. */
int  mouse_init(void);       /* 0 on success, -1 if no device found */
int  mouse_active(void);
int  mouse_read_events(struct qvos_input_event *dst, size_t max_events);

#endif
