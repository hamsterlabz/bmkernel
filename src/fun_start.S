# fun_start.S - BARE reset for a fun (fn.*) benchmark. rvfun programs do NOT use
# glibc, so this REPLACES glibc's _start (no __libc_start_main, no C-runtime
# init). It still boots at 0x80000000 exactly like the GHC ELF (the ext_rom stub
# jumps here), sets gp/sp, enables F, programs the UART, takes the FULL-RUN reset
# PMC snapshot, and calls fun_run (C). Reuses the bmkernel bmperf (PMC) + syscalls
# UART as libraries; nothing glibc, so glibc init + UART printing are not in the
# measured window. Assembled by the plain gcc (cpp handles IS_SIM); no fn.* here.
  .section .text.init,"ax"
  .globl _reset
_reset:
  .option push
  .option norelax
  la    gp, __global_pointer$
  .option pop
  la    sp, _estack
  li    t0, 0x6000
  csrs  mstatus, t0            # FS=11: enable the F datapath (float blobs)

  # software UART init (16550 programmable divisor), same as the GHC start.S
  lui   t5, 0x10011
  li    t0, 0x83               # LCR: DLAB=1 | 8N1
  sw    t0, 0x0C(t5)
#ifdef IS_SIM
  li    t0, 15                 # DLL = 15 (16 clk/bit, fast sim)
  sw    t0, 0x00(t5)
  sw    zero, 0x04(t5)         # DLM = 0
#else
  li    t0, 0x63               # DLL = 867 & 0xFF (115200 @ 100 MHz)
  sw    t0, 0x00(t5)
  li    t0, 0x03               # DLM = 867 >> 8
  sw    t0, 0x04(t5)
#endif
  li    t0, 0x03               # LCR: 8N1, DLAB=0
  sw    t0, 0x0C(t5)

  # FULL-RUN window start (reset), before ANY fun work
  la    a0, g_full_start
  call  bmperf_snap

  call  fun_run                # runs the reduction, reports, writes tohost; no return
1:
  j     1b
