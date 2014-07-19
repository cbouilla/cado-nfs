/* 
   Size-optimize the raw polynomials in a file of format:
   "Yi: xxxxxx"
   "ci: xxxxxx"
   "N: xxxxxx"
*/

/* standard header files */
#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <gmp.h>

/* CADO-NFS header files */
#include "portability.h"
#include "utils.h"
#include "auxiliary.h"
#include "area.h"

#define MAX_LINE_LENGTH 1024

/* Assume the file already contains both raw and optimized polynomial
   in pairs. SKIP_YES 1 will pick the raw poly and optimize it.
   So it will ignore the even number of polynomials found, since
   we assume this is an optimized result. */
#define SKIP_YES 0

/* optimize anyway regardless of the common root condition
   this is only used when you want to try wierd things. */
int fake = 0;

/* only use translation in optimization */
int translate = 0;

/* use LLL-based algorithm */
int use_lll = 0;

/* 
   Care: poly_print is named in utils.h
*/
static void
polyprint (mpz_t *f, mpz_t *g, int deg, mpz_t N) 
{
  int i;
  gmp_printf ("\nn: %Zd\n", N);
  for (i = deg; i >= 0; i --)
  {
    gmp_printf ("c%d: %Zd\n", i, f[i]);
  }
  for (i = 1; i >= 0; i --)
  {
    gmp_printf ("Y%d: %Zd\n", i, g[i]);
  }
}


/*
  Optimize all raw polynomial in the file.
*/
static int
opt_file (FILE *file, int deg, mpz_t N) {
  int i, nroots, skip = 0;
  unsigned flag = 0UL, count = 0;
  char str[MAX_LINE_LENGTH];
  mpz_t g[2];
  mpz_t *f;
  mpz_poly_t F;
  mpz_t t, M;
  double skew = 0.0, alpha = 0.0, logmu = 0.0;
  double ave_logmu = 0.0, ave_alpha = 0.0;
  double min_logmu = DBL_MAX, max_logmu = 0.0;

  mpz_init (t);
  mpz_init (M);
  mpz_init (g[1]);
  mpz_init (g[0]);
  mpz_poly_init (F, deg);
  F->deg = deg;
  f = F->coeff;

  while (1) {
    /* wrong line or EOF */
    if (fgets(str, MAX_LINE_LENGTH, file) == NULL)
      break;
    if ( str[0] == 'Y' ) {
      if ( str[1] == '1' ) {
        gmp_sscanf (str, "Y1: %Zd\n", g[1]);
        (flag) ^= ( 1<<(8) );
      }
      else if ( str[1] == '0' ) {
        gmp_sscanf (str, "Y0: %Zd\n", g[0]);
        (flag) ^= ( 1<<(7) );
      }
      else {
        fprintf (stderr, "Error in parsing line %s", str);
        exit (1);
      }
    }
    else if ( str[0] == 'c') {
      if ( str[1] == '0' ) {
        gmp_sscanf (str, "c0: %Zd\n", f[0]);
        (flag) ^= ( 1<<(0) );
      }
      else if ( str[1] == '1' ) {
        gmp_sscanf (str, "c1: %Zd\n", f[1]);
        (flag) ^= ( 1<<(1) );
      }
      else if ( str[1] == '2' ) {
        gmp_sscanf (str, "c2: %Zd\n", f[2]);
        (flag) ^= ( 1<<(2) );
      }
      else if ( str[1] == '3' ) {
        gmp_sscanf (str, "c3: %Zd\n", f[3]);
        (flag) ^= ( 1<<(3) );
      }
      else if ( str[1] == '4' ) {
        gmp_sscanf (str, "c4: %Zd\n", f[4]);
        (flag) ^= ( 1<<(4) );
      }
      else if ( str[1] == '5' ) {
        gmp_sscanf (str, "c5: %Zd\n", f[5]);
        (flag) ^= ( 1<<(5) );
      }
      else if ( str[1] == '6' ) {
        gmp_sscanf (str, "c6: %Zd\n", f[6]);
        (flag) ^= ( 1<<(6) );
      }
      else
      {
        fprintf (stderr, "Error in parsing line %s", str);
        exit (1);
      }
    }
    else
      continue;

    /* consider raw polynomial only */
    if ( (flag == 511UL || flag == 447UL) &&  (skip == 0) ) {

      /* M = -Y0/Y1 mod N */
      mpz_invert (M, g[1], N);
      mpz_neg (M, M);
      mpz_mul (M, M, g[0]);
      mpz_mod (M, M, N);
      /* check M ? a root of the algebraic polynomial mod N */
      mpz_set (t, f[deg]);
      for (i = deg - 1; i >= 0; i --) {
        mpz_mul (t, t, M);
        mpz_add (t, t, f[i]);
        mpz_mod (t, t, N);
      }
         
      if (!fake) {
        if (mpz_cmp_ui (t, 0) != 0) {
          fprintf (stderr, "This polynomials have no common root mod N\n");
          polyprint (f, g, deg, N);
          exit (1);
        }
      }

      /* output raw polynomials */
      nroots = numberOfRealRoots (f, deg, 0, 0, NULL);
      skew = L2_skewness (F, SKEWNESS_DEFAULT_PREC);
      logmu = L2_lognorm (F, skew);
      alpha = get_alpha (F, ALPHA_BOUND);
      fprintf (stdout, "\n# Raw polynomial (#%6d):", count + 1);
      polyprint (f, g, deg, N);
      printf ("# lognorm %1.2f, alpha %1.2f, E %1.2f, %u rroots, skew: %f\n",
              logmu, alpha, logmu + alpha, nroots, skew);

      /* optimize */
      if (translate)
        optimize_aux (F, g, 0, 0);
      else if (use_lll == 0)
        optimize (F, g, 0, 1);
      else
        optimize_lll (F, g, 0);

      /* output size-optimized polynomials */
      nroots = numberOfRealRoots (f, deg, 0, 0, NULL);
      skew = L2_skewness (F, SKEWNESS_DEFAULT_PREC);
      logmu = L2_lognorm (F, skew);
      alpha = get_alpha (F, ALPHA_BOUND);
      fprintf (stdout, "# Optimized polynomial (#%10d): ", count + 1);
      polyprint (f, g, deg, N);
      printf ("# lognorm %1.2f, alpha %1.2f, E %1.2f, %u rroots, skew: %.2f\n",
              logmu, alpha, logmu + alpha, nroots, skew);

      ave_logmu += logmu;
      min_logmu = (logmu < min_logmu) ? logmu : min_logmu;
      max_logmu = (logmu > max_logmu) ? logmu : max_logmu;
      ave_alpha += alpha;
      count += 1;
      flag = 0UL;
      skip = SKIP_YES;
    }

    /* skip optimized (may be by some old opt method) polynomials,
			 note, we assume "raw" and "optimized" polys appears
			 in pair and sequential. So it is very fragile if the input file
			 is broken for some reason (for instance, order of poly is not
       sequencial due to some threads problem.) */
    if ( (flag == 511UL || flag == 447UL) && (skip == 1) ) {
      flag = 0UL;
      skip = 0;
    }
  }

  fprintf(stdout, "\n# total num. of polys: %u\n", count);
  fprintf(stdout, "# ave. l2norm: %3.3f\n", ave_logmu / count);
  fprintf(stdout, "# min. l2norm: %3.3f\n", min_logmu);
  fprintf(stdout, "# max. l2norm: %3.3f\n", max_logmu);
  fprintf(stdout, "# ave. alpha: %3.3f\n", ave_alpha / count);

  mpz_clear (g[0]);
  mpz_clear (g[1]);
  mpz_clear (t);
  mpz_clear (M);
  mpz_poly_clear (F);
  return 0;
}


/*
   print usage and exit 
*/
static void
usage (void) {
  fprintf (stdout, "Usage: polyopt [--fake] [--translate] [--lll] -f POLYFILE -d DEGREE -n NUMBER\n");
  exit(1);
}


/*
   main function 
*/
int main (int argc, char **argv)
{
  int degree = 0;
  FILE *file = NULL;
  const char *filename = NULL;
  mpz_t N;
  mpz_init (N);

  /* read params */
  if (argc == 1) usage();
  param_list pl;
  param_list_init (pl);
  param_list_configure_switch (pl, "--fake", &fake);
  param_list_configure_switch (pl, "--translate", &translate);
  param_list_configure_switch (pl, "--lll", &use_lll);

  argv++, argc--;
  for ( ; argc; ) {
    if (param_list_update_cmdline (pl, &argc, &argv)) continue;
    fprintf (stderr, "Unhandled parameter %s\n", argv[0]);
    usage ();
  }

  /* parse n */
  int have_n = param_list_parse_mpz(pl, "n", N);
  if (!have_n) {
    fprintf(stderr, "No N defined ; sorry.\n");
    exit(1);
  }
  if (mpz_cmp_ui (N, 0) <= 0) usage();

  /* parse degree */
  param_list_parse_int (pl, "d", &degree);
  if (degree <= 0) usage();
  
  /* parse poly filename */
  filename = param_list_lookup_string (pl, "f");

  /* print out commands */
  param_list_print_command_line (stdout, pl);

  if (param_list_warn_unused(pl)) usage();

  /* read */
  file = fopen(filename, "r");
  if (file == NULL) {
    fprintf(stderr, "# Error in reading file\n");
    mpz_clear (N);
    exit (1);
  }

  /* optimize the raw polys in the file */
  opt_file (file, degree, N);

  fclose (file);
  mpz_clear (N);
  param_list_clear (pl);
  return 0;
}
