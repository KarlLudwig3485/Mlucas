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

// This main loop is same for un-and-multithreaded, so stick into a header file
// (can't use a macro because of the #if-enclosed stuff).

for(k=1; k <= khi; k++)	/* Do n/(radix(1)*nwt) outer loop executions...	*/
{
	/* In SIMD mode, data are arranged in [re_0,...,re_n-1,im_0,...,im_n-1] groups, not the usual [re_0,im_0],...,[re_n-1,im_n-1] pairs.
	Thus we can still increment the j-index as if stepping through the residue array-of-doubles in strides of 2,
	but to point to the proper real datum, we need to index-map e.g. [0,1,2,3] ==> [0,2,1,3] in 2-way SIMD mode.
	(But only ever need to explicitly do this in debug mode).
	*/
	for(j = jstart; j < jhi; j += stride)
	{
		j1 =  j;
		j1 = j1 + ( (j1 >> DAT_BITS) << PAD_BITS );	/* padded-array fetch index is here */
		j2 = j1 + RE_IM_STRIDE;

/*...The radix-128 DIT pass is here:	*/

#ifdef USE_SSE2

/* Gather the needed data (128 64-bit complex, i.e. 256 64-bit reals) and do 8 twiddleless length-16 subtransforms: */

  #ifdef USE_AVX
	#define OFF1	0x40
	#define OFF2	0x80
	#define OFF3	0xc0
	#define OFF4	0x100
  #else
	#define OFF1	0x20
	#define OFF2	0x40
	#define OFF3	0x60
	#define OFF4	0x80
  #endif

	// Start of isrt2/cc16/ss16 triplet needed for radix-16 SSE2 DFT macros [use instead of isrt2 here]
  #if USE_COMPACT_OBJ_CODE
	tmp = isrt2;	// In USE_COMPACT_OBJ_CODE mode, use that [isrt2] ptr heads up the needed isrt2/cc16/ss16 pointer-triplet
					// (actually a quartet in AVX2 mode, since there need sqrt2 preceding isrt2)
  #else
	tmp = sqrt2+1;	// In non-compact-obj-code mode, access the same (unnamed) pointer this way
  #endif
	tm1 = r00;
	for(l = 0; l < 8; l++) {
		add0 = &a[j1] + poff[l<<2];	// poff[4*l] = p10,p20,...,pf0
		add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
			add8 = add0+p08; add9 = add1+p08; adda = add2+p08; addb = add3+p08; addc = add4+p08; addd = add5+p08; adde = add6+p08; addf = add7+p08;
/*
((vec_dbl*)add0)->d0 = 3; ((vec_dbl*)add0)->d1 = 5;
((vec_dbl*)add1)->d0 = 1; ((vec_dbl*)add1)->d1 = 3;
((vec_dbl*)add2)->d0 = 4; ((vec_dbl*)add2)->d1 = 5;
((vec_dbl*)add3)->d0 = 1; ((vec_dbl*)add3)->d1 = 8;
((vec_dbl*)add4)->d0 = 5; ((vec_dbl*)add4)->d1 = 9;
((vec_dbl*)add5)->d0 = 9; ((vec_dbl*)add5)->d1 = 7;
((vec_dbl*)add6)->d0 = 2; ((vec_dbl*)add6)->d1 = 9;
((vec_dbl*)add7)->d0 = 6; ((vec_dbl*)add7)->d1 = 3;
((vec_dbl*)add8)->d0 = 2; ((vec_dbl*)add8)->d1 = 2;
((vec_dbl*)add9)->d0 = 7; ((vec_dbl*)add9)->d1 = 8;
((vec_dbl*)adda)->d0 = 1; ((vec_dbl*)adda)->d1 = 4;
((vec_dbl*)addb)->d0 = 8; ((vec_dbl*)addb)->d1 = 5;
((vec_dbl*)addc)->d0 = 2; ((vec_dbl*)addc)->d1 = 9;
((vec_dbl*)addd)->d0 = 8; ((vec_dbl*)addd)->d1 = 0;
((vec_dbl*)adde)->d0 = 1; ((vec_dbl*)adde)->d1 = 4;
((vec_dbl*)addf)->d0 = 8; ((vec_dbl*)addf)->d1 = 5;
*/
		SSE2_RADIX16_DIT_0TWIDDLE(
			add0,add1,add2,add3,add4,add5,add6,add7,add8,add9,adda,addb,addc,addd,adde,addf, tmp,two,
			tm1,OFF1,OFF2,OFF3,OFF4
		);
/*
printf("Radix-16 outs:\n");
printf("0: %15.10f, %15.10f\n",(tm1+0x0)->d0,(tm1+0x0)->d1);
printf("1: %15.10f, %15.10f\n",(tm1+0x1)->d0,(tm1+0x1)->d1);
printf("2: %15.10f, %15.10f\n",(tm1+0x2)->d0,(tm1+0x2)->d1);
printf("3: %15.10f, %15.10f\n",(tm1+0x3)->d0,(tm1+0x3)->d1);
printf("4: %15.10f, %15.10f\n",(tm1+0x4)->d0,(tm1+0x4)->d1);
printf("5: %15.10f, %15.10f\n",(tm1+0x5)->d0,(tm1+0x5)->d1);
printf("6: %15.10f, %15.10f\n",(tm1+0x6)->d0,(tm1+0x6)->d1);
printf("7: %15.10f, %15.10f\n",(tm1+0x7)->d0,(tm1+0x7)->d1);
printf("8: %15.10f, %15.10f\n",(tm1+0x8)->d0,(tm1+0x8)->d1);
printf("9: %15.10f, %15.10f\n",(tm1+0x9)->d0,(tm1+0x9)->d1);
printf("A: %15.10f, %15.10f\n",(tm1+0xA)->d0,(tm1+0xA)->d1);
printf("B: %15.10f, %15.10f\n",(tm1+0xB)->d0,(tm1+0xB)->d1);
printf("C: %15.10f, %15.10f\n",(tm1+0xC)->d0,(tm1+0xC)->d1);
printf("D: %15.10f, %15.10f\n",(tm1+0xD)->d0,(tm1+0xD)->d1);
printf("E: %15.10f, %15.10f\n",(tm1+0xE)->d0,(tm1+0xE)->d1);
printf("F: %15.10f, %15.10f\n",(tm1+0xF)->d0,(tm1+0xF)->d1);
if(l==7)exit(0);
*/
		tm1 += 32;
	}

	#undef OFF1
	#undef OFF2
	#undef OFF3
	#undef OFF4

/*...and now do 16 radix-8 subtransforms w/internal twiddles - cf. radix64_dit_pass1 for details: */

/* Block 0: */	jt = j1;
  #ifdef USE_AVX2
	SSE2_RADIX8_DIT_0TWIDDLE_OOP(	// This outputs o[07654321], so reverse o-index order of latter 7 outputs
		r00,r10,r20,r30,r40,r50,r60,r70,
		s1p00,s1p70,s1p60,s1p50,s1p40,s1p30,s1p20,s1p10, isrt2,two
	);
  #else
	SSE2_RADIX8_DIT_0TWIDDLE_OOP(	// This outputs o[07654321], so reverse o-index order of latter 7 outputs
		r00,r10,r20,r30,r40,r50,r60,r70,
		s1p00,s1p70,s1p60,s1p50,s1p40,s1p30,s1p20,s1p10, isrt2
	);
  #endif

// Note: 1st of the 15 sincos args in each call to SSE2_RADIX8_DIT_TWIDDLE_OOP is the basic isrt2 needed for
// radix-8. This is a workaround of GCC's 30-arg limit for inline ASM macros, which proves a royal pain here.

  #if USE_COMPACT_OBJ_CODE

	// Remaining 15 sets of macro calls done in loop:
	for(l = 1; l < 16; l++) {
		k1 = reverse(l,16);
		// Twid-offsets are multiples of 21 vec_dbl; +1 to point to cos [middle] term of each [isrt2,c,s] triplet
		tm1 = r00 + (k1<<1); tm2 = s1p00 + (k1<<1); tmp = twid0 + (k1<<4)+(k1<<2)+k1;
		VEC_DBL_INIT(tm2,2.0);
		vec_dbl
		*u0 = tm1, *u1 = tm1 + 0x20, *u2 = tm1 + 0x40, *u3 = tm1 + 0x60, *u4 = tm1 + 0x80, *u5 = tm1 + 0xa0, *u6 = tm1 + 0xc0, *u7 = tm1 + 0xe0,
		*v0 = tm2, *v1 = tm2 + 0x80, *v2 = tm2 + 0x40, *v3 = tm2 + 0xc0, *v4 = tm2 + 0x20, *v5 = tm2 + 0xa0, *v6 = tm2 + 0x60, *v7 = tm2 + 0xe0,
		// 7 [c,s] pointer-pairs, +=3 between pairs; c0 = tmp+1 to point to cos [middle] term of each [isrt2,c,s] triplet:
		*c1=tmp+1,*s1=tmp+2, *c2=c1+3,*s2=c1+4, *c3=c2+3,*s3=c2+4, *c4=c3+3,*s4=c3+4, *c5=c4+3,*s5=c4+4, *c6=c5+3,*s6=c5+4, *c7=c6+3,*s7=c6+4;
/*
u0->d0 = 3; u0->d1 = 5;
u1->d0 = 1; u1->d1 = 3;
u2->d0 = 4; u2->d1 = 5;
u3->d0 = 1; u3->d1 = 8;
u4->d0 = 5; u4->d1 = 9;
u5->d0 = 9; u5->d1 = 7;
u6->d0 = 2; u6->d1 = 9;
u7->d0 = 6; u7->d1 = 3;
*/
		SSE2_RADIX8_DIT_TWIDDLE_OOP(
			u0,u1,u2,u3,u4,u5,u6,u7,
			v0,v1,v2,v3,v4,v5,v6,v7,
			c1,s1, c2,s2, c3,s3, c4,s4, c5,s5, c6,s6, c7,s7
		);
/*
printf("Radix-8 outs:\n");
printf("0: %15.10f, %15.10f\n",v0->d0,v0->d1);
printf("1: %15.10f, %15.10f\n",v1->d0,v1->d1);
printf("2: %15.10f, %15.10f\n",v2->d0,v2->d1);
printf("3: %15.10f, %15.10f\n",v3->d0,v3->d1);
printf("4: %15.10f, %15.10f\n",v4->d0,v4->d1);
printf("5: %15.10f, %15.10f\n",v5->d0,v5->d1);
printf("6: %15.10f, %15.10f\n",v6->d0,v6->d1);
printf("7: %15.10f, %15.10f\n",v7->d0,v7->d1);
if(l==15)exit(0);
*/
	}

  #else

	//...and another kludge for the 30-arg limit: put a copy of (vec_dbl)2.0 into the first of each s1p*r outputs:
	VEC_DBL_INIT(s1p01,2.0);
	VEC_DBL_INIT(s1p02,2.0);
	VEC_DBL_INIT(s1p03,2.0);
	VEC_DBL_INIT(s1p04,2.0);
	VEC_DBL_INIT(s1p05,2.0);
	VEC_DBL_INIT(s1p06,2.0);
	VEC_DBL_INIT(s1p07,2.0);
	VEC_DBL_INIT(s1p08,2.0);
	VEC_DBL_INIT(s1p09,2.0);
	VEC_DBL_INIT(s1p0a,2.0);
	VEC_DBL_INIT(s1p0b,2.0);
	VEC_DBL_INIT(s1p0c,2.0);
	VEC_DBL_INIT(s1p0d,2.0);
	VEC_DBL_INIT(s1p0e,2.0);
	VEC_DBL_INIT(s1p0f,2.0);

/* Block 8: */	jt = j1 + p08;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r08,r18,r28,r38,r48,r58,r68,r78,
		s1p08,s1p48,s1p28,s1p68,s1p18,s1p58,s1p38,s1p78,
		ss0,cc0,isrt2,isrt2,nisrt2,isrt2,cc16,ss16,nss16,cc16,ss16,cc16,ncc16,ss16
	);
/* Block 4: */	jt = j1 + p04;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r04,r14,r24,r34,r44,r54,r64,r74,
		s1p04,s1p44,s1p24,s1p64,s1p14,s1p54,s1p34,s1p74,
		isrt2,isrt2,cc16,ss16,ss16,cc16,cc32_1,ss32_1,ss32_3,cc32_3,cc32_3,ss32_3,ss32_1,cc32_1
	);
/* Block C: */	jt = j1 + p0c;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0C,r1C,r2C,r3C,r4C,r5C,r6C,r7C,
		s1p0c,s1p4c,s1p2c,s1p6c,s1p1c,s1p5c,s1p3c,s1p7c,
		nisrt2,isrt2,ss16,cc16,ncc16,nss16,cc32_3,ss32_3,ncc32_1,ss32_1,nss32_1,cc32_1,nss32_3,ncc32_3
	);
/* Block 2: */	jt = j1 + p02;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r02,r12,r22,r32,r42,r52,r62,r72,
		s1p02,s1p42,s1p22,s1p62,s1p12,s1p52,s1p32,s1p72,
		cc16,ss16,cc32_1,ss32_1,cc32_3,ss32_3,cc64_1,ss64_1,cc64_5,ss64_5,cc64_3,ss64_3,cc64_7,ss64_7
	);
/* Block A: */	jt = j1 + p0a;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0A,r1A,r2A,r3A,r4A,r5A,r6A,r7A,
		s1p0a,s1p4a,s1p2a,s1p6a,s1p1a,s1p5a,s1p3a,s1p7a,
		nss16,cc16,ss32_3,cc32_3,ncc32_1,ss32_1,cc64_5,ss64_5,ncc64_7,ss64_7,ss64_1,cc64_1,ncc64_3,nss64_3
	);
/* Block 6: */	jt = j1 + p06;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r06,r16,r26,r36,r46,r56,r66,r76,
		s1p06,s1p46,s1p26,s1p66,s1p16,s1p56,s1p36,s1p76,
		ss16,cc16,cc32_3,ss32_3,nss32_1,cc32_1,cc64_3,ss64_3,ss64_1,cc64_1,ss64_7,cc64_7,nss64_5,cc64_5
	);
/* Block E: */	jt = j1 + p0e;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0E,r1E,r2E,r3E,r4E,r5E,r6E,r7E,
		s1p0e,s1p4e,s1p2e,s1p6e,s1p1e,s1p5e,s1p3e,s1p7e,
		ncc16,ss16,ss32_1,cc32_1,nss32_3,ncc32_3,cc64_7,ss64_7,ncc64_3,nss64_3,nss64_5,cc64_5,ss64_1,ncc64_1
	);
/* Block 1: */	jt = j1 + p01;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r01,r11,r21,r31,r41,r51,r61,r71,
		s1p01,s1p41,s1p21,s1p61,s1p11,s1p51,s1p31,s1p71,
		cc32_1,ss32_1, cc64_1,ss64_1, cc64_3,ss64_3, cc128_1,ss128_1, cc128_5,ss128_5, cc128_3,ss128_3, cc128_7,ss128_7
	);
/* Block 9: */	jt = j1 + p09;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r09,r19,r29,r39,r49,r59,r69,r79,
		s1p09 ,s1p49 ,s1p29 ,s1p69 ,s1p19 ,s1p59 ,s1p39 ,s1p79 ,
		nss32_1,cc32_1, ss64_7,cc64_7, ncc64_5,ss64_5, cc128_9,ss128_9, nss128_d,cc128_d, ss128_5,cc128_5, ncc128_1,ss128_1
	);
/* Block 5: */	jt = j1 + p05;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r05,r15,r25,r35,r45,r55,r65,r75,
		s1p05,s1p45,s1p25,s1p65,s1p15,s1p55,s1p35,s1p75,
		ss32_3,cc32_3, cc64_5,ss64_5, ss64_1,cc64_1, cc128_5,ss128_5, ss128_7,cc128_7, cc128_f,ss128_f, nss128_3,cc128_3
	);
/* Block D: */	jt = j1 + p0d;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0D,r1D,r2D,r3D,r4D,r5D,r6D,r7D,
		s1p0d,s1p4d,s1p2d,s1p6d,s1p1d,s1p5d,s1p3d,s1p7d,
		ncc32_3,ss32_3, ss64_3,cc64_3, ncc64_7,nss64_7, cc128_d,ss128_d, ncc128_1,nss128_1, nss128_7,cc128_7, nss128_5,ncc128_5
	);
/* Block 3: */	jt = j1 + p03;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r03,r13,r23,r33,r43,r53,r63,r73,
		s1p03,s1p43,s1p23,s1p63,s1p13,s1p53,s1p33,s1p73,
		cc32_3,ss32_3, cc64_3,ss64_3, ss64_7,cc64_7, cc128_3,ss128_3, cc128_f,ss128_f, cc128_9,ss128_9, ss128_b,cc128_b
	);
/* Block B: */	jt = j1 + p0b;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0B,r1B,r2B,r3B,r4B,r5B,r6B,r7B,
		s1p0b,s1p4b,s1p2b,s1p6b,s1p1b,s1p5b,s1p3b,s1p7b,
		nss32_3,cc32_3, ss64_5,cc64_5, ncc64_1,nss64_1, cc128_b,ss128_b, ncc128_9,ss128_9, nss128_1,cc128_1, ncc128_d,nss128_d
	);
/* Block 7: */	jt = j1 + p07;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r07,r17,r27,r37,r47,r57,r67,r77,
		s1p07,s1p47,s1p27,s1p67,s1p17,s1p57,s1p37,s1p77,
		ss32_1,cc32_1, cc64_7,ss64_7, nss64_5,cc64_5, cc128_7,ss128_7, nss128_3,cc128_3, ss128_b,cc128_b, ncc128_f,ss128_f
	);
/* Block F: */	jt = j1 + p0f;
	SSE2_RADIX8_DIT_TWIDDLE_OOP(
		r0F,r1F,r2F,r3F,r4F,r5F,r6F,r7F,
		s1p0f,s1p4f,s1p2f,s1p6f,s1p1f,s1p5f,s1p3f,s1p7f,
		ncc32_1,ss32_1, ss64_1,cc64_1, nss64_3,ncc64_3, cc128_f,ss128_f, ncc128_b,nss128_b, nss128_d,cc128_d, ss128_9,ncc128_9
	);

  #endif	// USE_COMPACT_OBJ_CODE ?

#else	// USE_SSE2 = False, or non-64-bit-GCC:

/* Gather the needed data (128 64-bit complex, i.e. 256 64-bit reals) and do 8 twiddleless length-16 subtransforms: */

	tptr = t;
	for(l = 0; l < 8; l++) {
		k2 = poff[l<<2];	// poff[4*l] = p00,10,...,60,70
		jt = j1 + k2; jp = j2 + k2;
		RADIX_16_DIT(
			a[jt],a[jp],a[jt+p01],a[jp+p01],a[jt+p02],a[jp+p02],a[jt+p03],a[jp+p03],a[jt+p04],a[jp+p04],a[jt+p05],a[jp+p05],a[jt+p06],a[jp+p06],a[jt+p07],a[jp+p07],a[jt+p08],a[jp+p08],a[jt+p09],a[jp+p09],a[jt+p0a],a[jp+p0a],a[jt+p0b],a[jp+p0b],a[jt+p0c],a[jp+p0c],a[jt+p0d],a[jp+p0d],a[jt+p0e],a[jp+p0e],a[jt+p0f],a[jp+p0f],
			tptr->re,tptr->im,(tptr+1)->re,(tptr+1)->im,(tptr+2)->re,(tptr+2)->im,(tptr+3)->re,(tptr+3)->im,(tptr+4)->re,(tptr+4)->im,(tptr+5)->re,(tptr+5)->im,(tptr+6)->re,(tptr+6)->im,(tptr+7)->re,(tptr+7)->im,(tptr+8)->re,(tptr+8)->im,(tptr+9)->re,(tptr+9)->im,(tptr+10)->re,(tptr+10)->im,(tptr+11)->re,(tptr+11)->im,(tptr+12)->re,(tptr+12)->im,(tptr+13)->re,(tptr+13)->im,(tptr+14)->re,(tptr+14)->im,(tptr+15)->re,(tptr+15)->im,
			c16,s16
		);
		tptr += 16;
	}

/*...and now do 16 radix-8 subtransforms, including the internal twiddle factors - we use the same positive-power
roots as in the DIF here, just fiddle with signs within the macro to effect the conjugate-multiplies. Twiddles occur
in the same order here as DIF, but the in-and-output-index offsets are BRed: j1 + p[0,8,4,c,2,a,6,e,1,9,5,d,3,b,7,f],
as are the index offsets of each sets of complex outputs in the A-array: [jt,jp] + p10*[0,4,2,6,1,5,3,7]:
*/
	/* Block 0: */
	tptr = t;	jt = j1;	jp = j2;
	/* 0-index block has all-unity twiddles: Remember, the twiddleless DIT also bit-reverses its outputs, so a_p* terms appear in-order here: */
	RADIX_08_DIT_OOP(
		tptr->re,tptr->im,(tptr+0x10)->re,(tptr+0x10)->im,(tptr+0x20)->re,(tptr+0x20)->im,(tptr+0x30)->re,(tptr+0x30)->im,(tptr+0x40)->re,(tptr+0x40)->im,(tptr+0x50)->re,(tptr+0x50)->im,(tptr+0x60)->re,(tptr+0x60)->im,(tptr+0x70)->re,(tptr+0x70)->im,
		a[jt],a[jp],a[jt+p10],a[jp+p10],a[jt+p20],a[jp+p20],a[jt+p30],a[jp+p30],a[jt+p40],a[jp+p40],a[jt+p50],a[jp+p50],a[jt+p60],a[jp+p60],a[jt+p70],a[jp+p70]
	);

	// Remaining 15 sets of macro calls done in loop:
	for(l = 1; l < 16; l++) {
		tptr = t + reverse(l,16);
		k2 = po_br[l];	// po_br[] = p[084c2a6e195d3b7f]
		jt = j1 + k2; jp = j2 + k2;
		addr = DFT128_TWIDDLES[l]; addi = addr+1;	// Pointer to required row of 2-D twiddles array
		RADIX_08_DIT_TWIDDLE_OOP(
			tptr->re,tptr->im,(tptr+0x10)->re,(tptr+0x10)->im,(tptr+0x20)->re,(tptr+0x20)->im,(tptr+0x30)->re,(tptr+0x30)->im,(tptr+0x40)->re,(tptr+0x40)->im,(tptr+0x50)->re,(tptr+0x50)->im,(tptr+0x60)->re,(tptr+0x60)->im,(tptr+0x70)->re,(tptr+0x70)->im,
			a[jt],a[jp],a[jt+p40],a[jp+p40],a[jt+p20],a[jp+p20],a[jt+p60],a[jp+p60],a[jt+p10],a[jp+p10],a[jt+p50],a[jp+p50],a[jt+p30],a[jp+p30],a[jt+p70],a[jp+p70],
			*(addr+0x00),*(addi+0x00), *(addr+0x02),*(addi+0x02), *(addr+0x04),*(addi+0x04), *(addr+0x06),*(addi+0x06), *(addr+0x08),*(addi+0x08), *(addr+0x0a),*(addi+0x0a), *(addr+0x0c),*(addi+0x0c)
		);
	}

#endif	// USE_SSE2 ?

/*...Now do the carries. Since the outputs would
normally be getting dispatched to [radix] separate blocks of the A-array, we need [radix] separate carries.	*/

/************ See the radix16_ditN_cy_dif1 routine for details on how the SSE2 carry stuff works **********/
	if(MODULUS_TYPE == MODULUS_TYPE_MERSENNE)
	{
	#ifdef USE_AVX

		add1 = &wt1[col  ];
		add2 = &wt1[co2-1];
		add3 = &wt1[co3-1];

		l= j & (nwt-1);						tmp = half_arr + 64;	/* ptr to local storage for the doubled wtl,wtn terms: */
		n_minus_sil  ->d0 = n-si[l  ];		tmp->d0 = wt0[    l  ];
		n_minus_silp1->d0 = n-si[l+1];		tmp->d1 = wt0[nwt-l  ]*scale;
		sinwt        ->d0 = si[nwt-l  ];	tmp->d2 = wt0[    l+1];
		sinwtm1      ->d0 = si[nwt-l-1];	tmp->d3 = wt0[nwt-l-1]*scale;

		l= (j+2) & (nwt-1);					++tmp;	/* Get ready for next 4 weights-related doubles... */
		n_minus_sil  ->d1 = n-si[l  ];		tmp->d0 = wt0[    l  ];
		n_minus_silp1->d1 = n-si[l+1];		tmp->d1 = wt0[nwt-l  ]*scale;
		sinwt        ->d1 = si[nwt-l  ];	tmp->d2 = wt0[    l+1];
		sinwtm1      ->d1 = si[nwt-l-1];	tmp->d3 = wt0[nwt-l-1]*scale;

		l= (j+4) & (nwt-1);					++tmp;	/* Get ready for next 4 weights-related doubles... */
		n_minus_sil  ->d2 = n-si[l  ];		tmp->d0 = wt0[    l  ];
		n_minus_silp1->d2 = n-si[l+1];		tmp->d1 = wt0[nwt-l  ]*scale;
		sinwt        ->d2 = si[nwt-l  ];	tmp->d2 = wt0[    l+1];
		sinwtm1      ->d2 = si[nwt-l-1];	tmp->d3 = wt0[nwt-l-1]*scale;

		l= (j+6) & (nwt-1);					++tmp;	/* Get ready for next 4 weights-related doubles... */
		n_minus_sil  ->d3 = n-si[l  ];		tmp->d0 = wt0[    l  ];
		n_minus_silp1->d3 = n-si[l+1];		tmp->d1 = wt0[nwt-l  ]*scale;
		sinwt        ->d3 = si[nwt-l  ];	tmp->d2 = wt0[    l+1];
		sinwtm1      ->d3 = si[nwt-l-1];	tmp->d3 = wt0[nwt-l-1]*scale;

	/* In AVX mode advance carry-ptrs just 1 for each vector-carry-macro call: */
		tm1 = s1p00; tmp = cy_r; itmp = bjmodn;
		// Each AVX carry macro call also processes 4 prefetches of main-array data
		add0 = a + j1 + pfetch_dist;
		AVX_cmplx_carry_norm_pow2_errcheck0_X4(tm1,add1,add2,add3,tmp,itmp,half_arr,i,n_minus_silp1,n_minus_sil,sign_mask,sinwt,sinwtm1,sse_bw,sse_nm1,sse_sw, add0,p01,p02,p03);
		tm1 += 8; tmp++; itmp += 4;
		for(l = 1; l < RADIX>>2; l++) {
			// Each AVX carry macro call also processes 4 prefetches of main-array data
			add0 = a + j1 + pfetch_dist + poff[l];
			AVX_cmplx_carry_norm_pow2_errcheck1_X4(tm1,add1,add2,add3,tmp,itmp,half_arr,  n_minus_silp1,n_minus_sil,sign_mask,sinwt,sinwtm1,sse_bw,sse_nm1,sse_sw, add0,p01,p02,p03);
			tm1 += 8; tmp++; itmp += 4;
		}

		co2 = co3;	// For all data but the first set in each j-block, co2=co3. Thus, after the first block of data is done
					// (and only then: for all subsequent blocks it's superfluous), this assignment decrements co2 by radix(1).

		i =((uint32)(sw - bjmodn[0]) >> 31);	/* get ready for the next set...	*/

	#elif defined(USE_SSE2)

		l= j & (nwt-1);			/* We want (S*J mod N) - SI(L) for all 32 carries, so precompute	*/
		n_minus_sil   = n-si[l  ];		/* N - SI(L) and for each J, find N - (B*J mod N) - SI(L)		*/
		n_minus_silp1 = n-si[l+1];		/* For the inverse weight, want (S*(N - J) mod N) - SI(NWT - L) =	*/
		sinwt   = si[nwt-l  ];		/*	= N - (S*J mod N) - SI(NWT - L) = (B*J mod N) - SI(NWT - L).	*/
		sinwtm1 = si[nwt-l-1];

		wtl     =wt0[    l  ];
		wtn     =wt0[nwt-l  ]*scale;	/* Include 1/(n/2) scale factor of inverse transform here...	*/
		wtlp1   =wt0[    l+1];
		wtnm1   =wt0[nwt-l-1]*scale;	/* ...and here.	*/

		ctmp = (struct complex *)half_arr + 16;	/* ptr to local storage for the doubled wtl,wtn terms: */
		ctmp->re = ctmp->im = wtl;		++ctmp;
		ctmp->re = ctmp->im = wtn;		++ctmp;
		ctmp->re = ctmp->im = wtlp1;	++ctmp;
		ctmp->re = ctmp->im = wtnm1;

		add1 = &wt1[col  ];
		add2 = &wt1[co2-1];
		add3 = &wt1[co3-1];

		tm1 = s1p00; tmp = cy_r; tm2 = cy_r+0x01; itmp = bjmodn;
		// Each SSE2 carry macro call also processes 2 prefetches of main-array data
		add0 = a + j1 + pfetch_dist;
		SSE2_cmplx_carry_norm_pow2_errcheck0_2B(tm1,add1,add2,add3,tmp,tm2,itmp,half_arr,i,n_minus_silp1,n_minus_sil,sign_mask,sinwt,sinwtm1,sse_bw,sse_nm1,sse_sw, add0,p01);	tm1 += 8; tmp += 2; tm2 += 2; itmp += 4;
		for(l = 1; l < RADIX>>2; l++) {
			// Each SSE2 carry macro call also processes 2 prefetches of main-array data
			add0 = a + j1 + pfetch_dist + poff[l];	// poff[] = p0,4,8,...
			add0 += (-(l&0x1)) & p02;	// Base-addr incr by extra p2 on odd-index passes
			SSE2_cmplx_carry_norm_pow2_errcheck1_2B(tm1,add1,add2,add3,tmp,tm2,itmp,half_arr,  n_minus_silp1,n_minus_sil,sign_mask,sinwt,sinwtm1,sse_bw,sse_nm1,sse_sw, add0,p01);	tm1 += 8; tmp += 2; tm2 += 2; itmp += 4;
		}

		l= (j+2) & (nwt-1);			/* We want (S*J mod N) - SI(L) for all 16 carries, so precompute	*/
		n_minus_sil   = n-si[l  ];		/* N - SI(L) and for each J, find N - (B*J mod N) - SI(L)		*/
		n_minus_silp1 = n-si[l+1];		/* For the inverse weight, want (S*(N - J) mod N) - SI(NWT - L) =	*/
		sinwt   = si[nwt-l  ];		/*	= N - (S*J mod N) - SI(NWT - L) = (B*J mod N) - SI(NWT - L).	*/
		sinwtm1 = si[nwt-l-1];

		wtl     =wt0[    l  ];
		wtn     =wt0[nwt-l  ]*scale;	/* Include 1/(n/2) scale factor of inverse transform here...	*/
		wtlp1   =wt0[    l+1];
		wtnm1   =wt0[nwt-l-1]*scale;	/* ...and here.	*/

		ctmp = (struct complex *)half_arr + 16;	/* ptr to local storage for the doubled wtl,wtn terms: */
		ctmp->re = ctmp->im = wtl;		++ctmp;
		ctmp->re = ctmp->im = wtn;		++ctmp;
		ctmp->re = ctmp->im = wtlp1;	++ctmp;
		ctmp->re = ctmp->im = wtnm1;

		co2 = co3;	// For all data but the first set in each j-block, co2=co3. Thus, after the first block of data is done
					// (and only then: for all subsequent blocks it's superfluous), this assignment decrements co2 by radix(1).
		add1 = &wt1[col  ];
		add2 = &wt1[co2-1];

		tm1 = s1p00; tmp = cy_r; tm2 = cy_r+0x01; itmp = bjmodn;
		for(l = 0; l < RADIX>>2; l++) {
			// Each SSE2 carry macro call also processes 2 prefetches of main-array data
			add0 = a + j1 + pfetch_dist + poff[l];	// poff[] = p0,4,8,...
			add0 += (-(l&0x1)) & p02;	// Base-addr incr by extra p2 on odd-index passes
			SSE2_cmplx_carry_norm_pow2_errcheck2_2B(tm1,add1,add2,     tmp,tm2,itmp,half_arr,  n_minus_silp1,n_minus_sil,sign_mask,sinwt,sinwtm1,sse_bw,sse_nm1,sse_sw, add0,p02,p03);	tm1 += 8; tmp += 2; tm2 += 2; itmp += 4;
		}

		i =((uint32)(sw - bjmodn[0]) >> 31);	/* get ready for the next set...	*/

	#else	// Scalar-double mode:

		l= j & (nwt-1);			/* We want (S*J mod N) - SI(L) for all 32 carries, so precompute	*/
		n_minus_sil   = n-si[l  ];		/* N - SI(L) and for each J, find N - (B*J mod N) - SI(L)		*/
		n_minus_silp1 = n-si[l+1];		/* For the inverse weight, want (S*(N - J) mod N) - SI(NWT - L) =	*/
		sinwt   = si[nwt-l  ];		/*	= N - (S*J mod N) - SI(NWT - L) = (B*J mod N) - SI(NWT - L).	*/
		sinwtm1 = si[nwt-l-1];

		wtl     =wt0[    l  ];
		wtn     =wt0[nwt-l  ]*scale;	/* Include 1/(n/2) scale factor of inverse transform here...	*/
		wtlp1   =wt0[    l+1];
		wtnm1   =wt0[nwt-l-1]*scale;	/* ...and here.	*/

		/*...set0 is slightly different from others; divide work into blocks of RADIX/4 macro calls, 1st set of which gets pulled out of loop: */

		l = 0; addr = cy_r; itmp = bjmodn;
	   cmplx_carry_norm_pow2_errcheck0(a[j1    ],a[j2    ],*addr,*itmp  ); ++l; ++addr; ++itmp;
		cmplx_carry_norm_pow2_errcheck(a[j1+p01],a[j2+p01],*addr,*itmp,l); ++l; ++addr; ++itmp;
		cmplx_carry_norm_pow2_errcheck(a[j1+p02],a[j2+p02],*addr,*itmp,l); ++l; ++addr; ++itmp;
		cmplx_carry_norm_pow2_errcheck(a[j1+p03],a[j2+p03],*addr,*itmp,l); ++l; ++addr; ++itmp;
		// Remaining quartets of macro calls done in loop:
		for(ntmp = 1; ntmp < RADIX>>2; ntmp++) {
			jt = j1 + poff[ntmp]; jp = j2 + poff[ntmp];	// poff[] = p04,08,...,60
			cmplx_carry_norm_pow2_errcheck(a[jt    ],a[jp    ],*addr,*itmp,l); ++l; ++addr; ++itmp;
			cmplx_carry_norm_pow2_errcheck(a[jt+p01],a[jp+p01],*addr,*itmp,l); ++l; ++addr; ++itmp;
			cmplx_carry_norm_pow2_errcheck(a[jt+p02],a[jp+p02],*addr,*itmp,l); ++l; ++addr; ++itmp;
			cmplx_carry_norm_pow2_errcheck(a[jt+p03],a[jp+p03],*addr,*itmp,l); ++l; ++addr; ++itmp;
		}

		i =((uint32)(sw - bjmodn[0]) >> 31);	/* get ready for the next set...	*/
		co2=co3;	/* For all data but the first set in each j-block, co2=co3. Thus, after the first block of data is done
				 and only then: for all subsequent blocks it's superfluous), this assignment decrements co2 by radix(1).	*/

	#endif	// USE_AVX?
	}
	else	/* Fermat-mod carry in SIMD mode */
	{
	#ifdef USE_AVX

		// For a description of the data movement for Fermat-mod carries in SSE2 mode, see radix16_ditN_cy_dif1.c.
		// For a description of the data movement in AVX mode, see radix28_ditN_cy_dif1.

		tmp = half_arr+2;
		VEC_DBL_INIT(tmp, scale);
		/* Get the needed Nth root of -1: */
		add1 = (double *)&rn0[0];
		add2 = (double *)&rn1[0];

		idx_offset = j;
		idx_incr = NDIVR;

		tmp = base_negacyclic_root;	tm2 = tmp+1;

		// Hi-accuracy version needs 8 copies of each base root:
		l = (j >> 1);	k1=(l & NRTM1);	k2=(l >> NRT_BITS);
		dtmp=rn0[k1].re;			wt_im=rn0[k1].im;
		rt  =rn1[k2].re;			it   =rn1[k2].im;
		wt_re =dtmp*rt-wt_im*it;	wt_im =dtmp*it+wt_im*rt;
		for(i = 0; i < (RADIX << 1); i += 8) {
			VEC_DBL_INIT(tmp+ i,wt_re);	VEC_DBL_INIT(tm2+ i,wt_im);
		}
		tmp += 2;	tm2 += 2;
		l += 1;	k1=(l & NRTM1);	k2=(l >> NRT_BITS);
		dtmp=rn0[k1].re;			wt_im=rn0[k1].im;
		rt  =rn1[k2].re;			it   =rn1[k2].im;
		wt_re =dtmp*rt-wt_im*it;	wt_im =dtmp*it+wt_im*rt;
		for(i = 0; i < (RADIX << 1); i += 8) {
			VEC_DBL_INIT(tmp+ i,wt_re);	VEC_DBL_INIT(tm2+ i,wt_im);
		}
		tmp += 2;	tm2 += 2;
		l += 1;	k1=(l & NRTM1);	k2=(l >> NRT_BITS);
		dtmp=rn0[k1].re;			wt_im=rn0[k1].im;
		rt  =rn1[k2].re;			it   =rn1[k2].im;
		wt_re =dtmp*rt-wt_im*it;	wt_im =dtmp*it+wt_im*rt;
		for(i = 0; i < (RADIX << 1); i += 8) {
			VEC_DBL_INIT(tmp+ i,wt_re);	VEC_DBL_INIT(tm2+ i,wt_im);
		}
		tmp += 2;	tm2 += 2;
		l += 1;	k1=(l & NRTM1);	k2=(l >> NRT_BITS);
		dtmp=rn0[k1].re;			wt_im=rn0[k1].im;
		rt  =rn1[k2].re;			it   =rn1[k2].im;
		wt_re =dtmp*rt-wt_im*it;	wt_im =dtmp*it+wt_im*rt;
		for(i = 0; i < (RADIX << 1); i += 8) {
			VEC_DBL_INIT(tmp+ i,wt_re);	VEC_DBL_INIT(tm2+ i,wt_im);
		}

		// AVX-custom 4-way carry macro - each contains 4 of the RADIX stride-n/RADIX-separated carries
		// (processed independently in parallel), and steps through sequential-data indices j,j+2,j+4,j+6:

	// The starting value of the literal pointer offsets following 'tmp' in these macro calls = RADIX*2*sizeof(vec_dbl) = 2^12
	// which is the byte offset between the 'active' negacyclic weights [pointed to by base_negacyclic_root] and the
	// precomputed multipliers in the HIACC-wrapped section of the SIMD data initializations. Each 0x100-byte quartet of base roots
	// uses the same 0x40-byte up-multiplier, so the literal offsets advance (+0x100-0x40) = -0xc0 bytes between macro calls:
		tm0 = s1p00; tmp = base_negacyclic_root; tm1 = cy_r; tm2 = cy_i; l = 0x2000;
		for(i = 0; i < RADIX>>2; i++) {	// RADIX/4 loop passes
			// Each AVX carry macro call also processes 4 prefetches of main-array data
			add0 = a + j1 + pfetch_dist + poff[i];	// Can't use tm2 as prefetch base-ptr for pow2 Fermat-mod carry-macro since that's used for cy_i
			SSE2_fermat_carry_norm_pow2_errcheck_X4(tm0,tmp,l,tm1,tm2,half_arr,sign_mask, add0,p01,p02,p03);
			tm0 += 8; tm1++; tm2++; tmp += 8; l -= 0xc0;
		}

	#elif defined(USE_SSE2)

		// For a description of the data movement for Fermat-mod carries in SSE2 mode, see radix16_ditN_cy_dif1.c.
		// For a description of the data movement in AVX mode, see radix28_ditN_cy_dif1.

		tmp = half_arr+2;
		VEC_DBL_INIT(tmp, scale);
		/* Get the needed Nth root of -1: */
		add1 = (double *)&rn0[0];
		add2 = (double *)&rn1[0];

		idx_offset = j;
		idx_incr = NDIVR;

		tm1 = s1p00; tmp = cy_r;	/* <*** Again rely on contiguity of cy_r,i here ***/
	  #if (OS_BITS == 32)
		for(l = 0; l < RADIX; l++) {	// RADIX loop passes
			// Each SSE2 carry macro call also processes 1 prefetch of main-array data
			tm2 = a + j1 + pfetch_dist + poff[l];	// poff[] = p0,4,8,...
			tm2 += (-(l&0x10)) & p02;
			tm2 += (-(l&0x01)) & p01;
			SSE2_fermat_carry_norm_pow2_errcheck   (tm1,tmp,NRT_BITS,NRTM1,idx_offset,idx_incr,half_arr,sign_mask,add1,add2, tm2);
			tm1 += 2; tmp++;
		}
	  #else	// 64-bit SSE2
		for(l = 0; l < RADIX>>1; l++) {	// RADIX/2 loop passes
			// Each SSE2 carry macro call also processes 2 prefetches of main-array data
			tm2 = a + j1 + pfetch_dist + poff[l];	// poff[] = p0,4,8,...; (tm1-cy_r) acts as a linear loop index running from 0,...,RADIX-1 here.
			tm2 += (-(l&0x1)) & p02;	// Base-addr incr by extra p2 on odd-index passes
			SSE2_fermat_carry_norm_pow2_errcheck_X2(tm1,tmp,NRT_BITS,NRTM1,idx_offset,idx_incr,half_arr,sign_mask,add1,add2, tm2,p01);
			tm1 += 4; tmp += 2;
		}
	  #endif

	#else	// Scalar-double mode:

		// Can't use l as loop index here, since it gets used in the Fermat-mod carry macro (as are k1,k2);
		ntmp = 0; addr = cy_r; addi = cy_i;
		for(m = 0; m < RADIX>>2; m++) {
			jt = j1 + poff[m]; jp = j2 + poff[m];	// poff[] = p04,08,...,60
			fermat_carry_norm_pow2_errcheck(a[jt    ],a[jp    ],*addr,*addi,ntmp,NRTM1,NRT_BITS);	ntmp += NDIVR; ++addr; ++addi;
			fermat_carry_norm_pow2_errcheck(a[jt+p01],a[jp+p01],*addr,*addi,ntmp,NRTM1,NRT_BITS);	ntmp += NDIVR; ++addr; ++addi;
			fermat_carry_norm_pow2_errcheck(a[jt+p02],a[jp+p02],*addr,*addi,ntmp,NRTM1,NRT_BITS);	ntmp += NDIVR; ++addr; ++addi;
			fermat_carry_norm_pow2_errcheck(a[jt+p03],a[jp+p03],*addr,*addi,ntmp,NRTM1,NRT_BITS);	ntmp += NDIVR; ++addr; ++addi;
		}

	#endif	/* #ifdef USE_SSE2 */

	}	/* if(MODULUS_TYPE == ...) */

/*...The radix-128 DIF pass is here:	*/

#ifdef USE_SSE2

// Gather the needed data and do 8 twiddleless length-16 subtransforms, with p-offsets in br8 order: 04261537:
// NOTE that unlike the RADIX_08_DIF_OOP() macro used for pass 1 of the radix-64 DFT, RADIX_16_DIF outputs are IN-ORDER rather than BR:

  #ifdef USE_AVX
	#define OFF1	0x200
	#define OFF2	0x400
	#define OFF3	0x600
	#define OFF4	0x800
  #else
	#define OFF1	0x100
	#define OFF2	0x200
	#define OFF3	0x300
	#define OFF4	0x400
  #endif
// NOTE that unlike the RADIX_08_DIF_OOP() macro used for pass 1 of the radix-64 DFT, RADIX_16_DIF outputs are IN-ORDER rather than BR:
	// Start of isrt2/cc16/ss16 triplet needed for radix-16 SSE2 DFT macros [use instead of isrt2 here]
  #if USE_COMPACT_OBJ_CODE
	tmp = isrt2;	// In USE_COMPACT_OBJ_CODE mode, use that [isrt2] ptr heads up the needed isrt2/cc16/ss16 pointer-triplet
					// (actually a quartet in AVX2 mode, since there need sqrt2 preceding isrt2)
  #else
	tmp = sqrt2+1;	// In non-compact-obj-code mode, access the same (unnamed) pointer this way
  #endif
	tm1 = r00;
	for(l = 0; l < 8; l++) {
		k1 = reverse(l,8)<<1;
		tm2 = s1p00 + k1;
	#if (OS_BITS == 32)
								 add1 = (vec_dbl*)tm1+ 2; add2 = (vec_dbl*)tm1+ 4; add3 = (vec_dbl*)tm1+ 6; add4 = (vec_dbl*)tm1+ 8; add5 = (vec_dbl*)tm1+10; add6 = (vec_dbl*)tm1+12; add7 = (vec_dbl*)tm1+14;
		add8 = (vec_dbl*)tm1+16; add9 = (vec_dbl*)tm1+18; adda = (vec_dbl*)tm1+20; addb = (vec_dbl*)tm1+22; addc = (vec_dbl*)tm1+24; addd = (vec_dbl*)tm1+26; adde = (vec_dbl*)tm1+28; addf = (vec_dbl*)tm1+30;
		SSE2_RADIX16_DIF_0TWIDDLE  (tm2,OFF1,OFF2,OFF3,OFF4, tmp,two, tm1,add1,add2,add3,add4,add5,add6,add7,add8,add9,adda,addb,addc,addd,adde,addf);
	#else
		SSE2_RADIX16_DIF_0TWIDDLE_B(tm2,OFF1,OFF2,OFF3,OFF4, tmp,two, tm1);
	#endif
		tm1 += 32;
	}

	#undef OFF1
	#undef OFF2
	#undef OFF3
	#undef OFF4

/*...and now do 16 radix-8 subtransforms, including the internal twiddle factors: */

	/* Block 0: */
	add0 = &a[j1]      ; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	/* 0-index block has all-unity twiddles: Remember, the twiddleless DIF bit-reverses both its in-and-outputs,
	so swap index-offset pairs 1/4 and 3/6 in t*-inputs and a-outputs: */
	SSE2_RADIX8_DIF_0TWIDDLE(
	  #ifdef USE_AVX
		r00,0x1000,0x0800,0x1800,0x0400,0x1400,0x0c00,0x1c00,	//	r00,r40,r20,r60,r10,r50,r30,r70
	  #else	// USE_SSE2
		r00, 0x800, 0x400, 0xc00, 0x200, 0xa00, 0x600, 0xe00,	//	r00,r40,r20,r60,r10,r50,r30,r70
	  #endif
	  #ifdef USE_AVX2
		add0,add1,add2,add3,add4,add5,add6,add7, isrt2,two
	  #else
		add0,add1,add2,add3,add4,add5,add6,add7, isrt2
	  #endif
	);

  #if USE_COMPACT_OBJ_CODE

	// Remaining 15 sets of macro calls done in loop:
	for(l = 1; l < 16; l++) {
		k1 = reverse(l,16);
		// I-ptrs and sincos-ptrs; Twid-offsets are multiples of 21 vec_dbl; +1 to point to cos [middle] term of each [isrt2,c,s] triplet
		tm1 = r00 + (l<<1); tmp = twid0 + (k1<<4)+(k1<<2)+k1;
		vec_dbl
		*u0 = tm1, *u1 = tm1 + 0x80, *u2 = tm1 + 0x40, *u3 = tm1 + 0xc0, *u4 = tm1 + 0x20, *u5 = tm1 + 0xa0, *u6 = tm1 + 0x60, *u7 = tm1 + 0xe0,
		*c1=tmp+1,*s1=tmp+2, *c2=c1+3,*s2=c1+4, *c3=c2+3,*s3=c2+4, *c4=c3+3,*s4=c3+4, *c5=c4+3,*s5=c4+4, *c6=c5+3,*s6=c5+4, *c7=c6+3,*s7=c6+4;
		// O-ptrs: a[j1] offset here = p08,p10,p18,...
		add0 = &a[j1] + poff[l<<1]; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
		VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
/*
u0->d0 = 3; u0->d1 = 5;
u1->d0 = 1; u1->d1 = 3;
u2->d0 = 4; u2->d1 = 5;
u3->d0 = 1; u3->d1 = 8;
u4->d0 = 5; u4->d1 = 9;
u5->d0 = 9; u5->d1 = 7;
u6->d0 = 2; u6->d1 = 9;
u7->d0 = 6; u7->d1 = 3;
*/
		SSE2_RADIX8_DIF_TWIDDLE_OOP(
			u0,u1,u2,u3,u4,u5,u6,u7,
			add0,add1,add2,add3,add4,add5,add6,add7,
			c1,s1, c2,s2, c3,s3, c4,s4, c5,s5, c6,s6, c7,s7
		);
/*
printf("Radix-8 outs:\n");
printf("0: %15.10f, %15.10f\n",((vec_dbl*)add0)->d0,((vec_dbl*)add0)->d1);
printf("1: %15.10f, %15.10f\n",((vec_dbl*)add1)->d0,((vec_dbl*)add1)->d1);
printf("2: %15.10f, %15.10f\n",((vec_dbl*)add2)->d0,((vec_dbl*)add2)->d1);
printf("3: %15.10f, %15.10f\n",((vec_dbl*)add3)->d0,((vec_dbl*)add3)->d1);
printf("4: %15.10f, %15.10f\n",((vec_dbl*)add4)->d0,((vec_dbl*)add4)->d1);
printf("5: %15.10f, %15.10f\n",((vec_dbl*)add5)->d0,((vec_dbl*)add5)->d1);
printf("6: %15.10f, %15.10f\n",((vec_dbl*)add6)->d0,((vec_dbl*)add6)->d1);
printf("7: %15.10f, %15.10f\n",((vec_dbl*)add7)->d0,((vec_dbl*)add7)->d1);
if(l==15)exit(0);
*/
	}

  #else

	/* Block 8: */
	add0 = &a[j1] + p08; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
//...and another kludge for the 30-arg limit: put copies of (vec_dbl)2.0,SQRT2 into the first 2 of each set of outputs.
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r01,r41,r21,r61,r11,r51,r31,r71,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ss0,cc0, isrt2,isrt2, nisrt2,isrt2, cc16,ss16,nss16,cc16,ss16,cc16,ncc16,ss16
	);
	/* Block 4: */
	add0 = &a[j1] + p10; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r02,r42,r22,r62,r12,r52,r32,r72,
		add0,add1,add2,add3,add4,add5,add6,add7,
		isrt2,isrt2,cc16,ss16,ss16,cc16,cc32_1,ss32_1,ss32_3,cc32_3,cc32_3,ss32_3,ss32_1,cc32_1
	);
	/* Block C: */
	add0 = &a[j1] + p18; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r03,r43,r23,r63,r13,r53,r33,r73,
		add0,add1,add2,add3,add4,add5,add6,add7,
		nisrt2,isrt2,ss16,cc16,ncc16,nss16,cc32_3,ss32_3,ncc32_1,ss32_1,nss32_1,cc32_1,nss32_3,ncc32_3
	);
	/* Block 2: */
	add0 = &a[j1] + p20; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r04,r44,r24,r64,r14,r54,r34,r74,
		add0,add1,add2,add3,add4,add5,add6,add7,
		cc16,ss16,cc32_1,ss32_1,cc32_3,ss32_3,cc64_1,ss64_1,cc64_5,ss64_5,cc64_3,ss64_3,cc64_7,ss64_7
	);
	/* Block A: */
	add0 = &a[j1] + p28; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r05,r45,r25,r65,r15,r55,r35,r75,
		add0,add1,add2,add3,add4,add5,add6,add7,
		nss16,cc16,ss32_3,cc32_3,ncc32_1,ss32_1,cc64_5,ss64_5,ncc64_7,ss64_7,ss64_1,cc64_1,ncc64_3,nss64_3
	);
	/* Block 6: */
	add0 = &a[j1] + p30; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r06,r46,r26,r66,r16,r56,r36,r76,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ss16,cc16,cc32_3,ss32_3,nss32_1,cc32_1,cc64_3,ss64_3,ss64_1,cc64_1,ss64_7,cc64_7,nss64_5,cc64_5
	);
	/* Block E: */
	add0 = &a[j1] + p38; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r07,r47,r27,r67,r17,r57,r37,r77,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ncc16,ss16,ss32_1,cc32_1,nss32_3,ncc32_3,cc64_7,ss64_7,ncc64_3,nss64_3,nss64_5,cc64_5,ss64_1,ncc64_1
	);
	/* Block 1: */
	add0 = &a[j1] + p40; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r08,r48,r28,r68,r18,r58,r38,r78,
		add0,add1,add2,add3,add4,add5,add6,add7,
		cc32_1,ss32_1, cc64_1,ss64_1, cc64_3,ss64_3, cc128_1,ss128_1, cc128_5,ss128_5, cc128_3,ss128_3, cc128_7,ss128_7
	);
	/* Block 9: */
	add0 = &a[j1] + p48; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r09,r49,r29,r69,r19,r59,r39,r79,
		add0,add1,add2,add3,add4,add5,add6,add7,
		nss32_1,cc32_1, ss64_7,cc64_7, ncc64_5,ss64_5, cc128_9,ss128_9, nss128_d,cc128_d, ss128_5,cc128_5, ncc128_1,ss128_1
	);
	/* Block 5: */
	add0 = &a[j1] + p50; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0A,r4A,r2A,r6A,r1A,r5A,r3A,r7A,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ss32_3,cc32_3, cc64_5,ss64_5, ss64_1,cc64_1, cc128_5,ss128_5, ss128_7,cc128_7, cc128_f,ss128_f, nss128_3,cc128_3
	);
	/* Block D: */
	add0 = &a[j1] + p58; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0B,r4B,r2B,r6B,r1B,r5B,r3B,r7B,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ncc32_3,ss32_3, ss64_3,cc64_3, ncc64_7,nss64_7, cc128_d,ss128_d, ncc128_1,nss128_1, nss128_7,cc128_7, nss128_5,ncc128_5
	);
	/* Block 3: */
	add0 = &a[j1] + p60; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0C,r4C,r2C,r6C,r1C,r5C,r3C,r7C,
		add0,add1,add2,add3,add4,add5,add6,add7,
		cc32_3,ss32_3, cc64_3,ss64_3, ss64_7,cc64_7, cc128_3,ss128_3, cc128_f,ss128_f, cc128_9,ss128_9, ss128_b,cc128_b
	);
	/* Block B: */
	add0 = &a[j1] + p68; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0D,r4D,r2D,r6D,r1D,r5D,r3D,r7D,
		add0,add1,add2,add3,add4,add5,add6,add7,
		nss32_3,cc32_3, ss64_5,cc64_5, ncc64_1,nss64_1, cc128_b,ss128_b, ncc128_9,ss128_9, nss128_1,cc128_1, ncc128_d,nss128_d
	);
	/* Block 7: */
	add0 = &a[j1] + p70; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0E,r4E,r2E,r6E,r1E,r5E,r3E,r7E,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ss32_1,cc32_1, cc64_7,ss64_7, nss64_5,cc64_5, cc128_7,ss128_7, nss128_3,cc128_3, ss128_b,cc128_b, ncc128_f,ss128_f
	);
	/* Block F: */
	add0 = &a[j1] + p78; add1 = add0+p01; add2 = add0+p02; add3 = add0+p03; add4 = add0+p04; add5 = add0+p05; add6 = add0+p06; add7 = add0+p07;
	VEC_DBL_INIT((vec_dbl *)add0,2.0);	VEC_DBL_INIT((vec_dbl *)add1,SQRT2);
	SSE2_RADIX8_DIF_TWIDDLE_OOP(
		r0F,r4F,r2F,r6F,r1F,r5F,r3F,r7F,
		add0,add1,add2,add3,add4,add5,add6,add7,
		ncc32_1,ss32_1, ss64_1,cc64_1, nss64_3,ncc64_3, cc128_f,ss128_f, ncc128_b,nss128_b, nss128_d,cc128_d, ss128_9,ncc128_9
	);

  #endif	// USE_COMPACT_OBJ_CODE ?

#else	// USE_SSE2 = False:

// Gather the needed data and do 8 twiddleless length-16 subtransforms, with p-offsets in br8 order: 04261537:
// NOTE that unlike the RADIX_08_DIF_OOP() macro used for pass 1 of the radix-64 DFT, RADIX_16_DIF outputs are IN-ORDER rather than BR:

	tptr = t;
	for(l = 0; l < 8; l++) {
		k2 = po_br[l+l];	// po_br[2*l] = p[04261537]
		jt = j1 + k2; jp = j2 + k2;
		RADIX_16_DIF(
			a[jt    ],a[jp    ],a[jt+p08],a[jp+p08],a[jt+p10],a[jp+p10],a[jt+p18],a[jp+p18],a[jt+p20],a[jp+p20],a[jt+p28],a[jp+p28],a[jt+p30],a[jp+p30],a[jt+p38],a[jp+p38],a[jt+p40],a[jp+p40],a[jt+p48],a[jp+p48],a[jt+p50],a[jp+p50],a[jt+p58],a[jp+p58],a[jt+p60],a[jp+p60],a[jt+p68],a[jp+p68],a[jt+p70],a[jp+p70],a[jt+p78],a[jp+p78],
			tptr->re,tptr->im,(tptr+1)->re,(tptr+1)->im,(tptr+2)->re,(tptr+2)->im,(tptr+3)->re,(tptr+3)->im,(tptr+4)->re,(tptr+4)->im,(tptr+5)->re,(tptr+5)->im,(tptr+6)->re,(tptr+6)->im,(tptr+7)->re,(tptr+7)->im,(tptr+8)->re,(tptr+8)->im,(tptr+9)->re,(tptr+9)->im,(tptr+10)->re,(tptr+10)->im,(tptr+11)->re,(tptr+11)->im,(tptr+12)->re,(tptr+12)->im,(tptr+13)->re,(tptr+13)->im,(tptr+14)->re,(tptr+14)->im,(tptr+15)->re,(tptr+15)->im,
			c16,s16
		);
		tptr += 16;
	}

/*...and now do 16 radix-8 subtransforms, including the internal twiddle factors: */
	// Block 0: t*0ri, has all-unity twiddles
	tptr = t;	jt = j1;	jp = j2;
	// Remember, the twiddleless DIT also bit-reverses its outputs, so a_p* terms appear in BR-order here [swap index pairs 1/4 and 3/6]:
	RADIX_08_DIF_OOP(
		tptr->re,tptr->im,(tptr+0x40)->re,(tptr+0x40)->im,(tptr+0x20)->re,(tptr+0x20)->im,(tptr+0x60)->re,(tptr+0x60)->im,(tptr+0x10)->re,(tptr+0x10)->im,(tptr+0x50)->re,(tptr+0x50)->im,(tptr+0x30)->re,(tptr+0x30)->im,(tptr+0x70)->re,(tptr+0x70)->im,
		a[jt],a[jp],a[jt+p04],a[jp+p04],a[jt+p02],a[jp+p02],a[jt+p06],a[jp+p06],a[jt+p01],a[jp+p01],a[jt+p05],a[jp+p05],a[jt+p03],a[jp+p03],a[jt+p07],a[jp+p07]
	);	tptr++;

	// Remaining 15 sets of macro calls done in loop:
	for(l = 1; l < 16; l++) {
		k2 = poff[l+l];	// poffs[2*l] = p00,08,10,...,70,78
		jt = j1 + k2; jp = j2 + k2;
		addr = DFT128_TWIDDLES[l]; addi = addr+1;	// Pointer to required row of 2-D twiddles array
		RADIX_08_DIF_TWIDDLE_OOP(
			tptr->re,tptr->im,(tptr+0x10)->re,(tptr+0x10)->im,(tptr+0x20)->re,(tptr+0x20)->im,(tptr+0x30)->re,(tptr+0x30)->im,(tptr+0x40)->re,(tptr+0x40)->im,(tptr+0x50)->re,(tptr+0x50)->im,(tptr+0x60)->re,(tptr+0x60)->im,(tptr+0x70)->re,(tptr+0x70)->im,
			a[jt],a[jp],a[jt+p01],a[jp+p01],a[jt+p02],a[jp+p02],a[jt+p03],a[jp+p03],a[jt+p04],a[jp+p04],a[jt+p05],a[jp+p05],a[jt+p06],a[jp+p06],a[jt+p07],a[jp+p07],
			*(addr+0x00),*(addi+0x00), *(addr+0x02),*(addi+0x02), *(addr+0x04),*(addi+0x04), *(addr+0x06),*(addi+0x06), *(addr+0x08),*(addi+0x08), *(addr+0x0a),*(addi+0x0a), *(addr+0x0c),*(addi+0x0c)
		);	tptr++;
	}

#endif	/* #ifdef USE_SSE2 */

	}	/* end for(j=_jstart[ithread]; j < _jhi[ithread]; j += 2) */

	if(MODULUS_TYPE == MODULUS_TYPE_MERSENNE)
	{
		jstart += nwt;
		jhi    += nwt;

		col += RADIX;
		co3 -= RADIX;
	}
}	/* end for(k=1; k <= khi; k++) */
