/* bm.ld - bare-metal layout for a static glibc rv32 binary (C or GHC) run on
 * the Clash model via +ELF. _reset must be first (at 0x80000000) so the reset
 * ROM's fixed jump lands on it. tohost is the device word at 0x10012000 (not
 * in RAM) - written directly by the exit syscall, not placed here. */
ENTRY(_reset)

MEMORY
{
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 0x08000000   /* 128 MB */
}

/* RAM is 0x80000000..0x88000000 (128 MB). The loaded image (.text/.data/.bss)
 * occupies 0x80000000.._end; a big GHC binary reaches ~13 MB, so the C stack
 * and the malloc/mmap arenas MUST sit ABOVE _end - a fixed low _estack (e.g.
 * 8 MB) would put the downward-growing stack INSIDE the image and corrupt code.
 * Layout: image .. _kheap_end(48MB) brk | .. _estack(80MB) stack-down | slab. */
_kheap_end  = 0x83000000;    /* brk arena: _end .. 48 MB                        */
_estack     = 0x85000000;    /* process/C stack top (80 MB), grows down to 48MB */
_slab_start = 0x85000000;    /* mmap MBlock slab: 80 MB .. 128 MB (1 MB-aligned)*/
_slab_end   = 0x88000000;    /* end of RAM                                      */

/* fun (fn.*) benchmark machine (fun_reduce.S): the graph base (csrw 0x7c1,
 * bookkeeping) and the node heap (csrw 0x7c0). A fun program uses no glibc
 * malloc/RTS, so the 48 MB mmap slab is free for the reduction node heap; it
 * grows UP from _slab_start while the C stack grows DOWN from _estack below it. */
_fungraph_start = 0x80000000;
_fungraph_heap  = _slab_start;
/* The collector is BUNDLED into the emitted listing now (MicroHs c86296a), so
 * the fun arm of this harness has to name what it reads: the end of the static
 * graph, the node-heap bounds, and the fatal word.  The static graph lives in
 * the image, so it ends where the node heap begins. */
_fungraph_end   = _slab_start;
_heap           = _slab_start;
_heap_end       = _slab_end;
tohost          = 0x10012000;

SECTIONS
{
  . = 0x80000000;
  .text : ALIGN(4) {
    KEEP(*(.text.init))            /* _reset FIRST -> entry = 0x80000000 */
    *(.text .text.* .gnu.linkonce.t.*)
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
  } > ram

  /* DWARF unwind tables. MUST be explicitly placed and CONTIGUOUS in link
   * order: crtbeginT.o opens .eh_frame (__EH_FRAME_BEGIN__), the user/lib FDEs
   * follow, crtend.o closes it with the 4-byte ZERO terminator. Left as orphan
   * sections, ld's orphan placement can reorder these so the terminator is not
   * last, and libgcc's FDE scanner (_Unwind_Find_FDE -> read_encoded_value_
   * with_base) then runs off the end of the table and spins forever. */
  .eh_frame_hdr : ALIGN(4) {
    __GNU_EH_FRAME_HDR = .;
    *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*)
  } > ram
  .eh_frame : ALIGN(4) {
    KEEP(*(.eh_frame)) KEEP(*(.eh_frame.*))
  } > ram
  .gcc_except_table : ALIGN(4) {
    *(.gcc_except_table .gcc_except_table.*)
  } > ram

  /* glibc init/fini + ctors/dtors (constructors incl. bm_perf_start hook) */
  .init_array : ALIGN(4) {
    __init_array_start = .;
    KEEP(*(.init_array .init_array.*))
    __init_array_end = .;
  } > ram
  .fini_array : ALIGN(4) {
    __fini_array_start = .;
    KEEP(*(.fini_array .fini_array.*))
    __fini_array_end = .;
  } > ram
  .preinit_array : ALIGN(4) {
    __preinit_array_start = .;
    KEEP(*(.preinit_array))
    __preinit_array_end = .;
  } > ram

  .data : ALIGN(8) {
    _sdata = .;
    *(.data .data.* .gnu.linkonce.d.*)
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    . = ALIGN(8);
    _edata = .;
  } > ram
  __global_pointer$ = _sdata + 0x800;

  /* glibc TLS template (static): .tdata/.tbss must be present for __libc_setup_tls.
   * The fake program-header (phdrs.c, pointed at by AT_PHDR) needs the TLS
   * segment's vaddr + file/mem sizes; expose them as linker-valued symbols
   * (symbol ADDRESS == the value, read in C as (uint32_t)&sym). */
  .tdata : ALIGN(8) { PROVIDE_HIDDEN(__tls_vaddr = .); *(.tdata .tdata.*) } > ram
  .tbss  : ALIGN(8) { *(.tbss .tbss.* .tcommon) } > ram
  PROVIDE_HIDDEN(__tls_fsz = SIZEOF(.tdata));
  PROVIDE_HIDDEN(__tls_msz = SIZEOF(.tdata) + SIZEOF(.tbss));

  .bss (NOLOAD) : ALIGN(8) {
    _sbss = .;
    *(.bss .bss.* .sbss .sbss.* .gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > ram

  _end = .;
  PROVIDE(end = .);

  /* glibc dl-support references __ehdr_start; for a static non-PIE binary it
   * is only dereferenced when AT_PHDR is absent from auxv. start.S supplies
   * AT_PHDR, so this dummy just satisfies the link. */
  PROVIDE_HIDDEN(__ehdr_start = 0x80000000);
}

/* Fail the link (not silently corrupt at run time) if the image .text/.data/.bss
 * grows into the brk arena. If this fires, raise _kheap_end/_estack/_slab_* above. */
ASSERT(_end < _kheap_end,
  "bm.ld: image overruns the brk arena; raise _kheap_end/_estack in bm.ld")
