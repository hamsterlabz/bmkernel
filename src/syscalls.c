/* syscalls.c - Linux syscall emulation for a static glibc rv32 binary running
 * bare-metal in M-mode. Called from start.S's ecall dispatcher as
 *   long bm_syscall(a0,a1,a2,a3,a4,a5, long nr)
 * Only the calls a static glibc program + the GHC RTS actually issue are
 * serviced; everything else returns -ENOSYS. All I/O and time map to the SoC. */
#include <stdint.h>
#include <stddef.h>

/* ---- SoC devices ---- */
/* console driver: the shared HAL (thinOS/hal is the bare-metal standard) */
#include "board.h"
#define CLOCK_HZ 100000000ull                          /* core clock (bench convention) */

/* ---- rv32 (asm-generic) syscall numbers ---- */
enum { SYS_ppoll_time64=414, SYS_sched_getaffinity=123, SYS_ioctl=29, SYS_readlinkat=78, SYS_read=63, SYS_write=64, SYS_writev=66,
       SYS_openat=56, SYS_close=57, SYS_lseek=62,   /* files, on the card */
       SYS_exit=93, SYS_exit_group=94, SYS_set_tid_address=96, SYS_futex=98,
       SYS_set_robust_list=99, SYS_nanosleep=101, SYS_clock_gettime=113,
       SYS_tgkill=131, SYS_rt_sigaction=134, SYS_rt_sigprocmask=135,
       SYS_uname=160, SYS_getpid=172, SYS_gettid=178, SYS_sysinfo=179,
       SYS_brk=214, SYS_munmap=215, SYS_mmap=222, SYS_mprotect=226,
       SYS_madvise=233, SYS_prlimit64=261, SYS_getrandom=278, SYS_statx=291,
       SYS_clock_gettime64=403, SYS_clock_getres_time64=406, SYS_futex_time64=422 };
#define EBADF   9
#define ENOSYS  38
#define ENOTTY  25
#define ENOENT  2
#define EIO     5
#define EINVAL 22

/* ---- console (exposed for bmperf.c: safe from the trap handler, no ecall) ---- */
void bm_uputc(int c){ board_putc(c); }   /* board port: one console driver */
void bm_uputs(const char*s){ while(*s) bm_uputc(*s++); }
void bm_uputu(unsigned long v){ char b[12]; int i=0;
  if(!v){bm_uputc('0');return;} while(v){b[i++]=(char)('0'+v%10); v/=10;} while(i) bm_uputc(b[--i]); }
#define uputc bm_uputc
#define uputs bm_uputs
static void ux(unsigned v){ const char*h="0123456789abcdef"; char b[8]; int i=0;
  if(!v){uputc('0');return;} while(v){b[i++]=h[v&15]; v>>=4;} while(i) uputc(b[--i]); }

/* ---- C heap (brk arena) ---- */
extern char _end[];                 /* end of .bss (linker) */
extern char _kheap_end[];           /* top of the brk arena (linker) */
static char *brk_cur = 0;

/* ---- MBlock slab (mmap anonymous) ---- */
extern char _slab_start[], _slab_end[];
static char *slab_cur = 0;

char *bm_brk_hwm = 0;               /* brk arena high-water; bmperf prints it */
char *bm_slab_hwm = 0;              /* mmap slab high-water (RTS mblocks ride mmap) */

static long do_brk(unsigned long nb){
  if(!brk_cur) brk_cur = _end;
  if(nb == 0) return (long)brk_cur;                 /* query */
  if((char*)nb < _end || (char*)nb > _kheap_end) return (long)brk_cur; /* refuse; keep */
  brk_cur = (char*)nb;
  if(brk_cur > bm_brk_hwm) bm_brk_hwm = brk_cur;
  return (long)brk_cur;
}

static long do_mmap(unsigned long addr, unsigned long len, long prot,
                    long flags, long fd, long off){
  (void)addr; (void)prot; (void)off;
  if(fd >= 0) return -ENOSYS;                        /* only anonymous */
  if(!slab_cur) slab_cur = _slab_start;
  unsigned long a = ((unsigned long)slab_cur + 0xfffff) & ~0xfffffUL; /* 1 MB align */
  if((char*)(a + len) > _slab_end) return -12;       /* ENOMEM */
  if((char*)(a + len) > bm_slab_hwm) bm_slab_hwm = (char*)(a + len);
  slab_cur = (char*)(a + len);
  (void)flags;
  return (long)a;
}

/* clock: struct timespec {long tv_sec; long tv_nsec;} for 32-bit;
 * struct __timespec64 {long long tv_sec; long tv_nsec;} for *_time64. */
static unsigned long long rdcycle64(void){
  unsigned lo, hi, hi2;
  do { __asm__ volatile("rdcycleh %0":"=r"(hi));
       __asm__ volatile("rdcycle  %0":"=r"(lo));
       __asm__ volatile("rdcycleh %0":"=r"(hi2)); } while(hi!=hi2);
  return ((unsigned long long)hi<<32)|lo;
}
static long do_clock(int t64, void *ts){
  unsigned long long c = rdcycle64();
  unsigned long long sec = c / CLOCK_HZ;
  unsigned long nsec = (unsigned long)(((c % CLOCK_HZ) * 1000000000ull) / CLOCK_HZ);
  if(t64){ long long *p = ts; p[0]=(long long)sec; ((long*)ts)[2]=(long)nsec; }
  else   { long *p = ts; p[0]=(long)sec; p[1]=(long)nsec; }
  return 0;
}

/* PMC snapshot at first entry, reported at exit (see bmperf.c). */
void bm_perf_start(void);
void bm_perf_report_exit(void);

__attribute__((noreturn)) static void do_exit(int code){
  bm_perf_report_exit();
  board_exit((unsigned)code);
}

/* Override glibc exit/_exit/_Exit/abort: go straight to PMC-report + tohost,
 * bypassing glibc's static __run_exit_handlers / _dl_fini path (which faults
 * bare-metal without a real link map). We are earlier than -lc in the link
 * group, so __libc_start_main's post-main exit() resolves here. */
__attribute__((noreturn)) void exit(int code){ do_exit(code); }
__attribute__((noreturn)) void _exit(int code){ do_exit(code); }
__attribute__((noreturn)) void _Exit(int code){ do_exit(code); }
__attribute__((noreturn)) void abort(void){ bm_uputs("\n[bmkernel] abort\n"); do_exit(134); }

/* Neutralize libgcc's DWARF stack-unwinder. It is invoked bare-metal ONLY via
 * backtrace() (glibc malloc_printerr/__libc_message build a backtrace before
 * writing their error). The FDE scan hangs bare-metal; stubbing _Unwind_Backtrace
 * to "end of stack" makes backtrace() return 0 frames, so any abort path writes
 * its diagnostic to UART instead of spinning. (--allow-multiple-definition: ours,
 * being first in the link group, wins over libgcc's.) */
int _Unwind_Backtrace(void *trace_fn, void *arg){ (void)trace_fn; (void)arg; return 5; /* _URC_END_OF_STACK */ }

/* ---- files: fat16.c over the HAL sd.c, the same pair thinOS boots on ----
 * fat_* fds start at 0, the syscall world wants them clear of 0/1/2, so the
 * two are offset by FDBASE. The card is mounted on first use.  */
#define BMFS_FDBASE 3
int  fat_mount(void);
int  fat_open(const char *path);
int  fat_read(int fd, void *dst, unsigned n);
int  fat_lseek(int fd, unsigned pos);
unsigned fat_size(int fd);
/* Declared-only, it broke the link for every nofib/gadt/flite fun target. Weak, so a build that
   DOES link the FAT gets the real one and everything else gets a definition
   rather than an undefined reference. */
__attribute__((weak)) unsigned fat_size(int fd) { (void)fd; return 0u; }

__attribute__((weak)) int  fat_mount(void) { return -1; }
__attribute__((weak)) int  fat_open(const char *path) { (void)path; return -1; }
__attribute__((weak)) int  fat_read(int fd, void *dst, unsigned n) { (void)fd; (void)dst; (void)n; return -1; }
__attribute__((weak)) int  fat_lseek(int fd, unsigned pos) { (void)fd; (void)pos; return -1; }
__attribute__((weak)) int  fat_close(int fd) { (void)fd; return -1; }

int  fat_close(int fd);

static int bmfs_up = 0;
static unsigned bmfs_pos[8];

static int bmfs_ready(void){
  if(!bmfs_up){ if(fat_mount() != 0) return -ENOENT; bmfs_up = 1; }
  return 0;
}

static long bmfs_open(const char *path){
  int r = bmfs_ready(); if(r) return r;
  /* the RTS opens with a full path; the card has one volume and 8.3 names */
  const char *p = path; for(const char *q = path; *q; q++) if(*q=='/') p = q+1;
  int fd = fat_open(p);
  if(fd < 0) return -ENOENT;
  if(fd < 8) bmfs_pos[fd] = 0;
  return fd + BMFS_FDBASE;
}

static long bmfs_read(int fd, void *dst, unsigned n){
  int f = fd - BMFS_FDBASE; if(f < 0) return -EBADF;
  int k = fat_read(f, dst, n);
  if(k < 0) return -EIO;
  if(f < 8) bmfs_pos[f] += (unsigned)k;
  return k;
}

static long bmfs_lseek(int fd, long off, int whence){
  int f = fd - BMFS_FDBASE; if(f < 0 || f >= 8) return -EBADF;
  unsigned base = (whence==1) ? bmfs_pos[f] : (whence==2) ? fat_size(f) : 0u;
  unsigned pos = base + (unsigned)off;
  if(fat_lseek(f, pos) < 0) return -EINVAL;
  bmfs_pos[f] = pos;
  return (long)pos;
}

static long bmfs_size(int fd){
  int f = fd - BMFS_FDBASE; if(f < 0) return 0;
  return (long)fat_size(f);
}

static long bmfs_close(int fd){
  int f = fd - BMFS_FDBASE; if(f < 0) return -EBADF;
  return fat_close(f) < 0 ? -EIO : 0;
}

long bm_syscall(long a0,long a1,long a2,long a3,long a4,long a5,long nr){
#ifdef BM_TRACE_SYSCALLS
  uputs("\n<s"); bm_uputu((unsigned long)nr);
  uputc(' '); ux((unsigned)a0); uputc(' '); ux((unsigned)a1);
  uputc(' '); ux((unsigned)a2); uputc('>');
#endif
  switch(nr){
    case SYS_write: {
      if(a0==1||a0==2||a0==250){ const char*p=(const char*)a1; for(long i=0;i<a2;i++) uputc(p[i]); return a2; }
      return -EBADF; }
    case SYS_writev: {
      if(a0==1||a0==2){ const long *iov=(const long*)a1; long tot=0;
        for(long i=0;i<a2;i++){ const char*base=(const char*)iov[i*2]; long l=iov[i*2+1];
          for(long j=0;j<l;j++) uputc(base[j]); tot+=l; } return tot; }
      return -EBADF; }
    case SYS_ppoll_time64: {
      /* GHC's fdReady probes readiness before hGetBuf; every fd here is
       * always ready (stdin is a memory blob, files are the FAT card). */
      if (a1 > 0) { short *ev = (short*)a0; for (long i = 0; i < a1; i++) ev[i*4+3] = ev[i*4+2]; }
      return a1 > 0 ? a1 : 0; }
    case SYS_sched_getaffinity: {
      /* one hart: a single-CPU mask, so thread-pool sizing (CPU_COUNT)
       * reads 1 instead of uninitialized stack */
      char *m = (char*)a2; for (long i = 0; i < a1; i++) m[i] = 0;
      if (a1 > 0) m[0] = 1;
      return a1; }
    case SYS_read: {
      /* stdin can be an embedded blob: link an object defining
       * bm_stdin_data/bm_stdin_len (builders: STDIN=<file>).  Absent
       * (weak = 0), fd 0 stays EOF as before. */
      if(a0==0){
        extern const char bm_stdin_data[] __attribute__((weak));
        extern const unsigned bm_stdin_len __attribute__((weak));
        static unsigned pos = 0;
        if(!&bm_stdin_len) return 0;
        unsigned left = bm_stdin_len - pos, n = (unsigned)a2 < left ? (unsigned)a2 : left;
        for(unsigned i=0;i<n;i++) ((char*)a1)[i] = bm_stdin_data[pos+i];
        pos += n; return n; }
      if(a0<=2) return 0;                             /* stdout/err: EOF */
      return bmfs_read((int)a0,(void*)a1,(unsigned)a2); }
    case SYS_openat:
      /* a WRITE-open (the RTS's .hp heap-profile stream) gets a synthetic
       * fd that mirrors to the UART; reads keep going to the FAT card */
      if (a2 & 01101) return 250;   /* O_WRONLY|O_CREAT|O_TRUNC bits */
      return bmfs_open((const char*)a1);
    case SYS_close:   return (a0<=2||a0==250)?0:bmfs_close((int)a0);
    case SYS_lseek:   return bmfs_lseek((int)a0,(long)a1,(int)a3);
    case SYS_brk:  return do_brk((unsigned long)a0);
    case SYS_mmap: return do_mmap(a0,a1,a2,a3,a4,a5);
    case SYS_munmap: return 0;                        /* bump slab: leak */
    case SYS_mprotect: case SYS_madvise: return 0;    /* no MMU */
    case SYS_clock_gettime:   return do_clock(0,(void*)a1);
    case SYS_clock_gettime64: return do_clock(1,(void*)a1);
    /* glibc probes the resolution before it will trust a clockid; ENOSYS
     * here made clock_gettime(CLOCK_THREAD_CPUTIME_ID) -- the RTS's -T
     * CPU-time source -- report "Function not implemented" and the RTS
     * treat it as fatal.  One core, one clock: 10 ns at 100 MHz. */
    case SYS_clock_getres_time64: {
      long long *pp = (long long*)a1;
      if(pp){ pp[0]=0; ((long*)a1)[2]=10; }
      return 0; }
    case SYS_getrandom: { unsigned char*b=(unsigned char*)a0; unsigned long long c=rdcycle64();
      for(long i=0;i<a1;i++){ c=c*6364136223846793005ull+1; b[i]=(unsigned char)(c>>33); } return a1; }
    case SYS_prlimit64: {                             /* old_limit at a3: {cur,max} 64-bit */
      if(a3){ unsigned long long *o=(unsigned long long*)a3;
        o[0]=(a1==3)?0x800000ull:256ull; o[1]=o[0]; } return 0; }
    case SYS_readlinkat: {                             /* /proc/self/exe -> fake */
      const char*p=(const char*)a1; char*buf=(char*)a2; long sz=a3;
      const char*r="/ghc-bare"; long l=0; while(r[l]&&l<sz){buf[l]=r[l];l++;} (void)p; return l; }
    case SYS_statx: {                                  /* fd 0/1/2 -> char device */
      /* statx buf: fill stx_mask, stx_mode. mode offset per Linux struct statx:
       * u32 mask@0, blksize@4, u64 attributes@8, u32 nlink@16, uid@20, gid@24,
       * u16 mode@28. Set S_IFCHR (0020000). */
      unsigned char*b=(unsigned char*)a4; if(!b) return -EBADF;
      for(int i=0;i<256;i++) b[i]=0;
      *(unsigned*)(b+0)=0x000007ffu;                   /* STATX_BASIC_STATS */
      if((int)a0>2){                                   /* a file on the card */
        *(unsigned short*)(b+28)=0100000;              /* S_IFREG */
        *(unsigned long long*)(b+40)=bmfs_size((int)a0);  /* stx_size */
      } else
        *(unsigned short*)(b+28)=0020000;              /* S_IFCHR */
      return 0; }
    case SYS_ioctl: return (a0==0||a0==1||a0==2)?0:-ENOTTY;  /* pretend tty */
    case SYS_set_tid_address: return 1;
    case SYS_set_robust_list: return 0;
    case SYS_rt_sigaction: case SYS_rt_sigprocmask: return 0;
    case SYS_futex: case SYS_futex_time64: return 0;   /* non-threaded: uncontended */
    case SYS_nanosleep: return 0;
    case SYS_getpid: case SYS_gettid: case SYS_tgkill: return 1;
    case SYS_uname: { char*u=(char*)a0; for(int i=0;i<6*65;i++)u[i]=0;
      const char*s="Linux"; for(int i=0;s[i];i++)u[i]=s[i]; return 0; }
    case SYS_sysinfo: {                                /* struct sysinfo is 64 B on rv32 (16 words):
       * writing more smashes the CALLER's stack buffer -> __stack_chk_fail. */
      unsigned*s=(unsigned*)a0; for(int i=0;i<16;i++)s[i]=0;
      s[4]=0x08000000u;                                /* totalram = 128 MB   */
      s[5]=0x04000000u;                                /* freeram  = 64 MB    */
      s[15]=1u;                                        /* mem_unit = 1 byte   */
      return 0; }
    case SYS_exit: case SYS_exit_group: do_exit((int)a0);
    default:
      uputs("\n[bmkernel] unhandled syscall "); ux((unsigned)nr); uputc('\n');
      return -ENOSYS;
  }
}

__attribute__((noreturn)) void bm_trap_fatal(unsigned cause, unsigned epc, unsigned tval, unsigned *fr){
  uputs("\n[bmkernel] FATAL trap cause="); ux(cause);
  uputs(" epc="); ux(epc); uputs(" tval="); ux(tval); uputc('\n');
  /* saved GPR frame: [0]=ra [4..9]=a0..a5 (see trap_vec in start.S) */
  uputs("  ra="); ux(fr[0]);
  uputs(" a0="); ux(fr[4]); uputs(" a1="); ux(fr[5]); uputs(" a2="); ux(fr[6]);
  uputs(" a3="); ux(fr[7]); uputs(" a4="); ux(fr[8]); uputs(" a5="); ux(fr[9]);
  uputc('\n');
  board_exit(139u);                                    /* exit 139 (SIGSEGV-ish) */
}
