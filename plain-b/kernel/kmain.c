/* Stage 6: VFS + ELF64 loader.
 * The kernel carries no root filesystem: "/" is a FAT32 volume mounted
 * from /dev/vda at boot, and /bin/init is started through the VFS path.
 */
#include <kernel/file.h>
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/proc.h>
#include <kernel/rtc.h>
#include <kernel/smp.h>
#include <kernel/virtioblk.h>
#include <kernel/virtioinput.h>
#include <kernel/virtiokbd.h>
#include <kernel/virtiosnd.h>
#include <kernel/vm.h>

__attribute__((noreturn)) void kmain(uint64_t hartid, uint64_t dtb)
{
    klog_init();
    klog_puts("\n==== qvos Stage 10 ====\n");
    klog_puts("[qvos] boot hart ");
    klog_hex(hartid);
    klog_puts("\n");

    pmm_init(dtb);
    smp_init(hartid);
    vm_init();
    trap_init_percpu();
    fb_init();
    vda_init();
    kbd_init();
    mouse_init();
    rtc_init();
    snd_init();
    proc_init();
    file_init();

    /* Root filesystem: the kernel image carries none.  "/" must come from
     * a FAT32 volume on /dev/vda, just like Linux booting root=/dev/vda. */
    if (vfs_mount("/dev/vda", "/") != 0) {
        klog_puts("[qvos] PANIC: VFS: unable to mount root fs on /dev/vda\n");
        for (;;)
            __asm__ volatile("wfi");
    }
    struct file *probe = file_open("/bin/init", 0);
    if (!probe) {
        klog_puts("[qvos] PANIC: root fs has no /bin/init\n");
        for (;;)
            __asm__ volatile("wfi");
    }
    file_close(probe);
    klog_puts("[qvos] root: /dev/vda (persistent FAT32)\n");

    char *init_argv[] = { "/bin/init", 0 };
    if (proc_spawn("/bin/init", init_argv) < 0) {
        klog_puts("[qvos] PANIC: cannot exec /bin/init\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    trap_enable_timer(10000);
    smp_start_secondary_harts();
    klog_puts("[qvos] STAGE10_OK: FAT32 + SMP + framebuffer + virtio-blk + mouse + kbd + sound running\n");

    scheduler();   /* never returns */
}
