/* atomics.c - 64-bit atomic intrinsics for rv32, which has no native 64-bit
 * atomics and no libatomic here. This is a SINGLE-HART, NON-THREADED bare-metal
 * system (the RTS runs non-threaded, one HEC, no other core), so there is no
 * concurrency: the operations are correct as plain non-atomic load/modify/store.
 * Provides both the __atomic_*_8 (compiler/libatomic ABI) and __sync_*_8
 * (legacy) surfaces the GHC RTS (ghc-prim atomic.c) references. */
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t u8t;

/* ---- __atomic_*_8 ---- */
u8t  __atomic_load_8 (const volatile void *p, int m){ (void)m; return *(const volatile u8t*)p; }
void __atomic_store_8(volatile void *p, u8t v, int m){ (void)m; *(volatile u8t*)p = v; }
u8t  __atomic_exchange_8(volatile void *p, u8t v, int m){ (void)m;
  volatile u8t*q=p; u8t o=*q; *q=v; return o; }
bool __atomic_compare_exchange_8(volatile void *p, void *ex, u8t des,
                                 bool weak, int sm, int fm){ (void)weak;(void)sm;(void)fm;
  volatile u8t*q=p; u8t*e=ex; if(*q==*e){ *q=des; return true; } *e=*q; return false; }
u8t  __atomic_fetch_add_8 (volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=o+v; return o; }
u8t  __atomic_fetch_sub_8 (volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=o-v; return o; }
u8t  __atomic_fetch_and_8 (volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=o&v; return o; }
u8t  __atomic_fetch_or_8  (volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=o|v; return o; }
u8t  __atomic_fetch_xor_8 (volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=o^v; return o; }
u8t  __atomic_fetch_nand_8(volatile void *p, u8t v, int m){ (void)m; volatile u8t*q=p; u8t o=*q; *q=~(o&v); return o; }

/* ---- __sync_*_8 (legacy; ghc-prim/cbits/atomic.c) ---- */
u8t  __sync_fetch_and_add_8 (volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=o+v;return o; }
u8t  __sync_fetch_and_sub_8 (volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=o-v;return o; }
u8t  __sync_fetch_and_and_8 (volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=o&v;return o; }
u8t  __sync_fetch_and_or_8  (volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=o|v;return o; }
u8t  __sync_fetch_and_xor_8 (volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=o^v;return o; }
u8t  __sync_fetch_and_nand_8(volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=~(o&v);return o; }
u8t  __sync_val_compare_and_swap_8(volatile void*p,u8t o,u8t n){ volatile u8t*q=p;u8t v=*q; if(v==o)*q=n; return v; }
bool __sync_bool_compare_and_swap_8(volatile void*p,u8t o,u8t n){ volatile u8t*q=p; if(*q==o){*q=n;return true;} return false; }
u8t  __sync_lock_test_and_set_8(volatile void*p,u8t v){ volatile u8t*q=p;u8t o=*q;*q=v;return o; }
