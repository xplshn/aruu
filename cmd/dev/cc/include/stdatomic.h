#ifndef _STDATOMIC_H
#define _STDATOMIC_H

/* ISOC11 atomics. cc1's own __atomic_* builtins pick the right size
 * from the pointer argument at compile time, so unlike a _Generic-
 * dispatched implementation these macros need no per-type expansion
 * at all. every memory order argument is accepted for source
 * compatibility but ignored: cc1 lowers every access to a seq_cst
 * runtime call, see cmd/dev/cc/qbe.c's mkatomicfn */

typedef enum {
  memory_order_relaxed,
  memory_order_consume,
  memory_order_acquire,
  memory_order_release,
  memory_order_acq_rel,
  memory_order_seq_cst,
} memory_order;

typedef _Atomic _Bool              atomic_bool;
typedef _Atomic char               atomic_char;
typedef _Atomic signed char        atomic_schar;
typedef _Atomic unsigned char      atomic_uchar;
typedef _Atomic short              atomic_short;
typedef _Atomic unsigned short     atomic_ushort;
typedef _Atomic int                atomic_int;
typedef _Atomic unsigned int       atomic_uint;
typedef _Atomic long               atomic_long;
typedef _Atomic unsigned long      atomic_ulong;
typedef _Atomic long long          atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;

#define ATOMIC_VAR_INIT(value)   (value)
#define atomic_init(obj, value) (*(obj) = (value))

#define atomic_load(obj)          __atomic_load_n((obj), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) __atomic_load_n((obj), (order))
#define atomic_store(obj, val)    __atomic_store_n((obj), (val), memory_order_seq_cst)
#define atomic_store_explicit(obj, val, order) __atomic_store_n((obj), (val), (order))
#define atomic_exchange(obj, val) __atomic_exchange_n((obj), (val), memory_order_seq_cst)
#define atomic_exchange_explicit(obj, val, order)                                                 \
  __atomic_exchange_n((obj), (val), (order))

#define atomic_compare_exchange_strong(obj, exp, val)                                             \
  __atomic_compare_exchange_n((obj), (exp), (val), 0, memory_order_seq_cst, memory_order_seq_cst)
#define atomic_compare_exchange_weak(obj, exp, val)                                                \
  __atomic_compare_exchange_n((obj), (exp), (val), 1, memory_order_seq_cst, memory_order_seq_cst)
#define atomic_compare_exchange_strong_explicit(obj, exp, val, succ, fail)                         \
  __atomic_compare_exchange_n((obj), (exp), (val), 0, (succ), (fail))
#define atomic_compare_exchange_weak_explicit(obj, exp, val, succ, fail)                           \
  __atomic_compare_exchange_n((obj), (exp), (val), 1, (succ), (fail))

#define atomic_fetch_add(obj, val) __atomic_fetch_add((obj), (val), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, val, order) __atomic_fetch_add((obj), (val), (order))
#define atomic_fetch_sub(obj, val) __atomic_fetch_sub((obj), (val), memory_order_seq_cst)
#define atomic_fetch_sub_explicit(obj, val, order) __atomic_fetch_sub((obj), (val), (order))
#define atomic_fetch_and(obj, val) __atomic_fetch_and((obj), (val), memory_order_seq_cst)
#define atomic_fetch_and_explicit(obj, val, order) __atomic_fetch_and((obj), (val), (order))
#define atomic_fetch_or(obj, val)  __atomic_fetch_or((obj), (val), memory_order_seq_cst)
#define atomic_fetch_or_explicit(obj, val, order) __atomic_fetch_or((obj), (val), (order))
#define atomic_fetch_xor(obj, val) __atomic_fetch_xor((obj), (val), memory_order_seq_cst)
#define atomic_fetch_xor_explicit(obj, val, order) __atomic_fetch_xor((obj), (val), (order))

#define atomic_is_lock_free(obj) ((void)(obj), 1)

typedef struct {
  atomic_bool _flag;
} atomic_flag;

#define ATOMIC_FLAG_INIT \
  { 0 }
#define atomic_flag_test_and_set(f) __atomic_exchange_n(&(f)->_flag, 1, memory_order_seq_cst)
#define atomic_flag_test_and_set_explicit(f, order) __atomic_exchange_n(&(f)->_flag, 1, (order))
#define atomic_flag_clear(f) __atomic_store_n(&(f)->_flag, 0, memory_order_seq_cst)
#define atomic_flag_clear_explicit(f, order) __atomic_store_n(&(f)->_flag, 0, (order))

#define atomic_thread_fence(order) ((void)(order))
#define atomic_signal_fence(order) ((void)(order))

#endif /* _STDATOMIC_H */
