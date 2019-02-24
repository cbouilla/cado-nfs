/* merge --- new merge program

Copyright 2019 Paul Zimmermann.

This file is part of CADO-NFS.

CADO-NFS is free software; you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2.1 of the License, or (at your option)
any later version.

CADO-NFS is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
details.

You should have received a copy of the GNU Lesser General Public License
along with CADO-NFS; see the file COPYING.  If not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>  /* for _O_BINARY */
#include <gmp.h>    /* for mpn_ior_n */
#ifdef HAVE_OPENMP
#include <omp.h>
#endif
#ifdef HAVE_MALLOPT
#include <malloc.h>
#endif

/* Define MARKOWITZ to use Markowitz pivoting to estimate the fill-in of
   a merge in routine merge_cost (instead of computing the real fill-in
   when adding the row of smallest weight to the other ones. */
#define MARKOWITZ

/* define DEBUG if printRow is needed */
// #define DEBUG

/* CBOUND_INCR is the increment on the maximal cost of merges at each step.
   Setting it to 1 is optimal in terms of matrix size, but will take a very
   long time (typically 10 times more than with CBOUND_INCR=10). */
#ifndef FOR_DL
/* Experimentally on the RSA-512 matrix, CBOUND_INCR=11 gives a matrix which
   is 0.5% larger than the matrix obtained with CBOUND_INCR=1,
   and takes only 20% more time than with CBOUND_INCR=20.
   With MARKOWITZ, CBOUND_INCR=13 gives a similar number of steps than
   without MARKOWITZ and CBOUND_INCR=11. */
#ifndef MARKOWITZ
#define CBOUND_INCR 11
#else
#define CBOUND_INCR 13
#endif
#else
/* For the p180 matrix (http://caramba.loria.fr/p180.txt), CBOUND_INCR=20
   gives a matrix which is 0.2% larger than the matrix obtained with
   CBOUND_INCR=1 (with target_density=200), and takes only 17% more time than
   with CBOUND_INCR=30.
   With MARKOWITZ, CBOUND_INCR=31 gives a similar number of steps than
   without MARKOWITZ and CBOUND_INCR=20. */
#ifndef MARKOWITZ
#define CBOUND_INCR 20
#else
#define CBOUND_INCR 31
#endif
#endif

#include "portability.h"

#include "filter_config.h"
#include "utils_with_io.h"
#include "merge_replay_matrix.h" /* for filter_matrix_t */
#include "report.h"     /* for report_t */
#include "sparse.h"
#include "mst.h"

/* Note about variables used in the code:
 * cwmax is the (current) maximal weight of columns that will be considered
   for a merge. It starts at cwmax=2. Once we have performed *all* 2-merges,
   we increase cwmax to 3, and at each step of the algorithm, we increase it
   by 1 (not waiting for all 3-merges to be completed).
 * cbound is the maximum (current) fill-in that is allowed for a merge
   (in fact, it is a biased value to avoid negative values, one should subtract
    BIAS from cbound to get the actual value). It starts at 0, and once all
    the 2-merges have been performed (which all give a negative fill-in, thus
    they will all be allowed), we increase cbound by CBOUND_INCR at each step
    of the algorithm (where CBOUND_INCR differs for integer factorization and
    discrete logarithm).
 * j0 means that we assume that columns of index < j0 cannot have
   weight <= cwmax. It depends on cwmax (decreases when cwmax increases).
   At the first call to compute_weights(), the values j0(cwmax=2) up to
   j0(MERGE_LEVEL_MAX) are computed once for all (since the weight of a
   column usually does not decrease, the values of j0 should remain correct
   during the algorithm, but not optimal).
   In several places we use the fact that the rows are sorted by increasing
   columns: if we start from the end, we can stop at soon as j < j0.
*/

/* 0: compute_weights
   1: compute_R
   2: compute_merges
   3: apply_merges
   4: pass
   5: renumber */
double cpu_t[6] = {0};
double wct_t[6] = {0};

#define MARGIN 5 /* reallocate dynamic lists with increment 1/MARGIN */

static void
declare_usage(param_list pl)
{
  param_list_decl_usage(pl, "mat", "input purged file");
  param_list_decl_usage(pl, "out", "output history file");
  param_list_decl_usage(pl, "skip", "number of heavy columns to bury (default "
                                    STR(DEFAULT_MERGE_SKIP) ")");
  param_list_decl_usage(pl, "target_density", "stop when the average row density exceeds this value"
                            " (default " STR(DEFAULT_MERGE_TARGET_DENSITY) ")");
  param_list_decl_usage(pl, "force-posix-threads", "force the use of posix threads, do not rely on platform memory semantics");
  param_list_decl_usage(pl, "path_antebuffer", "path to antebuffer program");
  param_list_decl_usage(pl, "t", "number of threads");
}

static void
usage (param_list pl, char *argv0)
{
    param_list_print_usage(pl, argv0, stderr);
    exit(EXIT_FAILURE);
}

/* wrapper for omp_get_num_threads, returns 1 when OpenMP is not available */
static int
get_num_threads (void)
{
  int nthreads;
#ifdef HAVE_OPENMP
#pragma omp parallel
#pragma omp master
  nthreads = omp_get_num_threads ();
#else
  nthreads = 1;
#endif
  return nthreads;
}

/* wrapper for omp_get_thread_num, returns 0 when OpenMP is not available */
static int
get_thread_num (void)
{
  int i;
#ifdef HAVE_OPENMP
  i = omp_get_thread_num ();
#else
  i = 0;
#endif
  return i;
}

#ifndef FOR_DL
/* sort row[0], row[1], ..., row[n-1] in non-decreasing order */
static void
sort_relation (index_t *row, unsigned int n)
{
  unsigned int i, j;

  for (i = 1; i < n; i++)
    {
      index_t t = row[i];
      if (t < row[i-1])
        {
          row[i] = row[i-1];
          for (j = i - 1; j > 0 && t < row[j-1]; j--)
            row[j] = row[j-1];
          row[j] = t;
        }
    }
}
#endif

/* callback function called by filter_rels */
static void *
insert_rel_into_table (void *context_data, earlyparsed_relation_ptr rel)
{
  filter_matrix_t *mat = (filter_matrix_t *) context_data;
  unsigned int j = 0;
  typerow_t buf[UMAX(weight_t)]; /* rel->nb is of type weight_t */

  for (unsigned int i = 0; i < rel->nb; i++)
  {
    index_t h = rel->primes[i].h;
    mat->rem_ncols += (mat->wt[h] == 0);
    mat->wt[h] += (mat->wt[h] != UMAX(unsigned char));
    if (h < mat->skip) continue; /* we skip (bury) the first 'skip' indices */
#ifdef FOR_DL
    exponent_t e = rel->primes[i].e;
    /* For factorization, they should not be any multiplicity here.
       For DL we do not want to count multiplicity in mat->wt */
    buf[++j] = (ideal_merge_t) {.id = h, .e = e};
#else
    ASSERT(rel->primes[i].e == 1);
    buf[++j] = h;
#endif
  }

#ifdef FOR_DL
  buf[0].id = j;
#else
  buf[0] = j;
#endif

  /* only count the non-skipped coefficients */
  mat->tot_weight += j;

  /* sort indices to ease row merges */
#ifndef FOR_DL
  sort_relation (&(buf[1]), j);
#else
  qsort (&(buf[1]), j, sizeof(typerow_t), cmp_typerow_t);
#endif

  mat->rows[rel->num] = mallocRow (j + 1);
  compressRow (mat->rows[rel->num], buf, j);

  return NULL;
}

static void
filter_matrix_read (filter_matrix_t *mat, const char *purgedname)
{
  uint64_t nread;
  char *fic[2] = {(char *) purgedname, NULL};

  /* read all rels */
  nread = filter_rels (fic, (filter_rels_callback_t) &insert_rel_into_table,
                       mat, EARLYPARSE_NEED_INDEX, NULL, NULL);
  ASSERT_ALWAYS(nread == mat->nrows);
  mat->rem_nrows = nread;
}

static void
print_timings (char *s, double cpu, double wct)
{
  printf ("%s %.1fs (cpu), %.1fs (wct) [cpu/wct=%.1f]\n",
	  s, cpu, wct, cpu / wct);
  fflush (stdout);
}

/* set bit j of z to 1 for every ideal index j in relations = k mod nthreads */
static void
renumber_get_zk (mpz_t z, filter_matrix_t *mat, int k, int nthreads)
{
  /* first ensure all bits of z are set to 0 */
  index_t n = (mat->ncols + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS;
  memset (z->_mp_d, 0, n * sizeof (mp_limb_t));
  for (index_t i = k; i < mat->nrows; i += nthreads)
    for (uint32_t l = 1; l <= matLengthRow (mat, i); l++)
      {
        index_t j = matCell (mat, i, l);
        mpz_setbit (z, j);
      }
}

/* zk[0] <- zk[0] or zk[1] or zk[2] ... or zk[nthreads-1] */
static void
renumber_get_z_xor (mpz_t *zk, index_t ncols, int k, int nthreads)
{
  /* nlimbs is the total number of limbs of each zk[i] */
  index_t nlimbs = (ncols + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS;
  /* m is the number of limbs processed by each thread */
  index_t m = (nlimbs + nthreads - 1) / nthreads;
  index_t i0 = k * m;
  index_t i1 = i0 + m;
  if (i1 > nlimbs)
    i1 = nlimbs;
  /* Warning: we can have i0 > nlimbs: take for example ncols=66242 with
     nthreads=64, then we get nlimbs=1036. For k=63, we have i0 = 1071. */
  if (i0 >= i1)
    return;
  /* now we have i0 < i1 <= nlimbs thus the mpn_ior_n call below is valid */
  mp_limb_t *z0 = zk[0]->_mp_d + i0;
  for (int j = 1; j < nthreads; j++)
    mpn_ior_n (z0, z0, zk[j]->_mp_d + i0, i1 - i0);
}

static void
renumber_get_z (mpz_t z, filter_matrix_t *mat)
{
  int nthreads;
  mpz_t *zk;

  nthreads = get_num_threads ();

  zk = malloc (nthreads * sizeof (mpz_t));
  for (int k = 0; k < nthreads; k++)
    mpz_init2 (zk[k], mat->ncols);
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    renumber_get_zk (zk[k], mat, k, nthreads);

#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    renumber_get_z_xor (zk, mat->ncols, k, nthreads);
  /* normalize zk[0] */
  index_t n = (mat->ncols + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS;
  while (n > 0 && zk[0]->_mp_d[n-1] == 0)
    n --;
  zk[0]->_mp_size = n;
  mpz_swap (z, zk[0]);

  for (int k = 0; k < nthreads; k++)
    mpz_clear (zk[k]);
  free (zk);
}

/* replace ideals j by p[j], for rows = k mod nthreads */
static void
renumber_mat_thread (filter_matrix_t *mat, index_t *p, int k, int nthreads)
{
  for (index_t i = k; i < mat->nrows; i += nthreads)
    {
      typerow_t *row = mat->rows[i];
      for (uint32_t l = 1; l <= rowCell(row,0); l++)
        rowCell(row,l) = p[rowCell(row,l)];
    }
}

/* replace all ideals j by p[j] */
static void
renumber_mat (filter_matrix_t *mat)
{
  int nthreads;
  index_t *p = mat->p;

  nthreads = get_num_threads ();

#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    renumber_mat_thread (mat, p, k, nthreads);
}

/* renumber the columns of mat to have consecutive indices 0..ncols-1 */
static void
renumber (filter_matrix_t *mat)
{
  double cpu = seconds (), wct = wct_seconds ();
  mpz_t z; /* bit j of z is set if wt[j] > 0 */
  mpz_init (z);

  /* first compute the columns of positive weight */
  renumber_get_z (z, mat);

  /* now compute p[j] such that ideal j is renamed to p[j] <= j */
  mat->p = malloc (mat->ncols * sizeof (index_t));
  index_t i = 0, j = 0;
  while (1)
    {
      j = mpz_scan1 (z, j); /* next set bit */
      if (j < mat->ncols)
        mat->p[j++] = i++;
      else
        break;
    }
  ASSERT(i == mpz_popcount (z));
  mat->rem_ncols = i;

  renumber_mat (mat);

#ifndef FOR_DL
  free (mat->p);
#else
  /* for the discrete logarithm, we keep the inverse of p, to print the
     original columns in the history file */
  for (i = j = 0; j < mat->ncols; j++)
    if (mat->p[j] == i) /* necessarily i <= j */
      mat->p[i++] = j;
#endif
  mpz_clear (z);

  /* reset ncols */
  mat->ncols = mat->rem_ncols;
  printf ("exit renumber, ncols=%" PRIu64 "\n", mat->ncols);
  fflush (stdout);

  cpu = seconds () - cpu;
  wct = wct_seconds () - wct;
  print_timings ("   renumber took", cpu, wct);
  cpu_t[5] += cpu;
  wct_t[5] += wct;
}

/* Thread k accumulates weights in Wt[k].
   We only consider ideals of index >= j0, and put the weight of ideal j,
   j >= j0, in Wt[k][j-j0]. */
static void
compute_weights_by_row (filter_matrix_t *mat, unsigned char **Wt,
			index_t i, index_t j0)
{
  if (mat->rows[i] == NULL) /* row was discarded */
    return;
  int k = get_thread_num ();
  unsigned char *wtk = Wt[k];
  for (uint32_t l = matLengthRow (mat, i); l >= 1; l--)
    {
      index_t j = matCell (mat, i, l);
      if (j < j0) /* assume ideals are sorted by increasing order */
	break;
      else if (wtk[j - j0] <= mat->cwmax)
	wtk[j - j0] ++;
    }
}

/* Thread k accumulates in Wt[0] the weights for the k-th block of columns,
   saturating at cwmax + 1:
   Wt[0][j] = min(cwmax+1, Wt[0][j] + Wt[1][j] + ... + Wt[nthreads-1][j]) */
static void
compute_weights_by_col (filter_matrix_t *mat, unsigned char **Wt, int k,
			int nthreads, index_t j0)
{
  index_t t0, t1;
  index_t n = mat->ncols - j0;
  t0 = k * (n / nthreads);
  t1 = (k < nthreads - 1) ? (k+1) * (n / nthreads) : n;
  unsigned char *wt0 = Wt[0];
  for (int l = 1; l < nthreads; l++)
    {
      unsigned char *wtl = Wt[l];
      for (index_t t = t0; t < t1; t++)
	if (wt0[t] + wtl[t] <= mat->cwmax)
	  wt0[t] += wtl[t];
	else
	  wt0[t] = mat->cwmax + 1;
    }
}

/* For 1 <= w <= MERGE_LEVEL_MAX, put in jmin[w] the smallest index j such that
   mat->wt[j] = w. This routine is called only once, at the first call of
   compute_weights. */
static void
compute_jmin (filter_matrix_t *mat, index_t *jmin)
{
  /* first initialize to ncols */
  for (int w = 1; w <= MERGE_LEVEL_MAX; w++)
    jmin[w] = mat->ncols;

#ifdef HAVE_OPENMP
#pragma omp parallel for
#endif
  for (index_t j = 0; j < mat->ncols; j++)
    {
      unsigned char w = mat->wt[j];
      /* the condition j < jmin[w] is true only for the smallest j,
         thus the critical part below is run at most MERGE_LEVEL_MAX times */
      if (0 < w && w <= MERGE_LEVEL_MAX)
        if (j < jmin[w])
#ifdef HAVE_OPENMP
#pragma omp critical
#endif
          jmin[w] = j;
    }

  jmin[0] = 1; /* to tell that jmin was initialized */

  /* make jmin[w] = min(jmin[w'], 1 <= w' <= w) */
  for (int w = 2; w <= MERGE_LEVEL_MAX; w++)
    if (jmin[w-1] < jmin[w])
      jmin[w] = jmin[w-1];
}

/* compute column weights (in fact, saturate to cwmax + 1 since we only need to
   know whether the weights are <= cwmax or not) */
static void
compute_weights (filter_matrix_t *mat, index_t *jmin)
{
  int nthreads;
  unsigned char **Wt;
  double cpu = seconds (), wct = wct_seconds ();

  nthreads = get_num_threads ();

  Wt = malloc (nthreads * sizeof (unsigned char*));

  index_t j0;
  if (jmin[0] == 0) /* jmin was not initialized */
    j0 = 0;
  else
    /* we only need to consider ideals of index >= j0, assuming the weight of
       an ideal cannot decrease (except when decreasing to zero when merged) */
    j0 = jmin[mat->cwmax];

  /* trick: we use wt for Wt[0] */
  Wt[0] = mat->wt + j0;

  /* first, thread k fills Wt[k] with subset of rows */
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    {
      index_t n = mat->ncols - j0;
      if (k != 0)
	       Wt[k] = malloc (n * sizeof (unsigned char));
      memset (Wt[k], 0, n * sizeof (unsigned char));
    }

  /* using schedule(dynamic,128) here is crucial, since during merge,
     the distribution of row lengths is no longer uniform (including
     discarded rows) */
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(dynamic,128)
#endif
  for (index_t i = 0; i < mat->nrows; i++)
    compute_weights_by_row (mat, Wt, i, j0);

  /* then we accumulate all weights in Wt[0] */
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    compute_weights_by_col (mat, Wt, k, nthreads, j0);

  /* start from 1 since Wt[0] = mat->wt + j0 should be kept */
  for (int k = 1; k < nthreads; k++)
    free (Wt[k]);
  free (Wt);

  if (jmin[0] == 0) /* jmin was not initialized */
    compute_jmin (mat, jmin);

  cpu = seconds () - cpu;
  wct = wct_seconds () - wct;
  print_timings ("   compute_weights took", cpu, wct);
  cpu_t[0] += cpu;
  wct_t[0] += wct;
}

/*************** level-2 buckets (to compute inverse matrix) *****************/

typedef struct
{
  index_t j; /* ideal index */
  index_t i; /* row index */
} index_pair_t;

typedef struct
{
  index_pair_t *list;
  unsigned long alloc;
  unsigned long size;
} bucket_t;

/* bucket B[k][j] contains buckets for thread k, with ideals = j mod nthreads */
static bucket_t**
init_buckets (int nthreads, unsigned long tot_weight)
{
  bucket_t **B;
  unsigned long w = tot_weight / (nthreads * nthreads);

  w += w / MARGIN;
  B = malloc (nthreads * sizeof (bucket_t*));
  for (int k = 0; k < nthreads; k++)
    {
      B[k] = malloc (nthreads * sizeof (bucket_t));
      for (int j = 0; j < nthreads; j++)
	{
	  B[k][j].list = malloc (w * sizeof (index_pair_t));
	  B[k][j].alloc = w;
	  B[k][j].size = 0;
	}
    }
  return B;
}

static void
clear_buckets (bucket_t **B, int nthreads)
{
  for (int k = 0; k < nthreads; k++)
    {
      for (int j = 0; j < nthreads; j++)
        free (B[k][j].list);
      free (B[k]);
    }
  free (B);
}

static void
add_bucket (bucket_t *Bi, index_t i, index_t j, int nthreads)
{
  int jj = j % nthreads;

  if (Bi[jj].size == Bi[jj].alloc)
    {
      Bi[jj].alloc += 1 + Bi[jj].alloc / MARGIN;
      Bi[jj].list = realloc (Bi[jj].list, Bi[jj].alloc * sizeof (index_pair_t));
    }
  ASSERT(Bi[jj].size < Bi[jj].alloc);
  Bi[jj].list[Bi[jj].size].i = i;
  Bi[jj].list[Bi[jj].size].j = j;
  Bi[jj].size ++;
}

/* add pairs (j,i) for row i, for j >= j0 */
static void
fill_buckets (bucket_t **B, filter_matrix_t *mat, index_t i, int nthreads,
	      index_t j0)
{
  if (mat->rows[i] == NULL) /* row was discarded */
    return;
  bucket_t *Bi = B[get_thread_num ()];
  for (uint32_t k = matLengthRow (mat, i); k >= 1; k--)
    {
      index_t j = matCell (mat, i, k);
      /* we use here the fact that the rows are sorted by increasing values */
      if (j < j0)
	break;
      /* we only consider ideals of weight <= cwmax,
	 for which wt[j] was reset to 0 in compute_R */
      if (mat->wt[j] == 0)
	add_bucket (Bi, i, j, nthreads);
    }
}

static void
add_R_entry (filter_matrix_t *mat, index_t j, index_t i)
{
  int k = mat->wt[j];

  mat->R[j][k] = i;
  mat->wt[j] = k + 1;
}

/* Apply all buckets B[i][k] to compute the inverse matrix. Since B[i][k] contains
   ideals j such that j = k mod nthreads, and two threads use a different value of k,
   this ensures there cannot be any conflict in incrementing mat->wt[j]. */
static void
apply_buckets (bucket_t **B, filter_matrix_t *mat, index_t k, int nthreads)
{
  ASSERT(mat->cwmax < 255);
  for (int i = 0; i < nthreads; i++)
    {
      bucket_t Bik = B[i][k];
      for (unsigned long t = 0; t < Bik.size; t++)
	add_R_entry (mat, Bik.list[t].j, Bik.list[t].i);
    }
}

/* return the total weight of all columns of weight <= cwmax */
static unsigned long
get_tot_weight_columns (filter_matrix_t *mat)
{
  unsigned long tot_weight = 0;

#ifdef HAVE_OPENMP
#pragma omp parallel for reduction(+:tot_weight)
#endif
  for (index_t j = 0; j < mat->ncols; j++)
	  if (mat->wt[j] <= mat->cwmax)
      tot_weight += mat->wt[j];
  return tot_weight;
}

/* computes the transposed matrix for columns of weight <= cwmax
   (we only have to consider columns >= j0) */
static void
compute_R (filter_matrix_t *mat, index_t j0)
{
  int nthreads;
  double cpu = seconds (), wct = wct_seconds ();

  /* the inverse matrix R is already allocated, but the individual entries
     R[j] are not */
  unsigned long tot_weight = get_tot_weight_columns (mat);

#ifdef HAVE_OPENMP
#pragma omp parallel for
#endif
  for (index_t j = j0; j < mat->ncols; j++)
    if (0 < mat->wt[j] && mat->wt[j] <= mat->cwmax)
      {
	mat->R[j] = realloc (mat->R[j], mat->wt[j] * sizeof (index_t));
	mat->wt[j] = 0; /* reset to 0 */
      }

  nthreads = get_num_threads ();

  /* we first compute buckets (j,i) where thread k processes rows i = k mod
     nthreads, and bucket l corresponds to j = l mod nthreads */
  bucket_t **B;
  B = init_buckets (nthreads, tot_weight);

  /* Using schedule(dynamic,128) below is critical for performance,
     since during the merge the row lengths are no longer uniform. */
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(dynamic,128)
#endif
  for (index_t i = 0; i < mat->nrows; i++)
    fill_buckets (B, mat, i, nthreads, j0);

#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
  for (int k = 0; k < nthreads; k++)
    apply_buckets (B, mat, k, nthreads);

  clear_buckets (B, nthreads);

  cpu = seconds () - cpu;
  wct = wct_seconds () - wct;
  print_timings ("   compute_R took", cpu, wct);
  cpu_t[1] += cpu;
  wct_t[1] += wct;
}

typedef struct {
  index_t **list;       /* list[c] is a list of j-values with merge cost c */
  unsigned long *size;  /* size[c] is the length of list[c] */
  unsigned long *alloc; /* alloc[c] is the allocated size of list[c] */
  int cmax;             /* all costs are <= cmax */
} cost_list_t;

/* doit == 0: return the weight of row i1 + row i2
   doit <> 0: add row i2 to row i1 */
#ifndef FOR_DL
/* special code for factorization */
static int32_t
add_row (filter_matrix_t *mat, index_t i1, index_t i2, int doit,
	 MAYBE_UNUSED index_t j)
{
  uint32_t k1 = matLengthRow (mat, i1);
  uint32_t k2 = matLengthRow (mat, i2);
  int32_t c = 0;
  uint32_t t1 = 1, t2 = 1;
  while (t1 <= k1 && t2 <= k2)
    {
      if (mat->rows[i1][t1] == mat->rows[i2][t2])
	t1 ++, t2 ++;
      else if (mat->rows[i1][t1] < mat->rows[i2][t2])
	t1 ++, c ++;
      else
	t2 ++, c ++;
    }
  c += (k1 + 1 - t1) + (k2 + 1 - t2);
  if (doit == 0)
    return c;
  /* now perform the real merge */
  index_t *t, *t0;
  t = malloc ((c + 1) * sizeof (index_t));
  t0 = t;
  *t++ = c;
  t1 = t2 = 1;
  while (t1 <= k1 && t2 <= k2)
    {
      if (mat->rows[i1][t1] == mat->rows[i2][t2])
	t1 ++, t2 ++;
      else if (mat->rows[i1][t1] < mat->rows[i2][t2])
	*t++ = mat->rows[i1][t1++];
      else
	*t++ = mat->rows[i2][t2++];
    }
  while (t1 <= k1)
    *t++ = mat->rows[i1][t1++];
  while (t2 <= k2)
    *t++ = mat->rows[i2][t2++];
  ASSERT (t0 + (c + 1) == t);
  free (mat->rows[i1]);
  mat->rows[i1] = t0;
  return c;
}
#else /* FOR_DL: j is the ideal to be merged */
#define INT32_MIN_64 (int64_t) INT32_MIN
#define INT32_MAX_64 (int64_t) INT32_MAX

static int32_t
add_row (filter_matrix_t *mat, index_t i1, index_t i2, int doit, index_t j)
{
  /* first look for the exponents of j in i1 and i2 */
  uint32_t k1 = matLengthRow (mat, i1);
  uint32_t k2 = matLengthRow (mat, i2);
  ideal_merge_t *r1 = mat->rows[i1];
  ideal_merge_t *r2 = mat->rows[i2];
  int32_t e1 = 0, e2 = 0;

  /* search by decreasing ideals as the ideal to be merged is likely large */
  for (int l = k1; l >= 1; l--)
    if (r1[l].id == j)
      {
        e1 = r1[l].e;
        break;
      }
  for (int l = k2; l >= 1; l--)
    if (r2[l].id == j)
      {
        e2 = r2[l].e;
        break;
      }

  /* we always check e1 and e2 are not zero, in order to prevent from zero
     exponents that would come from exponent overflows in previous merges */
  ASSERT_ALWAYS (e1 != 0 && e2 != 0);

  int d = (int) gcd_int64 ((int64_t) e1, (int64_t) e2);
  e1 /= -d;
  e2 /= d;
  /* we will multiply row i1 by e2, and row i2 by e1 */

  int32_t c = 0;
  uint32_t t1 = 1, t2 = 1;

  while (t1 <= k1 && t2 <= k2)
    {
      if (r1[t1].id == r2[t2].id)
        {
          /* If exponent do not cancel, add 1 to cost.
             Warning: we should ensure that r1[t1].e * e2 does not overflow,
             same for r2[t2].e * e1 and the sum.
             In fact, since the sum is computed modulo 2^32, the only bad case
             is when the sum is a non-zero multiple of 2^32. */
          int32_t e = r1[t1].e * e2 + r2[t2].e * e1;
          if (e != 0)
            c ++; /* we are sure that the sum is not zero */
          else
            {
              /* We compute the sum with 64-bit integers. Since all values are
                 in [-2^31, 2^31-1], the sum is in [-2^63,2^63-2^33+2], thus
                 always fits into an int64_t. */
              int64_t ee = (int64_t) r1[t1].e * (int64_t) e2 + (int64_t) r2[t2].e * (int64_t) e1;
              c += ee != 0;
            }
          t1 ++, t2 ++;
        }
      else if (r1[t1].id < r2[t2].id)
	t1 ++, c ++;
      else
	t2 ++, c ++;
    }
  c += (k1 + 1 - t1) + (k2 + 1 - t2);

  if (doit == 0)
    return c;

  /* now perform the real merge */
  ideal_merge_t *t, *t0;
  t = malloc ((c + 1) * sizeof (ideal_merge_t));
  t0 = t;
  (*t++).id = c; /* length of the new relation */
  t1 = t2 = 1;
  int64_t e;
  while (t1 <= k1 && t2 <= k2)
    {
      if (r1[t1].id == r2[t2].id)
        {
          /* as above, the exponent e below cannot overflow */
          e = (int64_t) e2 * (int64_t) r1[t1].e + (int64_t) e1 * (int64_t) r2[t2].e;
          if (e != 0) /* exponents do not cancel */
            {
              (*t).id = r1[t1].id;
              ASSERT_ALWAYS(INT32_MIN_64 <= e && e <= INT32_MAX_64);
              (*t).e = (int32_t) e;
              t ++;
            }
          t1 ++, t2 ++;
        }
      else if (r1[t1].id < r2[t2].id)
        {
          (*t).id = r1[t1].id;
          e = (int64_t) e2 * (int64_t) r1[t1].e;
          ASSERT_ALWAYS(INT32_MIN_64 <= e && e <= INT32_MAX_64);
          (*t).e = (int32_t) e;
          t1 ++, t ++;
        }
      else
        {
          (*t).id = r2[t2].id;
          e = (int64_t) e1 * (int64_t) r2[t2].e;
          ASSERT_ALWAYS(INT32_MIN_64 <= e && e <= INT32_MAX_64);
          (*t).e = (int32_t) e;
          t2 ++, t ++;
        }
    }
  while (t1 <= k1)
    {
      (*t).id = r1[t1].id;
      e = (int64_t) e2 * (int64_t) r1[t1].e;
      ASSERT_ALWAYS(INT32_MIN_64 <= e && e <= INT32_MAX_64);
      (*t).e = (int32_t) e;
      t1 ++, t ++;
    }
  while (t2 <= k2)
    {
      (*t).id = r2[t2].id;
      e = (int64_t) e1 * (int64_t) r2[t2].e;
      ASSERT_ALWAYS(INT32_MIN_64 <= e && e <= INT32_MAX_64);
      (*t).e = (int32_t) e;
      t2 ++, t ++;
    }
  ASSERT (t0 + (c + 1) == t);
  free (mat->rows[i1]);
  mat->rows[i1] = t0;
  return c;
}
#endif

static void
remove_row (filter_matrix_t *mat, index_t i)
{
  free (mat->rows[i]);
  mat->rows[i] = NULL;
}

#ifdef DEBUG
static void MAYBE_UNUSED
printRow (filter_matrix_t *mat, index_t i)
{
  int32_t k = matLengthRow (mat, i);
  printf ("%lu [%d]:", (unsigned long) i, k);
  for (int j = 1; j <= k; j++)
#ifndef FOR_DL
    printf (" %lu", (unsigned long) mat->rows[i][j]);
#else
    printf (" %lu^%d", (unsigned long) mat->rows[i][j].id, mat->rows[i][j].e);
#endif
  printf ("\n");
}
#endif

/* classical cost: merge the row of smaller weight with the other ones,
   and return the merge cost (taking account of cancellations) */
static int32_t
merge_cost (filter_matrix_t *mat, index_t j)
{
  index_t imin, i;
  int32_t cmin, c;
  int w = mat->wt[j];

  ASSERT (1 <= w && w <= mat->cwmax);

  if (w == 1)
    return -3; /* ensure all 1-merges are processed before 2-merges with no
		  cancellation */

  imin = mat->R[j][0];
  cmin = matLengthRow (mat, imin);
  for (int k = 1; k < w; k++)
    {
      i = mat->R[j][k];
      c = matLengthRow (mat, i);
      if (c < cmin)
	{
	  imin = i;
	  cmin = c;
	}
    }

  /* we remove row imin and add it to all w-1 others: cmin * (w - 2)
     the column j disappears: -w */
  c = -cmin; /* remove row imin */
  for (int k = 0; k < w; k++)
    {
      i = mat->R[j][k];
      if (i != imin)
	/* It is crucial here to take into account cancellations of
	   coefficients, and not to simply add the length of both
	   rows minus 2. Indeed, if row 'a' was added to two
	   relation-sets 'b' and 'c', and 'b' and 'c' are merged together,
	   all ideals from 'a' will cancel. */
#ifndef MARKOWITZ
	c += add_row (mat, i, imin, 0, j) - matLengthRow (mat, i);
#else /* estimation with Markowitz pivoting: might miss cancellations */
        c += cmin - 2;
#endif
    }
  return c;
}

/* Output a list of merges to a string.
   Assume rep->type = 0.
   size is the length of str.
   Return the number of characters written, except the final \0
   (or that would have been written if that number >= size) */
static int
#ifndef FOR_DL
sreportn (char *str, size_t size, index_signed_t *ind, int n)
#else
sreportn (char *str, size_t size, index_signed_t *ind, int n, index_t j)
#endif
{
  size_t m = 0; /* number of characters written */

  for (int i = 0; i < n; i++)
    {
      m += snprintf (str + m, size - m, "%ld", (long int) ind[i]);
      ASSERT(m < size);
      if (i < n-1)
        {
          m += snprintf (str + m, size - m, " ");
          ASSERT(m < size);
        }
    }
#ifdef FOR_DL
  m += snprintf (str + m, size - m, " #%lu", (unsigned long) j);
#endif
  m += snprintf (str + m, size - m, "\n");
  ASSERT(m < size);
  return m;
}

/* Perform the row additions given by the minimal spanning tree (stored in
   history[][]). */
static int
addFatherToSons (index_t history[MERGE_LEVEL_MAX][MERGE_LEVEL_MAX+1],
                 filter_matrix_t *mat, int m, index_t *ind, index_t j,
                 int *father, int *sons)
{
  int i, s, t;

  for (i = m - 2; i >= 0; i--)
    {
      s = father[i];
      t = sons[i];
      if (i == 0)
        {
          history[i][1] = ind[s];
          ASSERT(s == 0);
        }
      else
        history[i][1] = -(ind[s] + 1);
      add_row (mat, ind[t], ind[s], 1, j);
      history[i][2] = ind[t];
      history[i][0] = 2;
    }
  return m - 2;
}

/* perform the merge, computing the full spanning tree */
static int32_t
merge_do (filter_matrix_t *mat, index_t j, FILE *out)
{
  int32_t c;
  int w = mat->wt[j];

  ASSERT (1 <= w && w <= mat->cwmax);

  if (w == 1)
    {
      char s[MERGE_CHAR_MAX];
      int n MAYBE_UNUSED;
      index_signed_t i = mat->R[j][0]; /* only row containing j */
#ifndef FOR_DL
      n = sreportn (s, MERGE_CHAR_MAX, &i, 1);
#else
      n = sreportn (s, MERGE_CHAR_MAX, &i, 1, mat->p[j]);
#endif
      ASSERT(n < MERGE_CHAR_MAX);
      fprintf (out, "%s", s);
      remove_row (mat, i);
      return -3;
    }

  /* perform the real merge and output to history file */
  index_t *ind = mat->R[j];
  char s[MERGE_CHAR_MAX];
  int n = 0; /* number of characters written to s (except final \0) */
  int A[MERGE_LEVEL_MAX][MERGE_LEVEL_MAX];
  fillRowAddMatrix (A, mat, w, ind, j);
  /* mimic MSTWithA */
  int start[MERGE_LEVEL_MAX], end[MERGE_LEVEL_MAX];
  c = minimalSpanningTree (start, end, w, A);
  /* c is the weight of the minimal spanning tree, we have to remove
     the weights of the initial relations */
  for (int k = 0; k < w; k++)
    c -= matLengthRow (mat, ind[k]);
  index_t history[MERGE_LEVEL_MAX][MERGE_LEVEL_MAX+1];
  int hmax = addFatherToSons (history, mat, w, ind, j, start, end);
  for (int i = hmax; i >= 0; i--)
    {
#ifndef FOR_DL
      n += sreportn (s + n, MERGE_CHAR_MAX - n,
		     (index_signed_t*) (history[i]+1), history[i][0]);
#else
      n += sreportn (s + n, MERGE_CHAR_MAX - n,
		     (index_signed_t*) (history[i]+1), history[i][0],
		     mat->p[j]);
#endif
      ASSERT(n < MERGE_CHAR_MAX);
    }
  fprintf (out, "%s", s);
  remove_row (mat, ind[0]);
  return c;
}

static void
cost_list_init_aux (cost_list_t *l)
{
  l->list = NULL;
  l->size = 0;
  l->alloc = 0;
  l->cmax = -1;
}

static cost_list_t*
cost_list_init (int nthreads)
{
  cost_list_t *L;

  L = malloc (nthreads * sizeof (cost_list_t));
#ifdef HAVE_OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < nthreads; i++)
    cost_list_init_aux (L + i);
  return L;
}

/* return the number of bytes allocated */
static void
cost_list_clear_aux (cost_list_t *l)
{
  for (int i = 0; i <= l->cmax; i++)
    free (l->list[i]);
  free (l->list);
  free (l->size);
  free (l->alloc);
}

static void
cost_list_clear (cost_list_t *L, int nthreads)
{
#ifdef HAVE_OPENMP
#pragma omp parallel for
#endif
    for (int i = 0; i < nthreads; i++)
      cost_list_clear_aux (L + i);
    free (L);
}

/* add pair (j,c) into l */
static void
add_cost (cost_list_t *l, index_t j, int32_t c)
{
  if (c < 0)
    c = 0; /* ensures all costs are >= 0 */
  if (c > l->cmax)
    {
      l->list = realloc (l->list, (c + 1) * sizeof (index_t*));
      l->size = realloc(l->size, (c + 1) * sizeof (unsigned long));
      l->alloc = realloc(l->alloc, (c + 1) * sizeof (unsigned long));
      for (int i = l->cmax + 1; i <= c; i++)
	{
	  l->list[i] = NULL;
	  l->size[i] = 0;
	  l->alloc[i] = 0;
	}
      l->cmax = c;
    }
  ASSERT(c <= l->cmax);
  if (l->size[c] == l->alloc[c])
    {
      unsigned long new_alloc = l->alloc[c];
      new_alloc += 1 + new_alloc / MARGIN;
      l->list[c] = realloc (l->list[c], new_alloc * sizeof (index_t));
      l->alloc[c] = new_alloc;
    }
  ASSERT(l->size[c] < l->alloc[c]);
  l->list[c][l->size[c]] = j;
  l->size[c] ++;
}

/* since merge costs might be negative (for weight 2), we translate them by 3,
   so that 2-merges with no cancellation give biased cost -2+3=1, and those
   with cancellation give biased cost 0, so they will be merged first */
#define BIAS 3

/* compute the merge cost for column j, and adds it to l if needed */
static void
compute_merges_aux (cost_list_t *l, index_t j,
		     filter_matrix_t *mat, int cbound)
{
  if (1 <= mat->wt[j] && mat->wt[j] <= mat->cwmax)
    {
      int32_t c = merge_cost (mat, j) + BIAS;
      if (c <= cbound)
	add_cost (l + get_thread_num (), j, c);
    }
}

/* accumulate in L all merges of (biased) cost <= cbound */
static void
compute_merges (cost_list_t *L, filter_matrix_t *mat,
		int cbound, index_t j0)
{
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(dynamic,128)
#endif
  for (index_t j = j0; j < mat->ncols; j++)
    compute_merges_aux (L, j, mat, cbound);
}

typedef struct {
  index_t *list;
  unsigned long size;
  unsigned long alloc;
} merge_list_t;

static void
merge_list_init (merge_list_t *l)
{
  l->list = NULL;
  l->size = 0;
  l->alloc = 0;
}

static void
merge_list_swap (merge_list_t *l, merge_list_t *newl)
{
  index_t *t = l->list;
  l->list = newl->list;
  newl->list = t;
  unsigned long tmp = l->size;
  l->size = newl->size;
  newl->size = tmp;
  tmp = l->alloc;
  l->alloc = newl->alloc;
  newl->alloc = tmp;
}

static void
merge_list_add (merge_list_t *l, index_t j)
{
  if (l->size == l->alloc)
    {
      unsigned long new_alloc = l->alloc;
      new_alloc += 1 + l->alloc / MARGIN;
      l->list = realloc (l->list, new_alloc * sizeof (index_t));
      l->alloc = new_alloc;
    }
  l->list[l->size] = j;
  l->size ++;
}

static void
merge_list_clear (merge_list_t *l)
{
  free (l->list);
}

/* apply merges of index = k mod nthreads */
static void
apply_merge_aux (index_t *l, unsigned long size, int k, int nthreads,
		 filter_matrix_t *mat, FILE *out)
{
  int32_t fill_in = 0;

  /* Note: the 2-merges are not necessarily processed in increasing fill-in,
     since those with fill-in < -2 are capped to -3. All we are sure is that
     those with fill-in < -2 (thus -4, -6, -8, ...) are processed first, then
     those with fill-in -2. */
  for (unsigned long t = k; t < size; t += nthreads)
    fill_in += merge_do (mat, l[t], out);
#ifdef HAVE_OPENMP
#pragma omp critical
#endif
  mat->tot_weight += fill_in;
}

typedef struct
{
  int c, cmax;     /* c goes from 0 to cmax */
  int i, nthreads; /* i goes from 0 to nthreads-1 */
  unsigned long k; /* k goes from 0 to (L+i)->size[c]-1 */
} status_t;

static void
init_status (status_t *s, int cmax, int nthreads)
{
  s->c = 0;
  s->cmax = cmax;
  s->i = 0;
  s->nthreads = nthreads;
  s->k = 0;
}

static void
work (filter_matrix_t *mat, int k, int nthreads,
      merge_list_t *l, merge_list_t *newl, unsigned long wanted, mpz_t z,
      cost_list_t *L, status_t *s, FILE *out)
{
  ASSERT(nthreads >= 2);
  if (k == nthreads - 1) /* thread nthreads-1 computes up to 'wanted' merges
			    and puts them in newl */
    {
      newl->size = 0;
      while (s->c <= s->cmax && newl->size < wanted)
	{
	  /* the current (c,i,k) is not necessarily valid */
	  if (s->i >= s->nthreads)
	    {
	      (s->c) ++;
	      s->i = 0;
	    }
	  /* here i < s->nthreads */
	  else if (s->c > (L+(s->i))->cmax)
	    (s->i) ++;
	  /* here c <= (L+i)->cmax and i < s->nthreads */
	  else if (s->k >= (L+(s->i))->size[s->c])
	    {
	      (s->i) ++;
	      s->k = 0;
	    }
	  /* here c <= (L+i)->cmax, i < s->nthreads, and
	     k < (L+i)->size[c] */
	  else
	    {
	      index_t j = (L+(s->i))->list[s->c][s->k];
	      int ok = 1;
	      int w = mat->wt[j];
	      for (int t = 0; t < w && ok; t++)
		ok = mpz_tstbit (z, mat->R[j][t]) == 0;
	      if (ok) /* mark rows used */
		{
		  for (int t = 0; t < w; t++)
		    mpz_setbit (z, mat->R[j][t]);
		  merge_list_add (newl, j);
		}
	      (s->k) ++; /* go to the next merge */
	    }
	}
    }
  else /* the other threads apply the merges in l */
    apply_merge_aux (l->list, l->size, k, nthreads - 1, mat, out);
}

/* return the number of merges applied */
static unsigned long
apply_merges (cost_list_t *L, int nthreads, filter_matrix_t *mat, FILE *out,
	      int cmax_max)
{
  /* We first prepare a list of independent ideals of small cost to merge,
     i.e., all rows where those ideals appear are independent.
     This step is mono-thread. */
  mpz_t z; /* bit vector to detect rows not yet used */
  mpz_init (z);

  /* first compute the total number of possible merges */
  unsigned long total_merges = 0;
  for (int c = 0; c <= cmax_max; c++)
    for (int i = 0; i < nthreads; i++)
      if (c <= (L+i)->cmax)
	total_merges += (L+i)->size[c];

  merge_list_t l[1], newl[1]; /* list of independent merges */
  merge_list_init (l);
  merge_list_init (newl);
  unsigned long wanted = total_merges / 24;
  status_t s[1];
  init_status (s, cmax_max, nthreads);
  /* we need nthreads2 >= 2 */
  int nthreads2 = (nthreads == 1) ? 2 : nthreads;
  if (wanted < (unsigned long) nthreads2 - 1)
    wanted = nthreads2 - 1;
  while (1)
    {
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static,1)
#endif
      for (int k = 0; k < nthreads2; k++)
	work (mat, k, nthreads2, l, newl, wanted, z, L, s, out);

      /* each merge decreases the number of rows and columns by one */
      mat->rem_nrows -= l->size;
      mat->rem_ncols -= l->size;

      if (s->c > cmax_max && newl->size == 0)
	break;

      /* swap l and newl */
      merge_list_swap (l, newl);
    }

  if (mat->cwmax == 2) /* we first process all 2-merges */
    {
      if (l->size == total_merges)
        mat->cwmax ++;
    }
  else
    {
      if (mat->cwmax < MERGE_LEVEL_MAX)
        mat->cwmax ++;
    }

  merge_list_clear (l);
  merge_list_clear (newl);
  mpz_clear (z);

  return l->size;
}

static double
average_density (filter_matrix_t *mat)
{
  return (double) mat->tot_weight / (double) mat->rem_nrows;
}

int
main (int argc, char *argv[])
{
    char *argv0 = argv[0];

    filter_matrix_t mat[1];
    report_t rep[1];

    int nthreads = 1;
    uint32_t skip = DEFAULT_MERGE_SKIP;
    double target_density = DEFAULT_MERGE_TARGET_DENSITY;

#ifdef HAVE_MINGW
    _fmode = _O_BINARY;     /* Binary open for all files */
#endif

    double tt;
    double cpu0 = seconds ();
    double wct0 = wct_seconds ();
    param_list pl;
    param_list_init (pl);
    declare_usage(pl);
    argv++,argc--;

    param_list_configure_switch(pl, "force-posix-threads", &filter_rels_force_posix_threads);

#ifdef HAVE_MINGW
    _fmode = _O_BINARY;     /* Binary open for all files */
#endif

    if (argc == 0)
      usage (pl, argv0);

    for( ; argc ; ) {
      if (param_list_update_cmdline(pl, &argc, &argv)) continue;
      fprintf (stderr, "Unknown option: %s\n", argv[0]);
      usage (pl, argv0);
    }
    /* print command-line arguments */
    verbose_interpret_parameters (pl);
    param_list_print_command_line (stdout, pl);
    fflush(stdout);

    const char *purgedname = param_list_lookup_string (pl, "mat");
    const char *outname = param_list_lookup_string (pl, "out");
    const char *path_antebuffer = param_list_lookup_string(pl, "path_antebuffer");

    param_list_parse_int (pl, "t", &nthreads);
#ifdef HAVE_OPENMP
    omp_set_num_threads (nthreads);
#endif

#ifdef HAVE_MALLOPT
    /* experimentally, setting the number of arenas to twice the number of
       threads seems optimal (man mallopt says it should match the number of
       threads) */
    int arenas = 2 * nthreads;
    mallopt (M_ARENA_MAX, arenas);
#endif

    param_list_parse_uint (pl, "skip", &skip);

    param_list_parse_double (pl, "target_density", &target_density);

    /* Some checks on command line arguments */
    if (param_list_warn_unused(pl))
    {
      fprintf(stderr, "Error, unused parameters are given\n");
      usage(pl, argv0);
    }

    if (purgedname == NULL)
    {
      fprintf(stderr, "Error, missing -mat command line argument\n");
      usage (pl, argv0);
    }
    if (outname == NULL)
    {
      fprintf(stderr, "Error, missing -out command line argument\n");
      usage (pl, argv0);
    }

    set_antebuffer_path (argv0, path_antebuffer);

    /* Read number of rows and cols on first line of purged file */
    purgedfile_read_firstline (purgedname, &(mat->nrows), &(mat->ncols));

#if (__SIZEOF_INDEX__ == 4)
    if (mat->nrows >> 32)
      {
	fprintf (stderr, "Error, nrows = %" PRIu64 " larger than 2^32, please recompile with -D__SIZEOF_INDEX__=8\n", mat->nrows);
	exit (EXIT_FAILURE);
      }
    if (mat->ncols >> 32)
      {
	fprintf (stderr, "Error, ncols = %" PRIu64 " larger than 2^32, please recompile with -D__SIZEOF_INDEX__=8\n", mat->ncols);
	exit (EXIT_FAILURE);
      }
#endif

    /* initialize rep (i.e., mostly opens outname) and write matrix dimension */
    rep->type = 0;
    rep->outfile = fopen_maybe_compressed (outname, "w");
    ASSERT_ALWAYS(rep->outfile != NULL);

    /* some explanation about the history file */
    fprintf (rep->outfile, "# Every line starting with # is ignored.\n");
    fprintf (rep->outfile, "# A line i1 i2 ... ik means that row i1 ");
    fprintf (rep->outfile, "is added to i2, ..., ik, and row i1\n");
    fprintf (rep->outfile, "# is removed afterwards ");
    fprintf (rep->outfile, "(where row 0 is the first line in *.purged.gz).\n");
#ifdef FOR_DL
    fprintf (rep->outfile, "# A line ending with #j ");
    fprintf (rep->outfile, "means that ideal of index j should be merged.\n");
#endif

    /* initialize the matrix structure */
    initMat (mat, skip);

    /* we bury the 'skip' ideals of smallest index */
    mat->skip = skip;

    /* Read all rels and fill-in the mat structure */
    tt = seconds ();
    filter_matrix_read (mat, purgedname);
    printf ("Time for filter_matrix_read: %2.2lfs\n", seconds () - tt);

    double cpu_after_read = seconds ();
    double wct_after_read = wct_seconds ();

    renumber (mat);

    mat->R = (index_t **) malloc (mat->ncols * sizeof(index_t *));
    ASSERT_ALWAYS(mat->R != NULL);

#ifdef HAVE_MALLOPT
    printf ("Using MERGE_LEVEL_MAX=%d, CBOUND_INCR=%d, M_ARENA_MAX=%d\n",
            MERGE_LEVEL_MAX, CBOUND_INCR, arenas);
#else
    printf ("Using MERGE_LEVEL_MAX=%d, CBOUND_INCR=%d\n",
            MERGE_LEVEL_MAX, CBOUND_INCR);
#endif

    printf ("N=%" PRIu64 " W=%" PRIu64 " W/N=%.2f cpu=%.1fs wct=%.1fs mem=%luM\n",
	    mat->rem_nrows, mat->tot_weight, average_density (mat),
	    seconds () - cpu0, wct_seconds () - wct0,
	    PeakMemusage () >> 10);
    fflush (stdout);

    mat->cwmax = 2;

    /* initialize R[j] to NULL */
#ifdef HAVE_OPENMP
#pragma omp parallel for
#endif
    for (index_t j = 0; j < mat->ncols; j++)
      mat->R[j] = NULL;

    /* jmin[w] for 1 <= w <= MERGE_LEVEL_MAX is the smallest column of weight w
       at beginning. We set jmin[0] to 0 to tell that jmin[] was not
       initialized. */
    index_t jmin[MERGE_LEVEL_MAX + 1] = {0,};

    unsigned long lastN, lastW;
    double lastWoverN;
    int cbound = BIAS; /* bound for the (biased) cost of merges to apply */
    int pass = 0;
    while (1)
      {
	double cpu1 = seconds (), wct1 = wct_seconds ();

	pass ++;

	/* Once cwmax >= 3, tt each pass, we increase cbound to allow more
	   merges. If one decreases CBOUND_INCR, the final matrix will be
	   smaller, but merge will take more time.
	   If one increases CBOUND_INCR, merge will be faster, but the final
	   matrix will be larger. */
	if (mat->cwmax > 2)
	  cbound += CBOUND_INCR;

	lastN = mat->rem_nrows;
	lastW = mat->tot_weight;
	lastWoverN = (double) lastW / (double) lastN;

	compute_weights (mat, jmin);

	compute_R (mat, jmin[mat->cwmax]);

	double cpu2 = seconds (), wct2 = wct_seconds ();

	cost_list_t *L = cost_list_init (nthreads);
	compute_merges (L, mat, cbound, jmin[mat->cwmax]);

	cpu2 = seconds () - cpu2;
	wct2 = wct_seconds () - wct2;
	print_timings ("   compute_merges took", cpu2, wct2);
	cpu_t[2] += cpu2;
	wct_t[2] += wct2;

	double cpu3 = seconds (), wct3 = wct_seconds ();

	unsigned long nmerges = apply_merges (L, nthreads, mat, rep->outfile,
					      cbound);

	cpu3 = seconds () - cpu3;
	wct3 = wct_seconds () - wct3;
	print_timings ("   apply_merges took", cpu3, wct3);
	cpu_t[3] += cpu3;
	wct_t[3] += wct3;

	cost_list_clear (L, nthreads);

	cpu1 = seconds () - cpu1;
	wct1 = wct_seconds () - wct1;
	print_timings ("   pass took", cpu1, wct1);
	cpu_t[4] += cpu1;
	wct_t[4] += wct1;

	/* estimate current average fill-in */
	double av_fill_in = ((double) mat->tot_weight - (double) lastW)
	  / (double) (lastN - mat->rem_nrows);

	printf ("N=%" PRIu64 " W=%" PRIu64 " W/N=%.2f fill-in=%.2f cpu=%.1fs wct=%.1fs mem=%luM [pass=%d,cwmax=%d]\n",
		mat->rem_nrows, mat->tot_weight,
		(double) mat->tot_weight / (double) mat->rem_nrows, av_fill_in,
		seconds () - cpu0, wct_seconds () - wct0,
		PeakMemusage () >> 10, pass, mat->cwmax);
	fflush (stdout);

	if (average_density (mat) >= target_density)
	  break;

	if (nmerges == 0 && mat->cwmax == MERGE_LEVEL_MAX)
	  break;
      }

    fclose_maybe_compressed (rep->outfile, outname);

    if (average_density (mat) > target_density)
      {
        /* estimate N for W/N = target_density, assuming W/N = a*N + b */
        unsigned long N = mat->rem_nrows;
        double WoverN = (double) mat->tot_weight / (double) N;
        double a = (lastWoverN - WoverN) / (double) (lastN - N);
        double b = WoverN - a * (double) N;
        /* we want target_density = a*N_target + b */
        printf ("Estimated N=%" PRIu64 " for W/N=%.2f\n",
                (uint64_t) ((target_density - b) / a), target_density);
      }

#ifndef FOR_DL /* we don't do renumbering for DL */
    print_timings ("renumber       :", cpu_t[5], wct_t[5]);
#endif
    print_timings ("compute_weights:", cpu_t[0], wct_t[0]);
    print_timings ("compute_R      :", cpu_t[1], wct_t[1]);
    print_timings ("compute_merges :", cpu_t[2], wct_t[2]);
    print_timings ("apply_merges   :", cpu_t[3], wct_t[3]);
    print_timings ("pass           :", cpu_t[4], wct_t[4]);

    printf ("Final matrix has N=%" PRIu64 " nc=%" PRIu64 " (%" PRIu64
	    ") W=%" PRIu64 "\n", mat->rem_nrows, mat->rem_ncols,
	    mat->rem_nrows - mat->rem_ncols, mat->tot_weight);
    fflush (stdout);

#ifdef FOR_DL
    free (mat->p);
#endif
    clearMat (mat);

    param_list_clear (pl);

    print_timing_and_memory (stdout, cpu0, wct0);
    printf ("After matrix read:\n");
    print_timing_and_memory (stdout, cpu_after_read, wct_after_read);

    return 0;
}
