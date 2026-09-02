```
    __                __                             __
   / /_   ____ ___   / /__ ___   _____ ____   ___   / /
  / __ \ / __ `__ \ / //_// _ \ / ___// __ \ / _ \ / /
 / /_/ // / / / / // ,<  /  __// /   / / / //  __// /
/_.___//_/ /_/ /_//_/|_| \___//_/   /_/ /_/ \___//_/
```

# bmkernel: a bare-metal nanokernel for running unmodified Linux userspace programs (including full GHC Haskell) on a bare RV32 CPU

The bmkernel boots a statically-linked glibc program on bare metal and
services its Linux syscalls from an M-mode ecall trap handler.  It should work on any
rv32imaf CPU with the standard privilege modes (machine/supervisor/
user. SoC specifics live in (ld/bm.ld for the
memory map, the UART constants in src/start.S / src/syscalls.c). 

Using bmkernel, the same C or Haskell source that runs on Linux runs 
on the simulated (or FPGA) SoC, and brackets per-benchmark performance
counters around the compute part of each benchmark. This way, runtime systems 
and any initialization can be isolated from the desired measurements.  

## Project layout
* `src/`     kernel + harness sources (board-independent)
* `include/` public headers (bmperf.h)
* `board/`   the port layer: board.h is the four-point interface
  (early init, console putc, exit, extended-PMC flag); `board/bbird/`
  is the complete Blackbird example, `board/stub/` an empty template
  for a new rv32 CPU.  Select with BOARD=path (default board/bbird).
* `ld/`      the RAM linker script (adapt with your board)
* `scripts/` the two generic image builders
* `ghc-compat/` pre-containers API shims for 2004-era Haskell

## Boot path
* `src/start.S`: reset vector: gp/sp, FPU enable, UART divisor, mtvec,
  builds the Linux initial process stack (argc/argv/envp/auxv incl.
  AT_PHDR to a synthesized program-header table) and jumps to glibc's
  `_start`.  argv comes from a weak, builder-generated vector
  (`bm_argc`/`bm_argv_vec`), so programs read real arguments.
* `src/syscalls.c`, the syscall surface: write/writev (UART), read (fd 0 =
  an embedded stdin blob, files = FAT volume), openat/close/lseek, brk
  (with high-water tracking) and anonymous mmap (slab, also tracked),
  clock_gettime64/clock_getres_time64 (10 ns cycle clock),
  rt_sigaction/sigprocmask stubs, ppoll (always ready, GHC's fdReady),
  sched_getaffinity (one hart), uname/getpid/sysinfo/prlimit/getrandom,
  and a synthetic write-fd that mirrors RTS streams (e.g. -hT heap
  profiles) to the UART.
* `phdrs.c`: synthesized ELF phdrs for glibc's TLS/auxv needs.
* `atomics.c`: libatomic shims for rv32.
* `pthread_stub.c`, single-hart pthreads: create runs the routine
  inline, join returns its value, locks are no-ops.  Overrides glibc
  NPTL at link time.
* `ld/bm.ld`: RAM layout (image at 0x80000000, stack top, slab, brk
  arena bounds).

## Measurement (bmperf)

`bmperf.c` snapshots the full PMC set and prints two blocks over the
UART: "full-run" (constructor -> exit) and "main-loop (compute only)".
The compute window also drives the hardware-windowed raw counter block
(bus beats, cache misses, combinator histogram) via immediate-form
mcountinhibit bit-3 writes, so the testbench can follow the window from
the retire stream. RAM usage marks (brk + slab) are printed with
the counters.

## Toolchain
It has been tested with GCC and GHC cross compilers, both targeting riscv32-linux 
with static glibc, ilp32, rv32imaf:
* a riscv32 glibc GCC (any Yocto/OpenEmbedded or crosstool-ng build
  works; the scripts expect it as `riscv32-oe-linux-gcc` on PATH under
  $LAMBDA/xtool/bin, adjust the two variables at the top of the
  scripts for a differently-named triple);
* a riscv32 GHC cross-compiler (9.6.x) built against that same
  toolchain, expected at $LAMBDA/ghc-cross-install/bin.
Point LAMBDA= at the directory holding both.
