/* phdrs.c - a hand-built ELF program-header table in RAM, pointed at by AT_PHDR
 * in the initial auxv (start.S). glibc's static startup scans these for PT_TLS
 * (to set up its own thread-local storage) and PT_LOAD; +ELF does not load the
 * real ELF headers into RAM, so we synthesize a sufficient set here. Sizes/vaddr
 * of the TLS template come from linker-valued symbols (address == value). */
#include <stdint.h>

typedef struct { uint32_t type, off, vaddr, paddr, filesz, memsz, flags, align; } Phdr;
#define PT_LOAD 1
#define PT_TLS  7

extern char __tls_vaddr[], __tls_fsz[], __tls_msz[];

const Phdr bm_phdrs[] = {
  /* the whole loaded image, r-x-w (coarse; glibc only needs a covering PT_LOAD) */
  { PT_LOAD, 0, 0x80000000u, 0x80000000u, 0x00800000u, 0x00800000u, 7, 0x1000 },
  /* the TLS template */
  { PT_TLS,  0, (uint32_t)__tls_vaddr, (uint32_t)__tls_vaddr,
              (uint32_t)__tls_fsz, (uint32_t)__tls_msz, 4, 8 },
};
const uint32_t bm_phnum = sizeof(bm_phdrs) / sizeof(bm_phdrs[0]);
