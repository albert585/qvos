OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);  /* R-X */
  data PT_LOAD FLAGS(6);  /* RW- */
}

SECTIONS
{
  . = 0x80200000;

  .text : ALIGN(4K) {
    PROVIDE(_text_start = .);
    *(.text.entry)
    *(.text .text.*)
    PROVIDE(_text_end = .);
  } :text

  .trampoline : ALIGN(4K) {
    PROVIDE(_tramp_start = .);
    KEEP(*(.trampoline))
    . = ALIGN(4K);
    PROVIDE(_tramp_end = .);
  } :text

  .rodata : ALIGN(4K) {
    PROVIDE(_rodata_start = .);
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
    PROVIDE(_rodata_end = .);
  } :text

  .data : ALIGN(4K) {
    PROVIDE(_data_start = .);
    *(.sdata .sdata.*)
    *(.data .data.*)
    PROVIDE(_data_end = .);
  } :data

  .ramdisk : ALIGN(4K) {
    KEEP(*(.ramdisk))
  } :data

  .bss (NOLOAD) : ALIGN(4K) {
    PROVIDE(_bss_start = .);
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4K);
    PROVIDE(_bss_end = .);
  } :data

  /* 1 MiB of static boot/early-kernel storage area (page tables etc.) */
  .early (NOLOAD) : ALIGN(4K) {
    PROVIDE(_early_start = .);
    . += 1M;
    . = ALIGN(4K);
    PROVIDE(_early_end = .);
  } :data

  PROVIDE(_kernel_start = 0x80200000);
  PROVIDE(_kernel_end = .);
}
