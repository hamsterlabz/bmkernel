/* board.h - the bmkernel's board port interface.  Everything the kernel
 * knows about the SoC goes through these four points; board/bbird/ is a
 * complete example, board/stub/ an empty template for a new CPU. */
#ifndef BM_BOARD_H
#define BM_BOARD_H

/* Called from the reset path after sp/gp are live and BEFORE any console
 * output: program clocks, UART divisors, whatever the board needs. */
void board_early_init(void);

/* Blocking console byte out.  All kernel and program output funnels here. */
void board_putc(int c);

/* Terminate the run and report `code` to the outside world (a tohost
 * store, a debugger break, an LED pattern...).  Must not return. */
void board_exit(unsigned code) __attribute__((noreturn));

/* BOARD_XPMC: 1 if the CPU implements the extended custom performance
 * counters bmperf.c knows how to read (per-cause lost-cycle partition in
 * CSRs 0x7c2..0x7fc); 0 gives cycles/instret only. */
#include "board_cfg.h"
#ifndef BOARD_XPMC
#define BOARD_XPMC 0
#endif

#endif
