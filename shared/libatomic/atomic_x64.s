/* x86-64 out-of-line atomic helpers, called by cc1-emitted code for
 * __atomic_* builtins (see cmd/dev/cc/qbe.c's mkatomicfn). every
 * memory order argument is treated as seq_cst: a conservative choice
 * that is always standards-conformant, only ever slower than what a
 * weaker order would allow. matches the real gcc/clang libatomic
 * fallback abi (out-of-line calls for sizes the compiler does not
 * inline directly), size-suffixed per operation, so any future
 * inlining work can drop straight in without changing callers
 */

.text

.globl __atomic_load_4
__atomic_load_4:
	movl (%rdi), %eax
	ret

.globl __atomic_load_8
__atomic_load_8:
	movq (%rdi), %rax
	ret

/* plain store already has release ordering on x86-64; mfence upgrades
 * it to full seq_cst, ordering it against a later seq_cst load
 */
.globl __atomic_store_4
__atomic_store_4:
	movl %esi, (%rdi)
	mfence
	ret

.globl __atomic_store_8
__atomic_store_8:
	movq %rsi, (%rdi)
	mfence
	ret

/* xchg with a memory operand is implicitly locked, no explicit lock
 * prefix needed
 */
.globl __atomic_exchange_4
__atomic_exchange_4:
	movl %esi, %eax
	xchg %eax, (%rdi)
	ret

.globl __atomic_exchange_8
__atomic_exchange_8:
	movq %rsi, %rax
	xchg %rax, (%rdi)
	ret

.globl __atomic_fetch_add_4
__atomic_fetch_add_4:
	movl %esi, %eax
	lock xadd %eax, (%rdi)
	ret

.globl __atomic_fetch_add_8
__atomic_fetch_add_8:
	movq %rsi, %rax
	lock xadd %rax, (%rdi)
	ret

.globl __atomic_fetch_sub_4
__atomic_fetch_sub_4:
	neg %esi
	movl %esi, %eax
	lock xadd %eax, (%rdi)
	ret

.globl __atomic_fetch_sub_8
__atomic_fetch_sub_8:
	neg %rsi
	movq %rsi, %rax
	lock xadd %rax, (%rdi)
	ret

/* no single locked fetch-and-op instruction exists for and/or/xor:
 * load, compute the new value, lock cmpxchg it in, retry on failure
 * (another cpu changed *ptr in between). on a successful cmpxchg eax
 * is left unmodified, which is exactly the old value fetch-and-op
 * must return; on failure eax is reloaded with the real current
 * value by the instruction itself, which is exactly what the retry
 * needs
 */
.globl __atomic_fetch_and_4
__atomic_fetch_and_4:
	movl (%rdi), %eax
.Lretry_and_4:
	movl %eax, %ecx
	andl %esi, %ecx
	lock cmpxchg %ecx, (%rdi)
	jnz .Lretry_and_4
	ret

.globl __atomic_fetch_and_8
__atomic_fetch_and_8:
	movq (%rdi), %rax
.Lretry_and_8:
	movq %rax, %rcx
	andq %rsi, %rcx
	lock cmpxchg %rcx, (%rdi)
	jnz .Lretry_and_8
	ret

.globl __atomic_fetch_or_4
__atomic_fetch_or_4:
	movl (%rdi), %eax
.Lretry_or_4:
	movl %eax, %ecx
	orl %esi, %ecx
	lock cmpxchg %ecx, (%rdi)
	jnz .Lretry_or_4
	ret

.globl __atomic_fetch_or_8
__atomic_fetch_or_8:
	movq (%rdi), %rax
.Lretry_or_8:
	movq %rax, %rcx
	orq %rsi, %rcx
	lock cmpxchg %rcx, (%rdi)
	jnz .Lretry_or_8
	ret

.globl __atomic_fetch_xor_4
__atomic_fetch_xor_4:
	movl (%rdi), %eax
.Lretry_xor_4:
	movl %eax, %ecx
	xor %esi, %ecx
	lock cmpxchg %ecx, (%rdi)
	jnz .Lretry_xor_4
	ret

.globl __atomic_fetch_xor_8
__atomic_fetch_xor_8:
	movq (%rdi), %rax
.Lretry_xor_8:
	movq %rax, %rcx
	xor %rsi, %rcx
	lock cmpxchg %rcx, (%rdi)
	jnz .Lretry_xor_8
	ret

/* __atomic_compare_exchange_N(ptr, expected, desired): the accumulator
 * is loaded from *expected before cmpxchg, exactly matching the
 * instructions own semantics (compare accumulator against *ptr; on
 * match store desired and set zf; on mismatch load the accumulators
 * own register with *ptrs real value and clear zf), so success needs
 * no extra work at all and failure only needs writing the reloaded
 * accumulator back out to *expected
 */
.globl __atomic_compare_exchange_4
__atomic_compare_exchange_4:
	movl (%rsi), %eax
	lock cmpxchg %edx, (%rdi)
	jz .Lcas4_ok
	movl %eax, (%rsi)
	movl $0, %eax
	ret
.Lcas4_ok:
	movl $1, %eax
	ret

.globl __atomic_compare_exchange_8
__atomic_compare_exchange_8:
	movq (%rsi), %rax
	lock cmpxchg %rdx, (%rdi)
	jz .Lcas8_ok
	movq %rax, (%rsi)
	movl $0, %eax
	ret
.Lcas8_ok:
	movl $1, %eax
	ret
