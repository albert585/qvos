#ifndef QVOS_ELF_H
#define QVOS_ELF_H

#include <stdint.h>
#include <kernel/vm.h>

#define ELF_MAGIC 0x464c457fU
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_RISCV 243
#define ET_EXEC 2

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf64_phdr_t;

/* Loads an ELF64 RISC-V image from a memory buffer into pg.  Returns entry
 * point on success or 0; sets *brk to the first page after the image. */
uint64_t elf_load(pagetable_t pg, const uint8_t *image, size_t size,
                  uint64_t *brk);

#endif
