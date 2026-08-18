/* Minimal virtio-keyboard driver for QEMU virt.
 *
 * Scans the virtio-mmio transport window for device id 18, sets up the
 * eventq with 64 pre-posted 8-byte buffers, and polls the used ring from
 * /dev/kbd reads.  Events are buffered in a small kernel FIFO and the
 * used descriptors are re-posted immediately.  No PLIC IRQ needed.
 */
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/virtiokbd.h>
#include <atomic.h>

#define VIRTIO_MMIO_MAGIC        0x000
#define VIRTIO_MMIO_VERSION      0x004
#define VIRTIO_MMIO_DEVICE_ID    0x008
#define VIRTIO_MMIO_QUEUE_SEL    0x030
#define VIRTIO_MMIO_QUEUE_PFN    0x040
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM    0x038
#define VIRTIO_MMIO_QUEUE_READY  0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_STATUS       0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRV_LOW   0x090
#define VIRTIO_MMIO_QUEUE_DRV_HIGH  0x094
#define VIRTIO_MMIO_QUEUE_DEV_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_DEV_HIGH  0x0a4

#define VRING_DESC_F_WRITE 2
#define VIRTIO_INPUT_DEVICE_ID 18
#define VQ_SIZE 64
#define EVQ_SIZE 256

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_SIZE];
    uint16_t used_event;
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VQ_SIZE];
    uint16_t avail_event;
};

static uintptr_t kbd_base;
static uint32_t kbd_ver;
static int kbd_ok;

static volatile struct vring_desc *desc;
static volatile struct vring_avail *avail;
static volatile struct vring_used *used;
static uint16_t used_seen;

static struct virtio_input_event raw_events[VQ_SIZE];
static struct qvos_input_event evq[EVQ_SIZE];
static uint32_t evq_head, evq_tail;    /* FIFO indices (grow, wrap at use) */
static spinlock_t kbd_lock;

static uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(kbd_base + off);
}

static void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(kbd_base + off) = val;
}

static uint64_t virt_to_phys(void *p)
{
    return pmm_phys(p);
}

/* Poll completed eventq descriptors, buffer the events, re-post buffers.
 * Caller must hold kbd_lock. */
static int kbd_poll_locked(void)
{
    int collected = 0;

    while (used->idx != used_seen) {
        volatile struct vring_used_elem *ue =
            &used->ring[used_seen % VQ_SIZE];
        used_seen++;

        uint32_t id = ue->id;
        if (id < VQ_SIZE) {
            struct qvos_input_event e;
            e.type = raw_events[id].type;
            e.code = raw_events[id].code;
            e.value = raw_events[id].value;
            evq[evq_tail % EVQ_SIZE] = e;
            evq_tail++;
            if (evq_tail - evq_head > EVQ_SIZE)
                evq_head++;            /* drop the oldest event */
            collected++;
        }

        /* Give the descriptor back to the device. */
        uint16_t ai = avail->idx;
        avail->ring[ai % VQ_SIZE] = (uint16_t)id;
        atomic_fence_rw();
        avail->idx = (uint16_t)(ai + 1);
    }

    if (collected) {
        atomic_fence_rw();
        mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    }
    return collected;
}

static int virtio_input_init(uintptr_t base)
{
    kbd_base = base;
    uint32_t magic = mmio_read(VIRTIO_MMIO_MAGIC);
    uint32_t did = mmio_read(VIRTIO_MMIO_DEVICE_ID);
    kbd_ver = mmio_read(VIRTIO_MMIO_VERSION);
    uint32_t qmax = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);

    if (magic != 0x74726976 || did != VIRTIO_INPUT_DEVICE_ID)
        return -1;

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 1);       /* ACKNOWLEDGE */
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 3);       /* DRIVER */
    atomic_fence_rw();
    if (kbd_ver == 2) {
        mmio_write(VIRTIO_MMIO_STATUS, 11);  /* FEATURES_OK (no features) */
        atomic_fence_rw();
    }

    uint32_t qnum = VQ_SIZE;
    if (kbd_ver == 1 && qmax && qmax < qnum)
        qnum = qmax;

    void *ring = pmm_alloc_page();
    if (!ring)
        return -1;

    if (kbd_ver == 2) {
        desc = (struct vring_desc *)ring;
        avail = (struct vring_avail *)((char *)ring + qnum * sizeof(*desc));
        used = (struct vring_used *)((char *)avail + sizeof(*avail));
        used = (struct vring_used *)(((uintptr_t)used + 3) & ~(uintptr_t)3);

        mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
        mmio_write(VIRTIO_MMIO_QUEUE_NUM, qnum);
        mmio_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)virt_to_phys((void *)desc));
        mmio_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(virt_to_phys((void *)desc) >> 32));
        mmio_write(VIRTIO_MMIO_QUEUE_DRV_LOW, (uint32_t)virt_to_phys((void *)avail));
        mmio_write(VIRTIO_MMIO_QUEUE_DRV_HIGH, (uint32_t)(virt_to_phys((void *)avail) >> 32));
        mmio_write(VIRTIO_MMIO_QUEUE_DEV_LOW, (uint32_t)virt_to_phys((void *)used));
        mmio_write(VIRTIO_MMIO_QUEUE_DEV_HIGH, (uint32_t)(virt_to_phys((void *)used) >> 32));
        mmio_write(VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        desc = (struct vring_desc *)ring;
        uintptr_t avail_addr = (uintptr_t)ring + qnum * sizeof(*desc);
        avail = (struct vring_avail *)avail_addr;
        uintptr_t used_addr = avail_addr + 4 + qnum * 2;
        used_addr = (used_addr + 3) & ~(uintptr_t)3;
        used = (struct vring_used *)used_addr;
        mmio_write(0x028, 4096);             /* GuestPageSize */

        mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
        mmio_write(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(virt_to_phys(ring) >> 12));
    }

    mmio_write(VIRTIO_MMIO_STATUS, kbd_ver == 2 ? 15 : 7);  /* DRIVER_OK */
    atomic_fence_rw();

    /* Post every event buffer to the device. */
    for (uint32_t i = 0; i < qnum; i++) {
        desc[i].addr = virt_to_phys(&raw_events[i]);
        desc[i].len = sizeof(raw_events[i]);
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next = 0;
    }
    atomic_fence_rw();
    for (uint32_t i = 0; i < qnum; i++)
        avail->ring[i] = (uint16_t)i;
    atomic_fence_rw();
    avail->idx = (uint16_t)qnum;
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    klog_puts("[qvos] kbd: virtio-input @");
    klog_hex(kbd_base);
    klog_puts(" ver=");
    klog_hex(kbd_ver);
    klog_puts(" qnum=");
    klog_hex(qnum);
    klog_puts("\n");
    return 0;
}

int kbd_init(void)
{
    for (uintptr_t base = 0x10001000UL; base < 0x10021000UL; base += 0x1000) {
        volatile uint32_t *p = (volatile uint32_t *)base;
        uint32_t magic = p[0x000 / 4];
        uint32_t did = p[0x008 / 4];
        if (magic == 0x74726976 && did == VIRTIO_INPUT_DEVICE_ID) {
            if (virtio_input_init(base) == 0) {
                kbd_ok = 1;
                return 0;
            }
            return -1;
        }
    }
    klog_puts("[qvos] kbd: no virtio-input device in the window\n");
    return -1;
}

int kbd_active(void) { return kbd_ok; }

uintptr_t kbd_base_get(void) { return kbd_ok ? kbd_base : 0; }

int kbd_read_events(struct qvos_input_event *dst, size_t max_events)
{
    if (!kbd_ok || !dst || max_events == 0)
        return 0;

    uint64_t flags = spin_lock_irqsave(&kbd_lock);
    kbd_poll_locked();

    size_t avail_count = evq_tail - evq_head;
    size_t n = avail_count < max_events ? avail_count : max_events;
    for (size_t i = 0; i < n; i++)
        dst[i] = evq[(evq_head + i) % EVQ_SIZE];
    evq_head += (uint32_t)n;
    spin_unlock_irqrestore(&kbd_lock, flags);
    return (int)n;
}
