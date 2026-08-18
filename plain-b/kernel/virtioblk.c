/* Minimal virtio-mmio block device driver for QEMU virt.
 *
 * Scans the virtio-mmio transport window for device id 2 (block), negotiates
 * no feature bits, reads capacity/block-size from the device config space and
 * performs one 512-byte request per virtqueue round-trip.  Completion is
 * polled, like the framebuffer driver, so the disk works before the PLIC
 * driver is enabled.  Exposed to userspace as the /dev/vda character device.
 */
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/virtioblk.h>
#include <atomic.h>

#define VIRTIO_MMIO_MAGIC        0x000
#define VIRTIO_MMIO_VERSION      0x004
#define VIRTIO_MMIO_DEVICE_ID    0x008
#define VIRTIO_MMIO_VENDOR_ID    0x00c
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
#define VIRTIO_MMIO_CONFIG          0x100

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_BLK_DEVICE_ID 2
#define VIRTIO_BLK_T_IN       0
#define VIRTIO_BLK_T_OUT      1
#define VIRTIO_BLK_T_FLUSH    4
#define VIRTIO_BLK_S_OK       0

#define VQ_SIZE 64

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
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

static uintptr_t vda_base;   /* virtio-mmio base for the block device */
static uint32_t vda_ver;
static uint64_t vda_cap;        /* bytes */
static uint32_t vda_bs = 512;   /* logical block size */
static int vda_ok;

static volatile struct vring_desc *desc;
static volatile struct vring_avail *avail;
static volatile struct vring_used *used;
static uint16_t used_seen;
static spinlock_t vda_lock;

static struct virtio_blk_req vda_req;
static uint8_t vda_status;
static uint8_t *vda_buf;        /* one block-sized DMA buffer */

static uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(vda_base + off);
}

static void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(vda_base + off) = val;
}

static uint64_t virt_to_phys(void *p)
{
    return pmm_phys(p);
}

/* Submit one request: header (device-read), optionally a data descriptor,
 * and a one-byte status (device-written).  Returns 0 on VIRTIO_BLK_S_OK.
 * Caller must hold vda_lock. */
static int vda_io_nolock(uint32_t type, uint64_t sector, void *data, int write)
{
    uint16_t idx = avail->idx;
    int d0 = (int)(idx % VQ_SIZE);
    int d1 = (d0 + 1) % VQ_SIZE;
    int d2 = (d0 + 2) % VQ_SIZE;

    vda_req.type = type;
    vda_req.reserved = 0;
    vda_req.sector = sector;
    vda_status = 0;

    desc[d0].addr = virt_to_phys(&vda_req);
    desc[d0].len = sizeof(vda_req);
    desc[d0].flags = VRING_DESC_F_NEXT;
    desc[d0].next = (uint16_t)d1;

    if (type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_OUT) {
        desc[d1].addr = virt_to_phys(data);
        desc[d1].len = vda_bs;
        desc[d1].flags = (uint16_t)(VRING_DESC_F_NEXT |
                                    (write ? 0 : VRING_DESC_F_WRITE));
        desc[d1].next = (uint16_t)d2;

        desc[d2].addr = virt_to_phys(&vda_status);
        desc[d2].len = 1;
        desc[d2].flags = VRING_DESC_F_WRITE;
        desc[d2].next = 0;
    } else {                    /* FLUSH: header + status only */
        desc[d1].addr = virt_to_phys(&vda_status);
        desc[d1].len = 1;
        desc[d1].flags = VRING_DESC_F_WRITE;
        desc[d1].next = 0;
    }

    atomic_fence_rw();
    avail->ring[idx % VQ_SIZE] = (uint16_t)d0;
    atomic_fence_rw();
    avail->idx = (uint16_t)(idx + 1);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (used->idx == used_seen)
        __asm__ volatile("" : : : "memory");
    used_seen = used->idx;

    return vda_status == VIRTIO_BLK_S_OK ? 0 : -1;
}

static int virtio_blk_init(uintptr_t base)
{
    vda_base = base;
    uint32_t magic = mmio_read(VIRTIO_MMIO_MAGIC);
    uint32_t did = mmio_read(VIRTIO_MMIO_DEVICE_ID);
    vda_ver = mmio_read(VIRTIO_MMIO_VERSION);
    uint32_t qmax = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);

    if (magic != 0x74726976 || did != VIRTIO_BLK_DEVICE_ID)
        return -1;

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 1);       /* ACKNOWLEDGE */
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 3);       /* DRIVER */
    atomic_fence_rw();
    if (vda_ver == 2) {
        mmio_write(VIRTIO_MMIO_STATUS, 11);  /* FEATURES_OK (no features) */
        atomic_fence_rw();
    }

    uint32_t qnum = VQ_SIZE;
    if (vda_ver == 1 && qmax && qmax < qnum)
        qnum = qmax;

    void *ring = pmm_alloc_page();
    if (!ring)
        return -1;
    vda_buf = (uint8_t *)pmm_alloc_page();
    if (!vda_buf)
        return -1;

    if (vda_ver == 2) {
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

    mmio_write(VIRTIO_MMIO_STATUS, vda_ver == 2 ? 15 : 7);  /* DRIVER_OK */
    atomic_fence_rw();

    uint64_t cap_lo = mmio_read(VIRTIO_MMIO_CONFIG + 0x00);
    uint64_t cap_hi = mmio_read(VIRTIO_MMIO_CONFIG + 0x04);
    uint64_t sectors = cap_lo | (cap_hi << 32);   /* spec: 512-byte units */
    vda_cap = sectors * 512;
    uint32_t bs = mmio_read(VIRTIO_MMIO_CONFIG + 0x14);  /* blk_size */
    if (bs >= 512 && bs <= 4096)   /* vda_buf is one page */
        vda_bs = bs;

    klog_puts("[qvos] vda: virtio-blk @");
    klog_hex(vda_base);
    klog_puts(" ver=");
    klog_hex(vda_ver);
    klog_puts(" cap=");
    klog_hex(vda_cap);
    klog_puts(" blk=");
    klog_hex(vda_bs);
    klog_puts("\n");
    return 0;
}

int vda_init(void)
{
    /* The QEMU virt machine exposes virtio-mmio transports in a window at
     * 0x10001000..0x10020000; the framebuffer occupies slot 7.  Walk the
     * window and claim the first virtio-blk (device id 2). */
    for (uintptr_t base = 0x10001000UL; base < 0x10021000UL; base += 0x1000) {
        volatile uint32_t *p = (volatile uint32_t *)base;
        uint32_t magic = p[0x000 / 4];
        uint32_t did = p[0x008 / 4];
        if (magic == 0x74726976 && did == VIRTIO_BLK_DEVICE_ID) {
            if (virtio_blk_init(base) == 0) {
                vda_ok = 1;
                return 0;
            }
            return -1;
        }
    }
    klog_puts("[qvos] vda: no virtio-blk device in the virtio-mmio window\n");
    return -1;
}

int vda_active(void)    { return vda_ok; }
uint64_t vda_capacity(void) { return vda_ok ? vda_cap : 0; }
uint32_t vda_blk_size(void) { return vda_ok ? vda_bs : 0; }

int vda_read(void *dst, uint64_t off, size_t n)
{
    if (!vda_ok || !dst)
        return -1;
    if (off >= vda_cap)
        return 0;
    if (n > vda_cap - off)
        n = (size_t)(vda_cap - off);

    uint8_t *d = (uint8_t *)dst;
    size_t done = 0;
    uint64_t flags = spin_lock_irqsave(&vda_lock);
    while (done < n) {
        uint64_t pos = off + done;
        uint64_t sector = pos / vda_bs;
        uint32_t in_sector = (uint32_t)(pos % vda_bs);
        uint32_t chunk = vda_bs - in_sector;
        if (chunk > n - done)
            chunk = (uint32_t)(n - done);

        if (vda_io_nolock(VIRTIO_BLK_T_IN, sector, vda_buf, 0) != 0) {
            spin_unlock_irqrestore(&vda_lock, flags);
            return -1;
        }
        memcpy(d + done, vda_buf + in_sector, chunk);
        done += chunk;
    }
    spin_unlock_irqrestore(&vda_lock, flags);
    return (int)done;
}

int vda_write(const void *src, uint64_t off, size_t n)
{
    if (!vda_ok || !src)
        return -1;
    if (off >= vda_cap)
        return 0;
    if (n > vda_cap - off)
        n = (size_t)(vda_cap - off);

    const uint8_t *s = (const uint8_t *)src;
    size_t done = 0;
    uint64_t flags = spin_lock_irqsave(&vda_lock);
    while (done < n) {
        uint64_t pos = off + done;
        uint64_t sector = pos / vda_bs;
        uint32_t in_sector = (uint32_t)(pos % vda_bs);
        uint32_t chunk = vda_bs - in_sector;
        if (chunk > n - done)
            chunk = (uint32_t)(n - done);

        if (in_sector == 0 && chunk == vda_bs) {
            /* Whole sector: no read-modify-write needed. */
            memcpy(vda_buf, s + done, chunk);
        } else {
            if (vda_io_nolock(VIRTIO_BLK_T_IN, sector, vda_buf, 0) != 0) {
                spin_unlock_irqrestore(&vda_lock, flags);
                return -1;
            }
            memcpy(vda_buf + in_sector, s + done, chunk);
        }
        if (vda_io_nolock(VIRTIO_BLK_T_OUT, sector, vda_buf, 1) != 0) {
            spin_unlock_irqrestore(&vda_lock, flags);
            return -1;
        }
        done += chunk;
    }
    spin_unlock_irqrestore(&vda_lock, flags);
    return (int)done;
}
