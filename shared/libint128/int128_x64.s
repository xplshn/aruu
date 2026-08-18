/* x86-64 __int128 multiply/divide/remainder, called from cc1-emitted
 * code (see cmd/dev/cc/qbe.c's funcdiv128). qbe has no widening
 * multiply and no divide wider than 64 bits, and division needs a bit
 * serial long-division loop no ir-level codegen could express, so both
 * live here instead. names and abi match compiler-rt's own
 * __multi3/__udivmodti4, so a real libgcc could stand in unchanged.
 * division always takes the slower, always-correct bit serial path
 * rather than a divisor-fits-in-64-bits shortcut, the same "correct
 * first" choice this project's atomics runtime makes by always
 * compiling to seq_cst instead of a weaker order
 */

.text

/* truncated 128 bit product: a_hi*b_hi is entirely above bit 128 and
 * never computed. one widening mul (for the low*low term's own high
 * word) plus two truncating imul (the cross terms)
 */
.globl __multi3
__multi3:
	/* a: rdi=a_lo rsi=a_hi   b: rdx=b_lo rcx=b_hi */
	movq %rsi, %r8
	imul %rdx, %r8    /* a_hi * b_lo, low 64 */
	movq %rdi, %r9
	imul %rcx, %r9    /* a_lo * b_hi, low 64 */
	movq %rdi, %rax
	mul %rdx           /* rdx:rax = a_lo * b_lo, full 128 bit */
	addq %r8, %rdx
	addq %r9, %rdx
	ret

/* unsigned quotient in rax:rdx, remainder written to *rem. bit serial
 * restoring division: 128 iterations, each pulling the next dividend
 * bit into the remainder (a 128 bit left shift built from shl/shr/or,
 * no shld/rcl in this assembler) and setting the matching quotient bit
 * once the remainder is large enough to subtract the divisor back out
 * (subtract-with-borrow built from cmp/setb/sub, no sbb here either;
 * the same technique qbe.c uses for branchless __int128 subtraction)
 */
.globl __udivmodti4
__udivmodti4:
	/* a: rdi=a_lo rsi=a_hi   b: rdx=b_lo rcx=b_hi   rem: r8 */
	push %r12
	push %r13
	push %r8
	xorq %r8, %r8    /* q_lo = 0 */
	xorq %r9, %r9     /* q_hi = 0 */
	xorq %r10, %r10    /* r_lo = 0 */
	xorq %r11, %r11     /* r_hi = 0 */
	movq $128, %rax
.Ludivloop:
	/* r12 = bit_i(a), the msb about to fall out of the 128 bit
	   dividend as it shifts left */
	movq %rsi, %r12
	shrq $63, %r12

	/* a <<= 1 (128 bit): lo's old msb feeds hi's new lsb */
	movq %rdi, %r13
	shrq $63, %r13
	shlq $1, %rdi
	shlq $1, %rsi
	orq %r13, %rsi

	/* r = (r << 1) | bit_i(a) */
	movq %r10, %r13
	shrq $63, %r13
	shlq $1, %r10
	shlq $1, %r11
	orq %r13, %r11
	orq %r12, %r10

	/* q <<= 1, making room for this iteration's quotient bit */
	movq %r8, %r13
	shrq $63, %r13
	shlq $1, %r8
	shlq $1, %r9
	orq %r13, %r9

	/* r >= b (128 bit unsigned)? */
	cmpq %rcx, %r11
	ja .Ludivcommit
	jb .Ludivskip
	cmpq %rdx, %r10
	jb .Ludivskip
.Ludivcommit:
	/* r -= b, with the borrow from the low word carried into the
	   high word's subtraction by hand (no sbb in this assembler) */
	cmpq %rdx, %r10
	setb %r13b
	movzbq %r13b, %r13
	subq %rdx, %r10
	subq %rcx, %r11
	subq %r13, %r11
	orq $1, %r8
.Ludivskip:
	subq $1, %rax
	jnz .Ludivloop

	pop %rdi
	movq %r10, (%rdi)
	movq %r11, 8(%rdi)
	movq %r8, %rax
	movq %r9, %rdx
	pop %r13
	pop %r12
	ret
