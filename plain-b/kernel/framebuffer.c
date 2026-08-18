/* Minimal virtio-mmio GPU driver for QEMU virt.
 *
 * Brings up device 0x10008000 (virtio-mmio slot 8), negotiates nothing,
 * obtains the first display mode via VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
 * creates a 2D resource backed by DRAM pages, attaches it to scanout 0 and
 * flushes after drawing.  Virtqueue completion is polled; the PLIC IRQ is
 * intentionally left unused so the framebuffer works even before the PLIC
 * driver is enabled.
 */
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/vm.h>
#include <atomic.h>
#include "font8x8.h"

#define VIRTIO_MMIO_BASE 0x10008000UL

#define VIRTIO_MMIO_MAGIC        0x000
#define VIRTIO_MMIO_VERSION      0x004
#define VIRTIO_MMIO_DEVICE_ID    0x008
#define VIRTIO_MMIO_VENDOR_ID    0x00c
#define VIRTIO_MMIO_HOST_FEAT    0x010
#define VIRTIO_MMIO_GUEST_FEAT   0x020
#define VIRTIO_MMIO_QUEUE_SEL    0x030
#define VIRTIO_MMIO_QUEUE_PFN    0x040
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM    0x038
#define VIRTIO_MMIO_QUEUE_READY  0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTR_STATUS  0x060
#define VIRTIO_MMIO_INTR_ACK     0x064
#define VIRTIO_MMIO_STATUS       0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRV_LOW   0x090
#define VIRTIO_MMIO_QUEUE_DRV_HIGH  0x094
#define VIRTIO_MMIO_QUEUE_DEV_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_DEV_HIGH  0x0a4

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO   0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT        0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH     0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO   0x1101
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2

#define VQ_SIZE 64

struct vgpu_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t pad;
} __attribute__((packed));

struct vgpu_rect { uint32_t x, y, width, height; };

struct vgpu_display_one {
    struct vgpu_rect r;
    uint32_t enabled;
    uint32_t flags;
};

struct vgpu_resp_display_info {
    struct vgpu_hdr hdr;
    struct vgpu_display_one pmodes[16];
} __attribute__((packed));

struct vgpu_cmd_create_2d {
    struct vgpu_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct vgpu_cmd_attach_backing {
    struct vgpu_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    uint64_t addr;
    uint32_t length;
    uint32_t pad;
} __attribute__((packed));

struct vgpu_cmd_set_scanout {
    struct vgpu_hdr hdr;
    struct vgpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct vgpu_cmd_transfer {
    struct vgpu_hdr hdr;
    struct vgpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t pad;
} __attribute__((packed));

struct vgpu_cmd_flush {
    struct vgpu_hdr hdr;
    struct vgpu_rect r;
    uint32_t resource_id;
    uint32_t pad;
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

static volatile struct vring_desc *desc;
static volatile struct vring_avail *avail;
static volatile struct vring_used *used;
static uint16_t used_seen;
static struct vgpu_hdr gpu_resp;
static spinlock_t fb_lock;   /* serializes virtqueue submissions (2 harts) */

static uint32_t fb_w, fb_h;
static volatile uint32_t *fb_base;
static int fb_ok;
static uint32_t cur_x, cur_y;

static uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(VIRTIO_MMIO_BASE + off);
}

static void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(VIRTIO_MMIO_BASE + off) = val;
}

static uint64_t virt_to_phys(void *p)
{
    return pmm_phys(p);
}

static void gpu_cmd(void *out, size_t out_len, void *in, size_t in_len)
{
    uint64_t flags = spin_lock_irqsave(&fb_lock);

    uint16_t idx = avail->idx;
    int d0 = (int)(idx % VQ_SIZE);

    desc[d0].addr = virt_to_phys(out);
    desc[d0].len = (uint32_t)out_len;
    desc[d0].flags = 0;
    desc[d0].next = 0;

    if (in && in_len) {
        desc[d0].flags = VRING_DESC_F_NEXT;
        desc[d0].next = (uint16_t)((d0 + 1) % VQ_SIZE);
        int d1 = (d0 + 1) % VQ_SIZE;
        desc[d1].addr = virt_to_phys(in);
        desc[d1].len = (uint32_t)in_len;
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

    spin_unlock_irqrestore(&fb_lock, flags);
}

static int virtio_gpu_init(void)
{
    uint32_t magic = mmio_read(VIRTIO_MMIO_MAGIC);
    uint32_t did = mmio_read(VIRTIO_MMIO_DEVICE_ID);
    uint32_t ver = mmio_read(VIRTIO_MMIO_VERSION);
    uint32_t qmax = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);

    klog_puts("[qvos] fb: virtio-mmio magic=");
    klog_hex(magic);
    klog_puts(" dev=");
    klog_hex(did);
    klog_puts(" ver=");
    klog_hex(ver);
    klog_puts(" qmax=");
    klog_hex(qmax);
    klog_puts("\n");

    if (magic != 0x74726976 || did != 16)
        return -1;

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 1);       /* ACKNOWLEDGE */
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 3);       /* DRIVER */
    atomic_fence_rw();
    if (ver == 2) {
        mmio_write(VIRTIO_MMIO_STATUS, 11);  /* FEATURES_OK (no features) */
        atomic_fence_rw();
    }

    uint32_t qnum = VQ_SIZE;
    if (ver == 1 && qmax && qmax < qnum)
        qnum = qmax;
    void *ring = pmm_alloc_pages(8);
    if (!ring)
        return -1;

    if (ver == 2) {
        desc = (struct vring_desc *)ring;
        avail = (struct vring_avail *)((char *)ring + qnum * sizeof(*desc));
        used = (struct vring_used *)((char *)avail + sizeof(*avail));
        used = (struct vring_used *)(((uintptr_t)used + 3) & ~(uintptr_t)3);
    } else {
        /* Legacy virtio-mmio ring layout: descriptors, then avail ring,
         * then used ring (4-byte aligned), all packed contiguously. */
        desc = (struct vring_desc *)ring;
        uintptr_t avail_addr = (uintptr_t)ring + qnum * sizeof(*desc);
        avail = (struct vring_avail *)avail_addr;
        uintptr_t used_addr = avail_addr + 4 + qnum * 2;
        used_addr = (used_addr + 3) & ~(uintptr_t)3;
        used = (struct vring_used *)used_addr;
        mmio_write(0x028, 4096);             /* GuestPageSize */
    }

    if (ver == 2) {
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
        mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
        mmio_write(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(virt_to_phys(ring) >> 12));
    }

    mmio_write(VIRTIO_MMIO_STATUS, ver == 2 ? 15 : 7);   /* DRIVER_OK */
    atomic_fence_rw();
    klog_puts("[qvos] fb: virtio status ok (ver ");
    klog_hex(ver);
    klog_puts(") ring=");
    klog_hex(virt_to_phys(ring));
    klog_puts(" avail=");
    klog_hex(virt_to_phys((void *)avail));
    klog_puts(" used=");
    klog_hex(virt_to_phys((void *)used));
    klog_puts("\n");
    return 0;
}

int fb_init(void)
{
    if (virtio_gpu_init() != 0) {
        klog_puts("[qvos] fb: no virtio-gpu at 0x10008000, UART only\n");
        return -1;
    }

    static struct vgpu_hdr info_cmd;
    static struct vgpu_resp_display_info info;
    memset(&info_cmd, 0, sizeof(info_cmd));
    memset(&info, 0, sizeof(info));
    info_cmd.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    gpu_cmd(&info_cmd, sizeof(info_cmd), &info, sizeof(info));

    if (info.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO || !info.pmodes[0].enabled)
        return -1;
    fb_w = info.pmodes[0].r.width;
    fb_h = info.pmodes[0].r.height;
    if (fb_w == 0 || fb_h == 0 || fb_w > 4096 || fb_h > 4096)
        return -1;

    uint32_t stride = fb_w * 4;
    uint32_t bytes = stride * fb_h;
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    fb_base = (volatile uint32_t *)pmm_alloc_pages(pages);
    if (!fb_base)
        return -1;
    memset((void *)fb_base, 0, bytes);

    static struct vgpu_cmd_create_2d create;
    static struct vgpu_hdr resp;
    memset(&resp, 0, sizeof(resp));
    memset(&create, 0, sizeof(create));
    create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = 1;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    create.width = fb_w;
    create.height = fb_h;
    gpu_cmd(&create, sizeof(create), &resp, sizeof(resp));

    static struct vgpu_cmd_attach_backing attach;
    memset(&attach, 0, sizeof(attach));
    attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = 1;
    attach.nr_entries = 1;
    attach.addr = virt_to_phys((void *)fb_base);
    attach.length = bytes;
    gpu_cmd(&attach, sizeof(attach), &resp, sizeof(resp));

    static struct vgpu_cmd_set_scanout scan;
    memset(&scan, 0, sizeof(scan));
    scan.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scan.scanout_id = 0;
    scan.resource_id = 1;
    scan.r.x = 0;
    scan.r.y = 0;
    scan.r.width = fb_w;
    scan.r.height = fb_h;
    gpu_cmd(&scan, sizeof(scan), &resp, sizeof(resp));

    static struct vgpu_cmd_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    xfer.resource_id = 1;
    xfer.r.x = 0;
    xfer.r.y = 0;
    xfer.r.width = fb_w;
    xfer.r.height = fb_h;
    xfer.offset = 0;
    gpu_cmd(&xfer, sizeof(xfer), &resp, sizeof(resp));

    fb_ok = 1;
    cur_x = cur_y = 0;
    fb_fill(0, 0, fb_w, fb_h, 0x00000000);
    klog_puts("[qvos] fb: ");
    klog_hex(fb_w);
    klog_puts("x");
    klog_hex(fb_h);
    klog_puts(" B8G8R8X8 @");
    klog_hex(virt_to_phys((void *)fb_base));
    klog_puts("\n");
    return 0;
}

int fb_active(void) { return fb_ok; }

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    if (!fb_ok || x >= fb_w || y >= fb_h)
        return;
    fb_base[y * fb_w + x] = rgb;   /* memory order B,G,R,X -> value RGB */
}

void fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    if (!fb_ok)
        return;
    if (x >= fb_w || y >= fb_h)
        return;
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            fb_base[(y + yy) * fb_w + (x + xx)] = rgb;

    fb_flush();
}

void fb_flush(void)
{
    if (!fb_ok)
        return;

    /* Guest memory is not visible until TRANSFER_TO_HOST_2D, then the
     * affected rectangle must be flushed to the scanout. */
    static struct vgpu_cmd_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    xfer.resource_id = 1;
    xfer.r.x = 0;
    xfer.r.y = 0;
    xfer.r.width = fb_w;
    xfer.r.height = fb_h;
    xfer.offset = 0;
    gpu_cmd(&xfer, sizeof(xfer), &gpu_resp, sizeof(gpu_resp));

    static struct vgpu_cmd_flush flush;
    memset(&flush, 0, sizeof(flush));
    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.resource_id = 1;
    flush.r.x = 0;
    flush.r.y = 0;
    flush.r.width = fb_w;
    flush.r.height = fb_h;
    gpu_cmd(&flush, sizeof(flush), &gpu_resp, sizeof(gpu_resp));
}

static void fb_console_newline(void)
{
    cur_x = 0;
    cur_y += 8;
    if (cur_y + 8 > fb_h) {
        cur_y = 0;
        fb_fill(0, 0, fb_w, fb_h, 0x00000000);
    }
}

static void fb_console_putc(char c)
{
    if (!fb_ok)
        return;
    if (c == '\n') {
        fb_console_newline();
        return;
    }
    if (c == '\r')
        return;
    if (c < 0x20 || c > 0x7e)
        c = '?';
    if (cur_x + 8 > fb_w)
        fb_console_newline();

    const unsigned char *glyph = font8x8[c - 0x20];
    for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++)
            fb_put_pixel(cur_x + xx, cur_y + yy,
                         (glyph[yy] & (1 << xx)) ? 0x00ffffff : 0x00000000);
    cur_x += 8;
}

void fb_console_clear(uint32_t bg)
{
    if (!fb_ok)
        return;
    fb_fill(0, 0, fb_w, fb_h, bg);
    cur_x = cur_y = 0;
}

void fb_console_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        fb_console_putc(s[i]);
    fb_flush();
}

uint32_t fb_width(void)  { return fb_w; }
uint32_t fb_height(void) { return fb_h; }

uint64_t fb_paddr(void)
{
    if (!fb_ok)
        return 0;
    return virt_to_phys((void *)fb_base);
}

uint32_t fb_size_bytes(void)
{
    if (!fb_ok)
        return 0;
    return fb_w * fb_h * 4;
}
