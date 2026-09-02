/* fun_main.c - the C body of a BARE fun (fn.*) benchmark (no glibc). Called by
 * fun_start.S (which took the reset/full-run start snapshot). Reuses the SAME
 * bmperf.c (dual PMC; the counter set lives in bmperf.h) + syscalls.c UART (bm_uputc/bm_uputu) as the
 * GHC path, as libraries - the only fun-specific code is fun_config (the fn.*
 * reduction driver).
 *
 * Both PMC windows are snapshotted BEFORE any UART printing, so neither the
 * report printing nor glibc (there is none) is part of the measurement:
 *   main-loop = g_ml_start -> g_ml_end   (just the reduction)
 *   full-run  = g_full_start -> g_full_end (reset -> answer computed) */

#include "bmperf.h"   /* bb_perf_t + the bmperf_* prototypes. NEVER re-declare
                       * the buffer here: a local copy of the typedef that is
                       * smaller than the counter set makes bmperf_snap run off
                       * the end of one buffer into the next, and every counter
                       * past the local size reads back as garbage. */

extern long fun_config(void);                 /* fun_config.S: reduce -> answer  */
extern void bm_uputc(int);                    /* syscalls.c (reused, not copied)  */
extern void bm_uputu(unsigned long);
extern void bm_uputs(const char *);

/* g_full_start is filled at reset by fun_start.S; the rest here. */
bb_perf_t g_full_start, g_ml_start, g_ml_end, g_full_end;

static void print_int(long v){
  if(v < 0){ bm_uputc('-'); v = -v; }
  bm_uputu((unsigned long)v); bm_uputc('\n');
}

#define TOHOST (*(volatile unsigned *)0x10012000u)

void fun_error(void){ bm_uputs("error"); bm_uputc(10); *(volatile unsigned*)0x10012000u = 1; for(;;){} }

void fun_run(void){
  bmperf_snap(&g_ml_start);
  long r = fun_config();               /* the graph reduction = the main loop   */
  bmperf_snap(&g_ml_end);
  bmperf_snap(&g_full_end);            /* full-run end = reset -> answer computed */

  /* everything below (UART) is OUTSIDE both windows */
  bmperf_report(&g_ml_start, &g_ml_end);          /* main-loop block */
  print_int(r);                                    /* the answer */
  bmperf_report_full(&g_full_start, &g_full_end);  /* full-run block */

  TOHOST = 1;                          /* PASS -> the AXI4-lite peripheral halts */
  for(;;){}
}
