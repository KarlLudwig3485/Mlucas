/*******************************************************************************
*                                                                              *
*   (C) 1997-2014 by Ernst W. Mayer.                                           *
*                                                                              *
*  This program is free software; you can redistribute it and/or modify it     *
*  under the terms of the GNU General Public License as published by the       *
*  Free Software Foundation; either version 2 of the License, or (at your      *
*  option) any later version.                                                  *
*                                                                              *
*  This program is distributed in the hope that it will be useful, but WITHOUT *
*  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
*  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   *
*  more details.                                                               *
*                                                                              *
*  You should have received a copy of the GNU General Public License along     *
*  with this program; see the file GPL.txt.  If not, you may view one at       *
*  http://www.fsf.org/licenses/licenses.html, or obtain one by writing to the  *
*  Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA     *
*  02111-1307, USA.                                                            *
*                                                                              *
*******************************************************************************/

/*******************************************************************************
   We now include this header file if it was not included before.
*******************************************************************************/
#ifndef radix32_ditN_cy_dif1_gcc_h_included
#define radix32_ditN_cy_dif1_gcc_h_included

#ifdef USE_AVX2	// FMA-based versions of selected macros in this file for Intel AVX2/FMA3

	// Oct 2014: Aggressively deploy FMA, both to save steps in CMULs and in-place butterflies
	// (e.g. in the frequent non-FMA sequence x = x-y, y *= 2, y += x which produces x +- y in the
	// original y,x-regs, we replace the latter 2 ops with one FMA: y = 2*y + x), and also to
	// replace all the ADD/SUBs, thus trading the lower latency of those (3 cycles vs 5 for FMA)
	// with the higher throughput of FMA (2 per cycle vs just 1 ADD/SUB). In order to distinguish
	// the former FMAs from the latter "trivial" ones (in the sense that one multiplicand is unity),
	// we 'undent' the former one tab leftward relative to the trivial FMAs.
	//
	// For the ADD/SUB -> FMA we use rsi to hold the SIMD-register-width-propagated 1.0 and then replace like so:
	// ADD:
	//		vaddpd		%%ymm2,%%ymm1,%%ymm1		// ymm1 += ymm2
	//	-->	vfmadd132pd	(%%rsi),%%ymm2,%%ymm1		// ymm1 = ymm1*1.0 + ymm2
	// SUB:
	//		vsubpd		%%ymm2,%%ymm1,%%ymm1		// ymm1 -= ymm2
	//	-->	vfmsub132pd	(%%rsi),%%ymm2,%%ymm1		// ymm1 = ymm1*1.0 - ymm2
	//
	// The choice of the 132-variant of Intel FMA3 preserves the name (add or sub) of the replaced ADD/SUB op.
	// If we have a register free (or have > 2 such ops in a row, i.e. can spill/reload a data-reg with less
	// memory traffic than multiple implied-loads-of-1.0 would incur) we stick the 1.0 into a register.
	// The other kind of ADD/SUB we may wish to replace with FMA is one where one addend (subtrahend) is in mem,
	// in which case we can only FMAize if 1.0 is in-reg (call it ymm#), in which case we then replace like so:
	// ADD:
	//		vaddpd		(%%mem),%%ymm1,%%ymm1		// ymm1 += (mem)
	//	-->	vfmadd231pd	(%%mem),%%ymm#,%%ymm1		// ymm1 = +(mem)*1.0 + ymm1
	// SUB:
	//		vsubpd		%%ymm2,%%ymm1,%%ymm1		// ymm1 -= (mem)
	//	-->vfnmadd231pd	(%%mem),%%ymm#,%%ymm1		// ymm1 = -(mem)*1.0 + ymm1
	//
	/*
	For GCC-macro version of this, use that - in terms of vec_dbl pointer-offsets - isrt2 + 1,3,5 = cc0,cc1,cc3,
	isrt2 + 7,15,23,31,39,47,55,63 = c00,04,02,06,01,05,03,07, and isrt2 + 71,72 = one,two
	in order to reduce number of args to <= the GCC-allowed maximum of 30:
	*/
	/* Both DIF and DIT macros assume needed roots of unity laid out in memory relative to the input pointer isrt2 like so:
	[Note: AVX version of these macros supplement this basic consts-layout with additional slot for 2.0; AVX2 adds 1.0,sqrt2].
		Description																	Pointer-offset	Address-offset
		---------------------------------------------------------					------------	------------
		two																			isrt2 - 3		isrt2 - 0x60
		one																			isrt2 - 2		isrt2 - 0x40
		sqrt2																		isrt2 - 1		isrt2 - 0x20
		isrt2: 1/sqrt2, in terms of which thr basic 8th root of 1 = (1+i)/sqrt2		isrt2			isrt2
		cc0: Real part of basic 16th root of 1 = c16								isrt2 + 1		isrt2 + 0x20
		ss0: Real part of basic 16th root of 1 = s16								isrt2 + 2		isrt2 + 0x40
		cc1: Real part of basic 32th root of 1 = c32_1								isrt2 + 3		isrt2 + 0x60
		ss1: Real part of basic 32th root of 1 = s32_1								isrt2 + 4		isrt2 + 0x80
		cc3: Real part of 3rd   32th root of 1 = c32_3								isrt2 + 5		isrt2 + 0xa0
		ss3: Real part of 3rd   32th root of 1 = s32_3								isrt2 + 6		isrt2 + 0xc0
	*/
  #ifdef ALL_FMA	// Aggressive-FMA version: Replace most ADD/SUB by FMA-with-one-unity-multiplicand

	// Aggressive-FMA: replace [26 ADD, 154 SUB, 198 FMA, 38 MUL, 450 memref] ==> [10 ADD, 14 SUB, 354 FMA (198 nontrivial), 38 MUL, 535 memref].
	// I.e. trade ~150 ADD/SUB for same #FMA + 85 LOAD-of-vector-const-1.0, those where spill of a register datum in favor of 1.0 not justifiable.
	#define SSE2_RADIX32_DIT_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
	/*...Block 1: */\
		"movq	%[__isrt2],%%rdi \n\t leaq -0x40(%%rdi),%%r8 \n\t leaq -0x60(%%rdi),%%r9	\n\t"/* r8,r9 hold 1.0,2.0 throughout */\
		"movq	%[__r00],%%rsi	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r00)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r08): */\
		/* Can't simply addq these to a 64-bit base array address since the offsets are 4-byte array data: */\
		"movq	%[__arr_offsets],%%rdi				\n\t"\
		"movslq	0x00(%%rdi),%%rax		\n\t"/* p00 */"			movslq	0x10(%%rdi),%%r10	\n\t"/* p04 */\
		"movslq	0x04(%%rdi),%%rbx		\n\t"/* p01 */"			movslq	0x14(%%rdi),%%r11	\n\t"/* p05 */\
		"movslq	0x08(%%rdi),%%rcx		\n\t"/* p02 */"			movslq	0x18(%%rdi),%%r12	\n\t"/* p06 */\
		"movslq	0x0c(%%rdi),%%rdx		\n\t"/* p03 */"			movslq	0x1c(%%rdi),%%r13	\n\t"/* p07 */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10			\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11			\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r11),%%ymm9 			\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14			\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15			\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12			\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r13),%%ymm13			\n\t"*/\
		"vmovaps		(%%r8 ),%%ymm7				\n\t		vmovaps		(%%r9 ),%%ymm13 		\n\t"/*	Instead use ymm7,13 for 1.0,2.0: */\
		"vfmsub132pd	  %%ymm7,%%ymm0,%%ymm2		\n\t		vfmsub132pd	%%ymm7 ,%%ymm8 ,%%ymm10		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm1,%%ymm3		\n\t		vfmsub132pd	%%ymm7 ,%%ymm9 ,%%ymm11		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm4,%%ymm6		\n\t		vfmsub132pd	%%ymm7 ,%%ymm12,%%ymm14		\n\t"\
		"vfmsub132pd 0x20(%%rcx),%%ymm5,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm10 	\n\t"/* one */\
		"vfmsub132pd	%%ymm10,%%ymm7,%%ymm2		\n\t		vfmsub132pd	%%ymm10,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm6,%%ymm3		\n\t		vfmsub132pd	%%ymm10,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm4,%%ymm0		\n\t		vfmsub132pd	%%ymm10,%%ymm14,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm5,%%ymm1		\n\t		vfmsub132pd	(%%rsi),%%ymm15,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm15,%%ymm11		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm10,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r00,r02,r04,r06,r08,r0A,r0C,r0E): */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm13,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm12,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 2: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r10)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x20(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x30(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x24(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x34(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x28(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x38(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x2c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x3c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10			\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11			\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r11),%%ymm9 			\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14			\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15			\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12			\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r13),%%ymm13			\n\t"*/\
		"vmovaps		(%%r8 ),%%ymm7				\n\t		vmovaps		(%%r9 ),%%ymm13 		\n\t"/*	Instead use ymm7,13 for 1.0,2.0: */\
		"vfmsub132pd	  %%ymm7,%%ymm0,%%ymm2		\n\t		vfmsub132pd	%%ymm7 ,%%ymm8 ,%%ymm10		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm1,%%ymm3		\n\t		vfmsub132pd	%%ymm7 ,%%ymm9 ,%%ymm11		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm4,%%ymm6		\n\t		vfmsub132pd	%%ymm7 ,%%ymm12,%%ymm14		\n\t"\
		"vfmsub132pd 0x20(%%rcx),%%ymm5,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm10 	\n\t"/* one */\
		"vfmsub132pd	%%ymm10,%%ymm7,%%ymm2		\n\t		vfmsub132pd	%%ymm10,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm6,%%ymm3		\n\t		vfmsub132pd	%%ymm10,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm4,%%ymm0		\n\t		vfmsub132pd	%%ymm10,%%ymm14,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm5,%%ymm1		\n\t		vfmsub132pd	(%%rsi),%%ymm15,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm15,%%ymm11		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm10,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r10,r12,r14,r16,r18,r1A,r1C,r1E): */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm13,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm12,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 3: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r20)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r28): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x40(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x50(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x44(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x54(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x48(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x58(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x4c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x5c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10			\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11			\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r11),%%ymm9 			\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14			\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15			\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12			\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r13),%%ymm13			\n\t"*/\
		"vmovaps		(%%r8 ),%%ymm7				\n\t		vmovaps		(%%r9 ),%%ymm13 		\n\t"/*	Instead use ymm7,13 for 1.0,2.0: */\
		"vfmsub132pd	  %%ymm7,%%ymm0,%%ymm2		\n\t		vfmsub132pd	%%ymm7 ,%%ymm8 ,%%ymm10		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm1,%%ymm3		\n\t		vfmsub132pd	%%ymm7 ,%%ymm9 ,%%ymm11		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm4,%%ymm6		\n\t		vfmsub132pd	%%ymm7 ,%%ymm12,%%ymm14		\n\t"\
		"vfmsub132pd 0x20(%%rcx),%%ymm5,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm10 	\n\t"/* one */\
		"vfmsub132pd	%%ymm10,%%ymm7,%%ymm2		\n\t		vfmsub132pd	%%ymm10,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm6,%%ymm3		\n\t		vfmsub132pd	%%ymm10,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm4,%%ymm0		\n\t		vfmsub132pd	%%ymm10,%%ymm14,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm5,%%ymm1		\n\t		vfmsub132pd	(%%rsi),%%ymm15,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm15,%%ymm11		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm10,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r20,r22,r24,r26,r28,r2A,r2C,r2E): */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm13,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm12,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 4: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r01)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x60(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x70(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x64(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x74(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x68(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x78(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x6c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x7c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10			\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11			\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r11),%%ymm9 			\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14			\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15			\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12			\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r13),%%ymm13			\n\t"*/\
		"vmovaps		(%%r8 ),%%ymm7				\n\t		vmovaps		(%%r9 ),%%ymm13 		\n\t"/*	Instead use ymm7,13 for 1.0,2.0: */\
		"vfmsub132pd	  %%ymm7,%%ymm0,%%ymm2		\n\t		vfmsub132pd	%%ymm7 ,%%ymm8 ,%%ymm10		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm1,%%ymm3		\n\t		vfmsub132pd	%%ymm7 ,%%ymm9 ,%%ymm11		\n\t"\
		"vfmsub132pd	  %%ymm7,%%ymm4,%%ymm6		\n\t		vfmsub132pd	%%ymm7 ,%%ymm12,%%ymm14		\n\t"\
		"vfmsub132pd 0x20(%%rcx),%%ymm5,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm10 	\n\t"/* one */\
		"vfmsub132pd	%%ymm10,%%ymm7,%%ymm2		\n\t		vfmsub132pd	%%ymm10,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm6,%%ymm3		\n\t		vfmsub132pd	%%ymm10,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm4,%%ymm0		\n\t		vfmsub132pd	%%ymm10,%%ymm14,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm10,%%ymm5,%%ymm1		\n\t		vfmsub132pd	(%%rsi),%%ymm15,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm15,%%ymm11		\n\t"\
		"														vfmsub132pd	(%%r8),%%ymm10,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r30,r32,r34,r36,r38,r3A,r3C,r3E): */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm13,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm12,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
		/*...Block 1: r00,r10,r20,r30	*/						/*...Block 5: r08,r18,r28,r38	*/\
		"movq	%[__isrt2],%%rdi					\n\t		vmovaps	(%%rdi),%%ymm10	\n\t"/* isrt2 */\
		"movq		%[__r00]	,%%rax				\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x200(%%rax),%%rbx				\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x400(%%rax),%%rcx				\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx				\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm0,%%ymm0			\n\t		vaddpd	0x20(%%r12),%%ymm12,%%ymm12		\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm1,%%ymm1			\n\t		vsubpd	    (%%r12),%%ymm13,%%ymm13		\n\t"\
		"vaddpd	    (%%rax),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r13),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd	0x20(%%rax),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r13),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmulpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmulpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	    (%%rdx),%%ymm6				\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm7				\n\t"\
		"vsubpd	    (%%rdx),%%ymm4,%%ymm4			\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm5,%%ymm5			\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vaddpd	    (%%rcx),%%ymm6,%%ymm6			\n\t	vfnmadd231pd	%%ymm10,%%ymm8 ,%%ymm12		\n\t"\
		"vaddpd	0x20(%%rcx),%%ymm7,%%ymm7			\n\t	vfnmadd231pd	%%ymm10,%%ymm9 ,%%ymm13		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm6,%%ymm2		\n\t	 vfmadd231pd	%%ymm10,%%ymm8 ,%%ymm14		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm7,%%ymm3		\n\t	 vfmadd231pd	%%ymm10,%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
	"vfmadd132pd	(%%r9 ),%%ymm2,%%ymm6			\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
	"vfmadd132pd	(%%r9 ),%%ymm3,%%ymm7			\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"											\n\t		vsubpd	0x20(%%r11),%%ymm8 ,%%ymm8 		\n\t"\
		"											\n\t		vsubpd	    (%%r11),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vaddpd	    (%%r10),%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vaddpd	0x20(%%r10),%%ymm10,%%ymm10		\n\t"\
		"vfmadd132pd	(%%r8),%%ymm5,%%ymm0		\n\t		vfmsub132pd	(%%r8),%%ymm12,%%ymm11		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm4,%%ymm1		\n\t		vfmsub132pd	(%%r8),%%ymm13,%%ymm9 		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)				\n\t		vfmsub132pd	(%%r8),%%ymm15,%%ymm8 		\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vfmsub132pd	(%%r8),%%ymm14,%%ymm10		\n\t"\
		"vfmadd132pd	(%%r8),%%ymm5,%%ymm5		\n\t		vmovaps	%%ymm11,    (%%r12)	\n\t"\
		"vfmadd132pd	(%%r8),%%ymm4,%%ymm4		\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"vfmsub132pd	(%%r8),%%ymm5,%%ymm0		\n\t		vmovaps	%%ymm8 ,    (%%r13)	\n\t"\
		"vfmadd132pd	(%%r8),%%ymm4,%%ymm1		\n\t		vmovaps	%%ymm10,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t	vfmadd132pd		(%%r9 ),%%ymm11,%%ymm12		\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)				\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm13		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm15		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm10,%%ymm14		\n\t"\
		"														vmovaps	%%ymm12,    (%%r10)	\n\t"\
		"														vmovaps	%%ymm13,0x20(%%r10)	\n\t"\
		"														vmovaps	%%ymm15,    (%%r11)	\n\t"\
		"														vmovaps	%%ymm14,0x20(%%r13)	\n\t"\
		"leaq	0x20(%%rdi),%%rsi	\n\t"/* cc0 */\
		/*...Block 3: r04,r14,r24,r34	*/						/*...Block 7: r0C,r1C,r2C,r3C	*/\
		"addq	$0x80,%%rax 				\n\t"/* r04 */"		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx							\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx							\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx							\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	    (%%rsi),%%ymm3				\n\t		vmovaps	0x20(%%rsi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd		    %%ymm3 ,%%ymm5,%%ymm5		\n\t		vmulpd		    %%ymm11,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		    %%ymm11,%%ymm1,%%ymm1		\n\t		vmulpd		    %%ymm3 ,%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		    %%ymm3 ,%%ymm4,%%ymm4		\n\t		vmulpd		    %%ymm11,%%ymm12,%%ymm12		\n\t"\
		"vmulpd		    %%ymm11,%%ymm0,%%ymm0		\n\t		vmulpd		    %%ymm3 ,%%ymm8 ,%%ymm8 		\n\t"\
	"vfnmadd231pd	    %%ymm11,%%ymm6,%%ymm5		\n\t	vfnmadd231pd	    %%ymm3 ,%%ymm14,%%ymm13		\n\t"\
	"vfnmadd231pd	    %%ymm3 ,%%ymm2,%%ymm1		\n\t	vfnmadd231pd	    %%ymm11,%%ymm10,%%ymm9 		\n\t"\
	" vfmadd231pd	    %%ymm11,%%ymm7,%%ymm4		\n\t	 vfmadd231pd	    %%ymm3 ,%%ymm15,%%ymm12		\n\t"\
	" vfmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	 vfmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 		\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
	"vmovaps	%%ymm15,(%%rcx) 	\n\t"/* spill ymm15 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm15 	\n\t"/* one */\
		"vfmadd132pd	%%ymm15,%%ymm0,%%ymm4		\n\t		vfmadd132pd	%%ymm15,%%ymm8 ,%%ymm12		\n\t"\
		"vfmadd132pd	%%ymm15,%%ymm1,%%ymm5		\n\t		vfmadd132pd	%%ymm15,%%ymm9 ,%%ymm13		\n\t"\
		"vfmsub132pd	%%ymm15,%%ymm0,%%ymm6		\n\t		vfmsub132pd	%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
		"vfmsub132pd	%%ymm15,%%ymm1,%%ymm7		\n\t		vfmsub132pd	(%%rcx),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r11),%%ymm10,%%ymm10		\n\t"\
		"vsubpd	    (%%rbx),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r11),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2			\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3			\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0		\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1		\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
	"vmovaps	%%ymm9 ,(%%rcx) 	\n\t"/* spill ymm9  to make room for 1.0 */"	vmovaps	(%%r8),%%ymm9  	\n\t"/* one */\
		"vfmsub132pd	%%ymm9 ,%%ymm7,%%ymm0		\n\t		vfmsub132pd	%%ymm9 ,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm6,%%ymm1		\n\t		vfmsub132pd	%%ymm9 ,%%ymm12,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm4,%%ymm2		\n\t		vfmsub132pd	%%ymm9 ,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm5,%%ymm3		\n\t		vfmsub132pd	(%%rcx),%%ymm15,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm7			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm5			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm7,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm6,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"vmovaps	%%ymm4,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm5,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"\n\t"\
		/*...Block 2: r02,r12,r22,r32	*/						/*...Block 6: r0A,r1A,r2A,r3A	*/\
		"subq	$0x40,%%rax 			\n\t"/* r02 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"subq	$0x40,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"subq	$0x40,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x60,%%rdi 			\n\t"/* cc1 */"			addq	$0x80,%%rsi \n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0				\n\t"/*		vmovaps	    (%%r13),%%ymm8 	\n\t"*/\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t"/*		vmovaps	0x20(%%r12),%%ymm15	\n\t"*/\
		"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	    (%%rdi),%%ymm8				\n\t		vmovaps	    (%%rsi),%%ymm15	\n\t"/*	Instead use ymm8,15 for sincos: */\
		"vmovaps	0x20(%%rsi),%%ymm3				\n\t		vmovaps	0x20(%%rdi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd			%%ymm8 ,%%ymm5,%%ymm5		\n\t		vmulpd			%%ymm3 ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd			%%ymm15,%%ymm1,%%ymm1		\n\t		vmulpd			%%ymm8 ,%%ymm9 ,%%ymm9 	\n\t"\
		"vmulpd			%%ymm8 ,%%ymm4,%%ymm4		\n\t		vmulpd			%%ymm3 ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd			%%ymm15,%%ymm0,%%ymm0		\n\t		vmulpd		    (%%r13),%%ymm8 ,%%ymm8 	\n\t"\
	"vfnmadd231pd		%%ymm11,%%ymm6,%%ymm5		\n\t	vfnmadd231pd		%%ymm15,%%ymm14,%%ymm13	\n\t"\
	"vfnmadd231pd		%%ymm3 ,%%ymm2,%%ymm1		\n\t	 vfmadd231pd		%%ymm11,%%ymm10,%%ymm9 	\n\t"\
	" vfmadd231pd		%%ymm11,%%ymm7,%%ymm4		\n\t	 vfmadd231pd	0x20(%%r12),%%ymm15,%%ymm12	\n\t"\
	" vfmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	vfnmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 	\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
	"vmovaps	%%ymm15,(%%rcx) 	\n\t"/* spill ymm15 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm15 	\n\t"/* one */\
		"vfmsub132pd	%%ymm15,%%ymm0,%%ymm4		\n\t		vfmadd132pd	%%ymm15,%%ymm8 ,%%ymm12		\n\t"\
		"vfmsub132pd	%%ymm15,%%ymm1,%%ymm5		\n\t		vfmadd132pd	%%ymm15,%%ymm9 ,%%ymm13		\n\t"\
		"vfmadd132pd	%%ymm15,%%ymm0,%%ymm6		\n\t		vfmsub132pd	%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
		"vfmadd132pd	%%ymm15,%%ymm1,%%ymm7		\n\t		vfmsub132pd	(%%rcx),%%ymm9 ,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi 						\n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd			(%%rsi),%%ymm2,%%ymm2		\n\t		vmulpd		0x20(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vmulpd			(%%rsi),%%ymm3,%%ymm3		\n\t		vmulpd		0x20(%%rsi),%%ymm11,%%ymm11	\n\t"\
	" vfmadd231pd	0x20(%%rsi),%%ymm1,%%ymm2		\n\t	vfnmadd231pd	    (%%rsi),%%ymm9 ,%%ymm10	\n\t"\
	"vfnmadd231pd	0x20(%%rsi),%%ymm0,%%ymm3		\n\t	 vfmadd231pd	    (%%rsi),%%ymm8 ,%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0		\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1		\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
	"vmovaps	%%ymm9 ,(%%rcx) 	\n\t"/* spill ymm9  to make room for 1.0 */"	vmovaps	(%%r8),%%ymm9  	\n\t"/* one */\
		"vfmsub132pd	%%ymm9 ,%%ymm5,%%ymm0		\n\t		vfmsub132pd	%%ymm9 ,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm4,%%ymm1		\n\t		vfmsub132pd	%%ymm9 ,%%ymm12,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm6,%%ymm2		\n\t		vfmsub132pd	%%ymm9 ,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm7,%%ymm3		\n\t		vfmsub132pd	(%%rcx),%%ymm15,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm5			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm7			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"vmovaps	%%ymm5,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm4,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"\n\t"\
		/*...Block 4: r06,r16,r26,r36	*/						/*...Block 8: r0E,r1E,r2E,r3E	*/\
		"addq	$0x80,%%rax 			\n\t"/* r06 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x80,%%rsi				\n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
	/*	"vmovaps	    (%%rdx),%%ymm0		*/"		\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7		*/"		\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	0x20(%%rsi),%%ymm7				\n\t		vmovaps	0x20(%%rdi),%%ymm0 	\n\t"/*	Instead use ymm7,0  for sincos: */\
		"vmovaps	    (%%rdi),%%ymm3				\n\t		vmovaps	    (%%rsi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd			%%ymm11,%%ymm5,%%ymm5		\n\t		vmulpd			%%ymm0 ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd			%%ymm0 ,%%ymm1,%%ymm1		\n\t		vmulpd			%%ymm7 ,%%ymm9 ,%%ymm9 	\n\t"\
		"vmulpd			%%ymm11,%%ymm4,%%ymm4		\n\t		vmulpd			%%ymm0 ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd		    (%%rdx),%%ymm0,%%ymm0		\n\t		vmulpd			%%ymm7 ,%%ymm8 ,%%ymm8 	\n\t"\
	"vfnmadd231pd		%%ymm7 ,%%ymm6,%%ymm5		\n\t	vfnmadd231pd		%%ymm3 ,%%ymm14,%%ymm13	\n\t"\
	" vfmadd231pd		%%ymm3 ,%%ymm2,%%ymm1		\n\t	vfnmadd231pd		%%ymm11,%%ymm10,%%ymm9 	\n\t"\
	" vfmadd231pd	0x20(%%rcx),%%ymm7,%%ymm4		\n\t	 vfmadd231pd		%%ymm3 ,%%ymm15,%%ymm12	\n\t"\
	"vfnmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	 vfmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 	\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
	"vmovaps	%%ymm15,(%%rcx) 	\n\t"/* spill ymm15 to make room for 1.0 */"	vmovaps	(%%r8),%%ymm15 	\n\t"/* one */\
		"vfmadd132pd	%%ymm15,%%ymm0,%%ymm4		\n\t		vfmadd132pd	%%ymm15,%%ymm8 ,%%ymm12		\n\t"\
		"vfmadd132pd	%%ymm15,%%ymm1,%%ymm5		\n\t		vfmadd132pd	%%ymm15,%%ymm9 ,%%ymm13		\n\t"\
		"vfmsub132pd	%%ymm15,%%ymm0,%%ymm6		\n\t		vfmsub132pd	%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
		"vfmsub132pd	%%ymm15,%%ymm1,%%ymm7		\n\t		vfmsub132pd	(%%rcx),%%ymm9 ,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi \n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd		0x20(%%rsi),%%ymm2,%%ymm2		\n\t		vmulpd			(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vmulpd		0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vmulpd			(%%rsi),%%ymm11,%%ymm11	\n\t"\
	" vfmadd231pd	    (%%rsi),%%ymm1,%%ymm2		\n\t	vfnmadd231pd	0x20(%%rsi),%%ymm9 ,%%ymm10	\n\t"\
	"vfnmadd231pd	    (%%rsi),%%ymm0,%%ymm3		\n\t	 vfmadd231pd	0x20(%%rsi),%%ymm8 ,%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0		\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1		\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
	"vmovaps	%%ymm9 ,(%%rcx) 	\n\t"/* spill ymm9  to make room for 1.0 */"	vmovaps	(%%r8),%%ymm9  	\n\t"/* one */\
		"vfmsub132pd	%%ymm9 ,%%ymm5,%%ymm0		\n\t		vfmsub132pd	%%ymm9 ,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm4,%%ymm1		\n\t		vfmsub132pd	%%ymm9 ,%%ymm12,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm6,%%ymm2		\n\t		vfmsub132pd	%%ymm9 ,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm7,%%ymm3		\n\t		vfmsub132pd	(%%rcx),%%ymm15,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm5			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm7			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"vmovaps	%%ymm5,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm4,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r8","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

	// Aggressive-FMA: replace [18 ADD, 160 SUB, 198 FMA, 48 MUL, 435 memref] ==> [14 ADD, 6 SUB, 356 FMA (198 nontrivial), 48 MUL, 525 memref].
	// I.e. trade ~150 ADD/SUB for same #FMA + 90 LOAD-of-vector-const-1.0, those where spill of a register datum in favor of 1.0 not justifiable.
	#define SSE2_RADIX32_DIF_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
		"movq	%[__isrt2],%%rdi \n\t leaq -0x40(%%rdi),%%r8 \n\t leaq -0x60(%%rdi),%%r9	\n\t"/* r8,r9 hold 1.0,2.0 throughout */\
	/*...Block 1: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r00,r20,r10,r30): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r08,r28,r18,r38): */\
		"movq		%[__r00]	,%%rax			\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x400(%%rax),%%rbx			\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x200(%%rax),%%rcx			\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx			\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps		(%%r8 ),%%ymm13	\n\t"/* Use ymm13 for 1.0 instead */\
		"vfmsub132pd	%%ymm13,%%ymm0,%%ymm2	\n\t		vfmsub132pd	%%ymm13,%%ymm8 ,%%ymm10	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm1,%%ymm3	\n\t		vfmsub132pd	%%ymm13,%%ymm9 ,%%ymm11	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm4,%%ymm6	\n\t		vfmsub132pd	%%ymm13,%%ymm14,%%ymm12	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm5,%%ymm7	\n\t		vfmsub132pd	0x20(%%r12),%%ymm15,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm1	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm3	\n\t		vfmsub132pd	(%%rax),%%ymm12,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm12,%%ymm10		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm11,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r00,r10,r20,r30,r08,r18,r28,r38): */\
		/* Combine r00,r08,r20,r28: */					/* Combine r10,r18,r30,r38: */\
		"vfmsub132pd	(%%r8),%%ymm14,%%ymm4 	\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 	\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm15,%%ymm5 	\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 	\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 2: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r04,r24,r14,r34): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0C,r2C,r1C,r3C): */\
		"addq	$0x80,%%rax		\n\t"/* r04 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx		\n\t"/* r24 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2C */\
		"addq	$0x80,%%rcx		\n\t"/* r14 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1C */\
		"addq	$0x80,%%rdx		\n\t"/* r34 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3C */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps		(%%r8 ),%%ymm13	\n\t"/* Use ymm13 for 1.0 instead */\
		"vfmsub132pd	%%ymm13,%%ymm0,%%ymm2	\n\t		vfmsub132pd	%%ymm13,%%ymm8 ,%%ymm10	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm1,%%ymm3	\n\t		vfmsub132pd	%%ymm13,%%ymm9 ,%%ymm11	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm4,%%ymm6	\n\t		vfmsub132pd	%%ymm13,%%ymm14,%%ymm12	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm5,%%ymm7	\n\t		vfmsub132pd	0x20(%%r12),%%ymm15,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm1	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm3	\n\t		vfmsub132pd	(%%rax),%%ymm12,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm12,%%ymm10		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm11,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vfmsub132pd	(%%r8),%%ymm14,%%ymm4 	\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 	\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm15,%%ymm5 	\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 	\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 3: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r02,r22,r12,r32): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0A,r2A,r1A,r3A): */\
		"subq	$0x40,%%rax		\n\t"/* r02 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx		\n\t"/* r22 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2A */\
		"subq	$0x40,%%rcx		\n\t"/* r12 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1A */\
		"subq	$0x40,%%rdx		\n\t"/* r32 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3A */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps		(%%r8 ),%%ymm13	\n\t"/* Use ymm13 for 1.0 instead */\
		"vfmsub132pd	%%ymm13,%%ymm0,%%ymm2	\n\t		vfmsub132pd	%%ymm13,%%ymm8 ,%%ymm10	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm1,%%ymm3	\n\t		vfmsub132pd	%%ymm13,%%ymm9 ,%%ymm11	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm4,%%ymm6	\n\t		vfmsub132pd	%%ymm13,%%ymm14,%%ymm12	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm5,%%ymm7	\n\t		vfmsub132pd	0x20(%%r12),%%ymm15,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm1	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm3	\n\t		vfmsub132pd	(%%rax),%%ymm12,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm12,%%ymm10		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm11,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vfmsub132pd	(%%r8),%%ymm14,%%ymm4 	\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 	\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm15,%%ymm5 	\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 	\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 4: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r06,r26,r16,r36): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0E,r2E,r1E,r3E): */\
		"addq	$0x80,%%rax		\n\t"/* r06 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx		\n\t"/* r26 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2E */\
		"addq	$0x80,%%rcx		\n\t"/* r16 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1E */\
		"addq	$0x80,%%rdx		\n\t"/* r36 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3E */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps		(%%r8 ),%%ymm13	\n\t"/* Use ymm13 for 1.0 instead */\
		"vfmsub132pd	%%ymm13,%%ymm0,%%ymm2	\n\t		vfmsub132pd	%%ymm13,%%ymm8 ,%%ymm10	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm1,%%ymm3	\n\t		vfmsub132pd	%%ymm13,%%ymm9 ,%%ymm11	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm4,%%ymm6	\n\t		vfmsub132pd	%%ymm13,%%ymm14,%%ymm12	\n\t"\
		"vfmsub132pd	%%ymm13,%%ymm5,%%ymm7	\n\t		vfmsub132pd	0x20(%%r12),%%ymm15,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm14,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm1	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm3	\n\t		vfmsub132pd	(%%rax),%%ymm12,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm12,%%ymm10		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm11,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vfmsub132pd	(%%r8),%%ymm14,%%ymm4 	\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vfmsub132pd	(%%r8),%%ymm9 ,%%ymm0 	\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm15,%%ymm5 	\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm8 ,%%ymm1 	\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
	/*...Block 1: t00,t10,t20,t30 in r00,04,02,06 */		/*...Block 5: t08,t18,t28,t38	*/\
		"movq	%[__r00],%%rsi					\n\t"\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x0(%%rdi),%%rax	/* p00 */	\n\t		movslq	0x10(%%rdi),%%r10	/* p04 */	\n\t"\
		"movslq	0x4(%%rdi),%%rbx	/* p01 */	\n\t		movslq	0x14(%%rdi),%%r11	/* p05 */	\n\t"\
		"movslq	0x8(%%rdi),%%rcx	/* p02 */	\n\t		movslq	0x18(%%rdi),%%r12	/* p06 */	\n\t"\
		"movslq	0xc(%%rdi),%%rdx	/* p03 */	\n\t		movslq	0x1c(%%rdi),%%r13	/* p07 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"													movq	%[__isrt2],%%rdi				\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t"/*		vmovaps	0x1a0(%%rsi),%%ymm11	Instead use ymm11 for 2.0: */"	vmovaps	(%%r9),%%ymm11 	\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0	\n\t		vsubpd		 %%ymm13,%%ymm12,%%ymm12		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm6,%%ymm4	\n\t		vsubpd		 %%ymm14,%%ymm15,%%ymm15		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1	\n\t		vsubpd		 %%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm7,%%ymm5	\n\t		vsubpd	0x1a0(%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm0,%%ymm2		\n\t	vfmadd132pd		 %%ymm11,%%ymm12,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm4,%%ymm6		\n\t	vfmadd132pd		 %%ymm11,%%ymm15,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm1,%%ymm3		\n\t	vfmadd132pd		 %%ymm11,%%ymm9 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm5,%%ymm7		\n\t	vfmadd132pd	0x1a0(%%rsi),%%ymm8 ,%%ymm11		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm15,%%ymm15		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm14,%%ymm12		\n\t"\
		"													vfmsub132pd	(%%r8),%%ymm15,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
		"												vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
	"vmovaps	%%ymm9 ,(%%rax) 	\n\t"/* spill ymm9  to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm9 	\n\t"/* one */\
		"vfmsub132pd	%%ymm9 ,%%ymm6,%%ymm2	\n\t		vfmsub132pd	%%ymm9 ,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm5,%%ymm0	\n\t		vfmsub132pd	%%ymm9 ,%%ymm13,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm7,%%ymm3	\n\t		vfmsub132pd	%%ymm9 ,%%ymm15,%%ymm11		\n\t"\
		"vfmsub132pd	%%ymm9 ,%%ymm4,%%ymm1	\n\t		vfmsub132pd	(%%rax),%%ymm14,%%ymm9 		\n\t"\
	"vmovaps	%%ymm14,(%%rax) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm14	\n\t"/* two */\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)			\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,0x20(%%r11)			\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r12)			\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r13)			\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm2,%%ymm6	\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm0,%%ymm5	\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm3,%%ymm7	\n\t	vfmadd132pd		%%ymm14,%%ymm11,%%ymm15		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm1,%%ymm4	\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)			\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm13,0x20(%%r10)			\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm15,    (%%r13)			\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)			\n\t"\
		/*...Block 3: t04,t14,t24,t34	*/					/*...Block 7: t0C,t1C,t2C,t3C	*/\
		"addq	$0x400,%%rsi		/* r20 */	\n\t"		/* r28 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x20(%%rdi),%%rax	/* p08 */	\n\t		movslq	0x30(%%rdi),%%r10	/* p0c */	\n\t"\
		"movslq	0x24(%%rdi),%%rbx	/* p09 */	\n\t		movslq	0x34(%%rdi),%%r11	/* p0d */	\n\t"\
		"movslq	0x28(%%rdi),%%rcx	/* p0a */	\n\t		movslq	0x38(%%rdi),%%r12	/* p0e */	\n\t"\
		"movslq	0x2c(%%rdi),%%rdx	/* p0b */	\n\t		movslq	0x3c(%%rdi),%%r13	/* p0f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x20,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	    (%%rdi),%%ymm2			\n\t		vmovaps	0x20(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmulpd			%%ymm2 ,%%ymm4,%%ymm4	\n\t		vmulpd			%%ymm10 ,%%ymm12,%%ymm12		\n\t"\
		"vmulpd			%%ymm10,%%ymm6,%%ymm6	\n\t		vmulpd			%%ymm2  ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd			%%ymm2 ,%%ymm5,%%ymm5	\n\t		vmulpd			%%ymm10 ,%%ymm13,%%ymm13		\n\t"\
		"vmulpd			%%ymm10,%%ymm7,%%ymm7	\n\t		vmulpd			%%ymm2  ,%%ymm15,%%ymm15		\n\t"\
	"vfnmadd231pd		%%ymm10,%%ymm1,%%ymm4	\n\t	vfnmadd231pd		%%ymm2  ,%%ymm9 ,%%ymm12		\n\t"\
	"vfnmadd231pd		%%ymm2 ,%%ymm3,%%ymm6	\n\t	vfnmadd231pd		%%ymm10 ,%%ymm11,%%ymm14		\n\t"\
	" vfmadd231pd		%%ymm10,%%ymm0,%%ymm5	\n\t	 vfmadd231pd		%%ymm2  ,%%ymm8 ,%%ymm13		\n\t"\
	" vfmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7	\n\t	 vfmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15		\n\t"\
		"subq	$0x20,%%rdi						\n\t"/* isrt2 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x1a0(%%rsi),%%ymm11			\n\t"\
		"vfmsub132pd	(%%r8),%%ymm6,%%ymm4	\n\t		vfmsub132pd	(%%r8),%%ymm14,%%ymm12		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm7,%%ymm5	\n\t		vfmsub132pd	(%%r8),%%ymm15,%%ymm13		\n\t"\
		"vsubpd	0xa0(%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x1a0(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x80(%%rsi),%%ymm3,%%ymm3		\n\t		vsubpd	0x180(%%rsi),%%ymm11,%%ymm11	\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0	\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1	\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm3	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm1	\n\t		vfmsub132pd	(%%rax),%%ymm14,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 2: t02,t12,t22,t32	*/					/*...Block 6: t0A,t1A,t2A,t3A	*/\
		"subq	$0x200,%%rsi		/* r10 */	\n\t"		/* r18 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x40(%%rdi),%%rax	/* p10 */	\n\t		movslq	0x50(%%rdi),%%r10	/* p14 */	\n\t"\
		"movslq	0x44(%%rdi),%%rbx	/* p11 */	\n\t		movslq	0x54(%%rdi),%%r11	/* p15 */	\n\t"\
		"movslq	0x48(%%rdi),%%rcx	/* p12 */	\n\t		movslq	0x58(%%rdi),%%r12	/* p16 */	\n\t"\
		"movslq	0x4c(%%rdi),%%rdx	/* p13 */	\n\t		movslq	0x5c(%%rdi),%%r13	/* p17 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x60,%%rdi						\n\t"/* cc1 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t"/*		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"*/\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t"/*		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"*/\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmovaps	0x60(%%rdi),%%ymm2			\n\t		vmovaps	0x20(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x40(%%rdi),%%ymm8			\n\t		vmovaps		(%%rdi),%%ymm15	\n\t"/*	Instead use ymm8,15 for sincos: */\
		"vmulpd			%%ymm15,%%ymm4,%%ymm4	\n\t		vmulpd		%%ymm2  ,%%ymm12,%%ymm12		\n\t"\
		"vmulpd			%%ymm8 ,%%ymm6,%%ymm6	\n\t		vmulpd		%%ymm15 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd			%%ymm15,%%ymm5,%%ymm5	\n\t		vmulpd		%%ymm2  ,%%ymm13,%%ymm13		\n\t"\
		"vmulpd			%%ymm8 ,%%ymm7,%%ymm7	\n\t		vmulpd	0x1e0(%%rsi),%%ymm15,%%ymm15		\n\t"\
	"vfnmadd231pd		%%ymm10,%%ymm1,%%ymm4	\n\t	vfnmadd231pd		%%ymm8  ,%%ymm9 ,%%ymm12	\n\t"\
	"vfnmadd231pd		%%ymm2 ,%%ymm3,%%ymm6	\n\t	 vfmadd231pd		%%ymm10 ,%%ymm11,%%ymm14	\n\t"\
	" vfmadd231pd		%%ymm10,%%ymm0,%%ymm5	\n\t	 vfmadd231pd	0x140(%%rsi),%%ymm8 ,%%ymm13	\n\t"\
	" vfmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7	\n\t	vfnmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15	\n\t"\
		"subq	$0x40,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vfmsub132pd	(%%r8),%%ymm6,%%ymm4	\n\t		vfmsub132pd	(%%r8),%%ymm14,%%ymm12		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm7,%%ymm5	\n\t		vfmsub132pd	(%%r8),%%ymm15,%%ymm13		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x20(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	    (%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x20(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	    (%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm0,%%ymm2	\n\t		vfmadd132pd	(%%r8),%%ymm8 ,%%ymm10		\n\t"\
		"vfmadd132pd	(%%r8),%%ymm1,%%ymm3	\n\t		vfmsub132pd	(%%r8),%%ymm9 ,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0	\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1	\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm3	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm1	\n\t		vfmsub132pd	(%%rax),%%ymm14,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 4: t06,t16,t26,t36	*/					/*...Block 8: t0E,t1E,t2E,t3E	*/\
		"addq	$0x400,%%rsi		/* r30 */	\n\t"		/* r38 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x60(%%rdi),%%rax	/* p18 */	\n\t		movslq	0x70(%%rdi),%%r10	/* p1c */	\n\t"\
		"movslq	0x64(%%rdi),%%rbx	/* p19 */	\n\t		movslq	0x74(%%rdi),%%r11	/* p1d */	\n\t"\
		"movslq	0x68(%%rdi),%%rcx	/* p1a */	\n\t		movslq	0x78(%%rdi),%%r12	/* p1e */	\n\t"\
		"movslq	0x6c(%%rdi),%%rdx	/* p1b */	\n\t		movslq	0x7c(%%rdi),%%r13	/* p1f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x60,%%rdi						\n\t"/* cc1 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
	/*	"vmovaps	0xe0(%%rsi),%%ymm7		*/"	\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
	/*	"vmovaps	0x40(%%rsi),%%ymm0		*/"	\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmovaps		(%%rdi),%%ymm2			\n\t		vmovaps	0x40(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x60(%%rdi),%%ymm0			\n\t		vmovaps	0x20(%%rdi),%%ymm7 	\n\t"/*	Instead use ymm0,7  for sincos: */\
		"vmulpd			%%ymm10,%%ymm4,%%ymm4	\n\t		vmulpd			%%ymm7  ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd			%%ymm7 ,%%ymm6,%%ymm6	\n\t		vmulpd			%%ymm0  ,%%ymm14,%%ymm14	\n\t"\
		"vmulpd			%%ymm10,%%ymm5,%%ymm5	\n\t		vmulpd			%%ymm7  ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd		0xe0(%%rsi),%%ymm7,%%ymm7	\n\t		vmulpd			%%ymm0  ,%%ymm15,%%ymm15	\n\t"\
	"vfnmadd231pd		%%ymm0 ,%%ymm1,%%ymm4	\n\t	vfnmadd231pd		%%ymm2  ,%%ymm9 ,%%ymm12	\n\t"\
	" vfmadd231pd		%%ymm2 ,%%ymm3,%%ymm6	\n\t	vfnmadd231pd		%%ymm10 ,%%ymm11,%%ymm14	\n\t"\
	" vfmadd231pd	0x40(%%rsi),%%ymm0,%%ymm5	\n\t	 vfmadd231pd		%%ymm2  ,%%ymm8 ,%%ymm13	\n\t"\
	"vfnmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7	\n\t	 vfmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15	\n\t"\
		"subq	$0x40,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vfmsub132pd	(%%r8),%%ymm6,%%ymm4	\n\t		vfmsub132pd	(%%r8),%%ymm14,%%ymm12		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm7,%%ymm5	\n\t		vfmsub132pd	(%%r8),%%ymm15,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	0x20(%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	0x20(%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm0,%%ymm2	\n\t		vfmadd132pd	(%%r8),%%ymm8 ,%%ymm10		\n\t"\
		"vfmadd132pd	(%%r8),%%ymm1,%%ymm3	\n\t		vfmsub132pd	(%%r8),%%ymm9 ,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vfmsub132pd	(%%r8),%%ymm2,%%ymm0	\n\t		vfmsub132pd	(%%r8),%%ymm10,%%ymm8 		\n\t"\
		"vfmsub132pd	(%%r8),%%ymm3,%%ymm1	\n\t		vfmsub132pd	(%%r8),%%ymm11,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
	"vmovaps	%%ymm11,(%%rax) 	\n\t"/* spill ymm11 to make room for 1.0 */"	vmovaps	 (%%r8),%%ymm11	\n\t"/* one */\
		"vfmsub132pd	%%ymm11,%%ymm4,%%ymm2	\n\t		vfmsub132pd	%%ymm11,%%ymm12,%%ymm8 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm7,%%ymm0	\n\t		vfmsub132pd	%%ymm11,%%ymm15,%%ymm10		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm5,%%ymm3	\n\t		vfmsub132pd	%%ymm11,%%ymm13,%%ymm9 		\n\t"\
		"vfmsub132pd	%%ymm11,%%ymm6,%%ymm1	\n\t		vfmsub132pd	(%%rax),%%ymm14,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm4,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm7,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm5,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm6,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r8","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

  #else	// ALL_FMA = False: All FMAs have non-unity multiplicands:

	// FMA version: replace [240 ADD, 188 SUB, 184 MUL, 485 memref] ==> [26 ADD, 154 SUB, 198 FMA (198 nontrivial), 38 MUL, 450 memref].
	//
	#define SSE2_RADIX32_DIT_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
	/*...Block 1: */\
		"movq	%[__isrt2],%%rdi	\n\t	leaq -0x60(%%rdi),%%r9	\n\t"/* r9 holds 2.0 throughout */\
		"movq	%[__r00],%%rsi	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r00)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r08): */\
		/* Can't simply addq these to a 64-bit base array address since the offsets are 4-byte array data: */\
		"movq	%[__arr_offsets],%%rdi				\n\t"\
		"movslq	0x00(%%rdi),%%rax		\n\t"/* p00 */"			movslq	0x10(%%rdi),%%r10	\n\t"/* p04 */\
		"movslq	0x04(%%rdi),%%rbx		\n\t"/* p01 */"			movslq	0x14(%%rdi),%%r11	\n\t"/* p05 */\
		"movslq	0x08(%%rdi),%%rcx		\n\t"/* p02 */"			movslq	0x18(%%rdi),%%r12	\n\t"/* p06 */\
		"movslq	0x0c(%%rdi),%%rdx		\n\t"/* p03 */"			movslq	0x1c(%%rdi),%%r13	\n\t"/* p07 */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t"	/*	vmovaps	0x20(%%r13),%%ymm13	Instead use ymm13 for 2.0: */"	vmovaps	(%%r9),%%ymm13 	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r00,r02,r04,r06,r08,r0A,r0C,r0E): */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 2: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r10)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x20(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x30(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x24(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x34(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x28(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x38(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x2c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x3c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t"	/*	vmovaps	0x20(%%r13),%%ymm13	Instead use ymm13 for 2.0: */"	vmovaps	(%%r9),%%ymm13 	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r10,r12,r14,r16,r18,r1A,r1C,r1E): */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 3: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r20)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r28): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x40(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x50(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x44(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x54(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x48(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x58(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x4c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x5c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t"	/*	vmovaps	0x20(%%r13),%%ymm13	Instead use ymm13 for 2.0: */"	vmovaps	(%%r9),%%ymm13 	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r20,r22,r24,r26,r28,r2A,r2C,r2E): */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 4: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r01)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x60(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x70(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x64(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x74(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x68(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x78(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x6c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x7c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"movq	%[__isrt2],%%rdi					\n\t"/* isrt2 */\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t"	/*	vmovaps	0x20(%%r13),%%ymm13	Instead use ymm13 for 2.0: */"	vmovaps	(%%r9),%%ymm13 	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd	0x20(%%r13),%%ymm15,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm2,%%ymm0			\n\t	vfmadd132pd		%%ymm13,%%ymm10,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm3,%%ymm1			\n\t	vfmadd132pd		%%ymm13,%%ymm11,%%ymm9 		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm6,%%ymm4			\n\t	vfmadd132pd		%%ymm13,%%ymm14,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm13,%%ymm7,%%ymm5			\n\t	vfmadd132pd	0x20(%%r13),%%ymm15,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
	"vmovaps	%%ymm14,(%%rsi) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm14 	\n\t"/* two */\
	"vfmadd132pd	%%ymm14,%%ymm0,%%ymm4			\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm1,%%ymm5			\n\t	vfmadd132pd		%%ymm14,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm2,%%ymm7			\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm14,%%ymm3,%%ymm6			\n\t	vfmadd132pd		(%%rsi),%%ymm11,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm11,%%ymm15		\n\t"/* .two */\
		"													vfmadd132pd		(%%r9 ),%%ymm14,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r30,r32,r34,r36,r38,r3A,r3C,r3E): */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t	vfnmadd231pd	(%%rdi),%%ymm14,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm3 		\n\t"\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t	vfnmadd231pd	(%%rdi),%%ymm15,%%ymm7 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm6 		\n\t"\
	"vmovaps	%%ymm8 ,0x20(%%rsi) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm10,(%%rsi) 	\n\t"/* spill ymm10 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm10 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm10,%%ymm2 ,%%ymm14		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm5 ,%%ymm13		\n\t	vfmadd132pd		%%ymm10,%%ymm3 ,%%ymm11		\n\t"\
	"vfmadd132pd		%%ymm8 ,%%ymm4 ,%%ymm12		\n\t	vfmadd132pd		%%ymm10,%%ymm7 ,%%ymm15		\n\t"\
	"vfmadd132pd	0x20(%%rsi),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rsi),%%ymm6 ,%%ymm10		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
		/*...Block 1: r00,r10,r20,r30	*/						/*...Block 5: r08,r18,r28,r38	*/\
		"movq	%[__isrt2],%%rdi					\n\t		vmovaps	(%%rdi),%%ymm10	\n\t"/* isrt2 */\
		"movq		%[__r00]	,%%rax				\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x200(%%rax),%%rbx				\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x400(%%rax),%%rcx				\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx				\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm0,%%ymm0			\n\t		vaddpd	0x20(%%r12),%%ymm12,%%ymm12		\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm1,%%ymm1			\n\t		vsubpd	    (%%r12),%%ymm13,%%ymm13		\n\t"\
		"vaddpd	    (%%rax),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r13),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd	0x20(%%rax),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r13),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmulpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmulpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	    (%%rdx),%%ymm6				\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm7				\n\t"\
		"vsubpd	    (%%rdx),%%ymm4,%%ymm4			\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm5,%%ymm5			\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vaddpd	    (%%rcx),%%ymm6,%%ymm6			\n\t	vfnmadd231pd	%%ymm10,%%ymm8 ,%%ymm12		\n\t"\
		"vaddpd	0x20(%%rcx),%%ymm7,%%ymm7			\n\t	vfnmadd231pd	%%ymm10,%%ymm9 ,%%ymm13		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2			\n\t	 vfmadd231pd	%%ymm10,%%ymm8 ,%%ymm14		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3			\n\t	 vfmadd231pd	%%ymm10,%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
	"vfmadd132pd	(%%r9 ),%%ymm2,%%ymm6			\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
	"vfmadd132pd	(%%r9 ),%%ymm3,%%ymm7			\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"											\n\t		vsubpd	0x20(%%r11),%%ymm8 ,%%ymm8 		\n\t"\
		"											\n\t		vsubpd	    (%%r11),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vaddpd	    (%%r10),%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vaddpd	0x20(%%r10),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)				\n\t		vsubpd		%%ymm15,%%ymm8 ,%%ymm8 		\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vsubpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm5,%%ymm5,%%ymm5			\n\t		vmovaps	%%ymm11,    (%%r12)	\n\t"\
		"vaddpd		%%ymm4,%%ymm4,%%ymm4			\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vmovaps	%%ymm8 ,    (%%r13)	\n\t"\
		"vaddpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vmovaps	%%ymm10,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t	vfmadd132pd		(%%r9 ),%%ymm11,%%ymm12		\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)				\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm13		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm15		\n\t"\
		"													vfmadd132pd		(%%r9 ),%%ymm10,%%ymm14		\n\t"\
		"														vmovaps	%%ymm12,    (%%r10)	\n\t"\
		"														vmovaps	%%ymm13,0x20(%%r10)	\n\t"\
		"														vmovaps	%%ymm15,    (%%r11)	\n\t"\
		"														vmovaps	%%ymm14,0x20(%%r13)	\n\t"\
		"leaq	0x20(%%rdi),%%rsi	\n\t"/* cc0 */\
		/*...Block 3: r04,r14,r24,r34	*/						/*...Block 7: r0C,r1C,r2C,r3C	*/\
		"addq	$0x80,%%rax 				\n\t"/* r04 */"		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx							\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx							\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx							\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	    (%%rsi),%%ymm3				\n\t		vmovaps	0x20(%%rsi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd		    %%ymm3 ,%%ymm5,%%ymm5		\n\t		vmulpd		    %%ymm11,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		    %%ymm11,%%ymm1,%%ymm1		\n\t		vmulpd		    %%ymm3 ,%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		    %%ymm3 ,%%ymm4,%%ymm4		\n\t		vmulpd		    %%ymm11,%%ymm12,%%ymm12		\n\t"\
		"vmulpd		    %%ymm11,%%ymm0,%%ymm0		\n\t		vmulpd		    %%ymm3 ,%%ymm8 ,%%ymm8 		\n\t"\
	"vfnmadd231pd	    %%ymm11,%%ymm6,%%ymm5		\n\t	vfnmadd231pd	    %%ymm3 ,%%ymm14,%%ymm13		\n\t"\
	"vfnmadd231pd	    %%ymm3 ,%%ymm2,%%ymm1		\n\t	vfnmadd231pd	    %%ymm11,%%ymm10,%%ymm9 		\n\t"\
	" vfmadd231pd	    %%ymm11,%%ymm7,%%ymm4		\n\t	 vfmadd231pd	    %%ymm3 ,%%ymm15,%%ymm12		\n\t"\
	" vfmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	 vfmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 		\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm0,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm1,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r11),%%ymm10,%%ymm10		\n\t"\
		"vsubpd	    (%%rbx),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r11),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2			\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3			\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
		"vsubpd		%%ymm7,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm7			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm5			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm7,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm6,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"vmovaps	%%ymm4,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm5,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"\n\t"\
		/*...Block 2: r02,r12,r22,r32	*/						/*...Block 6: r0A,r1A,r2A,r3A	*/\
		"subq	$0x40,%%rax 			\n\t"/* r02 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"subq	$0x40,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"subq	$0x40,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x60,%%rdi 			\n\t"/* cc1 */"			addq	$0x80,%%rsi \n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0				\n\t"/*		vmovaps	    (%%r13),%%ymm8 	\n\t"*/\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t"/*		vmovaps	0x20(%%r12),%%ymm15	\n\t"*/\
		"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	    (%%rdi),%%ymm8				\n\t		vmovaps	    (%%rsi),%%ymm15	\n\t"/*	Instead use ymm8,15 for sincos: */\
		"vmovaps	0x20(%%rsi),%%ymm3				\n\t		vmovaps	0x20(%%rdi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd			%%ymm8 ,%%ymm5,%%ymm5		\n\t		vmulpd			%%ymm3 ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd			%%ymm15,%%ymm1,%%ymm1		\n\t		vmulpd			%%ymm8 ,%%ymm9 ,%%ymm9 	\n\t"\
		"vmulpd			%%ymm8 ,%%ymm4,%%ymm4		\n\t		vmulpd			%%ymm3 ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd			%%ymm15,%%ymm0,%%ymm0		\n\t		vmulpd		    (%%r13),%%ymm8 ,%%ymm8 	\n\t"\
	"vfnmadd231pd		%%ymm11,%%ymm6,%%ymm5		\n\t	vfnmadd231pd		%%ymm15,%%ymm14,%%ymm13	\n\t"\
	"vfnmadd231pd		%%ymm3 ,%%ymm2,%%ymm1		\n\t	 vfmadd231pd		%%ymm11,%%ymm10,%%ymm9 	\n\t"\
	" vfmadd231pd		%%ymm11,%%ymm7,%%ymm4		\n\t	 vfmadd231pd	0x20(%%r12),%%ymm15,%%ymm12	\n\t"\
	" vfmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	vfnmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 	\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vsubpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm0,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm1,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi 						\n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd			(%%rsi),%%ymm2,%%ymm2		\n\t		vmulpd		0x20(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vmulpd			(%%rsi),%%ymm3,%%ymm3		\n\t		vmulpd		0x20(%%rsi),%%ymm11,%%ymm11	\n\t"\
	" vfmadd231pd	0x20(%%rsi),%%ymm1,%%ymm2		\n\t	vfnmadd231pd	    (%%rsi),%%ymm9 ,%%ymm10	\n\t"\
	"vfnmadd231pd	0x20(%%rsi),%%ymm0,%%ymm3		\n\t	 vfmadd231pd	    (%%rsi),%%ymm8 ,%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm5			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm7			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"vmovaps	%%ymm5,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm4,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"\n\t"\
		/*...Block 4: r06,r16,r26,r36	*/						/*...Block 8: r0E,r1E,r2E,r3E	*/\
		"addq	$0x80,%%rax 			\n\t"/* r06 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x80,%%rsi				\n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
	/*	"vmovaps	    (%%rdx),%%ymm0		*/"		\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
	/*	"vmovaps	0x20(%%rcx),%%ymm7		*/"		\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
	/*	"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"*/\
		"vmovaps	0x20(%%rsi),%%ymm7				\n\t		vmovaps	0x20(%%rdi),%%ymm0 	\n\t"/*	Instead use ymm7,0  for sincos: */\
		"vmovaps	    (%%rdi),%%ymm3				\n\t		vmovaps	    (%%rsi),%%ymm11	\n\t"/*	Instead use ymm3,11 for sincos: */\
		"vmulpd			%%ymm11,%%ymm5,%%ymm5		\n\t		vmulpd			%%ymm0 ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd			%%ymm0 ,%%ymm1,%%ymm1		\n\t		vmulpd			%%ymm7 ,%%ymm9 ,%%ymm9 	\n\t"\
		"vmulpd			%%ymm11,%%ymm4,%%ymm4		\n\t		vmulpd			%%ymm0 ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd		    (%%rdx),%%ymm0,%%ymm0		\n\t		vmulpd			%%ymm7 ,%%ymm8 ,%%ymm8 	\n\t"\
	"vfnmadd231pd		%%ymm7 ,%%ymm6,%%ymm5		\n\t	vfnmadd231pd		%%ymm3 ,%%ymm14,%%ymm13	\n\t"\
	" vfmadd231pd		%%ymm3 ,%%ymm2,%%ymm1		\n\t	vfnmadd231pd		%%ymm11,%%ymm10,%%ymm9 	\n\t"\
	" vfmadd231pd	0x20(%%rcx),%%ymm7,%%ymm4		\n\t	 vfmadd231pd		%%ymm3 ,%%ymm15,%%ymm12	\n\t"\
	"vfnmadd231pd	0x20(%%rdx),%%ymm3,%%ymm0		\n\t	 vfmadd231pd	0x20(%%r13),%%ymm11,%%ymm8 	\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm0,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm1,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi \n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd		0x20(%%rsi),%%ymm2,%%ymm2		\n\t		vmulpd			(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vmulpd		0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vmulpd			(%%rsi),%%ymm11,%%ymm11	\n\t"\
	" vfmadd231pd	    (%%rsi),%%ymm1,%%ymm2		\n\t	vfnmadd231pd	0x20(%%rsi),%%ymm9 ,%%ymm10	\n\t"\
	"vfnmadd231pd	    (%%rsi),%%ymm0,%%ymm3		\n\t	 vfmadd231pd	0x20(%%rsi),%%ymm8 ,%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm0,%%ymm2			\n\t	vfmadd132pd		(%%r9 ),%%ymm8 ,%%ymm10		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm1,%%ymm3			\n\t	vfmadd132pd		(%%r9 ),%%ymm9 ,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	(%%r9),%%ymm15 	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm0,%%ymm5			\n\t	vfmadd132pd		%%ymm15,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm1,%%ymm4			\n\t	vfmadd132pd		%%ymm15,%%ymm11,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm6			\n\t	vfmadd132pd		%%ymm15,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm7			\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm15		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)				\n\t		vmovaps	%%ymm14,    (%%r10)	\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)				\n\t		vmovaps	%%ymm15,0x20(%%r10)	\n\t"\
		"vmovaps	%%ymm5,    (%%rbx)				\n\t		vmovaps	%%ymm13,    (%%r11)	\n\t"\
		"vmovaps	%%ymm4,0x20(%%rdx)				\n\t		vmovaps	%%ymm12,0x20(%%r13)	\n\t"\
		"\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

	// FMA version: replace [188 ADD, 188 SUB, 206 MUL, 580 memref] ==> [18 ADD, 160 SUB, 198 FMA (198 nontrivial), 48 MUL, 435 memref].
	//
	#define SSE2_RADIX32_DIF_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
		"movq	%[__isrt2],%%rdi	\n\t	leaq -0x60(%%rdi),%%r9	\n\t"/* r9 holds 2.0 throughout */\
	/*...Block 1: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r00,r20,r10,r30): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r08,r28,r18,r38): */\
		"movq		%[__r00]	,%%rax			\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x400(%%rax),%%rbx			\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x200(%%rax),%%rcx			\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx			\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vsubpd	     %%ymm0,%%ymm2,%%ymm2		\n\t		vsubpd	%%ymm8 ,%%ymm10,%%ymm10	\n\t"\
		"vsubpd	     %%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd	%%ymm9 ,%%ymm11,%%ymm11	\n\t"\
		"vsubpd	     %%ymm4,%%ymm6,%%ymm6		\n\t		vsubpd	%%ymm14,%%ymm12,%%ymm12	\n\t"\
		"vsubpd	     %%ymm5,%%ymm7,%%ymm7		\n\t		vsubpd	%%ymm15,%%ymm13,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r00,r10,r20,r30,r08,r18,r28,r38): */\
		/* Combine r00,r08,r20,r28: */						/* Combine r10,r18,r30,r38: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 2: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r04,r24,r14,r34): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0C,r2C,r1C,r3C): */\
		"addq	$0x80,%%rax		\n\t"/* r04 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx		\n\t"/* r24 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2C */\
		"addq	$0x80,%%rcx		\n\t"/* r14 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1C */\
		"addq	$0x80,%%rdx		\n\t"/* r34 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3C */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vsubpd	     %%ymm0,%%ymm2,%%ymm2		\n\t		vsubpd	%%ymm8 ,%%ymm10,%%ymm10	\n\t"\
		"vsubpd	     %%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd	%%ymm9 ,%%ymm11,%%ymm11	\n\t"\
		"vsubpd	     %%ymm4,%%ymm6,%%ymm6		\n\t		vsubpd	%%ymm14,%%ymm12,%%ymm12	\n\t"\
		"vsubpd	     %%ymm5,%%ymm7,%%ymm7		\n\t		vsubpd	%%ymm15,%%ymm13,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 3: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r02,r22,r12,r32): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0A,r2A,r1A,r3A): */\
		"subq	$0x40,%%rax		\n\t"/* r02 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx		\n\t"/* r22 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2A */\
		"subq	$0x40,%%rcx		\n\t"/* r12 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1A */\
		"subq	$0x40,%%rdx		\n\t"/* r32 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3A */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vsubpd	     %%ymm0,%%ymm2,%%ymm2		\n\t		vsubpd	%%ymm8 ,%%ymm10,%%ymm10	\n\t"\
		"vsubpd	     %%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd	%%ymm9 ,%%ymm11,%%ymm11	\n\t"\
		"vsubpd	     %%ymm4,%%ymm6,%%ymm6		\n\t		vsubpd	%%ymm14,%%ymm12,%%ymm12	\n\t"\
		"vsubpd	     %%ymm5,%%ymm7,%%ymm7		\n\t		vsubpd	%%ymm15,%%ymm13,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/*...Block 4: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r06,r26,r16,r36): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0E,r2E,r1E,r3E): */\
		"addq	$0x80,%%rax		\n\t"/* r06 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx		\n\t"/* r26 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2E */\
		"addq	$0x80,%%rcx		\n\t"/* r16 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1E */\
		"addq	$0x80,%%rdx		\n\t"/* r36 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3E */\
		"vmovaps	    (%%rbx),%%ymm0			\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r13),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r13),%%ymm15	\n\t"\
		"vsubpd	     %%ymm0,%%ymm2,%%ymm2		\n\t		vsubpd	%%ymm8 ,%%ymm10,%%ymm10	\n\t"\
		"vsubpd	     %%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd	%%ymm9 ,%%ymm11,%%ymm11	\n\t"\
		"vsubpd	     %%ymm4,%%ymm6,%%ymm6		\n\t		vsubpd	%%ymm14,%%ymm12,%%ymm12	\n\t"\
		"vsubpd	     %%ymm5,%%ymm7,%%ymm7		\n\t		vsubpd	%%ymm15,%%ymm13,%%ymm13	\n\t"\
	"vmovaps	%%ymm15,(%%rax) 	\n\t"/* spill ymm15 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm15	\n\t"/* two */\
	"vfmadd132pd	%%ymm15,%%ymm2,%%ymm0		\n\t	vfmadd132pd	%%ymm15,%%ymm10,%%ymm8 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm3,%%ymm1		\n\t	vfmadd132pd	%%ymm15,%%ymm11,%%ymm9 	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm6,%%ymm4		\n\t	vfmadd132pd	%%ymm15,%%ymm12,%%ymm14	\n\t"\
	"vfmadd132pd	%%ymm15,%%ymm7,%%ymm5		\n\t	vfmadd132pd	(%%rax),%%ymm13,%%ymm15	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
	"vmovaps	%%ymm12,(%%rax) 	\n\t"/* spill ymm12 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm12	\n\t"/* two */\
	"vfmadd132pd	%%ymm12,%%ymm0,%%ymm4		\n\t	vfmadd132pd		%%ymm12,%%ymm8 ,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm1,%%ymm5		\n\t	vfmadd132pd		%%ymm12,%%ymm9 ,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm2,%%ymm7		\n\t	vfmadd132pd		%%ymm12,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm12,%%ymm3,%%ymm6		\n\t	vfmadd132pd		(%%rax),%%ymm11,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9 ),%%ymm10,%%ymm12		\n\t"/* .two */\
		"												vfmadd132pd		(%%r9 ),%%ymm13,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t	vfnmadd231pd	(%%rdi),%%ymm10,%%ymm2 		\n\t"/* .isrt2 */\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t	vfnmadd231pd	(%%rdi),%%ymm13,%%ymm3 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t	vfnmadd231pd	(%%rdi),%%ymm12,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t	vfnmadd231pd	(%%rdi),%%ymm11,%%ymm7 		\n\t"\
	"vmovaps	%%ymm8 ,(%%rax) 	\n\t"/* spill ymm8  to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm8 	\n\t"/* two */\
	"vmovaps	%%ymm11,(%%rcx) 	\n\t"/* spill ymm11 to make room for sqrt2 */"	vmovaps	-0x20(%%rdi),%%ymm11 \n\t"/* sqrt2 */\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm4 ,%%ymm14		\n\t	vfmadd132pd		%%ymm11,%%ymm2 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm0 ,%%ymm9 		\n\t	vfmadd132pd		%%ymm11,%%ymm3 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm8 ,%%ymm5 ,%%ymm15		\n\t	vfmadd132pd		%%ymm11,%%ymm6 ,%%ymm12		\n\t"\
	"vfmadd132pd	(%%rax),%%ymm1 ,%%ymm8 		\n\t	vfmadd132pd		(%%rcx),%%ymm7 ,%%ymm11		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
	/*...Block 1: t00,t10,t20,t30 in r00,04,02,06 */		/*...Block 5: t08,t18,t28,t38	*/\
		"movq	%[__r00],%%rsi					\n\t"\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x0(%%rdi),%%rax	/* p00 */	\n\t		movslq	0x10(%%rdi),%%r10	/* p04 */	\n\t"\
		"movslq	0x4(%%rdi),%%rbx	/* p01 */	\n\t		movslq	0x14(%%rdi),%%r11	/* p05 */	\n\t"\
		"movslq	0x8(%%rdi),%%rcx	/* p02 */	\n\t		movslq	0x18(%%rdi),%%r12	/* p06 */	\n\t"\
		"movslq	0xc(%%rdi),%%rdx	/* p03 */	\n\t		movslq	0x1c(%%rdi),%%r13	/* p07 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"													movq	%[__isrt2],%%rdi				\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t"/*		vmovaps	0x1a0(%%rsi),%%ymm11	Instead use ymm11 for 2.0: */"	vmovaps	(%%r9),%%ymm11 	\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		 %%ymm13,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		 %%ymm14,%%ymm15,%%ymm15		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		 %%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd	0x1a0(%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm0,%%ymm2		\n\t	vfmadd132pd		 %%ymm11,%%ymm12,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm4,%%ymm6		\n\t	vfmadd132pd		 %%ymm11,%%ymm15,%%ymm14		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm1,%%ymm3		\n\t	vfmadd132pd		 %%ymm11,%%ymm9 ,%%ymm10		\n\t"\
	"vfmadd132pd	%%ymm11,%%ymm5,%%ymm7		\n\t	vfmadd132pd	0x1a0(%%rsi),%%ymm8 ,%%ymm11		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd	    (%%rdi),%%ymm15,%%ymm15		\n\t"\
		"													vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"													vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"												vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
		"												vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm9 ,%%ymm9 		\n\t"\
	"vmovaps	%%ymm14,(%%rax) 	\n\t"/* spill ymm14 to make room for 2.0 */"	vmovaps	 (%%r9),%%ymm14	\n\t"/* two */\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)			\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,0x20(%%r11)			\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm11,    (%%r12)			\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r13)			\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm2,%%ymm6	\n\t	vfmadd132pd		%%ymm14,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm0,%%ymm5	\n\t	vfmadd132pd		%%ymm14,%%ymm10,%%ymm13		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm3,%%ymm7	\n\t	vfmadd132pd		%%ymm14,%%ymm11,%%ymm15		\n\t"\
	"vfmadd132pd		%%ymm14,%%ymm1,%%ymm4	\n\t	vfmadd132pd		(%%rax),%%ymm9 ,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)			\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm13,0x20(%%r10)			\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm15,    (%%r13)			\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)			\n\t"\
		/*...Block 3: t04,t14,t24,t34	*/					/*...Block 7: t0C,t1C,t2C,t3C	*/\
		"addq	$0x400,%%rsi		/* r20 */	\n\t"		/* r28 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x20(%%rdi),%%rax	/* p08 */	\n\t		movslq	0x30(%%rdi),%%r10	/* p0c */	\n\t"\
		"movslq	0x24(%%rdi),%%rbx	/* p09 */	\n\t		movslq	0x34(%%rdi),%%r11	/* p0d */	\n\t"\
		"movslq	0x28(%%rdi),%%rcx	/* p0a */	\n\t		movslq	0x38(%%rdi),%%r12	/* p0e */	\n\t"\
		"movslq	0x2c(%%rdi),%%rdx	/* p0b */	\n\t		movslq	0x3c(%%rdi),%%r13	/* p0f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x20,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	    (%%rdi),%%ymm2			\n\t		vmovaps	0x20(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmulpd			%%ymm2 ,%%ymm4,%%ymm4		\n\t		vmulpd			%%ymm10 ,%%ymm12,%%ymm12		\n\t"\
		"vmulpd			%%ymm10,%%ymm6,%%ymm6		\n\t		vmulpd			%%ymm2  ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd			%%ymm2 ,%%ymm5,%%ymm5		\n\t		vmulpd			%%ymm10 ,%%ymm13,%%ymm13		\n\t"\
		"vmulpd			%%ymm10,%%ymm7,%%ymm7		\n\t		vmulpd			%%ymm2  ,%%ymm15,%%ymm15		\n\t"\
	"vfnmadd231pd		%%ymm10,%%ymm1,%%ymm4		\n\t	vfnmadd231pd		%%ymm2  ,%%ymm9 ,%%ymm12		\n\t"\
	"vfnmadd231pd		%%ymm2 ,%%ymm3,%%ymm6		\n\t	vfnmadd231pd		%%ymm10 ,%%ymm11,%%ymm14		\n\t"\
	" vfmadd231pd		%%ymm10,%%ymm0,%%ymm5		\n\t	 vfmadd231pd		%%ymm2  ,%%ymm8 ,%%ymm13		\n\t"\
	" vfmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7		\n\t	 vfmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15		\n\t"\
		"subq	$0x20,%%rdi						\n\t"/* isrt2 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x1a0(%%rsi),%%ymm11			\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd	0xa0(%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x1a0(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x80(%%rsi),%%ymm3,%%ymm3		\n\t		vsubpd	0x180(%%rsi),%%ymm11,%%ymm11	\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 2: t02,t12,t22,t32	*/					/*...Block 6: t0A,t1A,t2A,t3A	*/\
		"subq	$0x200,%%rsi		/* r10 */	\n\t"		/* r18 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x40(%%rdi),%%rax	/* p10 */	\n\t		movslq	0x50(%%rdi),%%r10	/* p14 */	\n\t"\
		"movslq	0x44(%%rdi),%%rbx	/* p11 */	\n\t		movslq	0x54(%%rdi),%%r11	/* p15 */	\n\t"\
		"movslq	0x48(%%rdi),%%rcx	/* p12 */	\n\t		movslq	0x58(%%rdi),%%r12	/* p16 */	\n\t"\
		"movslq	0x4c(%%rdi),%%rdx	/* p13 */	\n\t		movslq	0x5c(%%rdi),%%r13	/* p17 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x60,%%rdi						\n\t"/* cc1 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t"/*		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"*/\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t"/*		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"*/\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmovaps	0x60(%%rdi),%%ymm2			\n\t		vmovaps	0x20(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x40(%%rdi),%%ymm8			\n\t		vmovaps		(%%rdi),%%ymm15	\n\t"/*	Instead use ymm8,15 for sincos: */\
		"vmulpd			%%ymm15,%%ymm4,%%ymm4	\n\t		vmulpd		%%ymm2  ,%%ymm12,%%ymm12		\n\t"\
		"vmulpd			%%ymm8 ,%%ymm6,%%ymm6	\n\t		vmulpd		%%ymm15 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd			%%ymm15,%%ymm5,%%ymm5	\n\t		vmulpd		%%ymm2  ,%%ymm13,%%ymm13		\n\t"\
		"vmulpd			%%ymm8 ,%%ymm7,%%ymm7	\n\t		vmulpd	0x1e0(%%rsi),%%ymm15,%%ymm15		\n\t"\
	"vfnmadd231pd		%%ymm10,%%ymm1,%%ymm4	\n\t	vfnmadd231pd		%%ymm8  ,%%ymm9 ,%%ymm12	\n\t"\
	"vfnmadd231pd		%%ymm2 ,%%ymm3,%%ymm6	\n\t	 vfmadd231pd		%%ymm10 ,%%ymm11,%%ymm14	\n\t"\
	" vfmadd231pd		%%ymm10,%%ymm0,%%ymm5	\n\t	 vfmadd231pd	0x140(%%rsi),%%ymm8 ,%%ymm13	\n\t"\
	" vfmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7	\n\t	vfnmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15	\n\t"\
		"subq	$0x40,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x20(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	    (%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x20(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	    (%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2		\n\t		vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 4: t06,t16,t26,t36	*/					/*...Block 8: t0E,t1E,t2E,t3E	*/\
		"addq	$0x400,%%rsi		/* r30 */	\n\t"		/* r38 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x60(%%rdi),%%rax	/* p18 */	\n\t		movslq	0x70(%%rdi),%%r10	/* p1c */	\n\t"\
		"movslq	0x64(%%rdi),%%rbx	/* p19 */	\n\t		movslq	0x74(%%rdi),%%r11	/* p1d */	\n\t"\
		"movslq	0x68(%%rdi),%%rcx	/* p1a */	\n\t		movslq	0x78(%%rdi),%%r12	/* p1e */	\n\t"\
		"movslq	0x6c(%%rdi),%%rdx	/* p1b */	\n\t		movslq	0x7c(%%rdi),%%r13	/* p1f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x60,%%rdi						\n\t"/* cc1 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
	/*	"vmovaps	0xe0(%%rsi),%%ymm7		*/"	\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
	/*	"vmovaps	0x40(%%rsi),%%ymm0		*/"	\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
	/*	"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"*/\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmovaps		(%%rdi),%%ymm2			\n\t		vmovaps	0x40(%%rdi),%%ymm10	\n\t"/*	Instead use ymm2,10 for sincos: */\
		"vmovaps	0x60(%%rdi),%%ymm0			\n\t		vmovaps	0x20(%%rdi),%%ymm7 	\n\t"/*	Instead use ymm0,7  for sincos: */\
		"vmulpd			%%ymm10,%%ymm4,%%ymm4	\n\t		vmulpd			%%ymm7  ,%%ymm12,%%ymm12	\n\t"\
		"vmulpd			%%ymm7 ,%%ymm6,%%ymm6	\n\t		vmulpd			%%ymm0  ,%%ymm14,%%ymm14	\n\t"\
		"vmulpd			%%ymm10,%%ymm5,%%ymm5	\n\t		vmulpd			%%ymm7  ,%%ymm13,%%ymm13	\n\t"\
		"vmulpd		0xe0(%%rsi),%%ymm7,%%ymm7	\n\t		vmulpd			%%ymm0  ,%%ymm15,%%ymm15	\n\t"\
	"vfnmadd231pd		%%ymm0 ,%%ymm1,%%ymm4	\n\t	vfnmadd231pd		%%ymm2  ,%%ymm9 ,%%ymm12	\n\t"\
	" vfmadd231pd		%%ymm2 ,%%ymm3,%%ymm6	\n\t	vfnmadd231pd		%%ymm10 ,%%ymm11,%%ymm14	\n\t"\
	" vfmadd231pd	0x40(%%rsi),%%ymm0,%%ymm5	\n\t	 vfmadd231pd		%%ymm2  ,%%ymm8 ,%%ymm13	\n\t"\
	"vfnmadd231pd	0xc0(%%rsi),%%ymm2,%%ymm7	\n\t	 vfmadd231pd	0x1c0(%%rsi),%%ymm10,%%ymm15	\n\t"\
		"subq	$0x40,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	0x20(%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm4,%%ymm6		\n\t	vfmadd132pd		(%%r9),%%ymm12,%%ymm14		\n\t"\
	"vfmadd132pd	(%%r9),%%ymm5,%%ymm7		\n\t	vfmadd132pd		(%%r9),%%ymm13,%%ymm15		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	0x20(%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2		\n\t		vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm4,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm5,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm6,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t	vmovaps	 (%%r9),%%ymm2	\n\t"/* two */\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
	"vfmadd213pd	(%%rbx),%%ymm2,%%ymm4		\n\t	vfmadd132pd		%%ymm2 ,%%ymm8 ,%%ymm12		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm0,%%ymm7		\n\t	vfmadd132pd		%%ymm2 ,%%ymm10,%%ymm15		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm3,%%ymm5		\n\t	vfmadd132pd		%%ymm2 ,%%ymm9 ,%%ymm13		\n\t"\
	"vfmadd132pd	%%ymm2 ,%%ymm1,%%ymm6		\n\t	vfmadd132pd		%%ymm2 ,%%ymm11,%%ymm14		\n\t"\
		"vmovaps	%%ymm4,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm7,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm5,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm6,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

  #endif	// ALL_FMA ?

#elif defined(USE_AVX)	// AVX and AVX2 both use 256-bit registers

	/* Both DIF and DIT macros assume needed roots of unity laid out in memory relative to the input pointer isrt2 like so:
	[Note: AVX2 versions of these macros supplement this basic consts-layout with additional slots for 1.0,2.0,sqrt2].
		Description																	Pointer-offset	Address-offset
		---------------------------------------------------------					------------	------------
		isrt2: 1/sqrt2, in terms of which thr basic 8th root of 1 = (1+i)/sqrt2		isrt2			isrt2
		cc0: Real part of basic 16th root of 1 = c16								isrt2 + 1		isrt2 + 0x20
		ss0: Real part of basic 16th root of 1 = s16								isrt2 + 2		isrt2 + 0x40
		cc1: Real part of basic 32th root of 1 = c32_1								isrt2 + 3		isrt2 + 0x60
		ss1: Real part of basic 32th root of 1 = s32_1								isrt2 + 4		isrt2 + 0x80
		cc3: Real part of 3rd   32th root of 1 = c32_3								isrt2 + 5		isrt2 + 0xa0
		ss3: Real part of 3rd   32th root of 1 = s32_3								isrt2 + 6		isrt2 + 0xc0
	*/
	// DIT opcounts: [240 ADD, 188 SUB, 184 MUL, 485 memref]
	#define SSE2_RADIX32_DIT_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
	/*...Block 1: */\
		"movq	%[__isrt2],%%r8	\n\t	leaq	-0x60(%%r8),%%r9	/* two */\n\t"\
		"movq	%[__r00],%%rsi	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r00)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r08): */\
		/* Can't simply addq these to a 64-bit base array address since the offsets are 4-byte array data: */\
		"movq	%[__arr_offsets],%%rdi				\n\t"\
		"movslq	0x00(%%rdi),%%rax		\n\t"/* p00 */"			movslq	0x10(%%rdi),%%r10	\n\t"/* p04 */\
		"movslq	0x04(%%rdi),%%rbx		\n\t"/* p01 */"			movslq	0x14(%%rdi),%%r11	\n\t"/* p05 */\
		"movslq	0x08(%%rdi),%%rcx		\n\t"/* p02 */"			movslq	0x18(%%rdi),%%r12	\n\t"/* p06 */\
		"movslq	0x0c(%%rdi),%%rdx		\n\t"/* p03 */"			movslq	0x1c(%%rdi),%%r13	\n\t"/* p07 */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r13),%%ymm13	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm0,%%ymm0			\n\t		vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm1,%%ymm1			\n\t		vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm6,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm7,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7			\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6			\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"														vaddpd		%%ymm11,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"					vmovaps	%%ymm10,(%%rsi)	\n\t	vmovaps	(%%r8),%%ymm10	\n\t"/* spill to make room for isrt2 */\
		"														vmulpd		%%ymm10,%%ymm11,%%ymm11		\n\t"\
		"														vmulpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vmulpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"														vmulpd		(%%rsi),%%ymm10,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r00,r02,r04,r06,r08,r0A,r0C,r0E): */\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t		vsubpd		%%ymm15,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t		vsubpd		%%ymm14,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t		vsubpd		%%ymm11,%%ymm3 ,%%ymm3 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t		vsubpd		%%ymm10,%%ymm6 ,%%ymm6 		\n\t"\
		"vmulpd		(%%r9),%%ymm12,%%ymm12			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm13,%%ymm13			\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 			\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm12,%%ymm12			\n\t		vaddpd		%%ymm7 ,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 			\n\t		vaddpd		%%ymm2 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm13,%%ymm13			\n\t		vaddpd		%%ymm3 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 			\n\t		vaddpd		%%ymm6 ,%%ymm10,%%ymm10		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 2: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r10)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x20(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x30(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x24(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x34(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x28(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x38(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x2c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x3c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r13),%%ymm13	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm0,%%ymm0			\n\t		vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm1,%%ymm1			\n\t		vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm6,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm7,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7			\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6			\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"														vaddpd		%%ymm11,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"					vmovaps	%%ymm10,(%%rsi)	\n\t	vmovaps	(%%r8),%%ymm10	\n\t"/* spill to make room for isrt2 */\
		"														vmulpd		%%ymm10,%%ymm11,%%ymm11		\n\t"\
		"														vmulpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vmulpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"														vmulpd		(%%rsi),%%ymm10,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r10,r12,r14,r16,r18,r1A,r1C,r1E): */\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t		vsubpd		%%ymm15,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t		vsubpd		%%ymm14,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t		vsubpd		%%ymm11,%%ymm3 ,%%ymm3 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t		vsubpd		%%ymm10,%%ymm6 ,%%ymm6 		\n\t"\
		"vmulpd		(%%r9),%%ymm12,%%ymm12			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm13,%%ymm13			\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 			\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm12,%%ymm12			\n\t		vaddpd		%%ymm7 ,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 			\n\t		vaddpd		%%ymm2 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm13,%%ymm13			\n\t		vaddpd		%%ymm3 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 			\n\t		vaddpd		%%ymm6 ,%%ymm10,%%ymm10		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 3: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r20)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r28): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x40(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x50(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x44(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x54(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x48(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x58(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x4c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x5c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r13),%%ymm13	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm0,%%ymm0			\n\t		vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm1,%%ymm1			\n\t		vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm6,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm7,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7			\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6			\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"														vaddpd		%%ymm11,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"					vmovaps	%%ymm10,(%%rsi)	\n\t	vmovaps	(%%r8),%%ymm10	\n\t"/* spill to make room for isrt2 */\
		"														vmulpd		%%ymm10,%%ymm11,%%ymm11		\n\t"\
		"														vmulpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vmulpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"														vmulpd		(%%rsi),%%ymm10,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r20,r22,r24,r26,r28,r2A,r2C,r2E): */\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t		vsubpd		%%ymm15,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t		vsubpd		%%ymm14,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t		vsubpd		%%ymm11,%%ymm3 ,%%ymm3 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t		vsubpd		%%ymm10,%%ymm6 ,%%ymm6 		\n\t"\
		"vmulpd		(%%r9),%%ymm12,%%ymm12			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm13,%%ymm13			\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 			\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm12,%%ymm12			\n\t		vaddpd		%%ymm7 ,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 			\n\t		vaddpd		%%ymm2 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm13,%%ymm13			\n\t		vaddpd		%%ymm3 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 			\n\t		vaddpd		%%ymm6 ,%%ymm10,%%ymm10		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/*...Block 4: */\
		"addq	$0x200,%%rsi			\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0,add1,add2,add3, r01)	SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4,add5,add6,add7, r18): */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x60(%%rdi),%%rax		\n\t"/* p08 */"			movslq	0x70(%%rdi),%%r10	\n\t"/* p0c */\
		"movslq	0x64(%%rdi),%%rbx		\n\t"/* p09 */"			movslq	0x74(%%rdi),%%r11	\n\t"/* p0d */\
		"movslq	0x68(%%rdi),%%rcx		\n\t"/* p0a */"			movslq	0x78(%%rdi),%%r12	\n\t"/* p0e */\
		"movslq	0x6c(%%rdi),%%rdx		\n\t"/* p0b */"			movslq	0x7c(%%rdi),%%r13	\n\t"/* p0f */\
		"movq	%[__add],%%rdi						\n\t"\
		"addq	%%rdi,%%rax							\n\t		addq	%%rdi,%%r10	\n\t"\
		"addq	%%rdi,%%rbx							\n\t		addq	%%rdi,%%r11	\n\t"\
		"addq	%%rdi,%%rcx							\n\t		addq	%%rdi,%%r12	\n\t"\
		"addq	%%rdi,%%rdx							\n\t		addq	%%rdi,%%r13	\n\t"\
		"vmovaps	    (%%rax),%%ymm2				\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3				\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm4				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1				\n\t		vmovaps	    (%%r13),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm5				\n\t		vmovaps	0x20(%%r13),%%ymm13	\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm5,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm0,%%ymm0			\n\t		vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm1,%%ymm1			\n\t		vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm6,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm7,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7			\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6			\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"														vsubpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"														vaddpd		%%ymm11,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"					vmovaps	%%ymm10,(%%rsi)	\n\t	vmovaps	(%%r8),%%ymm10	\n\t"/* spill to make room for isrt2 */\
		"														vmulpd		%%ymm10,%%ymm11,%%ymm11		\n\t"\
		"														vmulpd		%%ymm10,%%ymm14,%%ymm14		\n\t"\
		"														vmulpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"														vmulpd		(%%rsi),%%ymm10,%%ymm10		\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r30,r32,r34,r36,r38,r3A,r3C,r3E): */\
		"vsubpd		%%ymm12,%%ymm4 ,%%ymm4 			\n\t		vsubpd		%%ymm15,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 			\n\t		vsubpd		%%ymm14,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm13,%%ymm5 ,%%ymm5 			\n\t		vsubpd		%%ymm11,%%ymm3 ,%%ymm3 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 			\n\t		vsubpd		%%ymm10,%%ymm6 ,%%ymm6 		\n\t"\
		"vmulpd		(%%r9),%%ymm12,%%ymm12			\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 			\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm13,%%ymm13			\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 			\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm12,%%ymm12			\n\t		vaddpd		%%ymm7 ,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 			\n\t		vaddpd		%%ymm2 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm13,%%ymm13			\n\t		vaddpd		%%ymm3 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 			\n\t		vaddpd		%%ymm6 ,%%ymm10,%%ymm10		\n\t"\
		"vmovaps	%%ymm4 ,0x100(%%rsi)			\n\t		vmovaps		%%ymm7 ,0x140(%%rsi)		\n\t"\
		"vmovaps	%%ymm0 ,0x180(%%rsi)			\n\t		vmovaps		%%ymm2 ,0x1c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm5 ,0x120(%%rsi)			\n\t		vmovaps		%%ymm3 ,0x160(%%rsi)		\n\t"\
		"vmovaps	%%ymm1 ,0x0a0(%%rsi)			\n\t		vmovaps		%%ymm6 ,0x0e0(%%rsi)		\n\t"\
		"vmovaps	%%ymm12,     (%%rsi)			\n\t		vmovaps		%%ymm15,0x040(%%rsi)		\n\t"\
		"vmovaps	%%ymm9 ,0x080(%%rsi)			\n\t		vmovaps		%%ymm14,0x0c0(%%rsi)		\n\t"\
		"vmovaps	%%ymm13,0x020(%%rsi)			\n\t		vmovaps		%%ymm11,0x060(%%rsi)		\n\t"\
		"vmovaps	%%ymm8 ,0x1a0(%%rsi)			\n\t		vmovaps		%%ymm10,0x1e0(%%rsi)		\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
		/*...Block 1: r00,r10,r20,r30	*/						/*...Block 5: r08,r18,r28,r38	*/\
		"movq	%[__isrt2],%%rdi					\n\t		vmovaps	(%%rdi),%%ymm10	\n\t"/* isrt2 */\
		"movq		%[__r00]	,%%rax				\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x200(%%rax),%%rbx				\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x400(%%rax),%%rcx				\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx				\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm0,%%ymm0			\n\t		vaddpd	0x20(%%r12),%%ymm12,%%ymm12		\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm1,%%ymm1			\n\t		vsubpd	    (%%r12),%%ymm13,%%ymm13		\n\t"\
		"vaddpd	    (%%rax),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r13),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd	0x20(%%rax),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r13),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmulpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmulpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	    (%%rdx),%%ymm6				\n\t		vmulpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm7				\n\t		vmulpd		%%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd	    (%%rdx),%%ymm4,%%ymm4			\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm5,%%ymm5			\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vaddpd	    (%%rcx),%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd	0x20(%%rcx),%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm6,%%ymm2,%%ymm2			\n\t		vaddpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm7,%%ymm3,%%ymm3			\n\t		vaddpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	%%ymm2,    (%%rax)				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rax)				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vaddpd		%%ymm6,%%ymm6,%%ymm6			\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vaddpd		%%ymm7,%%ymm7,%%ymm7			\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r11),%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3			\n\t		vsubpd	    (%%r11),%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vaddpd	    (%%r10),%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vaddpd	0x20(%%r10),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)				\n\t		vmovaps	%%ymm11,    (%%r10)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r10)	\n\t"\
		"vaddpd		%%ymm5,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm12,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm4,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm13,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm4,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm11,    (%%r12)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"														vaddpd		%%ymm15,%%ymm8 ,%%ymm8 		\n\t"\
		"														vsubpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"														vmovaps	%%ymm8 ,    (%%r11)	\n\t"\
		"														vmovaps	%%ymm10,0x20(%%r11)	\n\t"\
		"														vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"														vaddpd		%%ymm14,%%ymm14,%%ymm14		\n\t"\
		"														vsubpd		%%ymm15,%%ymm8 ,%%ymm8 		\n\t"\
		"														vaddpd		%%ymm14,%%ymm10,%%ymm10		\n\t"\
		"														vmovaps	%%ymm8 ,    (%%r13)	\n\t"\
		"														vmovaps	%%ymm10,0x20(%%r13)	\n\t"\
		"leaq	0x20(%%rdi),%%rsi	\n\t"/* cc0 */\
		/*...Block 3: r04,r14,r24,r34	*/						/*...Block 7: r0C,r1C,r2C,r3C	*/\
		"addq	$0x80,%%rax 				\n\t"/* r04 */"		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx							\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx							\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx							\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rcx),%%ymm4				\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0				\n\t		vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5				\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1				\n\t		vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6				\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2				\n\t		vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7				\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm3				\n\t		vmovaps	0x20(%%r13),%%ymm11	\n\t"\
		"vmulpd	    (%%rsi),%%ymm4,%%ymm4			\n\t		vmulpd	0x20(%%rsi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm0,%%ymm0			\n\t		vmulpd	    (%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	    (%%rsi),%%ymm5,%%ymm5			\n\t		vmulpd	0x20(%%rsi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm1,%%ymm1			\n\t		vmulpd	    (%%rsi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm6,%%ymm6			\n\t		vmulpd	    (%%rsi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	    (%%rsi),%%ymm2,%%ymm2			\n\t		vmulpd	0x20(%%rsi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm7,%%ymm7			\n\t		vmulpd	    (%%rsi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	    (%%rsi),%%ymm3,%%ymm3			\n\t		vmulpd	0x20(%%rsi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm5,%%ymm5			\n\t		vsubpd		%%ymm14,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm2,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm7,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm15,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm11,%%ymm8 ,%%ymm8 		\n\t"\
		"vmovaps	%%ymm5,%%ymm7					\n\t		vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6					\n\t		vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm0,%%ymm6,%%ymm6			\n\t		vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm1,%%ymm7,%%ymm7			\n\t		vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	    (%%rbx),%%ymm2				\n\t		vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3				\n\t		vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rax),%%ymm0				\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1				\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm2,%%ymm2			\n\t		vsubpd	0x20(%%r11),%%ymm10,%%ymm10		\n\t"\
		"vsubpd	    (%%rbx),%%ymm3,%%ymm3			\n\t		vaddpd	    (%%r11),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2			\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3			\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm2,%%ymm2,%%ymm2			\n\t		vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm3,%%ymm3,%%ymm3			\n\t		vaddpd		%%ymm11,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm2,%%ymm2			\n\t		vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3			\n\t		vaddpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm4,%%ymm2,%%ymm2			\n\t		vaddpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm5,%%ymm3,%%ymm3			\n\t		vaddpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rax)				\n\t		vmovaps	%%ymm8 ,    (%%r10)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rax)				\n\t		vmovaps	%%ymm9 ,0x20(%%r10)	\n\t"\
		"vaddpd		%%ymm4,%%ymm4,%%ymm4			\n\t		vaddpd		%%ymm14,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5,%%ymm5,%%ymm5			\n\t		vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"vsubpd		%%ymm4,%%ymm2,%%ymm2			\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm3,%%ymm3			\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)				\n\t		vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)				\n\t		vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"vaddpd		%%ymm7,%%ymm0,%%ymm0			\n\t		vaddpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm1,%%ymm1			\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)				\n\t		vmovaps	%%ymm10,    (%%r11)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)				\n\t		vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vaddpd		%%ymm7,%%ymm7,%%ymm7			\n\t		vaddpd		%%ymm13,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm6,%%ymm6,%%ymm6			\n\t		vaddpd		%%ymm12,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm0,%%ymm0			\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm6,%%ymm1,%%ymm1			\n\t		vaddpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)				\n\t		vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)				\n\t		vmovaps	%%ymm11,0x20(%%r13)	\n\t"\
		"\n\t"\
		/*...Block 2: r02,r12,r22,r32	*/						/*...Block 6: r0A,r1A,r2A,r3A	*/\
		"subq	$0x40,%%rax 			\n\t"/* r02 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"subq	$0x40,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"subq	$0x40,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x60,%%rdi 			\n\t"/* cc1 */"			addq	$0x80,%%rsi \n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4			\n\t			vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0			\n\t			vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t			vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1			\n\t			vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t			vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2			\n\t			vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t			vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm3			\n\t			vmovaps	0x20(%%r13),%%ymm11	\n\t"\
		"vmulpd	    (%%rdi),%%ymm4,%%ymm4		\n\t			vmulpd	0x20(%%rsi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	    (%%rsi),%%ymm0,%%ymm0		\n\t			vmulpd	    (%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	    (%%rdi),%%ymm5,%%ymm5		\n\t			vmulpd	0x20(%%rsi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	    (%%rsi),%%ymm1,%%ymm1		\n\t			vmulpd	    (%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm6,%%ymm6		\n\t			vmulpd	    (%%rsi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm2,%%ymm2		\n\t			vmulpd	0x20(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm7,%%ymm7		\n\t			vmulpd	    (%%rsi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t			vmulpd	0x20(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm5,%%ymm5		\n\t			vsubpd		%%ymm14,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm2,%%ymm1,%%ymm1		\n\t			vaddpd		%%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm7,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm15,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm0,%%ymm0		\n\t			vsubpd		%%ymm11,%%ymm8 ,%%ymm8 		\n\t"\
		"vmovaps	%%ymm5,%%ymm7				\n\t			vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6				\n\t			vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vsubpd		%%ymm0,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm1,%%ymm5,%%ymm5		\n\t			vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm0,%%ymm6,%%ymm6		\n\t			vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm1,%%ymm7,%%ymm7		\n\t			vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi 					\n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2			\n\t			vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3			\n\t			vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0			\n\t			vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t			vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd	    (%%rsi),%%ymm2,%%ymm2		\n\t			vmulpd	0x20(%%rsi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm1,%%ymm1		\n\t			vmulpd	    (%%rsi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	    (%%rsi),%%ymm3,%%ymm3		\n\t			vmulpd	0x20(%%rsi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm0,%%ymm0		\n\t			vmulpd	    (%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm1,%%ymm2,%%ymm2		\n\t			vsubpd		%%ymm9 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm0,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm8 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rax),%%ymm0			\n\t			vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t			vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t			vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t			vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm2,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm3,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm11,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm6,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm7,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rax)			\n\t			vmovaps	%%ymm8 ,    (%%r10)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rax)			\n\t			vmovaps	%%ymm9 ,0x20(%%r10)	\n\t"\
		"vaddpd		%%ymm6,%%ymm6,%%ymm6		\n\t			vaddpd		%%ymm14,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm7,%%ymm7,%%ymm7		\n\t			vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t			vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t			vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)			\n\t			vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)			\n\t			vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"vaddpd		%%ymm5,%%ymm0,%%ymm0		\n\t			vaddpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t			vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)			\n\t			vmovaps	%%ymm10,    (%%r11)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)			\n\t			vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vaddpd		%%ymm5,%%ymm5,%%ymm5		\n\t			vaddpd		%%ymm13,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm12,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t			vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4,%%ymm1,%%ymm1		\n\t			vaddpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)			\n\t			vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t			vmovaps	%%ymm11,0x20(%%r13)	\n\t"\
		"\n\t"\
		/*...Block 4: r06,r16,r26,r36	*/						/*...Block 8: r0E,r1E,r2E,r3E	*/\
		"addq	$0x80,%%rax 			\n\t"/* r06 */"			leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx				\n\t					leaq	0x100(%%rbx),%%r11		\n\t"\
		"addq	$0x80,%%rcx				\n\t					leaq	0x100(%%rcx),%%r12		\n\t"\
		"addq	$0x80,%%rdx				\n\t					leaq	0x100(%%rdx),%%r13		\n\t"\
		"addq	$0x80,%%rsi				\n\t"/* cc3 */\
		"vmovaps	    (%%rcx),%%ymm4			\n\t			vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	    (%%rdx),%%ymm0			\n\t			vmovaps	    (%%r13),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t			vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm1			\n\t			vmovaps	0x20(%%r13),%%ymm9 	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t			vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	    (%%rdx),%%ymm2			\n\t			vmovaps	    (%%r13),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t			vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vmovaps	0x20(%%rdx),%%ymm3			\n\t			vmovaps	0x20(%%r13),%%ymm11	\n\t"\
		"vmulpd	    (%%rsi),%%ymm4,%%ymm4		\n\t			vmulpd	0x20(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm0,%%ymm0		\n\t			vmulpd	0x20(%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	    (%%rsi),%%ymm5,%%ymm5		\n\t			vmulpd	0x20(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm1,%%ymm1		\n\t			vmulpd	0x20(%%rsi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm6,%%ymm6		\n\t			vmulpd	    (%%rdi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t			vmulpd	    (%%rsi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm7,%%ymm7		\n\t			vmulpd	    (%%rdi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t			vmulpd	    (%%rsi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm5,%%ymm5		\n\t			vsubpd		%%ymm14,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm1,%%ymm1		\n\t			vsubpd		%%ymm10,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm7,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm15,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm3,%%ymm0,%%ymm0		\n\t			vaddpd		%%ymm11,%%ymm8 ,%%ymm8 		\n\t"\
		"vmovaps	%%ymm5,%%ymm7				\n\t			vmovaps	%%ymm13,%%ymm15		\n\t"\
		"vmovaps	%%ymm4,%%ymm6				\n\t			vmovaps	%%ymm12,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5		\n\t			vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm0,%%ymm6,%%ymm6		\n\t			vsubpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm1,%%ymm7,%%ymm7		\n\t			vsubpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"subq	$0x80,%%rsi \n\t"/* cc0 */\
		"vmovaps	    (%%rbx),%%ymm2			\n\t			vmovaps	    (%%r11),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm3			\n\t			vmovaps	0x20(%%r11),%%ymm11	\n\t"\
		"vmovaps	    (%%rbx),%%ymm0			\n\t			vmovaps	    (%%r11),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rbx),%%ymm1			\n\t			vmovaps	0x20(%%r11),%%ymm9 	\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm2,%%ymm2		\n\t			vmulpd	    (%%rsi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rsi),%%ymm1,%%ymm1		\n\t			vmulpd	0x20(%%rsi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t			vmulpd	    (%%rsi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rsi),%%ymm0,%%ymm0		\n\t			vmulpd	0x20(%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm1,%%ymm2,%%ymm2		\n\t			vsubpd		%%ymm9 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm0,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm8 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rax),%%ymm0			\n\t			vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t			vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t			vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t			vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm2,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm10,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm3,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm11,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm6,%%ymm2,%%ymm2		\n\t			vaddpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd		%%ymm7,%%ymm3,%%ymm3		\n\t			vaddpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rax)			\n\t			vmovaps	%%ymm8 ,    (%%r10)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rax)			\n\t			vmovaps	%%ymm9 ,0x20(%%r10)	\n\t"\
		"vaddpd		%%ymm6,%%ymm6,%%ymm6		\n\t			vaddpd		%%ymm14,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm7,%%ymm7,%%ymm7		\n\t			vaddpd		%%ymm15,%%ymm15,%%ymm15		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t			vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t			vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vmovaps	%%ymm2,    (%%rcx)			\n\t			vmovaps	%%ymm8 ,    (%%r12)	\n\t"\
		"vmovaps	%%ymm3,0x20(%%rcx)			\n\t			vmovaps	%%ymm9 ,0x20(%%r12)	\n\t"\
		"vaddpd		%%ymm5,%%ymm0,%%ymm0		\n\t			vaddpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t			vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rbx)			\n\t			vmovaps	%%ymm10,    (%%r11)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rbx)			\n\t			vmovaps	%%ymm11,0x20(%%r11)	\n\t"\
		"vaddpd		%%ymm5,%%ymm5,%%ymm5		\n\t			vaddpd		%%ymm13,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4,%%ymm4,%%ymm4		\n\t			vaddpd		%%ymm12,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t			vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm4,%%ymm1,%%ymm1		\n\t			vaddpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm0,    (%%rdx)			\n\t			vmovaps	%%ymm10,    (%%r13)	\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t			vmovaps	%%ymm11,0x20(%%r13)	\n\t"\
		"\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r8","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

	// DIF opcounts: [188 ADD, 188 SUB, 206 MUL, 580 memref]
	#define SSE2_RADIX32_DIF_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
		"movq	%[__isrt2],%%rdi				\n\t		leaq	-0x60(%%rdi),%%r9	/* two */\n\t"\
	/*...Block 1: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r00,r20,r10,r30): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r08,r28,r18,r38): */\
		"movq		%[__r00]	,%%rax			\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r08 */\
		"leaq		0x400(%%rax),%%rbx			\n\t		leaq	0x100(%%rbx),%%r11		\n\t"\
		"leaq		0x200(%%rax),%%rcx			\n\t		leaq	0x100(%%rcx),%%r12		\n\t"\
		"leaq		0x600(%%rax),%%rdx			\n\t		leaq	0x100(%%rdx),%%r13		\n\t"\
		"vmovaps	    (%%rax),%%ymm0			\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vaddpd	    (%%rbx),%%ymm0,%%ymm0		\n\t		vaddpd	    (%%r11),%%ymm8 ,%%ymm8 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm1,%%ymm1		\n\t		vaddpd	0x20(%%r11),%%ymm9 ,%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm2,%%ymm2		\n\t		vsubpd	    (%%r11),%%ymm10,%%ymm10	\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm3,%%ymm3		\n\t		vsubpd	0x20(%%r11),%%ymm11,%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vaddpd	    (%%rdx),%%ymm4,%%ymm4		\n\t		vaddpd	    (%%r13),%%ymm14,%%ymm14	\n\t"\
		"vaddpd	0x20(%%rdx),%%ymm5,%%ymm5		\n\t		vaddpd	0x20(%%r13),%%ymm15,%%ymm15	\n\t"\
		"vsubpd	    (%%rdx),%%ymm6,%%ymm6		\n\t		vsubpd	    (%%r13),%%ymm12,%%ymm12	\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm7,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm13,%%ymm13	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"													vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"													vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm12,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"													vaddpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"													vaddpd		%%ymm13,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4		\n\t		vmulpd		(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5		\n\t		vmulpd		(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vmulpd		(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vmulpd		(%%rdi),%%ymm11,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r00,r10,r20,r30,r08,r18,r28,r38): */\
		/* Combine r00,r08,r20,r28: */						/* Combine r10,r18,r30,r38: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t		vsubpd		%%ymm10,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t		vsubpd		%%ymm11,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t		vsubpd		%%ymm12,%%ymm6 ,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t		vsubpd		%%ymm13,%%ymm3 ,%%ymm3 		\n\t"\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm14,%%ymm14		\n\t		vaddpd		%%ymm2 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 		\n\t		vaddpd		%%ymm7 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm15,%%ymm15		\n\t		vaddpd		%%ymm6 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 		\n\t		vaddpd		%%ymm3 ,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"\n\t"\
	/*...Block 2: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r04,r24,r14,r34): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0C,r2C,r1C,r3C): */\
		"addq	$0x80,%%rax		\n\t"/* r04 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0C */\
		"addq	$0x80,%%rbx		\n\t"/* r24 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2C */\
		"addq	$0x80,%%rcx		\n\t"/* r14 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1C */\
		"addq	$0x80,%%rdx		\n\t"/* r34 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3C */\
		"vmovaps	    (%%rax),%%ymm0			\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vaddpd	    (%%rbx),%%ymm0,%%ymm0		\n\t		vaddpd	    (%%r11),%%ymm8 ,%%ymm8 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm1,%%ymm1		\n\t		vaddpd	0x20(%%r11),%%ymm9 ,%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm2,%%ymm2		\n\t		vsubpd	    (%%r11),%%ymm10,%%ymm10	\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm3,%%ymm3		\n\t		vsubpd	0x20(%%r11),%%ymm11,%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vaddpd	    (%%rdx),%%ymm4,%%ymm4		\n\t		vaddpd	    (%%r13),%%ymm14,%%ymm14	\n\t"\
		"vaddpd	0x20(%%rdx),%%ymm5,%%ymm5		\n\t		vaddpd	0x20(%%r13),%%ymm15,%%ymm15	\n\t"\
		"vsubpd	    (%%rdx),%%ymm6,%%ymm6		\n\t		vsubpd	    (%%r13),%%ymm12,%%ymm12	\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm7,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm13,%%ymm13	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"													vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"													vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm12,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"													vaddpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"													vaddpd		%%ymm13,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4		\n\t		vmulpd		(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5		\n\t		vmulpd		(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vmulpd		(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vmulpd		(%%rdi),%%ymm11,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t		vsubpd		%%ymm10,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t		vsubpd		%%ymm11,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t		vsubpd		%%ymm12,%%ymm6 ,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t		vsubpd		%%ymm13,%%ymm3 ,%%ymm3 		\n\t"\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm14,%%ymm14		\n\t		vaddpd		%%ymm2 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 		\n\t		vaddpd		%%ymm7 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm15,%%ymm15		\n\t		vaddpd		%%ymm6 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 		\n\t		vaddpd		%%ymm3 ,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"\n\t"\
	/*...Block 3: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r02,r22,r12,r32): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0A,r2A,r1A,r3A): */\
		"subq	$0x40,%%rax		\n\t"/* r02 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0A */\
		"subq	$0x40,%%rbx		\n\t"/* r22 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2A */\
		"subq	$0x40,%%rcx		\n\t"/* r12 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1A */\
		"subq	$0x40,%%rdx		\n\t"/* r32 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3A */\
		"vmovaps	    (%%rax),%%ymm0			\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vaddpd	    (%%rbx),%%ymm0,%%ymm0		\n\t		vaddpd	    (%%r11),%%ymm8 ,%%ymm8 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm1,%%ymm1		\n\t		vaddpd	0x20(%%r11),%%ymm9 ,%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm2,%%ymm2		\n\t		vsubpd	    (%%r11),%%ymm10,%%ymm10	\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm3,%%ymm3		\n\t		vsubpd	0x20(%%r11),%%ymm11,%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vaddpd	    (%%rdx),%%ymm4,%%ymm4		\n\t		vaddpd	    (%%r13),%%ymm14,%%ymm14	\n\t"\
		"vaddpd	0x20(%%rdx),%%ymm5,%%ymm5		\n\t		vaddpd	0x20(%%r13),%%ymm15,%%ymm15	\n\t"\
		"vsubpd	    (%%rdx),%%ymm6,%%ymm6		\n\t		vsubpd	    (%%r13),%%ymm12,%%ymm12	\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm7,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm13,%%ymm13	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"													vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"													vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm12,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"													vaddpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"													vaddpd		%%ymm13,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4		\n\t		vmulpd		(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5		\n\t		vmulpd		(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vmulpd		(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vmulpd		(%%rdi),%%ymm11,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t		vsubpd		%%ymm10,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t		vsubpd		%%ymm11,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t		vsubpd		%%ymm12,%%ymm6 ,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t		vsubpd		%%ymm13,%%ymm3 ,%%ymm3 		\n\t"\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm14,%%ymm14		\n\t		vaddpd		%%ymm2 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 		\n\t		vaddpd		%%ymm7 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm15,%%ymm15		\n\t		vaddpd		%%ymm6 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 		\n\t		vaddpd		%%ymm3 ,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"\n\t"\
	/*...Block 4: */\
		/* SSE2_RADIX4_DIF_IN_PLACE(r06,r26,r16,r36): */	/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0E,r2E,r1E,r3E): */\
		"addq	$0x80,%%rax		\n\t"/* r06 */"	\n\t		leaq	0x100(%%rax),%%r10		\n\t"/* r0E */\
		"addq	$0x80,%%rbx		\n\t"/* r26 */"	\n\t		leaq	0x100(%%rbx),%%r11		\n\t"/* r2E */\
		"addq	$0x80,%%rcx		\n\t"/* r16 */"	\n\t		leaq	0x100(%%rcx),%%r12		\n\t"/* r1E */\
		"addq	$0x80,%%rdx		\n\t"/* r36 */"	\n\t		leaq	0x100(%%rdx),%%r13		\n\t"/* r3E */\
		"vmovaps	    (%%rax),%%ymm0			\n\t		vmovaps	    (%%r10),%%ymm8 	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm1			\n\t		vmovaps	0x20(%%r10),%%ymm9 	\n\t"\
		"vmovaps	    (%%rax),%%ymm2			\n\t		vmovaps	    (%%r10),%%ymm10	\n\t"\
		"vmovaps	0x20(%%rax),%%ymm3			\n\t		vmovaps	0x20(%%r10),%%ymm11	\n\t"\
		"vaddpd	    (%%rbx),%%ymm0,%%ymm0		\n\t		vaddpd	    (%%r11),%%ymm8 ,%%ymm8 	\n\t"\
		"vaddpd	0x20(%%rbx),%%ymm1,%%ymm1		\n\t		vaddpd	0x20(%%r11),%%ymm9 ,%%ymm9 	\n\t"\
		"vsubpd	    (%%rbx),%%ymm2,%%ymm2		\n\t		vsubpd	    (%%r11),%%ymm10,%%ymm10	\n\t"\
		"vsubpd	0x20(%%rbx),%%ymm3,%%ymm3		\n\t		vsubpd	0x20(%%r11),%%ymm11,%%ymm11	\n\t"\
		"vmovaps	    (%%rcx),%%ymm4			\n\t		vmovaps	    (%%r12),%%ymm12	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm5			\n\t		vmovaps	0x20(%%r12),%%ymm13	\n\t"\
		"vmovaps	    (%%rcx),%%ymm6			\n\t		vmovaps	    (%%r12),%%ymm14	\n\t"\
		"vmovaps	0x20(%%rcx),%%ymm7			\n\t		vmovaps	0x20(%%r12),%%ymm15	\n\t"\
		"vaddpd	    (%%rdx),%%ymm4,%%ymm4		\n\t		vaddpd	    (%%r13),%%ymm14,%%ymm14	\n\t"\
		"vaddpd	0x20(%%rdx),%%ymm5,%%ymm5		\n\t		vaddpd	0x20(%%r13),%%ymm15,%%ymm15	\n\t"\
		"vsubpd	    (%%rdx),%%ymm6,%%ymm6		\n\t		vsubpd	    (%%r13),%%ymm12,%%ymm12	\n\t"\
		"vsubpd	0x20(%%rdx),%%ymm7,%%ymm7		\n\t		vsubpd	0x20(%%r13),%%ymm13,%%ymm13	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"vsubpd		%%ymm4,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm14,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm15,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm7,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm6,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm12,%%ymm11,%%ymm11		\n\t"\
		"													vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"													vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"													vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm8 ,%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm9 ,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm12,%%ymm12		\n\t"\
		"													vsubpd		%%ymm12,%%ymm10,%%ymm10		\n\t"\
		"													vsubpd		%%ymm11,%%ymm13,%%ymm13		\n\t"\
		"													vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"													vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"													vaddpd		%%ymm10,%%ymm12,%%ymm12		\n\t"\
		"													vaddpd		%%ymm13,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm0,%%ymm4,%%ymm4		\n\t		vmulpd		(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm1,%%ymm5,%%ymm5		\n\t		vmulpd		(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vmulpd		(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vmulpd		(%%rdi),%%ymm11,%%ymm11		\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS: */\
		"vsubpd		%%ymm14,%%ymm4 ,%%ymm4 		\n\t		vsubpd		%%ymm10,%%ymm2 ,%%ymm2 		\n\t"\
		"vsubpd		%%ymm9 ,%%ymm0 ,%%ymm0 		\n\t		vsubpd		%%ymm11,%%ymm7 ,%%ymm7 		\n\t"\
		"vsubpd		%%ymm15,%%ymm5 ,%%ymm5 		\n\t		vsubpd		%%ymm12,%%ymm6 ,%%ymm6 		\n\t"\
		"vsubpd		%%ymm8 ,%%ymm1 ,%%ymm1 		\n\t		vsubpd		%%ymm13,%%ymm3 ,%%ymm3 		\n\t"\
		"vmovaps	%%ymm4 ,    (%%r10)			\n\t		vmovaps	%%ymm2 ,    (%%r12)			\n\t"\
		"vmovaps	%%ymm0 ,    (%%rbx)			\n\t		vmovaps	%%ymm7 ,    (%%rdx)			\n\t"\
		"vmovaps	%%ymm5 ,0x20(%%r10)			\n\t		vmovaps	%%ymm6 ,0x20(%%r12)			\n\t"\
		"vmovaps	%%ymm1 ,0x20(%%r11)			\n\t		vmovaps	%%ymm3 ,0x20(%%r13)			\n\t"\
		"vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t		vmulpd		(%%r9),%%ymm10,%%ymm10		\n\t"\
		"vmulpd		(%%r9),%%ymm9 ,%%ymm9 		\n\t		vmulpd		(%%r9),%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm8 ,%%ymm8 		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm4 ,%%ymm14,%%ymm14		\n\t		vaddpd		%%ymm2 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm0 ,%%ymm9 ,%%ymm9 		\n\t		vaddpd		%%ymm7 ,%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm5 ,%%ymm15,%%ymm15		\n\t		vaddpd		%%ymm6 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm1 ,%%ymm8 ,%%ymm8 		\n\t		vaddpd		%%ymm3 ,%%ymm13,%%ymm13		\n\t"\
		"vmovaps	%%ymm14,    (%%rax)			\n\t		vmovaps	%%ymm10,    (%%rcx)			\n\t"\
		"vmovaps	%%ymm9 ,    (%%r11)			\n\t		vmovaps	%%ymm11,    (%%r13)			\n\t"\
		"vmovaps	%%ymm15,0x20(%%rax)			\n\t		vmovaps	%%ymm12,0x20(%%rcx)			\n\t"\
		"vmovaps	%%ymm8 ,0x20(%%rbx)			\n\t		vmovaps	%%ymm13,0x20(%%rdx)			\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
	/*...Block 1: t00,t10,t20,t30 in r00,04,02,06 */		/*...Block 5: t08,t18,t28,t38	*/\
		"movq	%[__r00],%%rsi					\n\t"\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x0(%%rdi),%%rax	/* p00 */	\n\t		movslq	0x10(%%rdi),%%r10	/* p04 */	\n\t"\
		"movslq	0x4(%%rdi),%%rbx	/* p01 */	\n\t		movslq	0x14(%%rdi),%%r11	/* p05 */	\n\t"\
		"movslq	0x8(%%rdi),%%rcx	/* p02 */	\n\t		movslq	0x18(%%rdi),%%r12	/* p06 */	\n\t"\
		"movslq	0xc(%%rdi),%%rdx	/* p03 */	\n\t		movslq	0x1c(%%rdi),%%r13	/* p07 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"													movq	%[__isrt2],%%rdi				\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vsubpd	0x80(%%rsi),%%ymm0,%%ymm0		\n\t		vsubpd	0x160(%%rsi),%%ymm12,%%ymm12		\n\t"\
		"vsubpd	0xc0(%%rsi),%%ymm4,%%ymm4		\n\t		vaddpd	0x140(%%rsi),%%ymm13,%%ymm13		\n\t"\
		"vsubpd	0xa0(%%rsi),%%ymm1,%%ymm1		\n\t		vaddpd	0x1e0(%%rsi),%%ymm14,%%ymm14		\n\t"\
		"vsubpd	0xe0(%%rsi),%%ymm5,%%ymm5		\n\t		vsubpd	0x1c0(%%rsi),%%ymm15,%%ymm15		\n\t"\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t		vmulpd	    (%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t		vmulpd	    (%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t		vmulpd	    (%%rdi),%%ymm14,%%ymm14		\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1a0(%%rsi),%%ymm11			\n\t		vmulpd	    (%%rdi),%%ymm15,%%ymm15		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vsubpd	0x1a0(%%rsi),%%ymm8 ,%%ymm8 		\n\t"\
		"vaddpd	0x40(%%rsi),%%ymm6,%%ymm6		\n\t		vsubpd	0x180(%%rsi),%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm10,%%ymm10		\n\t"\
		"vaddpd	0x60(%%rsi),%%ymm7,%%ymm7		\n\t		vaddpd	0x100(%%rsi),%%ymm11,%%ymm11		\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm13,%%ymm10,%%ymm10		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vsubpd		%%ymm15,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vsubpd		%%ymm14,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm2,%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm3,%%ymm7,%%ymm7		\n\t		vmovaps	%%ymm8 ,    (%%r11)			\n\t"\
		"vaddpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vmovaps	%%ymm10,0x20(%%r11)			\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm11,    (%%r12)			\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r13)			\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vaddpd		%%ymm10,%%ymm13,%%ymm13		\n\t"\
		"													vaddpd		%%ymm11,%%ymm15,%%ymm15		\n\t"\
		"													vaddpd		%%ymm9 ,%%ymm14,%%ymm14		\n\t"\
		"													vmovaps	%%ymm12,    (%%r10)			\n\t"\
		"													vmovaps	%%ymm13,0x20(%%r10)			\n\t"\
		"													vmovaps	%%ymm15,    (%%r13)			\n\t"\
		"													vmovaps	%%ymm14,0x20(%%r12)			\n\t"\
		/*...Block 3: t04,t14,t24,t34	*/					/*...Block 7: t0C,t1C,t2C,t3C	*/\
		"addq	$0x400,%%rsi		/* r20 */	\n\t"		/* r28 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x20(%%rdi),%%rax	/* p08 */	\n\t		movslq	0x30(%%rdi),%%r10	/* p0c */	\n\t"\
		"movslq	0x24(%%rdi),%%rbx	/* p09 */	\n\t		movslq	0x34(%%rdi),%%r11	/* p0d */	\n\t"\
		"movslq	0x28(%%rdi),%%rcx	/* p0a */	\n\t		movslq	0x38(%%rdi),%%r12	/* p0e */	\n\t"\
		"movslq	0x2c(%%rdi),%%rdx	/* p0b */	\n\t		movslq	0x3c(%%rdi),%%r13	/* p0f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x20,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmulpd	    (%%rdi),%%ymm4,%%ymm4		\n\t		vmulpd	0x20(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm6,%%ymm6		\n\t		vmulpd	    (%%rdi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	    (%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x20(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm5,%%ymm5		\n\t		vmulpd	0x20(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm7,%%ymm7		\n\t		vmulpd	    (%%rdi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	    (%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x20(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm9 ,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vsubpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm8 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"subq	$0x20,%%rdi						\n\t"/* isrt2 */\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm3			\n\t		vmovaps	0x1a0(%%rsi),%%ymm11			\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vsubpd	0xa0(%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x1a0(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x80(%%rsi),%%ymm3,%%ymm3		\n\t		vsubpd	0x180(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vaddpd		%%ymm4,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
		"vaddpd		%%ymm2,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 2: t02,t12,t22,t32	*/					/*...Block 6: t0A,t1A,t2A,t3A	*/\
		"subq	$0x200,%%rsi		/* r10 */	\n\t"		/* r18 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x40(%%rdi),%%rax	/* p10 */	\n\t		movslq	0x50(%%rdi),%%r10	/* p14 */	\n\t"\
		"movslq	0x44(%%rdi),%%rbx	/* p11 */	\n\t		movslq	0x54(%%rdi),%%r11	/* p15 */	\n\t"\
		"movslq	0x48(%%rdi),%%rcx	/* p12 */	\n\t		movslq	0x58(%%rdi),%%r12	/* p16 */	\n\t"\
		"movslq	0x4c(%%rdi),%%rdx	/* p13 */	\n\t		movslq	0x5c(%%rdi),%%r13	/* p17 */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x20,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmulpd	0x40(%%rdi),%%ymm4,%%ymm4		\n\t		vmulpd	0xa0(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	0x80(%%rdi),%%ymm6,%%ymm6		\n\t		vmulpd	0x40(%%rdi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	0x60(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	0x80(%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0xa0(%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x60(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x40(%%rdi),%%ymm5,%%ymm5		\n\t		vmulpd	0xa0(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x80(%%rdi),%%ymm7,%%ymm7		\n\t		vmulpd	0x40(%%rdi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	0x60(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	0x80(%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	0xa0(%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x60(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm9 ,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm8 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vsubpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vmulpd	    (%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x20(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	    (%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	    (%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x20(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	    (%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm4,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm5,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2		\n\t		vaddpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm6,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm5,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm7,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm4,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
		"vaddpd		%%ymm2,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm6,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm5,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm7,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm4,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		/*...Block 4: t06,t16,t26,t36	*/					/*...Block 8: t0E,t1E,t2E,t3E	*/\
		"addq	$0x400,%%rsi		/* r30 */	\n\t"		/* r38 */\
		"movq	%[__arr_offsets],%%rdi			\n\t"\
		"movslq	0x60(%%rdi),%%rax	/* p18 */	\n\t		movslq	0x70(%%rdi),%%r10	/* p1c */	\n\t"\
		"movslq	0x64(%%rdi),%%rbx	/* p19 */	\n\t		movslq	0x74(%%rdi),%%r11	/* p1d */	\n\t"\
		"movslq	0x68(%%rdi),%%rcx	/* p1a */	\n\t		movslq	0x78(%%rdi),%%r12	/* p1e */	\n\t"\
		"movslq	0x6c(%%rdi),%%rdx	/* p1b */	\n\t		movslq	0x7c(%%rdi),%%r13	/* p1f */	\n\t"\
		"movq	%[__add],%%rdi					\n\t"\
		"addq	%%rdi,%%rax						\n\t		addq	%%rdi,%%r10						\n\t"\
		"addq	%%rdi,%%rbx						\n\t		addq	%%rdi,%%r11						\n\t"\
		"addq	%%rdi,%%rcx						\n\t		addq	%%rdi,%%r12						\n\t"\
		"addq	%%rdi,%%rdx						\n\t		addq	%%rdi,%%r13						\n\t"\
		"movq	%[__isrt2],%%rdi				\n\t"\
		"addq	$0x20,%%rdi						\n\t"/* cc0 */\
		"vmovaps	0x40(%%rsi),%%ymm4			\n\t		vmovaps	0x140(%%rsi),%%ymm12			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm6			\n\t		vmovaps	0x1c0(%%rsi),%%ymm14			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm5			\n\t		vmovaps	0x160(%%rsi),%%ymm13			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm7			\n\t		vmovaps	0x1e0(%%rsi),%%ymm15			\n\t"\
		"vmovaps	0x40(%%rsi),%%ymm0			\n\t		vmovaps	0x140(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0xc0(%%rsi),%%ymm2			\n\t		vmovaps	0x1c0(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0x60(%%rsi),%%ymm1			\n\t		vmovaps	0x160(%%rsi),%%ymm9 			\n\t"\
		"vmovaps	0xe0(%%rsi),%%ymm3			\n\t		vmovaps	0x1e0(%%rsi),%%ymm11			\n\t"\
		"vmulpd	0x80(%%rdi),%%ymm4,%%ymm4		\n\t		vmulpd	0x60(%%rdi),%%ymm12,%%ymm12		\n\t"\
		"vmulpd	0x60(%%rdi),%%ymm6,%%ymm6		\n\t		vmulpd	0xa0(%%rdi),%%ymm14,%%ymm14		\n\t"\
		"vmulpd	0xa0(%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	0x40(%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vmulpd	0x40(%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	0x80(%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	0x80(%%rdi),%%ymm5,%%ymm5		\n\t		vmulpd	0x60(%%rdi),%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x60(%%rdi),%%ymm7,%%ymm7		\n\t		vmulpd	0xa0(%%rdi),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	0xa0(%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	0x40(%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd	0x40(%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	0x80(%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm1,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm9 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm3,%%ymm6,%%ymm6		\n\t		vsubpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vaddpd		%%ymm0,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm8 ,%%ymm13,%%ymm13		\n\t"\
		"vsubpd		%%ymm2,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vmovaps	0x80(%%rsi),%%ymm2			\n\t		vmovaps	0x180(%%rsi),%%ymm10			\n\t"\
		"vmovaps	0xa0(%%rsi),%%ymm0			\n\t		vmovaps	0x1a0(%%rsi),%%ymm8 			\n\t"\
		"vmovaps		%%ymm2,%%ymm1			\n\t		vmovaps		%%ymm10,%%ymm9 				\n\t"\
		"vmovaps		%%ymm0,%%ymm3			\n\t		vmovaps		%%ymm8 ,%%ymm11				\n\t"\
		"vsubpd		%%ymm6,%%ymm4,%%ymm4		\n\t		vsubpd		%%ymm14,%%ymm12,%%ymm12		\n\t"\
		"vsubpd		%%ymm7,%%ymm5,%%ymm5		\n\t		vsubpd		%%ymm15,%%ymm13,%%ymm13		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm2,%%ymm2		\n\t		vmulpd	    (%%rdi),%%ymm10,%%ymm10		\n\t"\
		"vmulpd	    (%%rdi),%%ymm0,%%ymm0		\n\t		vmulpd	0x20(%%rdi),%%ymm8 ,%%ymm8 		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd	0x20(%%rdi),%%ymm3,%%ymm3		\n\t		vmulpd	    (%%rdi),%%ymm11,%%ymm11		\n\t"\
		"vmulpd	    (%%rdi),%%ymm1,%%ymm1		\n\t		vmulpd	0x20(%%rdi),%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd		%%ymm4,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm12,%%ymm14,%%ymm14		\n\t"\
		"vsubpd		%%ymm0,%%ymm2,%%ymm2		\n\t		vaddpd		%%ymm8 ,%%ymm10,%%ymm10		\n\t"\
		"vaddpd		%%ymm5,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm13,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm1,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm9 ,%%ymm11,%%ymm11		\n\t"\
		"vmovaps	    (%%rsi),%%ymm0			\n\t		vmovaps	0x100(%%rsi),%%ymm8 			\n\t"\
		"vmovaps	0x20(%%rsi),%%ymm1			\n\t		vmovaps	0x120(%%rsi),%%ymm9 			\n\t"\
		"vsubpd		%%ymm2,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm10,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm3,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm11,%%ymm9 ,%%ymm9 		\n\t"\
		"vaddpd	    (%%rsi),%%ymm2,%%ymm2		\n\t		vaddpd	0x100(%%rsi),%%ymm10,%%ymm10	\n\t"\
		"vaddpd	0x20(%%rsi),%%ymm3,%%ymm3		\n\t		vaddpd	0x120(%%rsi),%%ymm11,%%ymm11	\n\t"\
		"vsubpd		%%ymm4,%%ymm2,%%ymm2		\n\t		vsubpd		%%ymm12,%%ymm8 ,%%ymm8 		\n\t"\
		"vsubpd		%%ymm7,%%ymm0,%%ymm0		\n\t		vsubpd		%%ymm15,%%ymm10,%%ymm10		\n\t"\
		"vsubpd		%%ymm5,%%ymm3,%%ymm3		\n\t		vsubpd		%%ymm13,%%ymm9 ,%%ymm9 		\n\t"\
		"vsubpd		%%ymm6,%%ymm1,%%ymm1		\n\t		vsubpd		%%ymm14,%%ymm11,%%ymm11		\n\t"\
		"vmulpd		(%%r9),%%ymm4,%%ymm4		\n\t		vmulpd		(%%r9),%%ymm12,%%ymm12		\n\t"\
		"vmulpd		(%%r9),%%ymm7,%%ymm7		\n\t		vmulpd		(%%r9),%%ymm15,%%ymm15		\n\t"\
		"vmulpd		(%%r9),%%ymm5,%%ymm5		\n\t		vmulpd		(%%r9),%%ymm13,%%ymm13		\n\t"\
		"vmulpd		(%%r9),%%ymm6,%%ymm6		\n\t		vmulpd		(%%r9),%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm2,    (%%rbx)			\n\t		vmovaps	%%ymm8 ,    (%%r11)				\n\t"\
		"vmovaps	%%ymm0,    (%%rcx)			\n\t		vmovaps	%%ymm10,    (%%r12)				\n\t"\
		"vmovaps	%%ymm3,0x20(%%rbx)			\n\t		vmovaps	%%ymm9 ,0x20(%%r11)				\n\t"\
		"vmovaps	%%ymm1,0x20(%%rdx)			\n\t		vmovaps	%%ymm11,0x20(%%r13)				\n\t"\
		"vaddpd		%%ymm2,%%ymm4,%%ymm4		\n\t		vaddpd		%%ymm8 ,%%ymm12,%%ymm12		\n\t"\
		"vaddpd		%%ymm0,%%ymm7,%%ymm7		\n\t		vaddpd		%%ymm10,%%ymm15,%%ymm15		\n\t"\
		"vaddpd		%%ymm3,%%ymm5,%%ymm5		\n\t		vaddpd		%%ymm9 ,%%ymm13,%%ymm13		\n\t"\
		"vaddpd		%%ymm1,%%ymm6,%%ymm6		\n\t		vaddpd		%%ymm11,%%ymm14,%%ymm14		\n\t"\
		"vmovaps	%%ymm4,    (%%rax)			\n\t		vmovaps	%%ymm12,    (%%r10)				\n\t"\
		"vmovaps	%%ymm7,    (%%rdx)			\n\t		vmovaps	%%ymm15,    (%%r13)				\n\t"\
		"vmovaps	%%ymm5,0x20(%%rax)			\n\t		vmovaps	%%ymm13,0x20(%%r10)				\n\t"\
		"vmovaps	%%ymm6,0x20(%%rcx)			\n\t		vmovaps	%%ymm14,0x20(%%r12)				\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","r9","r10","r11","r12","r13","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"	/* Clobbered registers */\
	);\
	}

#elif defined(USE_SSE2)

	/* Both DIF and DIT macros assume needed roots of unity laid out in memory relative to the input pointer isrt2 like so:
		Description																	Address
		---------------------------------------------------------					----------
		isrt2: 1/sqrt2, in terms of which thr basic 8th root of 1 = (1+i)/sqrt2		isrt2
		cc0: Real part of basic 16th root of 1 = c16								isrt2 + 0x10
		ss0: Real part of basic 16th root of 1 = s16								isrt2 + 0x20
		cc1: Real part of basic 32th root of 1 = c32_1								isrt2 + 0x30
		ss1: Real part of basic 32th root of 1 = s32_1								isrt2 + 0x40
		cc3: Real part of 3rd   32th root of 1 = c32_3								isrt2 + 0x50
		ss3: Real part of 3rd   32th root of 1 = s32_3								isrt2 + 0x60
	*/
	#define SSE2_RADIX32_DIT_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
	/*...Block 1: */\
		"movq	%[__r00],%%rsi	\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		/* Can't simply addq these to a 64-bit base array address since the offsets are 4-byte array data: */\
		"movslq	0x0(%%rdi),%%rax	\n\t"/* p00 */\
		"movslq	0x4(%%rdi),%%rbx	\n\t"/* p01 */\
		"movslq	0x8(%%rdi),%%rcx	\n\t"/* p02 */\
		"movslq	0xc(%%rdi),%%rdx	\n\t"/* p03 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"/* add0 + p00 */\
		"addq	%%rdi,%%rbx	\n\t"/* add0 + p01 */\
		"addq	%%rdi,%%rcx	\n\t"/* add0 + p02 */\
		"addq	%%rdi,%%rdx	\n\t"/* add0 + p03 */\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0, add1, add2, add3, r00): */\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm2,0x60(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"movaps	%%xmm7,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4, add5, add6, add7, r08): */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x10(%%rdi),%%rax	\n\t"/* p04 */\
		"movslq	0x14(%%rdi),%%rbx	\n\t"/* p05 */\
		"movslq	0x18(%%rdi),%%rcx	\n\t"/* p06 */\
		"movslq	0x1c(%%rdi),%%rdx	\n\t"/* p07 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm5	\n\t"/* isrt2 */\
		"movaps	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm6,%%xmm1		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"subpd	%%xmm2,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"mulpd	%%xmm5,%%xmm3		\n\t"\
		"mulpd	%%xmm5,%%xmm6		\n\t"\
		"mulpd	%%xmm5,%%xmm0		\n\t"\
		"mulpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		"movaps	%%xmm0,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x60(%%rsi)	\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r00,r02,r04,r06,r08,r0A,r0C,r0E): */\
		"movaps		-0x80(%%rsi),%%xmm0	\n\t"/* r00 */\
		"movaps		-0x40(%%rsi),%%xmm4	\n\t"/* r04 */\
		"movaps		-0x70(%%rsi),%%xmm1	\n\t"/* r01 */\
		"movaps		-0x30(%%rsi),%%xmm5	\n\t"/* r05 */\
		"movaps		     (%%rsi),%%xmm2	\n\t"/* r08 */\
		"movaps		 0x50(%%rsi),%%xmm7	\n\t"/* r0D */\
		"movaps		 0x10(%%rsi),%%xmm3	\n\t"/* r09 */\
		"movaps		 0x40(%%rsi),%%xmm6	\n\t"/* r0C */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0,     (%%rsi)\n\t"\
		"movaps		%%xmm4, 0x40(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x10(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x30(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x80(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x40(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x70(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x50(%%rsi)\n\t"\
		"\n\t"\
		"movaps		-0x60(%%rsi),%%xmm0	\n\t"/* r02 */\
		"movaps		-0x20(%%rsi),%%xmm4	\n\t"/* r06 */\
		"movaps		-0x50(%%rsi),%%xmm1	\n\t"/* r03 */\
		"movaps		-0x10(%%rsi),%%xmm5	\n\t"/* r07 */\
		"movaps		 0x20(%%rsi),%%xmm2	\n\t"/* r0A */\
		"movaps		 0x70(%%rsi),%%xmm7	\n\t"/* r0F */\
		"movaps		 0x30(%%rsi),%%xmm3	\n\t"/* r0B */\
		"movaps		 0x60(%%rsi),%%xmm6	\n\t"/* r0E */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0, 0x20(%%rsi)\n\t"\
		"movaps		%%xmm4, 0x60(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x30(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x10(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x60(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x20(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x50(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x70(%%rsi)\n\t"\
		"\n\t"\
	/*...Block 2: */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x20(%%rdi),%%rax	\n\t"/* p08 */\
		"movslq	0x24(%%rdi),%%rbx	\n\t"/* p09 */\
		"movslq	0x28(%%rdi),%%rcx	\n\t"/* p0a */\
		"movslq	0x2c(%%rdi),%%rdx	\n\t"/* p0b */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0, add1, add2, add3, r10): */\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm2,0x60(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"movaps	%%xmm7,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4, add5, add6, add7, r18): */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x30(%%rdi),%%rax	\n\t"/* p0c */\
		"movslq	0x34(%%rdi),%%rbx	\n\t"/* p0d */\
		"movslq	0x38(%%rdi),%%rcx	\n\t"/* p0e */\
		"movslq	0x3c(%%rdi),%%rdx	\n\t"/* p0f */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm5	\n\t"/* isrt2 */\
		"movaps	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm6,%%xmm1		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"subpd	%%xmm2,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"mulpd	%%xmm5,%%xmm3		\n\t"\
		"mulpd	%%xmm5,%%xmm6		\n\t"\
		"mulpd	%%xmm5,%%xmm0		\n\t"\
		"mulpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		"movaps	%%xmm0,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x60(%%rsi)	\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r10,r12,r14,r16,r18,r1A,r1C,r1E): */\
		"movaps		-0x80(%%rsi),%%xmm0	\n\t"/* r10 */\
		"movaps		-0x40(%%rsi),%%xmm4	\n\t"/* r14 */\
		"movaps		-0x70(%%rsi),%%xmm1	\n\t"/* r11 */\
		"movaps		-0x30(%%rsi),%%xmm5	\n\t"/* r15 */\
		"movaps		     (%%rsi),%%xmm2	\n\t"/* r18 */\
		"movaps		 0x50(%%rsi),%%xmm7	\n\t"/* r1D */\
		"movaps		 0x10(%%rsi),%%xmm3	\n\t"/* r19 */\
		"movaps		 0x40(%%rsi),%%xmm6	\n\t"/* r1C */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0,     (%%rsi)\n\t"\
		"movaps		%%xmm4, 0x40(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x10(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x30(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x80(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x40(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x70(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x50(%%rsi)\n\t"\
		"\n\t"\
		"movaps		-0x60(%%rsi),%%xmm0	\n\t"/* r12 */\
		"movaps		-0x20(%%rsi),%%xmm4	\n\t"/* r16 */\
		"movaps		-0x50(%%rsi),%%xmm1	\n\t"/* r13 */\
		"movaps		-0x10(%%rsi),%%xmm5	\n\t"/* r17 */\
		"movaps		 0x20(%%rsi),%%xmm2	\n\t"/* r1A */\
		"movaps		 0x70(%%rsi),%%xmm7	\n\t"/* r1F */\
		"movaps		 0x30(%%rsi),%%xmm3	\n\t"/* r1B */\
		"movaps		 0x60(%%rsi),%%xmm6	\n\t"/* r1E */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0, 0x20(%%rsi)\n\t"\
		"movaps		%%xmm4, 0x60(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x30(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x10(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x60(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x20(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x50(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x70(%%rsi)\n\t"\
		"\n\t"\
	/*...Block 3: */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x40(%%rdi),%%rax	\n\t"/* p10 */\
		"movslq	0x44(%%rdi),%%rbx	\n\t"/* p11 */\
		"movslq	0x48(%%rdi),%%rcx	\n\t"/* p12 */\
		"movslq	0x4c(%%rdi),%%rdx	\n\t"/* p13 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0, add1, add2, add3, r20): */\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm2,0x60(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"movaps	%%xmm7,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4, add5, add6, add7, r28): */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x50(%%rdi),%%rax	\n\t"/* p14 */\
		"movslq	0x54(%%rdi),%%rbx	\n\t"/* p15 */\
		"movslq	0x58(%%rdi),%%rcx	\n\t"/* p16 */\
		"movslq	0x5c(%%rdi),%%rdx	\n\t"/* p17 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm5	\n\t"/* isrt2 */\
		"movaps	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm6,%%xmm1		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"subpd	%%xmm2,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"mulpd	%%xmm5,%%xmm3		\n\t"\
		"mulpd	%%xmm5,%%xmm6		\n\t"\
		"mulpd	%%xmm5,%%xmm0		\n\t"\
		"mulpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		"movaps	%%xmm0,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x60(%%rsi)	\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r20,r22,r24,r26,r28,r2A,r2C,r2E): */\
		"movaps		-0x80(%%rsi),%%xmm0	\n\t"/* r20 */\
		"movaps		-0x40(%%rsi),%%xmm4	\n\t"/* r24 */\
		"movaps		-0x70(%%rsi),%%xmm1	\n\t"/* r21 */\
		"movaps		-0x30(%%rsi),%%xmm5	\n\t"/* r25 */\
		"movaps		     (%%rsi),%%xmm2	\n\t"/* r28 */\
		"movaps		 0x50(%%rsi),%%xmm7	\n\t"/* r2D */\
		"movaps		 0x10(%%rsi),%%xmm3	\n\t"/* r29 */\
		"movaps		 0x40(%%rsi),%%xmm6	\n\t"/* r2C */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0,     (%%rsi)\n\t"\
		"movaps		%%xmm4, 0x40(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x10(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x30(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x80(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x40(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x70(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x50(%%rsi)\n\t"\
		"\n\t"\
		"movaps		-0x60(%%rsi),%%xmm0	\n\t"/* r22 */\
		"movaps		-0x20(%%rsi),%%xmm4	\n\t"/* r26 */\
		"movaps		-0x50(%%rsi),%%xmm1	\n\t"/* r23 */\
		"movaps		-0x10(%%rsi),%%xmm5	\n\t"/* r27 */\
		"movaps		 0x20(%%rsi),%%xmm2	\n\t"/* r2A */\
		"movaps		 0x70(%%rsi),%%xmm7	\n\t"/* r2F */\
		"movaps		 0x30(%%rsi),%%xmm3	\n\t"/* r2B */\
		"movaps		 0x60(%%rsi),%%xmm6	\n\t"/* r2E */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0, 0x20(%%rsi)\n\t"\
		"movaps		%%xmm4, 0x60(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x30(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x10(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x60(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x20(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x50(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x70(%%rsi)\n\t"\
		"\n\t"\
	/*...Block 4: */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x60(%%rdi),%%rax	\n\t"/* p18 */\
		"movslq	0x64(%%rdi),%%rbx	\n\t"/* p19 */\
		"movslq	0x68(%%rdi),%%rcx	\n\t"/* p1a */\
		"movslq	0x6c(%%rdi),%%rdx	\n\t"/* p1b */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE(add0, add1, add2, add3, r30): */\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm2,0x60(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"movaps	%%xmm7,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		/* SSE2_RADIX4_DIT_0TWIDDLE_2NDOFTWO(add4, add5, add6, add7, r38): */\
		"addq	$0x80,%%rsi			\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x70(%%rdi),%%rax	\n\t"/* p1c */\
		"movslq	0x74(%%rdi),%%rbx	\n\t"/* p1d */\
		"movslq	0x78(%%rdi),%%rcx	\n\t"/* p1e */\
		"movslq	0x7c(%%rdi),%%rdx	\n\t"/* p1f */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	    (%%rdx),%%xmm4	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"movaps	0x10(%%rdx),%%xmm5	\n\t"\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"subpd	%%xmm0,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm1		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm5		\n\t"\
		"\n\t"\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,0x40(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x50(%%rsi)	\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"movaps	%%xmm4,    (%%rsi)	\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm5,0x10(%%rsi)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm5	\n\t"/* isrt2 */\
		"movaps	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm6,%%xmm1		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"subpd	%%xmm2,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"mulpd	%%xmm5,%%xmm3		\n\t"\
		"mulpd	%%xmm5,%%xmm6		\n\t"\
		"mulpd	%%xmm5,%%xmm0		\n\t"\
		"mulpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,0x30(%%rsi)	\n\t"\
		"movaps	%%xmm6,0x70(%%rsi)	\n\t"\
		"movaps	%%xmm0,0x20(%%rsi)	\n\t"\
		"movaps	%%xmm1,0x60(%%rsi)	\n\t"\
		/* SSE2_RADIX8_DIT_COMBINE_RAD4_SUBS(r30,r32,r34,r36,r38,r3A,r3C,r3E): */\
		"movaps		-0x80(%%rsi),%%xmm0	\n\t"/* r30 */\
		"movaps		-0x40(%%rsi),%%xmm4	\n\t"/* r34 */\
		"movaps		-0x70(%%rsi),%%xmm1	\n\t"/* r31 */\
		"movaps		-0x30(%%rsi),%%xmm5	\n\t"/* r35 */\
		"movaps		     (%%rsi),%%xmm2	\n\t"/* r38 */\
		"movaps		 0x50(%%rsi),%%xmm7	\n\t"/* r3D */\
		"movaps		 0x10(%%rsi),%%xmm3	\n\t"/* r39 */\
		"movaps		 0x40(%%rsi),%%xmm6	\n\t"/* r3C */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0,     (%%rsi)\n\t"\
		"movaps		%%xmm4, 0x40(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x10(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x30(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x80(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x40(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x70(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x50(%%rsi)\n\t"\
		"\n\t"\
		"movaps		-0x60(%%rsi),%%xmm0	\n\t"/* r32 */\
		"movaps		-0x20(%%rsi),%%xmm4	\n\t"/* r36 */\
		"movaps		-0x50(%%rsi),%%xmm1	\n\t"/* r33 */\
		"movaps		-0x10(%%rsi),%%xmm5	\n\t"/* r37 */\
		"movaps		 0x20(%%rsi),%%xmm2	\n\t"/* r3A */\
		"movaps		 0x70(%%rsi),%%xmm7	\n\t"/* r3F */\
		"movaps		 0x30(%%rsi),%%xmm3	\n\t"/* r3B */\
		"movaps		 0x60(%%rsi),%%xmm6	\n\t"/* r3E */\
		"subpd		%%xmm2,%%xmm0\n\t"\
		"subpd		%%xmm7,%%xmm4\n\t"\
		"subpd		%%xmm3,%%xmm1\n\t"\
		"subpd		%%xmm6,%%xmm5\n\t"\
		"addpd		%%xmm2,%%xmm2\n\t"\
		"addpd		%%xmm7,%%xmm7\n\t"\
		"addpd		%%xmm3,%%xmm3\n\t"\
		"addpd		%%xmm6,%%xmm6\n\t"\
		"addpd		%%xmm0,%%xmm2\n\t"\
		"addpd		%%xmm4,%%xmm7\n\t"\
		"addpd		%%xmm1,%%xmm3\n\t"\
		"addpd		%%xmm5,%%xmm6\n\t"\
		"movaps		%%xmm0, 0x20(%%rsi)\n\t"\
		"movaps		%%xmm4, 0x60(%%rsi)\n\t"\
		"movaps		%%xmm1, 0x30(%%rsi)\n\t"\
		"movaps		%%xmm5,-0x10(%%rsi)\n\t"\
		"movaps		%%xmm2,-0x60(%%rsi)\n\t"\
		"movaps		%%xmm7,-0x20(%%rsi)\n\t"\
		"movaps		%%xmm3,-0x50(%%rsi)\n\t"\
		"movaps		%%xmm6, 0x70(%%rsi)\n\t"\
		"\n\t"\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors:	*/\
		/*...Block 1: r00,r10,r20,r30	*/\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"leaq	0x10(%%rdi),%%rsi	\n\t/* cc0 */"\
		"movq	%[__r00],%%rax	\n\t"\
		"movq	%%rax,%%rbx		\n\t"\
		"movq	%%rax,%%rcx		\n\t"\
		"movq	%%rax,%%rdx		\n\t"\
		"addq	$0x100,%%rbx	\n\t"\
		"addq	$0x200,%%rcx	\n\t"\
		"addq	$0x300,%%rdx	\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0		\n\t"\
		"movaps	0x10(%%rax),%%xmm1		\n\t"\
		"movaps	    (%%rbx),%%xmm2		\n\t"\
		"movaps	0x10(%%rbx),%%xmm3		\n\t"\
		"subpd	    (%%rbx),%%xmm0		\n\t"\
		"subpd	0x10(%%rbx),%%xmm1		\n\t"\
		"addpd	    (%%rax),%%xmm2		\n\t"\
		"addpd	0x10(%%rax),%%xmm3		\n\t"\
		"movaps	    (%%rcx),%%xmm4		\n\t"\
		"movaps	0x10(%%rcx),%%xmm5		\n\t"\
		"movaps	    (%%rdx),%%xmm6		\n\t"\
		"movaps	0x10(%%rdx),%%xmm7		\n\t"\
		"subpd	    (%%rdx),%%xmm4		\n\t"\
		"subpd	0x10(%%rdx),%%xmm5		\n\t"\
		"addpd	    (%%rcx),%%xmm6		\n\t"\
		"addpd	0x10(%%rcx),%%xmm7		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm2,xmm3,xmm0,xmm1,xmm6,xmm7,xmm4,xmm5): swap xmm[01]<->[23], xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm2		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm0		\n\t"\
		"subpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rdx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 5: r08,r18,r28,r38	*/\
		"\n\t"\
		"addq	$0x80,%%rax		\n\t"/* r08 */\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"movaps	(%%rdi),%%xmm2	\n\t"/* isrt2 */\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"addpd	0x10(%%rcx),%%xmm4	\n\t"\
		"subpd	    (%%rcx),%%xmm5	\n\t"\
		"subpd	0x10(%%rdx),%%xmm0	\n\t"\
		"addpd	    (%%rdx),%%xmm1	\n\t"\
		"mulpd	%%xmm2,%%xmm4		\n\t"\
		"mulpd	%%xmm2,%%xmm5		\n\t"\
		"mulpd	%%xmm2,%%xmm0		\n\t"\
		"mulpd	%%xmm2,%%xmm1		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"\n\t"\
		"subpd	%%xmm0,%%xmm4		\n\t"\
		"subpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"subpd	0x10(%%rbx),%%xmm0	\n\t"\
		"subpd	    (%%rbx),%%xmm1	\n\t"\
		"addpd	    (%%rax),%%xmm3	\n\t"\
		"addpd	0x10(%%rax),%%xmm2	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm3,xmm1,xmm0,xmm2,xmm4,xmm5,xmm6,xmm7): swap xmm0123<->3102 */\
		"addpd	%%xmm4,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,    (%%rax)	\n\t"\
		"movaps	%%xmm1,0x10(%%rax)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm3,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"subpd	%%xmm6,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm2,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"subpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rdx)	\n\t"\
		"movaps	%%xmm2,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 3: r04,r14,r24,r34	*/\
		"\n\t"\
		"subq	$0x40,%%rax 	\n\t"/* r04 */\
		"subq	$0x40,%%rbx		\n\t"\
		"subq	$0x40,%%rcx		\n\t"\
		"subq	$0x40,%%rdx		\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	    (%%rsi),%%xmm4	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm0	\n\t"\
		"mulpd	    (%%rsi),%%xmm5	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm6	\n\t"\
		"mulpd	    (%%rsi),%%xmm2	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm7	\n\t"\
		"mulpd	    (%%rsi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"subpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"subpd	%%xmm0,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"addpd	0x10(%%rbx),%%xmm2	\n\t"\
		"subpd	    (%%rbx),%%xmm3	\n\t"\
		"mulpd	    (%%rdi),%%xmm2		\n\t"\
		"mulpd	    (%%rdi),%%xmm3		\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm2,xmm3,xmm0,xmm1,xmm4,xmm5,xmm6,xmm7): swap xmm[01]<->[23] */\
		"addpd	%%xmm4,%%xmm2		\n\t"\
		"addpd	%%xmm5,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"subpd	%%xmm4,%%xmm2		\n\t"\
		"subpd	%%xmm5,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm7,%%xmm0		\n\t"\
		"subpd	%%xmm6,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"subpd	%%xmm7,%%xmm0		\n\t"\
		"addpd	%%xmm6,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rdx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 7: r0C,r1C,r2C,r3C	*/\
		"\n\t"\
		"addq	$0x80,%%rax 	\n\t"/* r0C */\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	0x10(%%rsi),%%xmm4	\n\t"\
		"mulpd	    (%%rsi),%%xmm0	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm5	\n\t"\
		"mulpd	    (%%rsi),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm6	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm2	\n\t"\
		"mulpd	    (%%rsi),%%xmm7	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"subpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"subpd	%%xmm0,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"subpd	0x10(%%rbx),%%xmm2	\n\t"\
		"addpd	    (%%rbx),%%xmm3	\n\t"\
		"mulpd	    (%%rdi),%%xmm2		\n\t"\
		"mulpd	    (%%rdi),%%xmm3		\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm0,xmm1,xmm2,xmm3,xmm6,xmm7,xmm4,xmm5): swap xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm0		\n\t"\
		"addpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rax)	\n\t"\
		"movaps	%%xmm1,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rbx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 2: r02,r12,r22,r32	*/\
		"\n\t"\
		"subq	$0xa0,%%rax 	\n\t"/* r02 */\
		"subq	$0xa0,%%rbx			\n\t"\
		"subq	$0xa0,%%rcx			\n\t"\
		"subq	$0xa0,%%rdx			\n\t"\
		"addq	$0x30,%%rdi \n\t"/* cc1 */\
		"addq	$0x40,%%rsi \n\t"/* cc3 */\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	    (%%rdi),%%xmm4	\n\t"\
		"mulpd	    (%%rsi),%%xmm0	\n\t"\
		"mulpd	    (%%rdi),%%xmm5	\n\t"\
		"mulpd	    (%%rsi),%%xmm1	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm6	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm2	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm7	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"subpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"subpd	%%xmm0,%%xmm4		\n\t"\
		"subpd	%%xmm1,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"subq	$0x40,%%rsi \n\t"/* cc0 */\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm2	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm3	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm0	\n\t"\
		"addpd	%%xmm1,%%xmm2		\n\t"\
		"subpd	%%xmm0,%%xmm3		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm2,xmm3,xmm0,xmm1,xmm6,xmm7,xmm4,xmm5): swap xmm[01]<->[23], xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm2		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm0		\n\t"\
		"subpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rdx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 6: r0A,r1A,r2A,r3A	*/\
		"\n\t"\
		"addq	$0x80,%%rax 	\n\t"/* r0A */\
		"addq	$0x80,%%rbx			\n\t"\
		"addq	$0x80,%%rcx			\n\t"\
		"addq	$0x80,%%rdx			\n\t"\
		"addq	$0x40,%%rsi \n\t"/* cc3 */\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	0x10(%%rsi),%%xmm4	\n\t"\
		"mulpd	    (%%rdi),%%xmm0	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm5	\n\t"\
		"mulpd	    (%%rdi),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm6	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm2	\n\t"\
		"mulpd	    (%%rsi),%%xmm7	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"subpd	%%xmm0,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"subq	$0x40,%%rsi \n\t"/* cc0 */\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm2	\n\t"\
		"mulpd	    (%%rsi),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm3	\n\t"\
		"mulpd	    (%%rsi),%%xmm0	\n\t"\
		"subpd	%%xmm1,%%xmm2		\n\t"\
		"addpd	%%xmm0,%%xmm3		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm0,xmm1,xmm2,xmm3,xmm6,xmm7,xmm4,xmm5): swap xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm0		\n\t"\
		"addpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rax)	\n\t"\
		"movaps	%%xmm1,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rbx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 4: r06,r16,r26,r36	*/\
		"\n\t"\
		"subq	$0x40,%%rax 	\n\t"/* r06 */\
		"subq	$0x40,%%rbx			\n\t"\
		"subq	$0x40,%%rcx			\n\t"\
		"subq	$0x40,%%rdx			\n\t"\
		"addq	$0x40,%%rsi \n\t"/* cc3 */\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	    (%%rsi),%%xmm4	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm0	\n\t"\
		"mulpd	    (%%rsi),%%xmm5	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm6	\n\t"\
		"mulpd	    (%%rdi),%%xmm2	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm7	\n\t"\
		"mulpd	    (%%rdi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"subpd	%%xmm0,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"subq	$0x40,%%rsi \n\t"/* cc0 */\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm2	\n\t"\
		"mulpd	    (%%rsi),%%xmm1	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm3	\n\t"\
		"mulpd	    (%%rsi),%%xmm0	\n\t"\
		"addpd	%%xmm1,%%xmm2		\n\t"\
		"subpd	%%xmm0,%%xmm3		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm2,xmm3,xmm0,xmm1,xmm6,xmm7,xmm4,xmm5): swap xmm[01]<->[23], xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm2		\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm0		\n\t"\
		"subpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm0		\n\t"\
		"addpd	%%xmm4,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rdx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"\
		"\n\t"\
		/*...Block 8: r0E,r1E,r2E,r3E	*/\
		"\n\t"\
		"addq	$0x80,%%rax 	\n\t"/* r0E */\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"addq	$0x40,%%rsi \n\t"/* cc3 */\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	    (%%rdx),%%xmm0	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	0x10(%%rdx),%%xmm1	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	    (%%rdx),%%xmm2	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"movaps	0x10(%%rdx),%%xmm3	\n\t"\
		"\n\t"\
		"mulpd	0x10(%%rdi),%%xmm4	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm0	\n\t"\
		"mulpd	0x10(%%rdi),%%xmm5	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm1	\n\t"\
		"mulpd	    (%%rdi),%%xmm6	\n\t"\
		"mulpd	    (%%rsi),%%xmm2	\n\t"\
		"mulpd	    (%%rdi),%%xmm7	\n\t"\
		"mulpd	    (%%rsi),%%xmm3	\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"subpd	%%xmm2,%%xmm1		\n\t"\
		"addpd	%%xmm7,%%xmm4		\n\t"\
		"addpd	%%xmm3,%%xmm0		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"movaps	%%xmm4,%%xmm6		\n\t"\
		"\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"subpd	%%xmm0,%%xmm6		\n\t"\
		"subpd	%%xmm1,%%xmm7		\n\t"\
		"\n\t"\
		"subq	$0x40,%%rsi \n\t"/* cc0 */\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rbx),%%xmm0	\n\t"\
		"movaps	0x10(%%rbx),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm2	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm1	\n\t"\
		"mulpd	    (%%rsi),%%xmm3	\n\t"\
		"mulpd	0x10(%%rsi),%%xmm0	\n\t"\
		"subpd	%%xmm1,%%xmm2		\n\t"\
		"addpd	%%xmm0,%%xmm3		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIT_LASTPAIR_IN_PLACE_STRIDE(xmm0,xmm1,xmm2,xmm3,xmm6,xmm7,xmm4,xmm5): swap xmm[45]<->[67] */\
		"addpd	%%xmm6,%%xmm0		\n\t"\
		"addpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rax)	\n\t"\
		"movaps	%%xmm1,0x10(%%rax)	\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addpd	%%xmm5,%%xmm2		\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rbx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"		/* Clobbered registers */\
	);\
	}

	#define SSE2_RADIX32_DIF_NOTWIDDLE(Xadd,Xarr_offsets, Xr00, Xisrt2)\
	{\
	__asm__ volatile (\
		/* SSE2_RADIX4_DIF_IN_PLACE(r00,r20,r10,r30): */\
		"\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"movq	%[__r00],%%rax	\n\t"\
		"movq	$0x200,%%rbx	\n\t"\
		"movq	$0x100,%%rcx	\n\t"\
		"movq	$0x300,%%rdx	\n\t"\
		"addq	%%rax,%%rbx		\n\t"\
		"addq	%%rax,%%rcx		\n\t"\
		"addq	%%rax,%%rdx		\n\t"\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm4	\n\t"\
		"addpd	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	    (%%rdx),%%xmm6	\n\t"\
		"subpd	0x10(%%rdx),%%xmm7	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"movaps	%%xmm4,    (%%rax)	\n\t"\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"\
		"\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r08,r28,r18,r38): */\
		"addq	$0x80,%%rax		\n\t"\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm6	\n\t"\
		"addpd	0x10(%%rdx),%%xmm7	\n\t"\
		"subpd	    (%%rdx),%%xmm4	\n\t"\
		"subpd	0x10(%%rdx),%%xmm5	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm5		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm4		\n\t"\
		"movaps	%%xmm6,    (%%rax)	\n\t"\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm0	\n\t"/* isrt2 */\
		"movaps	%%xmm2,%%xmm6		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"subpd	%%xmm4,%%xmm2		\n\t"\
		"subpd	%%xmm3,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm6		\n\t"\
		"addpd	%%xmm3,%%xmm7		\n\t"\
		"mulpd	%%xmm0,%%xmm2		\n\t"\
		"mulpd	%%xmm0,%%xmm5		\n\t"\
		"mulpd	%%xmm0,%%xmm6		\n\t"\
		"mulpd	%%xmm0,%%xmm7		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm5,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"movaps	%%xmm7,0x10(%%rdx)	\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r00,r10,r20,r30,r08,r18,r28,r38): */\
		"subq	$0x80,%%rax	\n\t"/* r00 */\
		"subq	$0x200,%%rbx	\n\t"/* r08 */\
		"addq	$0x80,%%rcx	\n\t"/* r20 */\
		"subq	$0x100,%%rdx	\n\t"/* r28 */\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addq	$0x100,%%rax	\n\t"/* r10 */\
		"addq	$0x100,%%rbx	\n\t"/* r18 */\
		"addq	$0x100,%%rcx	\n\t"/* r30 */\
		"addq	$0x100,%%rdx	\n\t"/* r38 */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE(r04,r24,r14,r34): */\
		"\n\t"\
		"subq	$0xC0,%%rax	\n\t"/* r04 */\
		"addq	$0xC0,%%rbx	\n\t"/* r24 */\
		"subq	$0x1C0,%%rcx	\n\t"/* r14 */\
		"subq	$0x40,%%rdx	\n\t"/* r34 */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm4	\n\t"\
		"addpd	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	    (%%rdx),%%xmm6	\n\t"\
		"subpd	0x10(%%rdx),%%xmm7	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"movaps	%%xmm4,    (%%rax)	\n\t"\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"\
		"\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0C,r2C,r1C,r3C): */\
		"addq	$0x80,%%rax		\n\t"\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm6	\n\t"\
		"addpd	0x10(%%rdx),%%xmm7	\n\t"\
		"subpd	    (%%rdx),%%xmm4	\n\t"\
		"subpd	0x10(%%rdx),%%xmm5	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm5		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm4		\n\t"\
		"movaps	%%xmm6,    (%%rax)	\n\t"\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm0	\n\t"/* isrt2 */\
		"movaps	%%xmm2,%%xmm6		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"subpd	%%xmm4,%%xmm2		\n\t"\
		"subpd	%%xmm3,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm6		\n\t"\
		"addpd	%%xmm3,%%xmm7		\n\t"\
		"mulpd	%%xmm0,%%xmm2		\n\t"\
		"mulpd	%%xmm0,%%xmm5		\n\t"\
		"mulpd	%%xmm0,%%xmm6		\n\t"\
		"mulpd	%%xmm0,%%xmm7		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm5,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"movaps	%%xmm7,0x10(%%rdx)	\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r04,r14,r24,r34,r0C,r1C,r2C,r3C): */\
		"subq	$0x80,%%rax	\n\t"/* r04 */\
		"subq	$0x200,%%rbx	\n\t"/* r0C */\
		"addq	$0x80,%%rcx	\n\t"/* r24 */\
		"subq	$0x100,%%rdx	\n\t"/* r2C */\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addq	$0x100,%%rax	\n\t"/* r14 */\
		"addq	$0x100,%%rbx	\n\t"/* r1C */\
		"addq	$0x100,%%rcx	\n\t"/* r34 */\
		"addq	$0x100,%%rdx	\n\t"/* r3C */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE(r02,r22,r12,r32): */\
		"\n\t"\
		"subq	$0x120,%%rax	\n\t"/* r02 */\
		"addq	$0x60,%%rbx	\n\t"/* r22 */\
		"subq	$0x220,%%rcx	\n\t"/* r12 */\
		"subq	$0xa0,%%rdx	\n\t"/* r32 */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm4	\n\t"\
		"addpd	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	    (%%rdx),%%xmm6	\n\t"\
		"subpd	0x10(%%rdx),%%xmm7	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"movaps	%%xmm4,    (%%rax)	\n\t"\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"\
		"\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0A,r2A,r1A,r3A): */\
		"addq	$0x80,%%rax		\n\t"\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm6	\n\t"\
		"addpd	0x10(%%rdx),%%xmm7	\n\t"\
		"subpd	    (%%rdx),%%xmm4	\n\t"\
		"subpd	0x10(%%rdx),%%xmm5	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm5		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm4		\n\t"\
		"movaps	%%xmm6,    (%%rax)	\n\t"\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm0	\n\t"/* isrt2 */\
		"movaps	%%xmm2,%%xmm6		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"subpd	%%xmm4,%%xmm2		\n\t"\
		"subpd	%%xmm3,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm6		\n\t"\
		"addpd	%%xmm3,%%xmm7		\n\t"\
		"mulpd	%%xmm0,%%xmm2		\n\t"\
		"mulpd	%%xmm0,%%xmm5		\n\t"\
		"mulpd	%%xmm0,%%xmm6		\n\t"\
		"mulpd	%%xmm0,%%xmm7		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm5,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"movaps	%%xmm7,0x10(%%rdx)	\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r02,r12,r22,r32,r0A,r1A,r2A,r3A): */\
		"subq	$0x80,%%rax	\n\t"/* r02 */\
		"subq	$0x200,%%rbx	\n\t"/* r0A */\
		"addq	$0x80,%%rcx	\n\t"/* r22 */\
		"subq	$0x100,%%rdx	\n\t"/* r2A */\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addq	$0x100,%%rax	\n\t"/* r12 */\
		"addq	$0x100,%%rbx	\n\t"/* r1A */\
		"addq	$0x100,%%rcx	\n\t"/* r32 */\
		"addq	$0x100,%%rdx	\n\t"/* r3A */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE(r06,r26,r16,r36): */\
		"\n\t"\
		"subq	$0xC0,%%rax	\n\t"/* r06 */\
		"addq	$0xC0,%%rbx	\n\t"/* r26 */\
		"subq	$0x1C0,%%rcx	\n\t"/* r16 */\
		"subq	$0x40,%%rdx	\n\t"/* r36 */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm4	\n\t"\
		"addpd	0x10(%%rdx),%%xmm5	\n\t"\
		"subpd	    (%%rdx),%%xmm6	\n\t"\
		"subpd	0x10(%%rdx),%%xmm7	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm4,%%xmm0		\n\t"\
		"subpd	%%xmm5,%%xmm1		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm0,%%xmm4		\n\t"\
		"addpd	%%xmm1,%%xmm5		\n\t"\
		"movaps	%%xmm4,    (%%rax)	\n\t"\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"\
		"\n\t"\
		"subpd	%%xmm7,%%xmm2		\n\t"\
		"subpd	%%xmm6,%%xmm3		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm6		\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		/* SSE2_RADIX4_DIF_IN_PLACE_2NDOFTWO(r0E,r2E,r1E,r3E): */\
		"addq	$0x80,%%rax		\n\t"\
		"addq	$0x80,%%rbx		\n\t"\
		"addq	$0x80,%%rcx		\n\t"\
		"addq	$0x80,%%rdx		\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	    (%%rax),%%xmm2	\n\t"\
		"movaps	0x10(%%rax),%%xmm3	\n\t"\
		"addpd	    (%%rbx),%%xmm0	\n\t"\
		"addpd	0x10(%%rbx),%%xmm1	\n\t"\
		"subpd	    (%%rbx),%%xmm2	\n\t"\
		"subpd	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rcx),%%xmm6	\n\t"\
		"movaps	0x10(%%rcx),%%xmm7	\n\t"\
		"addpd	    (%%rdx),%%xmm6	\n\t"\
		"addpd	0x10(%%rdx),%%xmm7	\n\t"\
		"subpd	    (%%rdx),%%xmm4	\n\t"\
		"subpd	0x10(%%rdx),%%xmm5	\n\t"\
		/* Finish radix-4 butterfly and store results into temporary-array slots: */\
		"subpd	%%xmm6,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm1		\n\t"\
		"subpd	%%xmm5,%%xmm2		\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"subpd	%%xmm4,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm5,%%xmm5		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm4,%%xmm4		\n\t"\
		"addpd	%%xmm0,%%xmm6		\n\t"\
		"addpd	%%xmm2,%%xmm5		\n\t"\
		"addpd	%%xmm1,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm4		\n\t"\
		"movaps	%%xmm6,    (%%rax)	\n\t"\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"\
		"\n\t"\
		"movaps	(%%rdi),%%xmm0	\n\t"/* isrt2 */\
		"movaps	%%xmm2,%%xmm6		\n\t"\
		"movaps	%%xmm5,%%xmm7		\n\t"\
		"subpd	%%xmm4,%%xmm2		\n\t"\
		"subpd	%%xmm3,%%xmm5		\n\t"\
		"addpd	%%xmm4,%%xmm6		\n\t"\
		"addpd	%%xmm3,%%xmm7		\n\t"\
		"mulpd	%%xmm0,%%xmm2		\n\t"\
		"mulpd	%%xmm0,%%xmm5		\n\t"\
		"mulpd	%%xmm0,%%xmm6		\n\t"\
		"mulpd	%%xmm0,%%xmm7		\n\t"\
		"movaps	%%xmm2,    (%%rcx)	\n\t"\
		"movaps	%%xmm5,    (%%rdx)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"movaps	%%xmm7,0x10(%%rdx)	\n\t"\
		/* SSE2_RADIX8_DIF_COMBINE_RAD4_SUBS(r06,r16,r26,r36,r0E,r1E,r2E,r3E): */\
		"subq	$0x80,%%rax	\n\t"/* r02 */\
		"subq	$0x200,%%rbx	\n\t"/* r0A */\
		"addq	$0x80,%%rcx	\n\t"/* r22 */\
		"subq	$0x100,%%rdx	\n\t"/* r2A */\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
		"addq	$0x100,%%rax	\n\t"/* r12 */\
		"addq	$0x100,%%rbx	\n\t"/* r1A */\
		"addq	$0x100,%%rcx	\n\t"/* r32 */\
		"addq	$0x100,%%rdx	\n\t"/* r3A */\
		"\n\t"\
		"movaps	    (%%rax),%%xmm0	\n\t"\
		"movaps	    (%%rcx),%%xmm4	\n\t"\
		"movaps	0x10(%%rax),%%xmm1	\n\t"\
		"movaps	0x10(%%rcx),%%xmm5	\n\t"\
		"movaps	    (%%rbx),%%xmm2	\n\t"\
		"movaps	0x10(%%rdx),%%xmm7	\n\t"\
		"movaps	0x10(%%rbx),%%xmm3	\n\t"\
		"movaps	    (%%rdx),%%xmm6	\n\t"\
		"subpd	%%xmm2,%%xmm0		\n\t"\
		"subpd	%%xmm7,%%xmm4		\n\t"\
		"subpd	%%xmm3,%%xmm1		\n\t"\
		"subpd	%%xmm6,%%xmm5		\n\t"\
		"addpd	%%xmm2,%%xmm2		\n\t"\
		"addpd	%%xmm7,%%xmm7		\n\t"\
		"addpd	%%xmm3,%%xmm3		\n\t"\
		"addpd	%%xmm6,%%xmm6		\n\t"\
		"addpd	%%xmm0,%%xmm2		\n\t"\
		"addpd	%%xmm4,%%xmm7		\n\t"\
		"addpd	%%xmm1,%%xmm3		\n\t"\
		"addpd	%%xmm5,%%xmm6		\n\t"\
		"\n\t"\
		"movaps	%%xmm0,    (%%rbx)	\n\t"\
		"movaps	%%xmm4,    (%%rcx)	\n\t"\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"\
		"movaps	%%xmm5,0x10(%%rdx)	\n\t"\
		"movaps	%%xmm2,    (%%rax)	\n\t"\
		"movaps	%%xmm7,    (%%rdx)	\n\t"\
		"movaps	%%xmm3,0x10(%%rax)	\n\t"\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"\
		"\n\t"\
	/**********************************************************************************/\
	/*...and now do eight radix-4 transforms, including the internal twiddle factors: */\
	/**********************************************************************************/\
		"\n\t"\
	/*...Block 1: t00,t10,t20,t30 in r00,04,02,06 - note swapped middle 2 indices! */\
		"movq	%[__r00],%%rsi	\n\t"\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x0(%%rdi),%%rax	\n\t"/* p00 */\
		"movslq	0x4(%%rdi),%%rbx	\n\t"/* p01 */\
		"movslq	0x8(%%rdi),%%rcx	\n\t"/* p02 */\
		"movslq	0xc(%%rdi),%%rdx	\n\t"/* p03 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t00 */\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t20 */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t01 */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t21 */\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t10 */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t30 */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* t11 */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t31 */\
		"subpd	0x40(%%rsi),%%xmm0	\n\t"/* t10=t00-rt */\
		"subpd	0x60(%%rsi),%%xmm4	\n\t"/* t30=t20-rt */\
		"subpd	0x50(%%rsi),%%xmm1	\n\t"/* t11=t01-it */\
		"subpd	0x70(%%rsi),%%xmm5	\n\t"/* t31=t21-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/* t00=t00+rt */\
		"addpd	0x20(%%rsi),%%xmm6	\n\t"/* t20=t20+rt */\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/* t01=t01+it */\
		"addpd	0x30(%%rsi),%%xmm7	\n\t"/* t21=t21+it */\
		"subpd	%%xmm6,%%xmm2		\n\t"/* t00 <- t00-t20 */\
		"subpd	%%xmm5,%%xmm0		\n\t"/* t10 <- t10-t31 */\
		"subpd	%%xmm7,%%xmm3		\n\t"/* t01 <- t01-t21 */\
		"subpd	%%xmm4,%%xmm1		\n\t"/* t11 <- t11-t30 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*          2*t20 */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*          2*t31 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*          2*t21 */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*          2*t30 */\
		"movaps	%%xmm2,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm0,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm2,%%xmm6		\n\t"/* t20 <- t00+t20 */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* t31 <- t10+t31 */\
		"addpd	%%xmm3,%%xmm7		\n\t"/* t21 <- t01+t21 */\
		"addpd	%%xmm1,%%xmm4		\n\t"/* t30 <- t11+t30 */\
		"movaps	%%xmm6,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm5,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm4,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 5: t08,t18,t28,t38	*/\
		"addq	$0x80,%%rsi			\n\t"/* r08 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x10(%%rdi),%%rax	\n\t"/* p04 */\
		"movslq	0x14(%%rdi),%%rbx	\n\t"/* p05 */\
		"movslq	0x18(%%rdi),%%rcx	\n\t"/* p06 */\
		"movslq	0x1c(%%rdi),%%rdx	\n\t"/* p07 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t28 */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t29 */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t38 */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t39 */\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t08 */\
		"subpd	0x30(%%rsi),%%xmm4	\n\t"/* t28-t29 */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t09 */\
		"addpd	0x20(%%rsi),%%xmm5	\n\t"/* t29+t28 */\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t18 */\
		"mulpd	    (%%rdi),%%xmm4		\n\t"/* t28 = (t28-t29)*isrt2 */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* t19 */\
		"mulpd	    (%%rdi),%%xmm5		\n\t"/* t29 = (t29+t28)*isrt2 */\
		"subpd	0x50(%%rsi),%%xmm0	\n\t"/* t08=t08-t19*/\
		"addpd	0x70(%%rsi),%%xmm6	\n\t"/* t38+t39 */\
		"subpd	0x40(%%rsi),%%xmm1	\n\t"/* t19=t09-t18*/\
		"subpd	0x60(%%rsi),%%xmm7	\n\t"/* t39-t38 */\
		"addpd	0x10(%%rsi),%%xmm2	\n\t"/* t09=t18+t09*/\
		"mulpd	    (%%rdi),%%xmm6		\n\t"/*  rt = (t38+t39)*isrt2 */\
		"addpd	    (%%rsi),%%xmm3	\n\t"/* t18=t19+t08*/\
		"mulpd	    (%%rdi),%%xmm7		\n\t"/*  it = (t39-t38)*isrt2 */\
		"\n\t"\
		"subpd	%%xmm6,%%xmm4		\n\t"/* t28=t28-rt */\
		"subpd	%%xmm7,%%xmm5		\n\t"/* t29=t29-it */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"addpd	%%xmm4,%%xmm6		\n\t"/* t38=t28+rt */\
		"addpd	%%xmm5,%%xmm7		\n\t"/* t39=t29+it */\
		"subpd	%%xmm4,%%xmm0		\n\t"/* t08-t28 */\
		"subpd	%%xmm5,%%xmm2		\n\t"/* t09-t29 */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t28 */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t29 */\
		"\n\t"\
		"subpd	%%xmm7,%%xmm3		\n\t"/* t18-t39 */\
		"subpd	%%xmm6,%%xmm1		\n\t"/* t19-t38 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t39 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t38 */\
		"movaps	%%xmm0,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm2,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm3,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm0,%%xmm4		\n\t"/* t08+t28 */\
		"addpd	%%xmm2,%%xmm5		\n\t"/* t09+t29 */\
		"addpd	%%xmm3,%%xmm7		\n\t"/* t18+t39 */\
		"addpd	%%xmm1,%%xmm6		\n\t"/* t19+t38 */\
		"movaps	%%xmm4,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm7,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 3: t04,t14,t24,t34	*/\
		"addq	$0x180,%%rsi		\n\t"/* r20 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x20(%%rdi),%%rax	\n\t"/* p08 */\
		"movslq	0x24(%%rdi),%%rbx	\n\t"/* p09 */\
		"movslq	0x28(%%rdi),%%rcx	\n\t"/* p0a */\
		"movslq	0x2c(%%rdi),%%rdx	\n\t"/* p0b */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t24 */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t34 */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t25 */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t35 */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t24 */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t34 */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t25 */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t35 */\
		"mulpd	    (%%rdi),%%xmm4	\n\t"/* t24*c */\
		"mulpd	0x10(%%rdi),%%xmm6	\n\t"/* t34*s */\
		"mulpd	0x10(%%rdi),%%xmm1	\n\t"/* t25*s */\
		"mulpd	    (%%rdi),%%xmm3	\n\t"/* t35*c */\
		"mulpd	    (%%rdi),%%xmm5	\n\t"/* t25*c */\
		"mulpd	0x10(%%rdi),%%xmm7	\n\t"/* t35*s */\
		"mulpd	0x10(%%rdi),%%xmm0	\n\t"/* t24*s */\
		"mulpd	    (%%rdi),%%xmm2	\n\t"/* t34*c */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t24 */\
		"subpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t25 */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"subq	$0x10,%%rdi			\n\t"/* isrt2 */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t14 */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t34=t24-rt */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* t15 */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t35=t25-it */\
		"subpd	0x50(%%rsi),%%xmm2	\n\t"/* t14-t15 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"addpd	0x40(%%rsi),%%xmm3	\n\t"/* t15+t14 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	    (%%rdi),%%xmm2		\n\t"/* rt = (t14-t15)*isrt2 */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t24=t24+rt */\
		"mulpd	    (%%rdi),%%xmm3		\n\t"/* it = (t15+t14)*isrt2 */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t25=t25+it */\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t04 */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t05 */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t14=t04-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t15=t05-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t04=rt +t04*/\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t05=it +t05*/\
		"subpd	%%xmm6,%%xmm2		\n\t"/* t04-t24 */\
		"subpd	%%xmm5,%%xmm0		\n\t"/* t14-t35 */\
		"subpd	%%xmm7,%%xmm3		\n\t"/* t05-t25 */\
		"subpd	%%xmm4,%%xmm1		\n\t"/* t15-t34 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t24 */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*          2*t35 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t25 */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*          2*t34 */\
		"movaps	%%xmm2,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm0,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm2,%%xmm6		\n\t"/* t04+t24 */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* t14+t35 */\
		"addpd	%%xmm3,%%xmm7		\n\t"/* t05+t25 */\
		"addpd	%%xmm1,%%xmm4		\n\t"/* t15+t34 */\
		"movaps	%%xmm6,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm5,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm4,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 7: t0C,t1C,t2C,t3C	*/\
		"addq	$0x80,%%rsi			\n\t"/* r28 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x30(%%rdi),%%rax	\n\t"/* p0c */\
		"movslq	0x34(%%rdi),%%rbx	\n\t"/* p0d */\
		"movslq	0x38(%%rdi),%%rcx	\n\t"/* p0e */\
		"movslq	0x3c(%%rdi),%%rdx	\n\t"/* p0f */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t2C */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t3C */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t2D */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t3D */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t2C */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t3C */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t2D */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t3D */\
		"mulpd	0x10(%%rdi),%%xmm4	\n\t"/* t2C*s */\
		"mulpd	    (%%rdi),%%xmm6	\n\t"/* t3C*c */\
		"mulpd	    (%%rdi),%%xmm1	\n\t"/* t2D*c */\
		"mulpd	0x10(%%rdi),%%xmm3	\n\t"/* t3D*s */\
		"mulpd	0x10(%%rdi),%%xmm5	\n\t"/* t2D*s */\
		"mulpd	    (%%rdi),%%xmm7	\n\t"/* t3D*c */\
		"mulpd	    (%%rdi),%%xmm0	\n\t"/* t2C*c */\
		"mulpd	0x10(%%rdi),%%xmm2	\n\t"/* t3C*s */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t24 */\
		"subpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t25 */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"subq	$0x10,%%rdi			\n\t"/* isrt2 */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t14 */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t2C=t2C-rt */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* t1D */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t2D=t2D-it */\
		"addpd	0x50(%%rsi),%%xmm2	\n\t"/* t1C+t1D */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"subpd	0x40(%%rsi),%%xmm3	\n\t"/* t1D-t1C */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	    (%%rdi),%%xmm2		\n\t"/* rt = (t1C+t1D)*isrt2 */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t3C=t2C+rt */\
		"mulpd	    (%%rdi),%%xmm3		\n\t"/* it = (t1D-t1C)*isrt2 */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t3D=t2D+it */\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t0C */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t0D */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t0C=t0C-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t0D=t0D-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t1C=rt +t0C*/\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t1D=it +t0D*/\
		"subpd	%%xmm4,%%xmm0		\n\t"/* t0C-t2C */\
		"subpd	%%xmm7,%%xmm2		\n\t"/* t1C-t3D */\
		"subpd	%%xmm5,%%xmm1		\n\t"/* t0D-t2D */\
		"subpd	%%xmm6,%%xmm3		\n\t"/* t1D-t3C */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t2C */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t3D */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t2D */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t3C */\
		"movaps	%%xmm0,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm2,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm0,%%xmm4		\n\t"/* t0C+t2C */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* t1C+t3D */\
		"addpd	%%xmm1,%%xmm5		\n\t"/* t0D+t2D */\
		"addpd	%%xmm3,%%xmm6		\n\t"/* t1D+t3C */\
		"movaps	%%xmm4,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm7,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 2: t02,t12,t22,t32	*/\
		"subq	$0x180,%%rsi		\n\t"/* r10 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x40(%%rdi),%%rax	\n\t"/* p10 */\
		"movslq	0x44(%%rdi),%%rbx	\n\t"/* p11 */\
		"movslq	0x48(%%rdi),%%rcx	\n\t"/* p12 */\
		"movslq	0x4c(%%rdi),%%rdx	\n\t"/* p13 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t22 */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t32 */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t23 */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t33 */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t22 */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t32 */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t23 */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t33 */\
		"mulpd	0x20(%%rdi),%%xmm4	\n\t"/* t22*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm6	\n\t"/* t32*c32_3 */\
		"mulpd	0x30(%%rdi),%%xmm1	\n\t"/* t23*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm3	\n\t"/* t33*s32_3 */\
		"mulpd	0x20(%%rdi),%%xmm5	\n\t"/* t23*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm7	\n\t"/* t33*c32_3 */\
		"mulpd	0x30(%%rdi),%%xmm0	\n\t"/* t22*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm2	\n\t"/* t32*s32_3 */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t22 */\
		"subpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t23 */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t12 */\
		"movaps	0x50(%%rsi),%%xmm0	\n\t"/* t13 */\
		"movaps	0x40(%%rsi),%%xmm1	\n\t"/* copy t12 */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* copy t13 */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t32=t22-rt */\
		"mulpd	    (%%rdi),%%xmm2	\n\t"/* t12*c */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t33=t23-it */\
		"mulpd	0x10(%%rdi),%%xmm0	\n\t"/* t13*s */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"mulpd	    (%%rdi),%%xmm3	\n\t"/* t13*c */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	0x10(%%rdi),%%xmm1	\n\t"/* t12*s */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t22=t22+rt */\
		"subpd	%%xmm0,%%xmm2		\n\t"/* rt */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t23=t23+it */\
		"addpd	%%xmm1,%%xmm3		\n\t"/* it */\
		"\n\t"\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t02 */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t03 */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t12=t02-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t13=t03-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t02=rt+t02 */\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t03=it+t03 */\
		"subpd	%%xmm6,%%xmm2		\n\t"/* t02-t22 */\
		"subpd	%%xmm5,%%xmm0		\n\t"/* t12-t33 */\
		"subpd	%%xmm7,%%xmm3		\n\t"/* t03-t23 */\
		"subpd	%%xmm4,%%xmm1		\n\t"/* t13-t32 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t22 */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t33 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t23 */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t32 */\
		"movaps	%%xmm2,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm0,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm2,%%xmm6		\n\t"/* t02+t22 */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* t12+t33 */\
		"addpd	%%xmm3,%%xmm7		\n\t"/* t03+t23 */\
		"addpd	%%xmm1,%%xmm4		\n\t"/* t13+t32 */\
		"movaps	%%xmm6,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm5,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm7,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm4,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 6: t0A,t1A,t2A,t3A	*/\
		"addq	$0x80,%%rsi			\n\t"/* r18 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x50(%%rdi),%%rax	\n\t"/* p14 */\
		"movslq	0x54(%%rdi),%%rbx	\n\t"/* p15 */\
		"movslq	0x58(%%rdi),%%rcx	\n\t"/* p16 */\
		"movslq	0x5c(%%rdi),%%rdx	\n\t"/* p17 */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t2A */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t3A */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t2B */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t3B */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t2A */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t3A */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t2B */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t3B */\
		"mulpd	0x50(%%rdi),%%xmm4	\n\t"/* t2A*s32_3 */\
		"mulpd	0x20(%%rdi),%%xmm6	\n\t"/* t3A*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm1	\n\t"/* t2B*c32_3 */\
		"mulpd	0x30(%%rdi),%%xmm3	\n\t"/* t3B*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm5	\n\t"/* t2B*s32_3 */\
		"mulpd	0x20(%%rdi),%%xmm7	\n\t"/* t3B*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm0	\n\t"/* t2A*c32_3 */\
		"mulpd	0x30(%%rdi),%%xmm2	\n\t"/* t3A*s32_1 */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t2A */\
		"addpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t2B */\
		"subpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t1A */\
		"movaps	0x50(%%rsi),%%xmm0	\n\t"/* t1B */\
		"movaps	0x40(%%rsi),%%xmm1	\n\t"/* copy t1A */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* copy t1B */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t2A=t2A-rt */\
		"mulpd	0x10(%%rdi),%%xmm2	\n\t"/* t1A*s */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t2B=t2B-it */\
		"mulpd	    (%%rdi),%%xmm0	\n\t"/* t1B*c */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"mulpd	0x10(%%rdi),%%xmm3	\n\t"/* t1B*s */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	    (%%rdi),%%xmm1	\n\t"/* t1A*c */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t3A=t2A+rt */\
		"addpd	%%xmm0,%%xmm2		\n\t"/* rt */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t3B=t2B+it */\
		"subpd	%%xmm1,%%xmm3		\n\t"/* it */\
		"\n\t"\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t0A */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t0B */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t0A=t0A-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t0B=t0B-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t1A=rt+t0A */\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t1B=it+t0B */\
		"subpd	%%xmm4,%%xmm0		\n\t"/* t0A-t2A */\
		"subpd	%%xmm7,%%xmm2		\n\t"/* t1A-t3B */\
		"subpd	%%xmm5,%%xmm1		\n\t"/* t0B-t2B */\
		"subpd	%%xmm6,%%xmm3		\n\t"/* t1B-t3A */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t2A */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t3B */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t2B */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t3A */\
		"movaps	%%xmm0,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm2,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm0,%%xmm4		\n\t"/* t0A+t2A */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* t1A+t3B */\
		"addpd	%%xmm1,%%xmm5		\n\t"/* t0B+t2B */\
		"addpd	%%xmm3,%%xmm6		\n\t"/* t1B+t3A */\
		"movaps	%%xmm4,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm7,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 4: t06,t16,t26,t36	*/\
		"addq	$0x180,%%rsi		\n\t"/* r30 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x60(%%rdi),%%rax	\n\t"/* p18 */\
		"movslq	0x64(%%rdi),%%rbx	\n\t"/* p19 */\
		"movslq	0x68(%%rdi),%%rcx	\n\t"/* p1a */\
		"movslq	0x6c(%%rdi),%%rdx	\n\t"/* p1b */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t26 */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t36 */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t27 */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t37 */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t26 */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t36 */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t27 */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t37 */\
		"mulpd	0x40(%%rdi),%%xmm4	\n\t"/* t26*s32_3 */\
		"mulpd	0x30(%%rdi),%%xmm6	\n\t"/* t36*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm1	\n\t"/* t27*s32_3 */\
		"mulpd	0x20(%%rdi),%%xmm3	\n\t"/* t37*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm5	\n\t"/* t27*c32_3 */\
		"mulpd	0x30(%%rdi),%%xmm7	\n\t"/* t37*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm0	\n\t"/* t26*s32_3 */\
		"mulpd	0x20(%%rdi),%%xmm2	\n\t"/* t36*c32_1 */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t26 */\
		"addpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t27 */\
		"subpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t16 */\
		"movaps	0x50(%%rsi),%%xmm0	\n\t"/* t17 */\
		"movaps	0x40(%%rsi),%%xmm1	\n\t"/* copy t16 */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* copy t17 */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t26=t26-rt */\
		"mulpd	0x10(%%rdi),%%xmm2	\n\t"/* t16*s */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t27=t27-it */\
		"mulpd	    (%%rdi),%%xmm0	\n\t"/* t17*c */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"mulpd	0x10(%%rdi),%%xmm3	\n\t"/* t17*s */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	    (%%rdi),%%xmm1	\n\t"/* t16*c */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t36=t26+rt */\
		"subpd	%%xmm0,%%xmm2		\n\t"/* rt */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t37=t27+it */\
		"addpd	%%xmm1,%%xmm3		\n\t"/* it */\
		"\n\t"\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t06 */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t07 */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t16=t06-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t17=t07-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t06=rt+t06 */\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t07=it+t07 */\
		"subpd	%%xmm4,%%xmm2		\n\t"/* t06-t26 */\
		"subpd	%%xmm7,%%xmm0		\n\t"/* t16-t37 */\
		"subpd	%%xmm5,%%xmm3		\n\t"/* t07-t27 */\
		"subpd	%%xmm6,%%xmm1		\n\t"/* t17-t36 */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t26 */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t37 */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t27 */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t36 */\
		"movaps	%%xmm2,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm0,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm3,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm1,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm2,%%xmm4		\n\t"/* t06+t26 */\
		"addpd	%%xmm0,%%xmm7		\n\t"/* t16+t37 */\
		"addpd	%%xmm3,%%xmm5		\n\t"/* t07+t27 */\
		"addpd	%%xmm1,%%xmm6		\n\t"/* t17+t36 */\
		"movaps	%%xmm4,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm7,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		/*...Block 8: t0E,t1E,t2E,t3E	*/\
		"addq	$0x80,%%rsi			\n\t"/* r38 */\
		"movq	%[__arr_offsets],%%rdi	\n\t"\
		"movslq	0x70(%%rdi),%%rax	\n\t"/* p1c */\
		"movslq	0x74(%%rdi),%%rbx	\n\t"/* p1d */\
		"movslq	0x78(%%rdi),%%rcx	\n\t"/* p1e */\
		"movslq	0x7c(%%rdi),%%rdx	\n\t"/* p1f */\
		"movq	%[__add],%%rdi		\n\t"\
		"addq	%%rdi,%%rax	\n\t"\
		"addq	%%rdi,%%rbx	\n\t"\
		"addq	%%rdi,%%rcx	\n\t"\
		"addq	%%rdi,%%rdx	\n\t"\
		"movq	%[__isrt2],%%rdi	\n\t"\
		"addq	$0x10,%%rdi			\n\t"/* cc0 */\
		"\n\t"\
		"movaps	0x20(%%rsi),%%xmm4	\n\t"/* t2E */\
		"movaps	0x60(%%rsi),%%xmm6	\n\t"/* t3E */\
		"movaps	0x30(%%rsi),%%xmm5	\n\t"/* t2F */\
		"movaps	0x70(%%rsi),%%xmm7	\n\t"/* t3F */\
		"movaps	0x20(%%rsi),%%xmm0	\n\t"/* copy t2E */\
		"movaps	0x60(%%rsi),%%xmm2	\n\t"/* copy t3E */\
		"movaps	0x30(%%rsi),%%xmm1	\n\t"/* copy t2F */\
		"movaps	0x70(%%rsi),%%xmm3	\n\t"/* copy t3F */\
		"mulpd	0x30(%%rdi),%%xmm4	\n\t"/* t2E*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm6	\n\t"/* t3E*c32_3 */\
		"mulpd	0x20(%%rdi),%%xmm1	\n\t"/* t2F*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm3	\n\t"/* t3F*s32_3 */\
		"mulpd	0x30(%%rdi),%%xmm5	\n\t"/* t2F*s32_1 */\
		"mulpd	0x50(%%rdi),%%xmm7	\n\t"/* t3F*c32_3 */\
		"mulpd	0x20(%%rdi),%%xmm0	\n\t"/* t2E*c32_1 */\
		"mulpd	0x40(%%rdi),%%xmm2	\n\t"/* t3E*s32_3 */\
		"subpd	%%xmm1,%%xmm4		\n\t"/* ~t2E */\
		"subpd	%%xmm3,%%xmm6		\n\t"/* rt */\
		"addpd	%%xmm0,%%xmm5		\n\t"/* ~t2F */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* it */\
		"\n\t"\
		"movaps	0x40(%%rsi),%%xmm2	\n\t"/* t1E */\
		"movaps	0x50(%%rsi),%%xmm0	\n\t"/* t1F */\
		"movaps	0x40(%%rsi),%%xmm1	\n\t"/* copy t1E */\
		"movaps	0x50(%%rsi),%%xmm3	\n\t"/* copy t1F */\
		"subpd	%%xmm6,%%xmm4		\n\t"/*~t2E=t2E-rt */\
		"mulpd	    (%%rdi),%%xmm2	\n\t"/* t1E*c */\
		"subpd	%%xmm7,%%xmm5		\n\t"/*~t2F=t2F-it */\
		"mulpd	0x10(%%rdi),%%xmm0	\n\t"/* t1F*s */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*      2* rt */\
		"mulpd	    (%%rdi),%%xmm3	\n\t"/* t1F*c */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*      2* it */\
		"mulpd	0x10(%%rdi),%%xmm1	\n\t"/* t1E*s */\
		"addpd	%%xmm4,%%xmm6		\n\t"/*~t3E=t2E+rt */\
		"addpd	%%xmm0,%%xmm2		\n\t"/* rt */\
		"addpd	%%xmm5,%%xmm7		\n\t"/*~t3F=t2F+it */\
		"subpd	%%xmm1,%%xmm3		\n\t"/* it */\
		"\n\t"\
		"movaps	    (%%rsi),%%xmm0	\n\t"/* t0E */\
		"movaps	0x10(%%rsi),%%xmm1	\n\t"/* t0F */\
		"subpd	%%xmm2,%%xmm0		\n\t"/*~t0E=t0E-rt */\
		"subpd	%%xmm3,%%xmm1		\n\t"/*~t0F=t0F-it */\
		"addpd	    (%%rsi),%%xmm2	\n\t"/*~t1E=rt+t0E */\
		"addpd	0x10(%%rsi),%%xmm3	\n\t"/*~t1F=it+t0F */\
		"subpd	%%xmm4,%%xmm0		\n\t"/* t0E-t2E */\
		"subpd	%%xmm7,%%xmm2		\n\t"/* t1E-t3F */\
		"subpd	%%xmm5,%%xmm1		\n\t"/* t0F-t2F */\
		"subpd	%%xmm6,%%xmm3		\n\t"/* t1F-t3E */\
		"addpd	%%xmm4,%%xmm4		\n\t"/*   2*t2E */\
		"addpd	%%xmm7,%%xmm7		\n\t"/*   2*t3F */\
		"addpd	%%xmm5,%%xmm5		\n\t"/*   2*t2F */\
		"addpd	%%xmm6,%%xmm6		\n\t"/*   2*t3E */\
		"movaps	%%xmm0,    (%%rbx)	\n\t"/* a(jt+p1 ) */\
		"movaps	%%xmm2,    (%%rcx)	\n\t"/* a(jt+p2 ) */\
		"movaps	%%xmm1,0x10(%%rbx)	\n\t"/* a(jp+p1 ) */\
		"movaps	%%xmm3,0x10(%%rdx)	\n\t"/* a(jp+p3 ) */\
		"addpd	%%xmm0,%%xmm4		\n\t"/* t0E+t2E */\
		"addpd	%%xmm2,%%xmm7		\n\t"/* t1E+t3F */\
		"addpd	%%xmm1,%%xmm5		\n\t"/* t0F+t2F */\
		"addpd	%%xmm3,%%xmm6		\n\t"/* t1F+t3E */\
		"movaps	%%xmm4,    (%%rax)	\n\t"/* a(jt+p0 ) */\
		"movaps	%%xmm7,    (%%rdx)	\n\t"/* a(jt+p3 ) */\
		"movaps	%%xmm5,0x10(%%rax)	\n\t"/* a(jp+p0 ) */\
		"movaps	%%xmm6,0x10(%%rcx)	\n\t"/* a(jp+p2 ) */\
		:					/* outputs: none */\
		: [__add] "m" (Xadd)	/* All inputs from memory addresses here */\
		 ,[__arr_offsets] "m" (Xarr_offsets)\
		 ,[__r00] "m" (Xr00)\
		 ,[__isrt2] "m" (Xisrt2)\
		: "cc","memory","rax","rbx","rcx","rdx","rdi","rsi","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"		/* Clobbered registers */\
	);\
	}

#endif	// AVX2 / AVX / SSE2 toggle

#endif	// radix32_ditN_cy_dif1_gcc_h_included
