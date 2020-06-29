/* This file is part of the gf2x library.

   Copyright 2007, 2008, 2009, 2010, 2013, 2015
   Richard Brent, Pierrick Gaudry, Emmanuel Thome', Paul Zimmermann

   This program is free software; you can redistribute it and/or modify it
   under the terms of either:
    - If the archive contains a file named toom-gpl.c (not a trivial
    placeholder), the GNU General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.
    - If the archive contains a file named toom-gpl.c which is a trivial
    placeholder, the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
   
   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the license text for more details.
   
   You should have received a copy of the GNU General Public License as
   well as the GNU Lesser General Public License along with this program;
   see the files COPYING and COPYING.LIB.  If not, write to the Free
   Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
   02110-1301, USA.
*/

/* General Toom_Cook multiplication, calls KarMul, Toom3Mul, Toom3WMul
   or Toom4Mul depending on which is expected to be the fastest. */

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "gf2x.h"
#include "gf2x/gf2x-impl.h"

/* We need gf2x_addmul_1_n */
#include "gf2x/gf2x-small.h"
#include "macros.h"

#if GPL_CODE_PRESENT
short best_tab[GF2X_TOOM_TUNING_LIMIT] = GF2X_BEST_TOOM_TABLE;
short best_utab[GF2X_TOOM_TUNING_LIMIT] = GF2X_BEST_UTOOM_TABLE;
#endif /* GPL_CODE_PRESENT */

#if GF2X_MUL_TOOM4_ALWAYS_THRESHOLD < 30
#error "GF2X_MUL_TOOM4_ALWAYS_THRESHOLD must be >= 30"
#endif

/* Returns 0 for KarMul, 1 for Toom3Mul, 2 for Toom3WMul, 3 for Toom4Mul
   depending on which is predicted to be fastest for the given degree n.

   RPB, 20070511 */

short gf2x_best_toom(unsigned long n GF2X_MAYBE_UNUSED)
{
// GF2X_BEST_TOOM_TABLE should be generated by the tuning program tunetoom.
//
// The n-th entry in the list gives the code for the fastest algorithm for
// input size n.  For example:
// #define GF2X_BEST_TOOM_TABLE {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,2,1,2,2,1,2}
// would be reasonable if Toom3Mul was fastest for n = 18, 21, 24.

#if GPL_CODE_PRESENT
    if (n < GF2X_MUL_KARA_THRESHOLD)
      return GF2X_SELECT_KARA;

    if (n > GF2X_TOOM_TUNING_LIMIT)
      return GF2X_SELECT_TC4;		// Toom4Mul

    /* now n <= GF2X_TOOM_TUNING_LIMIT */

    return best_tab[n - 1];	// Return table entry
#else /* GPL_CODE_PRESENT */
    return GF2X_SELECT_KARA;
#endif /* GPL_CODE_PRESENT */
}

short gf2x_best_utoom(unsigned long n GF2X_MAYBE_UNUSED)
{
// GF2X_BEST_UTOOM_TABLE should be generated by the tuning program tuneutoom.
//
// The n-th entry in the list gives the code for the fastest algorithm for
// input size n.  0 means the obvious splitting algorithm and 1 means
// Toom3uMul.

#if GPL_CODE_PRESENT
    if (n < GF2X_MUL_TOOMU_THRESHOLD)
	return GF2X_SELECT_UNB_DFLT;		// Default

    if (n >= GF2X_MUL_TOOMU_ALWAYS_THRESHOLD)
	    return GF2X_SELECT_UNB_TC3U;

    /* This would be a tuning bug */
    ASSERT (n <= GF2X_TOOM_TUNING_LIMIT);

    return best_utab[n - 1];	// Return table entry
#else /* GPL_CODE_PRESENT */
    return GF2X_SELECT_UNB_DFLT;
#endif /* GPL_CODE_PRESENT */
}

/* Returns the worst-case space (in words) needed by the Toom-Cook routines
   KarMul, Toom3Mul, Toom3WMul, Toom4Mul.

  Copyright 2007 Richard P. Brent.
*/

/*
 The memory sp(n) necessary for Toom3WMul satisfies
 sp(n) <== (n lt 8) ? 19 : 8*(floor(n/3) + 3) + sp(floor(n/3) + 2),
 sp(7) <= 21.

 It is assumed that KarMul is called for n < 8 <= GF2X_MUL_TOOMW_THRESHOLD
 and requires space KarMulMem(n) <= 3*ceil(n/2) + KarMulMem(ceil(n/2)),
 KarMulMem(7) <= 21.  The memory for Toom3Mul and Toom4Mul is no larger
 than that for Toom3WMul. We use here the simpler bound 5*n+29 (cf toom-gpl.c).

 Note: KarMulMem(7) is now 0, but would increase if GF2X_MUL_KARA_THRESHOLD
       were reduced. We have not changed gf2x_ToomSpace as a small overestimate
       in space is not harmful.
*/

#if (GF2X_MUL_KARA_THRESHOLD < 5)
#error "GF2X_MUL_KARA_THRESHOLD assumed to be at least 5"
#endif

#if (GF2X_MUL_TOOMW_THRESHOLD < 8)
#error "GF2X_MUL_TOOMW_THRESHOLD assumed to be at least 8"
#endif

long gf2x_toomspace(long n)
{
    long low = (GF2X_MUL_KARA_THRESHOLD < GF2X_MUL_TOOMW_THRESHOLD) ?
      GF2X_MUL_KARA_THRESHOLD : GF2X_MUL_TOOMW_THRESHOLD;
    if (n < low)
	return 0;
#ifdef HAVE_KARAX
    return 5 * n + 30; /* allocate an extra word for 128-bit alignement */
#else
    return 5 * n + 29;
#endif
}

/* Returns upper bound on space required by Toom3uMul (c, a, sa, b, stk):
   2*sa + 32 + gf2x_toomspace(sa/4 + 4) */

long gf2x_toomuspace(long sa)
{
    if (sa < GF2X_MUL_TOOMU_THRESHOLD)
	return 0;
    else
	return 2 * sa + 32 + gf2x_toomspace(sa / 4 + 4);
}
/*   stk should point to a block of sufficient memory for any of these
     routines (gf2x_toomspace(n) <= 5*n+17 words is enough).
     Output c must not overlap inputs a, b.
     The output c is a*b (where a, b and c are in GF(2)[x]).
     RPB, 20070510 */
void gf2x_mul_toom(unsigned long *c, const unsigned long *a,
			    const unsigned long *b, long n,
			    unsigned long *stk)
{
    while (n && a[n - 1] == 0 && b[n - 1] == 0) {
	c[2 * n - 1] = 0;
	c[2 * n - 2] = 0;
	n--;
    }

    assert(c != a);
    assert(c != b);

#if GPL_CODE_PRESENT
    switch (gf2x_best_toom(n)) {
    case GF2X_SELECT_KARA:
	gf2x_mul_kara(c, a, b, n, stk);
	break;
#ifdef HAVE_KARAX
        /* gf2x_mul_karax is LGPL, but for simplicity we put it only here */
    case GF2X_SELECT_KARAX:
	gf2x_mul_karax(c, a, b, n, stk);
	break;
        /* gf2x_mul_tc3x is copied from gf2x_mul_tc3, thus GPL only */
    case GF2X_SELECT_TC3X:
	gf2x_mul_tc3x(c, a, b, n, stk);
	break;
#endif
        /* TC3, TC3W, TC4 are GPL'ed code */
    case GF2X_SELECT_TC3:
	gf2x_mul_tc3(c, a, b, n, stk);
	break;
    case GF2X_SELECT_TC3W:
	gf2x_mul_tc3w(c, a, b, n, stk);
	break;
    case GF2X_SELECT_TC4:
	gf2x_mul_tc4(c, a, b, n, stk);
	break;
    default:
      {
        fprintf (stderr, "Unhandled case %d in gf2x_mul_toom\n",
                 gf2x_best_toom(n));
        abort();
      }
    }
#else /* GPL_CODE_PRESENT */
    gf2x_mul_kara(c, a, b, n, stk);
#endif /* GPL_CODE_PRESENT */
}

/* Version of Karatsuba multiplication with minimal temporary storage
   sp(n) = 3*ceil(n/2) + sp(ceil(n/2)) = 3n + O(log n) words.
   RPB, 20070522 */

void gf2x_mul_kara(unsigned long * c, const unsigned long * a, const unsigned long * b,
	      long n, unsigned long * stk)
{
    unsigned long t;
    unsigned long *aa, *bb, *cc;
    long j, d, n2;

    assert(c != a);
    assert(c != b);

#if 0
    if (n <= 0)
    {				/* if turned on this test shows that calls with n == 0 */
	/* do occur (e.g from tunefft, FFT(19683)), but don't  */
	/* seem to be harmful if mul_basecase_n just returns.  */
	printf("\nWarning: n %ld in call to KarMul\n", n);
	fflush(stdout);
    }
#endif

    if (n < GF2X_MUL_KARA_THRESHOLD) {
	gf2x_mul_basecase(c, a, n, b, n);
	return;
    }

    n2 = (n + 1) / 2;		/* ceil(n/2) */
    d = n & 1;			/* 2*n2 - n = 1 if n odd, 0 if n even */
    aa = stk;			/* Size n2   */
    bb = aa + n2;		/* Size n2   */
    cc = bb + n2;		/* Size n2   */

    stk = cc + n2;		/* sp(n) = 3*ceil(n/2)) + sp(ceil(n/2)) */

    const unsigned long *a1 = a + n2;	/* a[n2] */
    const unsigned long *b1 = b + n2;	/* b[n2] */
    unsigned long *c1 = c + n2;		/* c[n2]   */
    unsigned long *c2 = c1 + n2;	/* c[2*n2] */
    unsigned long *c3 = c2 + n2;	/* c[3*n2] */

    gf2x_mul_kara(c, a, b, n2, stk);	/* Low */

    gf2x_mul_kara(c2, a1, b1, n2 - d, stk);	/* High */

    for (j = 0; j < n2 - d; j++) {
	aa[j] = a[j] ^ a1[j];
	bb[j] = b[j] ^ b1[j];
	cc[j] = c1[j] ^ c2[j];
    }
    for (; j < n2; j++) {	/* Only when n odd */
	aa[j] = a[j];
	bb[j] = b[j];
	cc[j] = c1[j] ^ c2[j];
    }

    gf2x_mul_kara(c1, aa, bb, n2, stk);	/* Middle */

    for (j = 0; j < n2 - 2 * d; j++) {
	t = cc[j];
	c1[j] ^= t ^ c[j];
	c2[j] ^= t ^ c3[j];
    }
    for (; j < n2; j++) {	/* Only when n odd */
	c1[j] ^= cc[j] ^ c[j];
	c2[j] ^= cc[j];
    }
}
