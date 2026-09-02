/* bmperf.h - the PMC snapshot buffer, shared by everything that takes one.
 *
 * THE ONLY DEFINITION of bb_perf_t and BB_PMC_N. bmperf.c fills a buffer of
 * this size; fun_main.c and any other caller allocate one. They used to carry
 * separate copies of the typedef, which is fine right up to the moment the
 * counter set grows: bmperf_snap then writes past the end of a caller's
 * buffer and silently overwrites the next snapshot. (That is exactly what
 * happened when the set went 14 -> 40: the four consecutive fun_main.c
 * buffers were 56 bytes apart, so every snapshot clobbered the tail of the
 * previous one and every counter from index 14 up read back as garbage.)
 * One header, one size, no way to desynchronise. */
#ifndef BMPERF_H
#define BMPERF_H

/* 2 architectural + 9 event rates + 3 D-cache + 28 bubble causes */
#define BB_PMC_N 42
typedef struct { unsigned long v[BB_PMC_N]; } bb_perf_t;

void bmperf_snap(void *);
void bmperf_report(void *, void *);       /* prints the "main-loop" block */
void bmperf_report_full(void *, void *);  /* prints the "full-run"  block */
void *bmperf_alloc(void);

#endif /* BMPERF_H */
