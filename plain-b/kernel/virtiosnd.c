/* Minimal virtio-mmio sound driver (playback only).
 *
 * Exposes a raw PCM device to the VFS as /dev/pcm.  Only the classic
 * message transport is used (no shared-memory features): audio goes
 * through the tx virtqueue as period-sized I/O messages.
 */
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/virtiosnd.h>
#include <atomic.h>

#define VIRTIO_MMIO_MAGIC        0x000
#define VIRTIO_MMIO_VERSION      0x004
#define VIRTIO_MMIO_DEVICE_ID    0x008
#define VIRTIO_MMIO_VENDOR_ID    0x00c
#define VIRTIO_MMIO_QUEUE_SEL    0x030
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

#define VIRTIO_SND_DEVICE_ID 25
#define VIRTIO_SND_VQ_MAX    4
#define VQ_SIZE             16

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_SND_D_OUTPUT 0

#define VIRTIO_SND_R_PCM_INFO      0x0100
#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0101
#define VIRTIO_SND_R_PCM_PREPARE   0x0102
#define VIRTIO_SND_R_PCM_RELEASE   0x0103
#define VIRTIO_SND_R_PCM_START     0x0104
#define VIRTIO_SND_R_PCM_STOP      0x0105

#define VIRTIO_SND_PCM_FMT_S16 5
#define VIRTIO_SND_PCM_RATE_48000 7

#define VIRTIO_SND_S_OK      0x8000
#define VIRTIO_SND_S_BAD_MSG 0x8001
#define VIRTIO_SND_S_NOT_SUPP 0x8002
#define VIRTIO_SND_S_IO_ERR  0x8003

#define SND_PERIOD_BYTES 4096
#define SND_BUFFER_BYTES 8192
#define SND_CHANNELS    2

struct virtio_snd_config {
    uint32_t jacks;
    uint32_t streams;
    uint32_t chmaps;
    uint32_t controls;
};

struct virtio_snd_hdr {
    uint32_t code;
};

struct virtio_snd_query_info {
    struct virtio_snd_hdr hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
};

struct virtio_snd_info {
    uint32_t hda_fn_nid;
};

struct virtio_snd_pcm_info {
    struct virtio_snd_info hdr;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t  direction;
    uint8_t  channels_min;
    uint8_t  channels_max;
    uint8_t  padding[5];
};

struct virtio_snd_pcm_hdr {
    struct virtio_snd_hdr hdr;
    uint32_t stream_id;
};

struct virtio_snd_pcm_set_params {
    struct virtio_snd_pcm_hdr hdr;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t  channels;
    uint8_t  format;
    uint8_t  rate;
    uint8_t  padding;
};

struct virtio_snd_pcm_xfer {
    uint32_t stream_id;
};

struct virtio_snd_pcm_status {
    uint32_t status;
    uint32_t latency_bytes;
};

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

struct vsnd_vq {
    volatile struct vring_desc *desc;
    volatile struct vring_avail *avail;
    volatile struct vring_used *used;
    uint16_t used_seen;
    uint16_t qnum;
    uint16_t qidx;
};

static uintptr_t snd_base;
static int snd_ok;
static uint32_t snd_streams;
static int snd_stream_id = -1;
static int snd_started;
static spinlock_t snd_lock;

static struct vsnd_vq vqs[VIRTIO_SND_VQ_MAX];
static void *vq_rings[VIRTIO_SND_VQ_MAX];

static uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(snd_base + off);
}

static void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(snd_base + off) = val;
}

static uint64_t virt_to_phys(void *p)
{
    return pmm_phys(p);
}

static int vq_init(int idx, uint16_t qnum)
{
    void *ring = pmm_alloc_page();
    if (!ring)
        return -1;
    vq_rings[idx] = ring;

    vqs[idx].desc = (struct vring_desc *)ring;
    vqs[idx].avail = (struct vring_avail *)((char *)ring +
                                            qnum * sizeof(struct vring_desc));
    vqs[idx].used = (struct vring_used *)((char *)vqs[idx].avail +
                                          sizeof(*vqs[idx].avail));
    vqs[idx].used = (struct vring_used *)(((uintptr_t)vqs[idx].used + 3) &
                                          ~(uintptr_t)3);
    vqs[idx].qnum = qnum;
    vqs[idx].qidx = (uint16_t)idx;
    vqs[idx].used_seen = 0;

    mmio_write(VIRTIO_MMIO_QUEUE_SEL, (uint32_t)idx);
    mmio_write(VIRTIO_MMIO_QUEUE_NUM, qnum);
    mmio_write(VIRTIO_MMIO_QUEUE_DESC_LOW,
               (uint32_t)virt_to_phys((void *)vqs[idx].desc));
    mmio_write(VIRTIO_MMIO_QUEUE_DESC_HIGH,
               (uint32_t)(virt_to_phys((void *)vqs[idx].desc) >> 32));
    mmio_write(VIRTIO_MMIO_QUEUE_DRV_LOW,
               (uint32_t)virt_to_phys((void *)vqs[idx].avail));
    mmio_write(VIRTIO_MMIO_QUEUE_DRV_HIGH,
               (uint32_t)(virt_to_phys((void *)vqs[idx].avail) >> 32));
    mmio_write(VIRTIO_MMIO_QUEUE_DEV_LOW,
               (uint32_t)virt_to_phys((void *)vqs[idx].used));
    mmio_write(VIRTIO_MMIO_QUEUE_DEV_HIGH,
               (uint32_t)(virt_to_phys((void *)vqs[idx].used) >> 32));
    mmio_write(VIRTIO_MMIO_QUEUE_READY, 1);
    return 0;
}

static int vq_kick(struct vsnd_vq *vq, int d0, int d1, int d2, int ndesc)
{
    uint16_t idx = vq->avail->idx;
    int head = (int)(idx % VQ_SIZE);
    int spins = 0;

    vq->avail->ring[idx % VQ_SIZE] = (uint16_t)d0;
    (void)head; (void)d1; (void)d2; (void)ndesc;
    atomic_fence_rw();
    vq->avail->idx = (uint16_t)(idx + 1);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, vq->qidx);

    while (vq->used->idx == vq->used_seen) {
        if (++spins > 10000000) {
            klog_puts("[qvos] snd: virtqueue timeout\n");
            return -1;
        }
        __asm__ volatile("" : : : "memory");
    }
    vq->used_seen = vq->used->idx;
    return 0;
}

/* Submit a two-descriptor control request: request (read) + response (write). */
static int ctl_req(const void *req, size_t req_len,
                   void *resp, size_t resp_len)
{
    struct vsnd_vq *vq = &vqs[0];
    uint16_t idx = vq->avail->idx;
    int d0 = (int)(idx % VQ_SIZE);
    int d1 = (d0 + 1) % VQ_SIZE;

    vq->desc[d0].addr = virt_to_phys((void *)req);
    vq->desc[d0].len = (uint32_t)req_len;
    vq->desc[d0].flags = VRING_DESC_F_NEXT;
    vq->desc[d0].next = (uint16_t)d1;

    vq->desc[d1].addr = virt_to_phys(resp);
    vq->desc[d1].len = (uint32_t)resp_len;
    vq->desc[d1].flags = VRING_DESC_F_WRITE;
    vq->desc[d1].next = 0;

    return vq_kick(vq, d0, d1, 0, 2);
}

static uint32_t ctl_status(const void *resp)
{
    return ((const struct virtio_snd_hdr *)resp)->code;
}

static int pcm_ctl(uint32_t code, uint32_t stream_id)
{
    struct virtio_snd_pcm_hdr req;
    struct virtio_snd_hdr resp;
    uint64_t flags = spin_lock_irqsave(&snd_lock);

    memset(&req, 0, sizeof(req));
    req.hdr.code = code;
    req.stream_id = stream_id;
    if (ctl_req(&req, sizeof(req), &resp, sizeof(resp)) != 0) {
        spin_unlock_irqrestore(&snd_lock, flags);
        return -1;
    }
    spin_unlock_irqrestore(&snd_lock, flags);

    if (ctl_status(&resp) != VIRTIO_SND_S_OK) {
        klog_puts("[qvos] snd: ctl code=");
        klog_hex(code);
        klog_puts(" status=");
        klog_hex(ctl_status(&resp));
        klog_puts("\n");
        return -1;
    }
    return 0;
}

static int snd_configure(void)
{
    struct virtio_snd_query_info q;
    static uint8_t resp[1024];
    struct virtio_snd_pcm_info *info;
    uint32_t i;
    uint64_t flags;

    if (snd_streams == 0)
        return -1;

    memset(&q, 0, sizeof(q));
    q.hdr.code = VIRTIO_SND_R_PCM_INFO;
    q.start_id = 0;
    q.count = snd_streams;
    q.size = sizeof(struct virtio_snd_pcm_info);

    flags = spin_lock_irqsave(&snd_lock);
    if (ctl_req(&q, sizeof(q), resp, sizeof(resp)) != 0) {
        spin_unlock_irqrestore(&snd_lock, flags);
        return -1;
    }
    spin_unlock_irqrestore(&snd_lock, flags);

    if (ctl_status(resp) != VIRTIO_SND_S_OK)
        return -1;

    /* Pick the first output stream that supports S16/48kHz/stereo. */
    info = (struct virtio_snd_pcm_info *)(resp + sizeof(struct virtio_snd_hdr));
    for (i = 0; i < snd_streams; i++) {
        if (info[i].direction == VIRTIO_SND_D_OUTPUT &&
            info[i].channels_max >= SND_CHANNELS &&
            (info[i].formats & (1ULL << VIRTIO_SND_PCM_FMT_S16)) &&
            (info[i].rates & (1ULL << VIRTIO_SND_PCM_RATE_48000))) {
            snd_stream_id = (int)i;
            break;
        }
    }
    if (snd_stream_id < 0)
        return -1;

    struct virtio_snd_pcm_set_params sp;
    struct virtio_snd_hdr resp2;

    memset(&sp, 0, sizeof(sp));
    sp.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    sp.hdr.stream_id = (uint32_t)snd_stream_id;
    sp.buffer_bytes = SND_BUFFER_BYTES;
    sp.period_bytes = SND_PERIOD_BYTES;
    sp.features = 0;
    sp.channels = SND_CHANNELS;
    sp.format = VIRTIO_SND_PCM_FMT_S16;
    sp.rate = VIRTIO_SND_PCM_RATE_48000;

    flags = spin_lock_irqsave(&snd_lock);
    if (ctl_req(&sp, sizeof(sp), &resp2, sizeof(resp2)) != 0) {
        spin_unlock_irqrestore(&snd_lock, flags);
        return -1;
    }
    spin_unlock_irqrestore(&snd_lock, flags);

    if (ctl_status(&resp2) != VIRTIO_SND_S_OK)
        return -1;

    if (pcm_ctl(VIRTIO_SND_R_PCM_PREPARE, (uint32_t)snd_stream_id) != 0)
        return -1;
    if (pcm_ctl(VIRTIO_SND_R_PCM_START, (uint32_t)snd_stream_id) != 0)
        return -1;
    snd_started = 1;

    klog_puts("[qvos] snd: PCM stream ");
    klog_hex((uint64_t)snd_stream_id);
    klog_puts(" 48k/2ch/S16 ready\n");
    return 0;
}

static int virtio_snd_init(uintptr_t base)
{
    uint32_t magic, did, qmax;
    int i;

    snd_base = base;
    magic = mmio_read(VIRTIO_MMIO_MAGIC);
    did = mmio_read(VIRTIO_MMIO_DEVICE_ID);
    qmax = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (magic != 0x74726976 || did != VIRTIO_SND_DEVICE_ID)
        return -1;

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 1);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 3);
    atomic_fence_rw();
    mmio_write(VIRTIO_MMIO_STATUS, 11);   /* FEATURES_OK */
    atomic_fence_rw();

    for (i = 0; i < VIRTIO_SND_VQ_MAX; i++) {
        uint16_t qnum = VQ_SIZE;
        if (qmax && qmax < qnum)
            qnum = (uint16_t)qmax;
        if (vq_init(i, qnum) != 0)
            return -1;
    }

    mmio_write(VIRTIO_MMIO_STATUS, 15);   /* DRIVER_OK */
    atomic_fence_rw();

    snd_streams = mmio_read(VIRTIO_MMIO_CONFIG + 0x04);
    klog_puts("[qvos] snd: virtio-sound @");
    klog_hex(base);
    klog_puts(" streams=");
    klog_hex(snd_streams);
    klog_puts("\n");

    if (snd_configure() != 0) {
        klog_puts("[qvos] snd: stream setup failed\n");
        return -1;
    }

    snd_ok = 1;
    return 0;
}

int snd_init(void)
{
    for (uintptr_t base = 0x10001000UL; base < 0x10021000UL; base += 0x1000) {
        volatile uint32_t *p = (volatile uint32_t *)base;
        uint32_t magic = p[0x000 / 4];
        uint32_t did = p[0x008 / 4];
        if (magic == 0x74726976 && did == VIRTIO_SND_DEVICE_ID) {
            if (virtio_snd_init(base) == 0)
                return 0;
            return -1;
        }
    }
    klog_puts("[qvos] snd: no virtio-sound device\n");
    return -1;
}

int snd_active(void)
{
    return snd_ok;
}

int snd_write(const void *buf, size_t n)
{
    struct vsnd_vq *vq = &vqs[2];   /* tx queue */
    const uint8_t *p = (const uint8_t *)buf;
    size_t done = 0;
    uint64_t flags;

    if (!snd_ok || snd_stream_id < 0)
        return -1;

    flags = spin_lock_irqsave(&snd_lock);
    while (done < n) {
        size_t chunk = n - done;
        struct virtio_snd_pcm_xfer xfer;
        struct virtio_snd_pcm_status st;
        uint16_t idx = vq->avail->idx;
        int d0 = (int)(idx % VQ_SIZE);
        int d1 = (d0 + 1) % VQ_SIZE;
        int d2 = (d0 + 2) % VQ_SIZE;

        if (chunk > SND_PERIOD_BYTES)
            chunk = SND_PERIOD_BYTES;

        memset(&xfer, 0, sizeof(xfer));
        xfer.stream_id = (uint32_t)snd_stream_id;
        memset(&st, 0, sizeof(st));

        vq->desc[d0].addr = virt_to_phys(&xfer);
        vq->desc[d0].len = sizeof(xfer);
        vq->desc[d0].flags = VRING_DESC_F_NEXT;
        vq->desc[d0].next = (uint16_t)d1;

        vq->desc[d1].addr = virt_to_phys((void *)(p + done));
        vq->desc[d1].len = (uint32_t)chunk;
        vq->desc[d1].flags = VRING_DESC_F_NEXT;
        vq->desc[d1].next = (uint16_t)d2;

        vq->desc[d2].addr = virt_to_phys(&st);
        vq->desc[d2].len = sizeof(st);
        vq->desc[d2].flags = VRING_DESC_F_WRITE;
        vq->desc[d2].next = 0;

        if (vq_kick(vq, d0, d1, d2, 3) != 0) {
            klog_puts("[qvos] snd: tx timeout\n");
            break;
        }
        if (st.status != VIRTIO_SND_S_OK) {
            klog_puts("[qvos] snd: tx status=");
            klog_hex(st.status);
            klog_puts("\n");
            break;
        }
        done += chunk;
    }
    spin_unlock_irqrestore(&snd_lock, flags);

    return done ? (int)done : -1;
}
