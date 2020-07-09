/*******************************************************************************
*                                                                              *
*   (C) 1997-2019 by Ernst W. Mayer.                                           *
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

#include "radix256.h"

// Skip the usual include-this-header-file-if-it-was-not-included-before #ifndef wapper,
// since this file is not for defines/typedefs and such but rather to store a lengthy const-array-declaration
// and thus needs to be inline-able in multiple places in a source filing making use of it.

const double DFT256_TWIDDLES[16][30] = {
	{ 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0, 1,0 },
	{ 0,1, ISRT2,ISRT2, -ISRT2,ISRT2, c16,s16, -s16,c16, s16,c16, -c16,s16, c32_1,s32_1, -s32_1,c32_1, s32_3,c32_3, -c32_3,s32_3, c32_3,s32_3, -s32_3,c32_3, s32_1,c32_1, -c32_1,s32_1 },
	{ ISRT2,ISRT2, c16,s16, s16,c16, c32_1,s32_1, s32_3,c32_3, c32_3,s32_3, s32_1,c32_1, c64_1,s64_1, s64_7,c64_7, c64_5,s64_5, s64_3,c64_3, c64_3,s64_3, s64_5,c64_5, c64_7,s64_7, s64_1,c64_1 },
	{ -ISRT2,ISRT2, s16,c16, -c16,-s16, c32_3,s32_3, -c32_1,s32_1, -s32_1,c32_1, -s32_3,-c32_3, c64_3,s64_3, -c64_5,s64_5, s64_1,c64_1, -c64_7,-s64_7, s64_7,c64_7, -c64_1,-s64_1, -s64_5,c64_5, -s64_3,-c64_3 },
	{ c16,s16, c32_1,s32_1, c32_3,s32_3, c64_1,s64_1, c64_5,s64_5, c64_3,s64_3, c64_7,s64_7, c128_1,s128_1, c128_9,s128_9, c128_5,s128_5, c128_d,s128_d, c128_3,s128_3, c128_b,s128_b, c128_7,s128_7, c128_f,s128_f },
	{ -s16,c16, s32_3,c32_3, -c32_1,s32_1, c64_5,s64_5, -c64_7,s64_7, s64_1,c64_1, -c64_3,-s64_3, c128_5,s128_5, -s128_d,c128_d, s128_7,c128_7, -c128_1,-s128_1, c128_f,s128_f, -c128_9,s128_9, -s128_3,c128_3, -c128_b,-s128_b },
	{ s16,c16, c32_3,s32_3, -s32_1,c32_1, c64_3,s64_3, s64_1,c64_1, s64_7,c64_7, -s64_5,c64_5, c128_3,s128_3, s128_5,c128_5, c128_f,s128_f, -s128_7,c128_7, c128_9,s128_9, -s128_1,c128_1, s128_b,c128_b, -s128_d,c128_d },
	{ -c16,s16, s32_1,c32_1, -s32_3,-c32_3, c64_7,s64_7, -c64_3,-s64_3, -s64_5,c64_5, s64_1,-c64_1, c128_7,s128_7, -c128_1,s128_1, -s128_3,c128_3, -s128_5,-c128_5, s128_b,c128_b, -c128_d,-s128_d, -c128_f,s128_f, s128_9,-c128_9 },
	{ c32_1,s32_1, c64_1,s64_1, c64_3,s64_3, c128_1,s128_1, c128_5,s128_5, c128_3,s128_3, c128_7,s128_7, c256_01,s256_01, c256_09,s256_09, c256_05,s256_05, c256_0d,s256_0d, c256_03,s256_03, c256_0b,s256_0b, c256_07,s256_07, c256_0f,s256_0f },
	{ -s32_1,c32_1, s64_7,c64_7, -c64_5,s64_5, c128_9,s128_9, -s128_d,c128_d, s128_5,c128_5, -c128_1,s128_1, c256_09,s256_09, -s256_11,c256_11, s256_13,c256_13, -c256_0b,s256_0b, c256_1b,s256_1b, -c256_1d,s256_1d, s256_01,c256_01, -c256_07,-s256_07 },
	{ s32_3,c32_3, c64_5,s64_5, s64_1,c64_1, c128_5,s128_5, s128_7,c128_7, c128_f,s128_f, -s128_3,c128_3, c256_05,s256_05, s256_13,c256_13, c256_19,s256_19, -s256_01,c256_01, c256_0f,s256_0f, s256_09,c256_09, s256_1d,c256_1d, -s256_0b,c256_0b },
	{ -c32_3,s32_3, s64_3,c64_3, -c64_7,-s64_7, c128_d,s128_d, -c128_1,-s128_1, -s128_7,c128_7, -s128_5,-c128_5, c256_0d,s256_0d, -c256_0b,s256_0b, -s256_01,c256_01, -s256_17,-c256_17, s256_19,c256_19, -c256_0f,-s256_0f, -s256_1b,c256_1b, s256_03,-c256_03 },
	{ c32_3,s32_3, c64_3,s64_3, s64_7,c64_7, c128_3,s128_3, c128_f,s128_f, c128_9,s128_9, s128_b,c128_b, c256_03,s256_03, c256_1b,s256_1b, c256_0f,s256_0f, s256_19,c256_19, c256_09,s256_09, s256_1f,c256_1f, c256_15,s256_15, s256_13,c256_13 },
	{ -s32_3,c32_3, s64_5,c64_5, -c64_1,-s64_1, c128_b,s128_b, -c128_9,s128_9, -s128_1,c128_1, -c128_d,-s128_d, c256_0b,s256_0b, -c256_1d,s256_1d, s256_09,c256_09, -c256_0f,-s256_0f, s256_1f,c256_1f, -c256_07,s256_07, -s256_0d,c256_0d, -s256_1b,-c256_1b },
	{ s32_1,c32_1, c64_7,s64_7, -s64_5,c64_5, c128_7,s128_7, -s128_3,c128_3, s128_b,c128_b, -c128_f,s128_f, c256_07,s256_07, s256_01,c256_01, s256_1d,c256_1d, -s256_1b,c256_1b, c256_15,s256_15, -s256_0d,c256_0d, s256_0f,c256_0f, -c256_17,s256_17 },
	{ -c32_1,s32_1, s64_1,c64_1, -s64_3,-c64_3, c128_f,s128_f, -c128_b,-s128_b, -s128_d,c128_d, s128_9,-c128_9, c256_0f,s256_0f, -c256_07,-s256_07, -s256_0b,c256_0b, s256_03,-c256_03, s256_13,c256_13, -s256_1b,-c256_1b, -c256_17,s256_17, c256_1f,-s256_1f }
};

