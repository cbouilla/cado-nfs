#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"
#include "utils.h"
#include "auxiliary.h" /* for common routines with polyselect.c */
#include "size_optimization.h"
#include "area.h"

/* for the rotation, we try (j*x+k) for |k| <= 2^MAX_k */
int MAX_k = 16;

// #define KK -16346
// #define PP 7

/* if gpn and pn are not coprime, let g = gcd(gpn, pn):
   we must have g*g'*v = r + j*g*p' with g' = gpn/g and p'=pn/g:
   (a) if g does not divide r, there is no solution
   (b) if g divides r, then we must have:
   g'*v = r' + j*p' with r'=r/g, let v0 = r'/g' mod p',
   then v0, v0+p', ..., v0+(g-1)*p' are the g solutions. */
static void
special_update (double *A, long K0, long K1, const residueul_t gpn,
		modulusul_t Pn, residueul_t r, double alpha_p)
{
  modulusul_t Pp;
  residueul_t rp, gp;
  int ret;
  long k0, k;
  modintul_t gg;
  unsigned long g, rr, pp, pn;

  pn = modul_getmod_ul (Pn);
  modul_gcd (gg, gpn, Pn);
  g = gg[0];
  rr = modul_get_ul (r, Pn);
  if (rr % g == 0)
    {
      pp = pn / g;
      modul_initmod_ul (Pp, pp);
      modul_init (gp, Pp);
      modul_init (rp, Pp);
      modul_set_ul (gp, modul_get_ul (gpn, Pn) / g, Pp);
      modul_set_ul (rp, rr / g, Pp);
      ret = modul_inv (gp, gp, Pp);
      ASSERT_ALWAYS (ret != 0);
      modul_mul (rp, rp, gp, Pp);
      k0 = modul_get_ul (rp, Pp);
      for (k = k0; k <= K1; k += pp)
	{
	  A[k - K0] -= alpha_p;
#if (defined(KK) && defined(PP))
	  if (k == KK && (pn % PP) == 0)
	    fprintf (stderr, "subtract %f to AA[%d] for pp=%lu, now %f\n",
		     alpha_p, KK, pp, A[k - K0]);
#endif
	}
      for (k = k0 - pp; k >= K0; k -= pp)
	{
	  A[k - K0] -= alpha_p;
#if (defined(KK) && defined(PP))
	  if (k == KK && (pn % PP) == 0)
	    fprintf (stderr, "subtract %f to AA[%d] for pp=%lu, now %f\n",
		     alpha_p, KK, pp, A[k - K0]);
#endif
	}
      modul_clear (gp, Pp);
      modul_clear (rp, Pp);
      modul_clearmod (Pp);
    }
}

/* f(x) of degree d is the polynomial f0(x) + j*x*g(x)
   where g(x) = b*x-m.
   A is a table of K1-K0+1 entries, where A[k-K0] corresponds to the alpha
   contribution for f(x) + k*g(x)
   p is the current prime, we consider pn = p^n here */
void
update_table (mpz_t *f, int d, mpz_t m, mpz_t b, double *A, long K0, long K1,
	      unsigned long pn, double alpha_p)
{
  long k, k0;
  modul_poly_t fpn; /* f mod p^n */
  modulusul_t Pn;
  residueul_t l, gpn, bpn, r, v;
  int ret;

  modul_initmod_ul (Pn, pn);
  modul_init (gpn, Pn);
  modul_init (bpn, Pn);
  modul_init (r, Pn);
  modul_init (l, Pn);
  modul_init (v, Pn);

  modul_poly_init (fpn, d);

  /* first reduce f(x) and g(x) mod p^n */
  mpz_poly F;
  F->coeff = f;
  F->deg = d;
  modul_poly_set_mod_raw (fpn, F, Pn);

  modul_set_ul (gpn, mpz_fdiv_ui (m, pn), Pn);
  /* invariant: gpn = -g(l) */
  modul_set_ul (bpn, mpz_fdiv_ui (b, pn), Pn);
  modul_set_ul (l, 0, Pn);

  for (;;)
    {
      modul_poly_eval (r, fpn, l, Pn);
      /* invariant: gpn = -g(l) */
      /* we want r = v*gpn, i.e., v = r/gpn; r is never 0 otherwise f(x) would
	 be divisible by p^n, but gpn = g(l) can be 0 */
      ret = modul_intcmp_ul (gpn, 0);
      if (ret != 0)
	{
	  ret = modul_inv (v, gpn, Pn); /* FIXME: use batch inversion */
	  if (ret == 0) /* gpn and pn are not coprime */
	    special_update (A, K0, K1, gpn, Pn, r, alpha_p);
	  else
	    {
	      modul_mul (v, v, r, Pn);

	      k0 = modul_get_ul (v, Pn);
	      for (k = k0; k <= K1; k += pn)
		{
		  A[k - K0] -= alpha_p;
#if (defined(KK) && defined(PP))
		  if (k == KK && (pn % PP) == 0)
		    fprintf (stderr, "subtract %f to AA[%d] for l=%lu, pn=%lu, now %f\n",
			     alpha_p, KK, modul_get_ul (l, Pn), pn, A[k - K0]);
#endif
		}
	      for (k = k0 - (long) pn; k >= K0; k -= (long) pn)
		{
		  A[k - K0] -= alpha_p;
#if (defined(KK) && defined(PP))
		  if (k == KK && (pn % PP) == 0)
		    fprintf (stderr, "subtract %f to AA[%d] for l=%lu, pn=%lu, now %f\n",
			     alpha_p, KK, modul_get_ul (l, Pn), pn, A[k - K0]);
#endif
		}
	    }
	}

      /* since g(x) = b*x-m, -g(l) = m-b*l */
      modul_sub (gpn, gpn, bpn, Pn);
      modul_add_ul (l, l, 1, Pn);
      if (modul_intcmp_ul (l, 0) == 0)
	break;
    }

  modul_clear (gpn, Pn);
  modul_clear (bpn, Pn);
  modul_clear (r, Pn);
  modul_clear (l, Pn);
  modul_clear (v, Pn);
  modul_clearmod (Pn);
  modul_poly_clear (fpn);
}

/* D <- discriminant (f+k*g), which has degree d */
static void
discriminant_k (mpz_t *D, mpz_poly_ptr f, mpz_t m, mpz_t b)
{
  int i, j, k;
  uint32_t **M, pivot;
  int d = f->deg;

  ASSERT_ALWAYS(d <= 9);

  /* we first put in D[i] the value of disc(f + i*g) for 0 <= i <= d,
     thus if disc(f + k*g) = a[d]*k^d + ... + a[0], then
          D[0] = a[0]
          D[1] = a[0] + a[1] + ... + a[d]
          ...
          D[d] = a[0] + a[1]*d + ... + a[d]*d^d */

  mpz_poly_discriminant (D[0], f);
  for (i = 1; i <= d; i++)
    {
      /* add b*x - m */
      mpz_add (f->coeff[1], f->coeff[1], b);
      mpz_sub (f->coeff[0], f->coeff[0], m);
      mpz_poly_discriminant (D[i], f);
    }

  /* initialize matrix coefficients */
  M = (uint32_t**) malloc ((d + 1) * sizeof(uint32_t*));
  for (i = 0; i <= d; i++)
    {
      M[i] = (uint32_t*) malloc ((d + 1) * sizeof(uint32_t));
      M[i][0] = 1;
      for (j = 1; j <= d; j++)
        M[i][j] = i * M[i][j-1];
    }

  for (j = 0; j < d; j++)
    {
      /* invariant: D[i] = M[i][0] * a[0] + ... + M[i][d] * a[d]
         with M[i][k] = 0 for k < j and k < i */
      for (i = j + 1; i <= d; i++)
        {
          /* eliminate M[i][j] */
          pivot = M[i][j] / M[j][j];
          mpz_submul_ui (D[i], D[j], pivot);
          for (k = j; k <= d; k++)
            M[i][k] -= pivot * M[j][k];
        }
    }

  /* now we have an upper triangular matrix */
  for (j = d; j > 0; j--)
    {
      for (k = j + 1; k <= d; k++)
        mpz_submul_ui (D[j], D[k], M[j][k]);
      ASSERT_ALWAYS(mpz_divisible_ui_p (D[j], M[j][j]));
      mpz_divexact_ui (D[j], D[j], M[j][j]);
    }

  /* restore the original f[] */
  mpz_submul_ui (f->coeff[1], b, d);
  mpz_addmul_ui (f->coeff[0], m, d);

  for (i = 0; i <= d; i++)
    free (M[i]);
  free (M);
}

unsigned long
rotate_area (long K0, long K1, long J0, long J1)
{
  return (unsigned long) (J1 - J0 + 1) * (unsigned long) (K1 - K0 + 1);
}

/* Use Emmanuel Thome's idea: assuming alpha(f + k*g) admits a Gaussian
   distribution with mean 'm' and standard deviation 's', then the probability
   that one polynomial has a value of alpha >= A is
   1/2*(1 - erf((A-m)/s/sqrt(2))), thus the probability that K polynomials
   have all their alpha values >= A is [1/2*(1 - erf((A-m)/s/sqrt(2)))]^K.
   For 'm' and 's' fixed, and a given K, we define the value of A for which
   this probability is 1/2 to be the expected value of A for K polynomials.

   We assume lognorm(f + k*g) + E(alpha(f + k*g)) is first decreasing, then
   increasing, then the optimal K corresponds to the minimum of that function.
*/
void
rotate_bounds (mpz_poly_ptr f, mpz_t b, mpz_t m, long *K0, long *K1,
               long *J0, long *J1, int verbose)
{
  /* exp_alpha[i] corresponds to K=2^i polynomials for m=0, s=1
     f := x -> 1/2*(1 - erf(x/sqrt(2)));
     seq(fsolve(f(x)^(2^k) = 1/2, x=-log[2](1.0+k)), k=0..30);
   */
  double exp_alpha[] = {0.0 /* K=1 */, -0.5449521356 /* 2 */,
                        -0.9981488825 /* 4 */, -1.385198061 /* 8 */,
                        -1.723526050 /* 16 */, -2.025111894 /* 32 */,
                        -2.298313131 /* 64 */, -2.549067054 /* 128 */,
                        -2.781676726 /* 256 */, -2.999326227 /* 512 */,
                        -3.204420841 /* 1024 */, -3.398814100 /* 2048 */,
                        -3.583961388 /* 4096 */, -3.761025425 /* 8192 */,
                        -3.930949902 /* 16384 */, -4.094511733 /* 32768 */,
                        -4.252358774 /* 65536 */, -4.405037486 /* 131072 */,
                        -4.553013560 /* 262144 */, -4.696687518 /* 524288 */,
                        -4.836406692 /* 2^20 */, -4.972474538 /* 2^21 */,
                        -5.105157963 /* 2^22 */, -5.234693169 /* 2^23 */,
                        -5.361290351 /* 2^24 */, -5.485137511 /* 2^25 */,
                        -5.606403590 /* 2^26 */, -5.725241052 /* 2^27 */,
                        -5.841788041 /* 2^28 */, -5.956170181 /* 2^29 */,
                        DBL_MAX};
  int i;
  long k0 = 0, j0 = 0;
  double lognorm, alpha, E0, E, best_E;
  double skewness = L2_skewness (f, SKEWNESS_DEFAULT_PREC);
  long jmax = (long) ((double) (1L << MAX_k) / skewness);
  unsigned long max_area = 1UL << MAX_k;

#define MARGIN 0.12 /* we allow a small error margin in the expected lognorm
                       + alpha values, to get a larger search range */

  E0 = L2_lognorm (f, skewness);

  *K0 = -2;
  *J0 = -16;
  *J1 = 16;

  /* look for negative k: -2, -4, -8, ... */
  best_E = E0;
  for (i = 1; rotate_area (*K0, -*K0, *J0, *J1) < max_area; i++, *K0 *= 2)
    {
      k0 = rotate_aux (f->coeff, b, m, k0, *K0, 0);
      lognorm = L2_skew_lognorm (f, SKEWNESS_DEFAULT_PREC);
      alpha = exp_alpha[i];
      E = lognorm + alpha;
      if (E < best_E + MARGIN)
        {
          if (E < best_E)
            best_E = E;
        }
      else
        break;
    }
  /* go back to k=0 */
  k0 = rotate_aux (f->coeff, b, m, k0, 0, 0);
  *K1 = -*K0;

  /* now try negative j: -1, -3, -7, ... */
  for (i++; exp_alpha[i] != DBL_MAX && rotate_area (*K0, *K1, *J0, -*J0) < max_area; i++, *J0 = 2 * *J0 - 1)
    {
      j0 = rotate_aux (f->coeff, b, m, j0, *J0, 1);
      lognorm = L2_skew_lognorm (f, SKEWNESS_DEFAULT_PREC);
      alpha = exp_alpha[i];
      E = lognorm + alpha;
      if (E < best_E + MARGIN)
        {
          if (E < best_E)
            best_E = E;
        }
      else
        break;
      if (1 - 2 * *J0 > jmax)
        break;
    }
  *J1 = -*J0;

  if (verbose > 0)
    fprintf (stderr, "# Rotate bounds: %ld <= j <= %ld, %ld <= k <= %ld\n",
             *J0, *J1, *K0, *K1);

  /* rotate back to j=k=0 */
  rotate_aux (f->coeff, b, m, k0, 0, 0);
  rotate_aux (f->coeff, b, m, j0, 0, 1);
}

/* Return the smallest value of lognorm + alpha(f + (j*x+k)*(b*x-m)) for
   j and k small enough such that the norm does not increase too much, and
   modify f[] accordingly.
   The parameter "multi" means that several polynomials are wanted. If
   multi=0 or 1, then only 1 polynomial is returned (classical behavior).
   Otherwise, multi polynomials are stored in jmin and kmin (that
   must be initialized arrays with at least multi elements). This option
   might be useful for Coppersmith variant (a.k.a. MNFS).
   In the multi case, the smallest of the returned values of lognorm + alpha
   is returned (and f[] accordingly).
   Warning: the caller is responsible to update the skewness if needed.

   FIXME: it would be better to pass the polynomial g to rotate, instead of
   b and m.
   */
double
rotate (mpz_poly_ptr f, unsigned long alim, mpz_t m, mpz_t b,
        long *jmin, long *kmin, int multi, int verbose)
{
  mpz_array_t *D;
  long K0, K1, J0, J1, k0, k, i, j, j0, bestk;
  double *A, alpha, lognorm, best_alpha = DBL_MAX, best_lognorm = DBL_MAX;
  double corr = 0.0;
  double alpha0;
  unsigned long p;
  double *best_E = NULL; /* set to NULL to avoid warning... */
  double time_alpha = 0.0, time_norm = 0.0;
  int d = f->deg;
  unsigned long pp;
  double one_over_pm1, logp, average_alpha = 0.0;

  /* allocate best_E, to store the best (lognorm+alpha) in multi mode */
  if (multi > 1)
    {
      best_E = (double *) malloc (multi * sizeof (double));
      for (i = 0; i < multi; ++i)
        best_E[i] = DBL_MAX;
    }

  /* allocate D(k) = disc(f + (j*x+k)*g, x) */
  D = alloc_mpz_array (d + 1);

  /* compute range for k */
  rotate_bounds (f, b, m, &K0, &K1, &J0, &J1, verbose);
  ASSERT_ALWAYS(K0 <= 0 && 0 <= K1);

  /* allocate sieving zone for computing alpha */
  A = (double*) malloc ((K1 + 1 - K0) * sizeof (double));
  j0 = k0 = 0; /* the current coefficients f[] correspond to f+(j*x+k)*g */

  *jmin = *kmin = 0;

  alpha0 = get_alpha (f, alim); /* value of alpha without rotation */

  ASSERT_ALWAYS(J0 < 0 && 0 < J1);
  for (j = 0;;)
    {
      /* we consider j=0, 1, ..., J1, then J0, J0+1, ..., -1 */
      j0 = rotate_aux (f->coeff, b, m, j0, j, 1);
      /* go back to k=0 for the discriminant */
      k0 = rotate_aux (f->coeff, b, m, k0, 0, 0);
      /* D(k) = disc(f + (j*x+k)*g, x) (j is now fixed) */
      discriminant_k (D->data, f, m, b);

      for (k = K0; k <= K1; k++)
  A[k - K0] = 0.0; /* A[k - K0] will store the value alpha(f + k*g) */

  for (p = 2; p <= alim; p += 1 + (p & 1))
    if (ulong_isprime (p))
      {
        int i;
        /* We skip primes which divide all coefficients of f, since then
           f mod p is zero. This can only happen when p divides N, which is
           silly in GNFS, but maybe the user is stupid. */
        for (i = d; i >= 0 && mpz_divisible_ui_p (f->coeff[i], p); i--);
        if (i < 0)
          continue;

  if (k0 != 0)
    k0 = rotate_aux (f->coeff, b, m, k0, 0, 0);

  time_alpha -= seconds ();

  one_over_pm1 = 1.0 / (double) (p - 1);
  logp = log ((double) p);
  for (pp = p; pp <= alim; pp *= p)
    {
      /* Murphy (page 48) defines cont_p(F) = q_p*p/(p^2-1)
         = q_p*p/(p+1)*(1/p+1/p^2+...)
         The contribution for p^k is thus q_p*p/(p+1)/p^k. */
      alpha = logp / (double) (p + 1) * (double) p / (double) pp;
      /* the following is the average contribution for a prime not
         dividing the discriminant, cf alpha.sage, function alpha_p.
         We take it into account only for p, not for p^2, p^3, ... */
      if (p == pp)
        average_alpha += logp * one_over_pm1;
      /* we do not consider the projective roots here, since the
         corresponding correction will be considered separately for each
         row below */
      /* + alpha_p_projective (f, d, (D->data)[0], p); */
      update_table (f->coeff, d, m, b, A, K0, K1, pp, alpha);
    }

  time_alpha += seconds ();
      } /* end of loop on primes p */

  /* determine the best alpha in each row */
  bestk = K0;
  for (k = K0 + 1; k <= K1; k++)
    if (A[k - K0] < A[bestk - K0])
      bestk = k;

  /* Correction to apply to the current row (takes into account the projective
     roots). FIXME: we are lazy here, we should only consider the contribution
     from the projective roots. */
  k0 = rotate_aux (f->coeff, b, m, k0, bestk, 0);
  corr = get_alpha (f, alim) - A[bestk - K0];

  if (verbose > 1)
    fprintf (stderr, "# best alpha for j=%ld: k=%ld with %f\n",
             j, bestk, A[bestk - K0] + corr);

  /* now finds the best lognorm+alpha */
  time_norm -= seconds ();
  for (k = K0; k <= K1; k++)
    {
      alpha = A[k - K0] + corr;
      if (alpha < best_alpha + 2.0)
        {
          /* check lognorm + alpha < best_lognorm + best_alpha */

          /* translate from k0 to k */
          k0 = rotate_aux (f->coeff, b, m, k0, k, 0);
          lognorm = L2_skew_lognorm (f, SKEWNESS_DEFAULT_PREC);
          if (multi <= 1) {
              if (lognorm + alpha < best_lognorm + best_alpha) {
                  best_lognorm = lognorm;
                  best_alpha = alpha;
                  *kmin = k;
                  *jmin = j;
              }
          } else { /* multi mode */
              /* Rem: best_lognorm + best_alpha is the worse of the
                 preselected */
              double newE = lognorm + alpha;
              if (newE < best_E[multi-1]) {
                  int ii;
                  /* find position; assume list of preselected is sorted */
                  for (ii = 0; ii < multi; ++ii) {
                      if (best_E[ii] > newE)
                          break;
                  }
                  ASSERT_ALWAYS(ii < multi);
                  /* insert */
                  for (i = multi - 1; i > ii; --i) {
                      kmin[i] = kmin[i-1];
                      jmin[i] = jmin[i-1];
                      best_E[i] = best_E[i-1];
                  }
                  kmin[ii] = k;
                  jmin[ii] = j;
                  best_E[ii] = newE;
              }
          }
        }
    }
  time_norm += seconds ();

  j++;
  if (j > J1)
    j = J0;
  else if (j == 0)
    break;

    } /* end of loop on j */

  /* we now have f + (j0*x+k0)*(bx-m) and we want f + (jmin*x+kmin)*(bx-m),
     thus we have to add ((jmin-j0)*x+(kmin-k0)*(bx-m) */
  /* if you are in multi-mode, we use the best polynomial */
  rotate_aux (f->coeff, b, m, k0, *kmin, 0);
  rotate_aux (f->coeff, b, m, j0, *jmin, 1);

  if ((verbose > 0) && (multi <= 1))
    {
      fprintf (stderr, "# Rotate by ");
      if (*jmin != 0)
        {
          if (*jmin == -1)
            fprintf (stderr, "-");
          else if (*jmin != 1)
            fprintf (stderr, "%ld*", *jmin);
          fprintf (stderr, "x");
          if (*kmin >= 0)
            fprintf (stderr, "+");
        }
      fprintf (stderr, "%ld: alpha improved from %1.2f to %1.2f (alpha %1.2fs, norm %1.2fs)\n", *kmin, alpha0, best_alpha, time_alpha, time_norm);
    }

  if (verbose && (multi > 1)) {
      fprintf(stderr, "Found the following polynomials  (j, k, E):\n");
      for (i = 0; i < multi; ++i) {
          fprintf(stderr, "  %ld\t%ld\t%1.2f\n", jmin[i], kmin[i], best_E[i]);
      }
  }

  free (A);

  clear_mpz_array (D);

  {
      double ret_val = best_lognorm + best_alpha;
      if (multi>1) {
          ret_val = best_E[0]; /* we return the smallest */
          free(best_E);
      }
      return ret_val;
  }
}



static void
usage_and_die (char *argv0)
{
  fprintf (stderr, "usage: %s [-v] poly kmax\n", argv0);
  fprintf (stderr, "  apply rotation f += (j*x+k)*g to poly.\n");
  fprintf (stderr, "  poly: filename of polynomial\n");
  fprintf (stderr, "  j,k : integers\n");
  exit (1);
}

int
main (int argc, char **argv)
{
    cado_poly poly;
    long kmax, jmin, kmin;
    unsigned long alim = 2000;
    mpz_t b, m;
    int argc0 = argc, verbose = 0;
    char **argv0 = argv;

    while (argc >= 2 && strcmp (argv[1], "-v") == 0)
      {
        argv ++;
        argc --;
        verbose ++;
      }

    mpz_init(b);
    mpz_init(m);
    if (argc != 3)
        usage_and_die (argv0[0]);
    cado_poly_init (poly);
    if (!cado_poly_read(poly, argv[1])) {
        fprintf(stderr, "Problem when reading file %s\n", argv[1]);
        usage_and_die (argv0[0]);
    }
    kmax = strtol(argv[2], NULL, 10);
    MAX_k = kmax;

    poly->skew = L2_skewness (poly->pols[ALG_SIDE], SKEWNESS_DEFAULT_PREC);

    printf ("Initial polynomial:\n");
    if (verbose)
      print_cadopoly_extra (stdout, poly, argc0, argv0, 0);
    else
      printf ("skewness=%1.2f, alpha=%1.2f\n", poly->skew,
              get_alpha (poly->pols[ALG_SIDE], ALPHA_BOUND));
    size_optimization (poly->pols[ALG_SIDE], poly->pols[RAT_SIDE], poly->pols[ALG_SIDE], poly->pols[RAT_SIDE],
                       SOPT_DEFAULT_EFFORT, verbose - 1);
    poly->skew = L2_skewness (poly->pols[ALG_SIDE], SKEWNESS_DEFAULT_PREC);
    
    printf ("After norm optimization:\n");
    if (verbose)
      print_cadopoly_extra (stdout, poly, argc0, argv0, 0);
    else
      printf ("skewness=%1.2f, alpha=%1.2f\n",
              poly->skew, get_alpha (poly->pols[ALG_SIDE], ALPHA_BOUND));

    mpz_set (b, poly->pols[RAT_SIDE]->coeff[1]);
    mpz_neg (m, poly->pols[RAT_SIDE]->coeff[0]);
    rotate (poly->pols[ALG_SIDE], alim, m, b, &jmin, &kmin, 0, verbose - 1);
    mpz_set (poly->pols[RAT_SIDE]->coeff[1], b);
    mpz_neg (poly->pols[RAT_SIDE]->coeff[0], m);
    /* optimize again, but only translation */
    mpz_poly_fprintf (stdout, poly->pols[RAT_SIDE]);
    sopt_local_descent (poly->pols[ALG_SIDE], poly->pols[RAT_SIDE], poly->pols[ALG_SIDE], poly->pols[RAT_SIDE], 1, -1,
                                          SOPT_DEFAULT_MAX_STEPS, verbose - 1);
    poly->skew = L2_skewness (poly->pols[ALG_SIDE], SKEWNESS_DEFAULT_PREC);
    mpz_clear(b);
    mpz_clear(m);

    print_cadopoly_extra (stdout, poly, argc0, argv0, 0);
    return 0;
} 
