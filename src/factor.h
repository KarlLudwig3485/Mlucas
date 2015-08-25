/*******************************************************************************
*                                                                              *
*   (C) 1997-2012 by Ernst W. Mayer.                                           *
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
#ifndef factor_h_included
#define factor_h_included

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/********
PLEASE REFER TO FACTOR.C FOR A DESCRIPTION OF THE APPLICABLE #DEFINES

	(except for FACTOR_PASS_MAX and MAX_BITS_P|Q, which are defined in Mdata.h,
	 and MAX_IMUL_WORDS and MUL_LOHI64_SUBROUTINE, defined in imul_macro.h).
********/

#undef P1WORD
#if(!defined(P2WORD) && !defined(P3WORD) && !defined(P4WORD) && !defined(NWORD))
	#define P1WORD
#endif

#if(defined(NWORD) || defined(P4WORD))
	#undef TRYQ
	#define TRYQ	1
#endif

#ifdef USE_FLOAT
	/* This will need to be made platform-dependent at some point: */
  #ifndef TRYQ
	#define TRYQ	4
  #elif(TRYQ != 1 && TRYQ != 2 && TRYQ != 4 && TRYQ != 8 && TRYQ != 16)
	#error USE_FLOAT option requires TRYQ = 1, 2, 4, 8 or 16
  #endif

	/* FP-based modmul currently only supported for one-word p's: */
	#ifdef P2WORD
		#error P2WORD may not be used together with USE_FLOAT!
	#endif
//	#ifdef P3WORD
//		#error P3WORD may not be used together with USE_FLOAT!
//	#endif
	#ifdef P4WORD
		#error P4WORD may not be used together with USE_FLOAT!
	#endif

	#ifdef USE_65BIT
		#error USE_65BIT may not be used together with USE_FLOAT!
	#endif
	#ifdef USE_95BIT
		#error USE_65BIT may not be used together with USE_FLOAT!
	#endif
#elif defined(MUL_LOHI64_SUBROUTINE)
  #ifndef TRYQ
	#define TRYQ	4
  #elif(TRYQ != 1 && TRYQ != 4 && TRYQ != 4)
	#error MUL_LOHI64_SUBROUTINE option requires TRYQ = 1, 4 or 8
  #endif
#endif

#ifdef USE_FMADD
	/* This will need to be made platform-dependent at some point: */
  #ifndef TRYQ
	#define TRYQ	2
  #elif(TRYQ != 1 && TRYQ != 2 && TRYQ != 4)
	#error USE_FMADD option requires TRYQ = 1, 2 or 4
  #endif

	/* FMADD-based modmul currently only supported for one-word p's: */
	#ifdef P2WORD
		#error P2WORD may not be used together with USE_FMADD!
	#endif
	#ifdef P3WORD
		#error P3WORD may not be used together with USE_FMADD!
	#endif
	#ifdef P4WORD
		#error P4WORD may not be used together with USE_FMADD!
	#endif

	#ifdef USE_FLOAT
		#error USE_FLOAT may not be used together with USE_FMADD!
	#endif

	#ifdef USE_65BIT
		#error USE_65BIT may not be used together with USE_FMADD!
	#endif
	#ifdef USE_95BIT
		#error USE_65BIT may not be used together with USE_FMADD!
	#endif
#endif

/* Special-handling #define for 96-bit factor candidates: */
#ifdef USE_128x96
	/* Currently only values of 0,1,2 supported: */
	#if(USE_128x96 > 2)
		#error Unrecognized value of USE_128x96!
	#endif

	/* If 2-word-or-greater factoring not enabled, make sure factors < 2^96: */
	#if((MAX_BITS_Q > 96) && defined(P1WORD))
		#error Factoring > 96 bits requires PxWORD with x > 2 to be defined!
	#endif
#endif

/* Key debug #define off value of EWM_DEBUG (set in masterdefs.h) and FACTOR_STANDALONE, set at compile time: */
#undef FAC_DEBUG
#if EWM_DEBUG && defined(FACTOR_STANDALONE)
	#define FAC_DEBUG	1
#else
	#define FAC_DEBUG	0	/* Set == 1 to enable more-extensive self-checking in test_fac() */
#endif

#undef DBG_SIEVE
#if FAC_DEBUG
	#define	DBG_SIEVE	0	/* Set == 1 to enable sieve debugging (Note that no actual trial-divides will occur in this case) */
#endif

#ifndef TRYQ

	#if DBG_SIEVE
		#define TRYQ	0
	#elif defined(INTEGER_MUL_32)
		#define TRYQ	4
	#else
		#define TRYQ	8	/* # of candidates at a time to try. Must be of form 2^k, or zero to skip trial division step
					(e.g. to test sieve-related timings.) A value of 8 seems optimal on the Alpha 21264, which is
					reasonable - TRYQ < 8 may not allow all the integer MULs to complete by the time their results
					are needed, whereas TRYQ = 16 needs more registers than are available and at the same time
					doesn't pipeline significantly better than TRYQ = 8. */
	#endif

#endif

/* Don't have 16-input versions of the twopmodq routines for q > 2^63, so only allow TRYQ up to 8. */
#ifndef TRYQ
	#error TRYQ undefined!
#endif
#if(TRYQ != 0 && TRYQ != 1 && TRYQ != 2 && TRYQ != 4 && TRYQ != 8 && TRYQ != 16)
	#error	Illegal value of TRYQ
#endif
#if(TRYQ == 2 && !defined(USE_FLOAT) && !defined(USE_FMADD))
	#error	TRYQ = 2 only allowed if USE_FLOAT is defined
#endif

/* Make sure the TRYQ = 4 fused macros are only used on the PPC32: */
#ifdef MOD_INI_Q4
	#ifndef CPU_SUBTYPE_PPC32
		#error TRYQ = 4 fused macros should only used on the PPC!
	#endif
#endif

/* Factoring-only globals: */
extern int restart;
extern uint64 checksum1;	/* Sum (mod 2^64) of all q's tried during a predetermined interval */
extern uint64 checksum2;	/* Sum (mod 2^64) of all 2^p mod q's for  a predetermined interval */

#ifdef FACTOR_STANDALONE
	/* Declare a blank STATFILE string to ease program logic: */
	extern char STATFILE[];
#endif

/*******************************************************************************
   Function prototypes. The corresponding function definitions will either
   be in a {function name}.c file or (for cases where a .c file contains
   multiple function definitions) in the given .c file:
*******************************************************************************/

/* factor.c: */

#ifndef FACTOR_STANDALONE
	/* exponents > 64 bits require standalone-mode build: */
	int factor(char *pstring, double log2_min_factor, double log2_max_factor);
#endif

int		test_fac(void);
uint64	factor_qmmp_sieve64(uint32 p, uint64 k, uint64 imin, uint64 imax);

uint64	test_modsqr64    (uint64  x, uint64  q);
uint96	test_modsqr96    (uint96  x, uint96  q);
uint128	test_modsqr128_96(uint128 x, uint128 q);
uint128	test_modsqr128   (uint128 x, uint128 q);

// The single-trial-divisor versions of many of the modpow routines are in terms of exponent and *divisor*
// to allow them to serve both as TF modules and to be used more generally, e.g. for PRP testing.
// The multiple-trial-divisor routines are intended for TF use only, thus input their divisors
// q = 2.k.p+1 using the factor index k:
uint64	twopmodq63    (                                    uint64 p, uint64 q);	// q, not k!
uint64	twopmodq63_q4 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq63_q8 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);
uint64	twopmodq64    (                                    uint64 p, uint64 q);	// q, not k!
uint64	twopmodq64_q4 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq64_q8 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);
uint64	twopmodq65    (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k);
uint64	twopmodq65_q4 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq65_q8 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

// Conventional positive-power version of twopmodq, returns true mod:
uint64	twopmmodq64    (uint64 p, uint64 q);	// q, not k!
// This variant of twopmodq64_q4 returns the 4 true mods, overwriting the inputs
void	twopmmodq64_q4(uint64 p, uint64 *i0, uint64 *i1, uint64 *i2, uint64 *i3, uint64 *qi0, uint64 *qi1, uint64 *qi2, uint64 *qi3);

uint64 twopmodq63_x8(uint64 q0, uint64 q1, uint64 q2, uint64 q3, uint64 q4, uint64 q5, uint64 q6, uint64 q7);

uint64	twopmodq78_3WORD_DOUBLE   (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k);
uint64	twopmodq78_3WORD_DOUBLE_q2(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1);
uint64	twopmodq78_3WORD_DOUBLE_q4(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq78_3WORD_DOUBLE_q4_REF(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
#if defined(COMPILER_TYPE_GCC) && (OS_BITS == 64)
uint64	twopmodq78_3WORD_DOUBLE_q8 (uint64*checksum1, uint64*checksum2, uint64 p
		, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7
	);
uint64	twopmodq78_3WORD_DOUBLE_q16(uint64*checksum1, uint64*checksum2, uint64 p
		, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7, uint64 k8, uint64 k9, uint64 ka, uint64 kb, uint64 kc, uint64 kd, uint64 ke, uint64 kf
	);
uint64	twopmodq78_3WORD_DOUBLE_q32(uint64*checksum1, uint64*checksum2, uint64 p
		, uint64 k00, uint64 k01, uint64 k02, uint64 k03, uint64 k04, uint64 k05, uint64 k06, uint64 k07, uint64 k08, uint64 k09, uint64 k0a, uint64 k0b, uint64 k0c, uint64 k0d, uint64 k0e, uint64 k0f
		, uint64 k10, uint64 k11, uint64 k12, uint64 k13, uint64 k14, uint64 k15, uint64 k16, uint64 k17, uint64 k18, uint64 k19, uint64 k1a, uint64 k1b, uint64 k1c, uint64 k1d, uint64 k1e, uint64 k1f
	);
uint64	twopmodq78_3WORD_DOUBLE_q64(uint64*checksum1, uint64*checksum2, uint64 p
		, uint64 k00, uint64 k01, uint64 k02, uint64 k03, uint64 k04, uint64 k05, uint64 k06, uint64 k07, uint64 k08, uint64 k09, uint64 k0a, uint64 k0b, uint64 k0c, uint64 k0d, uint64 k0e, uint64 k0f
		, uint64 k10, uint64 k11, uint64 k12, uint64 k13, uint64 k14, uint64 k15, uint64 k16, uint64 k17, uint64 k18, uint64 k19, uint64 k1a, uint64 k1b, uint64 k1c, uint64 k1d, uint64 k1e, uint64 k1f
		, uint64 k20, uint64 k21, uint64 k22, uint64 k23, uint64 k24, uint64 k25, uint64 k26, uint64 k27, uint64 k28, uint64 k29, uint64 k2a, uint64 k2b, uint64 k2c, uint64 k2d, uint64 k2e, uint64 k2f
		, uint64 k30, uint64 k31, uint64 k32, uint64 k33, uint64 k34, uint64 k35, uint64 k36, uint64 k37, uint64 k38, uint64 k39, uint64 k3a, uint64 k3b, uint64 k3c, uint64 k3d, uint64 k3e, uint64 k3f
	);
#endif

/******************************************/
/*            GPU-TF stuff:               */
/******************************************/
#ifdef __CUDACC__

	// Self-tests:
	__global__ void VecModpow(const uint32*pvec, const uint32*pshft, const uint32*zshft, const uint32*stidx, const uint64*kvec, uint8*rvec, int N);
	// Production TFing:
	__global__ void GPU_TF78   (const uint32 p, const uint32 pshift, const uint32 zshift, const uint32 start_index, const uint64*kvec, uint8*rvec, int N);
	__global__ void GPU_TF78_q4(const uint32 p, const uint32 pshift, const uint32 zshift, const uint32 start_index, const uint64*kvec, uint8*rvec, int N);
	__global__ void GPU_TF78_q8(const uint32 p, const uint32 pshift, const uint32 zshift, const uint32 start_index, const uint64*kvec, uint8*rvec, int N);

	// Simple GPU-ized version of twopmodq78_3WORD_DOUBLE.
	// Return value: 1 if q = 2.k.p+1 divides 2^p-1, 0 otherwise.
	__device__ uint32 twopmodq78_gpu(
		const uint32 p, const uint32 pshift, const uint64 k,
		const uint32 start_index, const uint32 zshift,
		const int i	// thread id (handy for debug)
	);

	__device__ uint32 twopmodq78_q4_GPU(
		const uint32 p, const uint32 pshift, const uint64 k0, const uint64 k1, const uint64 k2, const uint64 k3,
		const uint32 start_index, const uint32 zshift,
		const int i	// thread id (handy for debug)
	);

#endif	// __CUDACC__

uint64	twopmodq100_2WORD_DOUBLE   (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k);
uint64	twopmodq100_2WORD_DOUBLE_q2(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1);
uint64	twopmodq100_2WORD_DOUBLE_q4(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);

uint96	twopmodq96    (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k);
uint64	twopmodq96_q4 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq96_q8 (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);
uint64	twopmodq96_q8_const_qhi(uint64*checksum1, uint64*checksum2, uint64 p, uint64 khi, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint64	twopmodq128_96   (uint64*checksum1, uint64*checksum2, uint64 p, uint64 k);
uint64	twopmodq128_96_q4(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq128_96_q8(uint64*checksum1, uint64*checksum2, uint64 p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);
uint64	twopmodq128_96_q8_const_qhi(uint64*checksum1, uint64*checksum2, uint64 p, uint64 khi, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint128	twopmodq128		(                                    uint128 p, uint128 q);	// q, not k!
uint64	twopmodq128x2	(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k);
uint64	twopmodq128_q4	(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq128_q8	(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint64	twopmodq160   (uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k);
uint64	twopmodq160_q4(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq160_q8(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint192	twopmodq192   (                                    uint192 p, uint192 q);	// q, not k!
uint64	twopmodq192_q4(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq192_q4_qmmp(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq192_q8(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint256	twopmodq200_8WORD_DOUBLE(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k);
uint256	twopmodq200_8WORD_qmmp  (uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k);

uint256	twopmodq256   (                                    uint256 p, uint256 q);	// q, not k!
uint64	twopmodq256_q4(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3);
uint64	twopmodq256_q8(uint64*checksum1, uint64*checksum2, uint64 *p, uint64 k0, uint64 k1, uint64 k2, uint64 k3, uint64 k4, uint64 k5, uint64 k6, uint64 k7);

uint32	CHECK_PKMOD60(uint32 pmod60, uint32 kmod60);

#ifdef __cplusplus
}
#endif

#endif	/* factor_h_included */
