/* bmperf.c - CSR PMC snapshot/report for the clash-rvfun core. Output goes
 * straight to the UART via bm_uputs/bm_uputu (NOT glibc printf): the whole-
 * program report is emitted from inside the exit-syscall trap handler, where a
 * nested ecall (printf->write) and glibc's torn-down stdio would be unsafe. IPC
 * is reported as integer milli-IPC (instret*1000/cycles) to avoid soft-float.
 *
 * CSR MAP: matches THIS core (src/Core/Pipeline.hs readCsr), NOT the mk24/
 * blackbird-f bb_perf.h layout.
 *
 * WHERE THE CYCLES GO. The report has two halves that must not be confused.
 *
 *  1. EVENTS (0x7d2..0x7e2 even, 0x7e6..0x7e8). Overlapping rates: one cycle
 *     can be a fetch starve AND a load-use stall, so these do NOT add up to
 *     anything. They answer "how often does X happen".
 *
 *  2. THE LOST-CYCLE PARTITION (0x7c2..0x7c9, 0x7e9..0x7fa). Every bubble is
 *     tagged at the pipeline boundary that created it and the tag rides down
 *     with the slot, so exactly one counter ticks per bubble cycle. These 26
 *     counters SUM to wb_real_bubble (0x7e2), and
 *
 *         cycles = instret + wb_real_bubble + dbus_freeze
 *
 *     closes the whole run. The report prints both residuals; a non-zero
 *     residual, or a non-zero "unknown", means the core's cause model is
 *     incomplete -- it is not a rounding artefact.
 *
 * Scopes: (a) whole-program - bm_perf_start() (constructor) + bm_perf_report_exit()
 * (from the exit syscall); (b) region - bmperf_snap/report/alloc, FFI-imported
 * by a Haskell benchmark to bracket just its measurement loop. */
#include <stdint.h>
#include "bmperf.h"   /* BB_PMC_N + bb_perf_t: the ONE definition, shared with
                       * every caller that allocates a snapshot buffer */
#define RDCSR(n) ({ unsigned long _v; __asm__ volatile("csrr %0, " #n:"=r"(_v)::"memory"); _v; })

extern void bm_uputc(int);
extern void bm_uputs(const char*);
extern void bm_uputu(unsigned long);

#include "board.h"
static void snap(bb_perf_t*a){
  /* 0..1  architectural */
  a->v[0]=RDCSR(0xB00); a->v[1]=RDCSR(0xB02);
#if BOARD_XPMC
  /* 2..10 event rates */
  a->v[2]=RDCSR(0x7da); a->v[3]=RDCSR(0x7dc); a->v[4]=RDCSR(0x7d6);
  a->v[5]=RDCSR(0x7d8); a->v[6]=RDCSR(0x7de); a->v[7]=RDCSR(0x7e0);
  a->v[8]=RDCSR(0x7d4); a->v[9]=RDCSR(0x7d2); a->v[10]=RDCSR(0x7e2);
  /* 11..13 D-cache */
  a->v[11]=RDCSR(0x7e6); a->v[12]=RDCSR(0x7e7); a->v[13]=RDCSR(0x7e8);
  /* 14..21 lost cycles born at EXECUTE */
  a->v[14]=RDCSR(0x7c2); a->v[15]=RDCSR(0x7c3); a->v[16]=RDCSR(0x7c4);
  a->v[17]=RDCSR(0x7c5); a->v[18]=RDCSR(0x7c6); a->v[19]=RDCSR(0x7c7);
  a->v[20]=RDCSR(0x7c8); a->v[21]=RDCSR(0x7c9);
  /* 22..27 lost cycles born at FETCH */
  a->v[22]=RDCSR(0x7e9); a->v[23]=RDCSR(0x7ea); a->v[24]=RDCSR(0x7eb);
  a->v[25]=RDCSR(0x7ec); a->v[26]=RDCSR(0x7ed); a->v[27]=RDCSR(0x7ee);
  /* 28..39 lost cycles born at DECODE, 40 unclassified */
  a->v[28]=RDCSR(0x7ef); a->v[29]=RDCSR(0x7f0); a->v[30]=RDCSR(0x7f1);
  a->v[31]=RDCSR(0x7f2); a->v[32]=RDCSR(0x7f3); a->v[33]=RDCSR(0x7f4);
  a->v[34]=RDCSR(0x7f5); a->v[35]=RDCSR(0x7f6); a->v[36]=RDCSR(0x7f7);
  a->v[37]=RDCSR(0x7f8); a->v[38]=RDCSR(0x7f9); a->v[39]=RDCSR(0x7fa);
  a->v[40]=RDCSR(0x7fb); a->v[41]=RDCSR(0x7fc);
#else
  for (int i = 2; i < 42; i++) a->v[i] = 0;
#endif
}

static void line(const char*name, unsigned long val){
  bm_uputs(name); bm_uputu(val); bm_uputc('\n');
}
/* signed print, for the two residuals (which must be zero) */
static void sline(const char*name, long val){
  bm_uputs(name);
  if (val < 0) { bm_uputc('-'); val = -val; }
  bm_uputu((unsigned long)val); bm_uputc('\n');
}

static const char *const bub_name[28] = {
  "  ex_squash:       ", "  ex_mmu_walk:     ", "  ex_dport_busy:   ",
  "  ex_misalign:     ", "  ex_amo:          ", "  ex_div:          ",
  "  ex_fdiv_fsqrt:   ", "  ex_fcvt:         ",
  "  fs_redir:        ", "  fs_iwait:        ", "  fs_poison:       ",
  "  fs_alloc_shadow: ", "  fs_imiss:        ", "  fs_other:        ",
  "  id_load_use:     ", "  id_mul_use:      ", "  id_late_int:     ",
  "  id_agu:          ", "  id_f_hazard:     ", "  id_csr_drain:    ",
  "  id_priv_drain:   ", "  id_fun_rcache:   ", "  id_fun_stage:    ",
  "  id_mispredict:   ", "  id_flush:        ", "  id_spine_link:   ",
  "  id_spine_elink:  ", "  UNKNOWN:         "
};

static void report(const char*title, const bb_perf_t*s,const bb_perf_t*e){
  unsigned long d[BB_PMC_N]; int i;
  unsigned long fs=0, id=0, ex=0, sum=0;
  for(i=0;i<BB_PMC_N;i++) d[i]=e->v[i]-s->v[i];
  for(i=14;i<22;i++) ex += d[i];
  for(i=22;i<28;i++) fs += d[i];
  for(i=28;i<41;i++) id += d[i];
  sum = ex + fs + id + d[41];
  unsigned long milli_ipc = d[0] ? (d[1]*1000UL)/d[0] : 0;
  bm_uputs("--- PMC "); bm_uputs(title); bm_uputs(" ---\n");
  line("cycles:            ", d[0]);
  line("instret:           ", d[1]);
  line("milli_IPC:         ", milli_ipc);
  line("dbus_freeze:       ", d[4]);
  line("wb_real_bubble:    ", d[10]);
  sline("cycle_residual:    ", (long)d[0] - (long)d[1] - (long)d[10] - (long)d[4]);
  bm_uputs("-- event rates (overlapping; these do NOT sum) --\n");
  line("fetch_starve:      ", d[2]);
  line("stall_ld:          ", d[3]);
  line("stall_mul:         ", d[5]);
  line("misp_nottaken:     ", d[6]);
  line("misp_taken:        ", d[7]);
  line("br_redir(jalr):    ", d[8]);
  line("trap_flush:        ", d[9]);
  line("dcache_fill:       ", d[11]);
  line("dcache_wb:         ", d[12]);
  line("dcache_uncached:   ", d[13]);
  bm_uputs("-- lost cycles, by cause (a partition of wb_real_bubble) --\n");
  for(i=0;i<28;i++) if(d[14+i]) line(bub_name[i], d[14+i]);
  line("  = fetch:         ", fs);
  line("  = decode:        ", id);
  line("  = execute:       ", ex);
  /* the spine cells are graph DATA committing a node, not stalled cycles:
   * report what the machine really issued per cycle alongside the RV IPC */
  line("slots_issued:      ", d[1] + d[39] + d[40]);
  sline("  bubble_residual: ", (long)d[10] - (long)sum);
#if BOARD_XPMC
  line("pmc_generation:    ", RDCSR(0x7ca));
#endif
  /* arena high-water marks, tracked by the kernel syscall layer: peak
   * memory consumed via brk (glibc malloc) and via the anonymous-mmap
   * slab (the GHC RTS mblock allocator).  Bytes above the arena base. */
  { extern char *bm_brk_hwm, *bm_slab_hwm;
    extern char _end[], _slab_start[];
    line("brk_hwm:           ", bm_brk_hwm  ? (unsigned long)(bm_brk_hwm - _end) : 0);
    line("slab_hwm:          ", bm_slab_hwm ? (unsigned long)(bm_slab_hwm - _slab_start) : 0); }
}

/* (a) whole-program: snap at program init via a constructor, report at exit. */
static bb_perf_t g_start; static int g_started, g_reported;
__attribute__((constructor)) void bm_perf_start(void){ if(!g_started){ g_started=1; snap(&g_start); } }
void bm_perf_report_exit(void){
  if(g_reported||!g_started) return; g_reported=1;
  /* full run: RTS bring-up (hs_init, one-time FDE sort, GC setup) + the whole
   * program, snapshotted in a constructor and reported at exit. */
  bb_perf_t e; snap(&e); report("full-run (RTS init + main)", &g_start,&e);
}

/* (b) region - FFI entry points for a Haskell benchmark to bracket JUST its
 * main loop (offset from RTS init). bmperf_report prints the "main-loop" block;
 * both blocks share the same counter format. */
static bb_perf_t g_pool[8]; static int g_pool_n;
void *bmperf_alloc(void){ return (g_pool_n<8)?&g_pool[g_pool_n++]:&g_pool[0]; }
#define WRCSR(n,v) __asm__ volatile("csrw " #n ", %0"::"r"(v):"memory")
/* the raw gc_pmc block (bus beats, cache misses, combi histogram) is dumped
 * by the testbench at finish and is windowed in HARDWARE: an mcountinhibit
 * falling edge clears it, mcountinhibit high freezes it.  Open the window
 * after the first region start-snapshot, freeze it at report time, so the
 * dump covers exactly the main run. */
static int g_region_open;
/* immediate CSR forms: the value rides in the instruction bits, so the
 * testbench can follow the window from the retire stream (ghcprof.inc). */
#define PMC_WIN_OPEN()   __asm__ volatile("csrsi 0x320, 8; csrci 0x320, 8":::"memory")
#define PMC_WIN_FREEZE() __asm__ volatile("csrsi 0x320, 8":::"memory")
void  bmperf_snap(void*p){ snap((bb_perf_t*)p);
  if(!g_region_open){ g_region_open=1; PMC_WIN_OPEN(); } }
/* no census may run after the window: a poke pending at the freeze would
 * fire at the next GC entry (inside shutdown), and hs_exit runs one final
 * census unconditionally while heap profiling is enabled.  bm_census_off
 * (bmprof.c, GHC images only -- weak here) clears both. */
extern void bm_census_off(void) __attribute__((weak));
void  bmperf_report(void*s,void*e){ PMC_WIN_FREEZE();   /* bit 3 only: bit 0 is the RTS clock */
  if(bm_census_off) bm_census_off();
  report("main-loop (compute only)", (const bb_perf_t*)s,(const bb_perf_t*)e); }
/* full-run report for the fun harness (no constructor there): reset -> answer. */
void  bmperf_report_full(void*s,void*e){ report("full-run (RTS init + main)", (const bb_perf_t*)s,(const bb_perf_t*)e); }
/* full-run from the reset snapshot (g_start) to a caller-provided END that was
 * taken BEFORE any PMC-report UART printing, so the window excludes the report's
 * own busy-wait. Marks g_reported so the exit path does not double-report. */
void  bmperf_full_from_start(void*e){ if(g_reported) return; g_reported=1;
  report("full-run (RTS init + main)", &g_start, (const bb_perf_t*)e); }
