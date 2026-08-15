/* Stage 6: FAT32 RAM disk, VFS file table and the ELF64 loader.
 * The kernel mounts / from the embedded 16 MiB FAT32 image and starts
 * /bin/init (an ELF64 RISC-V executable) through the real exec path.
 */
#include <kernel/fat32.h>
#include <kernel/file.h>
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/proc.h>
#include <kernel/smp.h>
#include <kernel/vm.h>

__attribute__((noreturn)) void kmain(uint64_t hartid, uint64_t dtb)
{
    klog_init();
    klog_puts("\n==== qvos Stage 8 ====\n");
    klog_puts("[qvos] boot hart ");
    klog_hex(hartid);
    klog_puts("\n");

    pmm_init(dtb);
    smp_init(hartid);
    vm_init();
    trap_init_percpu();
    fb_init();
    proc_init();
    file_init();

    if (fat32_init() != 0) {
        klog_puts("[qvos] PANIC: cannot mount FAT32 ramdisk\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    char *init_argv[] = { "/bin/init", 0 };
    if (proc_spawn("/bin/init", init_argv) < 0) {
        klog_puts("[qvos] PANIC: cannot exec /bin/init\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    trap_enable_timer(10000);
    smp_start_secondary_harts();
    klog_puts("[qvos] STAGE8_OK: FAT32 + SMP + framebuffer running\n");

    scheduler();   /* never returns */
}
