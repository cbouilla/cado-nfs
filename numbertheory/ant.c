#include <stdio.h>
#include <string.h>
#include <gmp.h>
#include <stdint.h>
#include <sys/resource.h>	/* for getrusage */
#include "macros.h"

/*{{{ typedefs */

// mpz_t : multiple precision Z-elements ; same for mpq_t
// a matric on mpz_t type
struct mpz_mat_s {
    mpz_t *x;
    unsigned int m,n;
};
typedef struct mpz_mat_s mpz_mat[1];
typedef struct mpz_mat_s * mpz_mat_ptr;
typedef const struct mpz_mat_s * mpz_mat_srcptr;

// same on mpq_t type
struct mpq_mat_s {
    mpq_t *x;
    unsigned int m,n;
};
typedef struct mpq_mat_s mpq_mat[1];
typedef struct mpq_mat_s * mpq_mat_ptr;
typedef const struct mpq_mat_s * mpq_mat_srcptr;
/*}}}*/
/*{{{ entry access*/
mpz_ptr mpz_mat_entry(mpz_mat_ptr M, unsigned int i, unsigned int j)
{
    return M->x[i * M->n + j];
}

mpz_srcptr mpz_mat_entry_const(mpz_mat_srcptr M, unsigned int i, unsigned int j)
{
    return M->x[i * M->n + j];
}

mpq_ptr mpq_mat_entry(mpq_mat_ptr M, unsigned int i, unsigned int j)
{
    return M->x[i * M->n + j];
}

mpq_srcptr mpq_mat_entry_const(mpq_mat_srcptr M, unsigned int i, unsigned int j)
{
    return M->x[i * M->n + j];
}
/*}}}*/
/*{{{ init/clear/realloc*/
void mpz_mat_init(mpz_mat_ptr M, unsigned int m, unsigned int n)
{
    M->x = (mpz_t*) ((m*n) ? malloc(m * n * sizeof(mpz_t)) : NULL);

    M->m = m;
    M->n = n;
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpz_init(mpz_mat_entry(M, i, j));
}

void mpz_mat_clear(mpz_mat_ptr M)
{
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpz_clear(mpz_mat_entry(M, i, j));
    free(M->x);
}

void mpz_mat_realloc(mpz_mat_ptr M, unsigned int m, unsigned int n)
{
    if (M->m == m && M->n == n) return;
    mpz_mat_clear(M);
    mpz_mat_init(M, m, n);
}

void mpq_mat_init(mpq_mat_ptr M, unsigned int m, unsigned int n)
{
    M->x = (mpq_t*) ((m*n) ? malloc(m * n * sizeof(mpq_t)) : NULL);
    M->m = m;
    M->n = n;
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpq_init(mpq_mat_entry(M, i, j));
}

void mpq_mat_clear(mpq_mat_ptr M)
{
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpq_clear(mpq_mat_entry(M, i, j));
    free(M->x);
}

void mpq_mat_realloc(mpq_mat_ptr M, unsigned int m, unsigned int n)
{
    if (M->m == m && M->n == n) return;
    mpq_mat_clear(M);
    mpq_mat_init(M, m, n);
}

/*}}}*/
/*{{{ operations on submatrices, and swaps*/
void mpz_mat_submat_swap(
        mpz_mat_ptr A0, unsigned int i0, unsigned int j0,
        mpz_mat_ptr A1, unsigned int i1, unsigned int j1,
        unsigned int dm, unsigned int dn)
{
    ASSERT_ALWAYS(i0 + dm <= A0->m);
    ASSERT_ALWAYS(i1 + dm <= A1->m);
    ASSERT_ALWAYS(j0 + dn <= A0->n);
    ASSERT_ALWAYS(j1 + dn <= A1->n);
    for(unsigned int i = 0 ; i < dm ; i++) {
        for(unsigned int j = 0 ; j < dn ; j++) {
            mpz_swap(
                    mpz_mat_entry(A0, i0 + i, j0 + j),
                    mpz_mat_entry(A1, i1 + i, j1 + j));
        }
    }
}

void mpz_mat_swap(mpz_mat_ptr A, mpz_mat_ptr B)
{
    mpz_mat C;
    memcpy(C, A, sizeof(mpz_mat));
    memcpy(A, B, sizeof(mpz_mat));
    memcpy(B, C, sizeof(mpz_mat));
}

void mpq_mat_submat_swap(
        mpq_mat_ptr A0, unsigned int i0, unsigned int j0,
        mpq_mat_ptr A1, unsigned int i1, unsigned int j1,
        unsigned int dm, unsigned int dn)
{
    ASSERT_ALWAYS(i0 + dm <= A0->m);
    ASSERT_ALWAYS(i1 + dm <= A1->m);
    ASSERT_ALWAYS(j0 + dn <= A0->n);
    ASSERT_ALWAYS(j1 + dn <= A1->n);
    for(unsigned int i = 0 ; i < dm ; i++) {
        for(unsigned int j = 0 ; j < dn ; j++) {
            mpq_swap(
                    mpq_mat_entry(A0, i0 + i, j0 + j),
                    mpq_mat_entry(A1, i1 + i, j1 + j));
        }
    }
}

void mpq_mat_swap(mpq_mat_ptr A, mpq_mat_ptr B)
{
    mpq_mat C;
    memcpy(C, A, sizeof(mpq_mat));
    memcpy(A, B, sizeof(mpq_mat));
    memcpy(B, C, sizeof(mpq_mat));
}

/*}}}*/
/*{{{ set/set_ui*/
void mpz_mat_set(mpz_mat_ptr dst, mpz_mat_srcptr src)
{
    mpz_mat_realloc(dst, src->m, src->n);
    for(unsigned int i = 0 ; i < src->m ; i++)
        for(unsigned int j = 0 ; j < src->n ; j++)
            mpz_set(mpz_mat_entry(dst, i, j), mpz_mat_entry_const(src, i, j));
}

void mpz_mat_set_ui(mpz_mat_ptr M, unsigned int a)
{
    if (a) {
        ASSERT_ALWAYS(M->m == M->n);
    }
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpz_set_ui(mpz_mat_entry(M, i, j), i == j ? a : 0);
}

void mpq_mat_set_ui(mpq_mat_ptr M, unsigned int a)
{
    if (a) {
        ASSERT_ALWAYS(M->m == M->n);
    }
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpq_set_ui(mpq_mat_entry(M, i, j), i == j ? a : 0, 1);
}

void mpq_mat_set(mpq_mat_ptr dst, mpq_mat_srcptr src)
{
    mpq_mat_realloc(dst, src->m, src->n);
    for(unsigned int i = 0 ; i < src->m ; i++)
        for(unsigned int j = 0 ; j < src->n ; j++)
            mpq_set(mpq_mat_entry(dst, i, j), mpq_mat_entry_const(src, i, j));
}

void mpz_mat_urandomm(mpz_mat_ptr M, gmp_randstate_t state, mpz_srcptr p)
{
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpz_urandomm(mpz_mat_entry(M, i, j), state, p);
}

void mpq_mat_urandomm(mpq_mat_ptr M, gmp_randstate_t state, mpz_srcptr p)
{
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++) {
            mpz_urandomm(mpq_numref(mpq_mat_entry(M, i, j)), state, p);
            mpz_set_ui(mpq_denref(mpq_mat_entry(M, i, j)), 1);
        }
}



/*}}}*/
/*{{{ miscellaneous */
void mpq_mat_numden(mpz_mat_ptr num, mpz_ptr den, mpq_mat_srcptr M)
{
    mpz_set_ui(den, 1);
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++)
            mpz_lcm(den, den, mpq_denref(mpq_mat_entry_const(M, i, j)));
    mpz_mat_realloc(num, M->m, M->n);
    for(unsigned int i = 0 ; i < M->m ; i++)
        for(unsigned int j = 0 ; j < M->n ; j++) {
            mpz_ptr dst = mpz_mat_entry(num, i, j);
            mpq_srcptr src = mpq_mat_entry_const(M, i, j);
            mpz_divexact(dst, den, mpq_denref(src));
            mpz_mul(dst, dst, mpq_numref(src));
        }
}
/*}}}*/
/* {{{ row-level operations */
void mpz_mat_swaprows(mpz_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    if (i0 == i1) return;
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_swap(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i1, j));
    }
}

void mpq_mat_swaprows(mpq_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    if (i0 == i1) return;
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpq_swap(mpq_mat_entry(M, i0, j), mpq_mat_entry(M, i1, j));
    }
}

/* apply a circular shift on rows [i0...i0+k[ */
void mpz_mat_rotatedownrows(mpz_mat_ptr M, unsigned int i0, unsigned int k)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i0 + k <= M->m);
    if (k <= 1) return;
    for(unsigned int j = 0 ; j < M->n ; j++) {
        for(unsigned int s = k - 1 ; s-- ; ) {
            mpz_swap(mpz_mat_entry(M, i0 + s, j), mpz_mat_entry(M, i0 + s + 1, j));
        }
    }
}

void mpq_mat_rotatedownrows(mpq_mat_ptr M, unsigned int i0, unsigned int k)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i0 + k <= M->m);
    if (k <= 1) return;
    for(unsigned int j = 0 ; j < M->n ; j++) {
        for(unsigned int s = k - 1 ; s-- ; ) {
            mpq_swap(mpq_mat_entry(M, i0 + s, j), mpq_mat_entry(M, i0 + s + 1, j));
        }
    }
}

/* put row perm[k] in row k */
void mpz_mat_permuterows(mpz_mat_ptr M, unsigned int * perm)
{
    mpz_mat Mx;
    mpz_mat_init(Mx, M->m, M->n);
    for(unsigned int i = 0 ; i < M->m ; i++) {
        for(unsigned int j = 0 ; j < M->n ; j++) {
            mpz_swap(mpz_mat_entry(Mx, i, j),
                    mpz_mat_entry(M, perm[i], j));
        }
    }
    mpz_mat_swap(M, Mx);
    mpz_mat_clear(Mx);
}

void mpq_mat_permuterows(mpq_mat_ptr M, unsigned int * perm)
{
    mpq_mat Mx;
    mpq_mat_init(Mx, M->m, M->n);
    for(unsigned int i = 0 ; i < M->m ; i++) {
        for(unsigned int j = 0 ; j < M->n ; j++) {
            mpq_swap(mpq_mat_entry(Mx, i, j),
                    mpq_mat_entry(M, perm[i], j));
        }
    }
    mpq_mat_swap(M, Mx);
    mpq_mat_clear(Mx);
}


/* add lambda times row i1 to row i0 */
void mpz_mat_addmulrow(mpz_mat_ptr M, unsigned int i0, unsigned int i1, mpz_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_addmul(mpz_mat_entry(M, i0, j),
                lambda,
                mpz_mat_entry(M, i1, j));
    }
}

void mpz_mat_addmulrow_mod(mpz_mat_ptr M, unsigned int i0, unsigned int i1, mpz_srcptr lambda, mpz_srcptr p)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_addmul(mpz_mat_entry(M, i0, j),
                lambda,
                mpz_mat_entry(M, i1, j));
        mpz_mod(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i0, j), p);
    }
}

/* add lambda times row i1 to row i0 */
void mpq_mat_addmulrow(mpq_mat_ptr M, unsigned int i0, unsigned int i1, mpq_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    mpq_t tmp;
    mpq_init(tmp);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        /* we have no mpq_addmul, unfortunately */
        mpq_mul(tmp,
                lambda,
                mpq_mat_entry(M, i1, j));
        mpq_add(mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i0, j),
                tmp);
    }
    mpq_clear(tmp);
}

/* subtract lambda times row i1 to row i0 */
void mpz_mat_submulrow(mpz_mat_ptr M, unsigned int i0, unsigned int i1, mpz_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_submul(mpz_mat_entry(M, i0, j),
                lambda,
                mpz_mat_entry(M, i1, j));
    }
}

void mpz_mat_submulrow_mod(mpz_mat_ptr M, unsigned int i0, unsigned int i1, mpz_srcptr lambda, mpz_srcptr p)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_submul(mpz_mat_entry(M, i0, j),
                lambda,
                mpz_mat_entry(M, i1, j));
        mpz_mod(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i0, j), p);
    }
}

/* subtract lambda times row i1 to row i0 */
void mpq_mat_submulrow(mpq_mat_ptr M, unsigned int i0, unsigned int i1, mpq_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    mpq_t tmp;
    mpq_init(tmp);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        /* we have no mpq_submul, unfortunately */
        mpq_mul(tmp,
                lambda,
                mpq_mat_entry(M, i1, j));
        mpq_sub(mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i0, j),
                tmp);
    }
    mpq_clear(tmp);
}


/* add row i1 to row i0 */
void mpz_mat_addrow(mpz_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_add(mpz_mat_entry(M, i0, j),
                mpz_mat_entry(M, i0, j),
                mpz_mat_entry(M, i1, j));
    }
}

/* add row i1 to row i0 */
void mpq_mat_addrow(mpq_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpq_add(mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i1, j));
    }
}


/* subtract row i1 to row i0 */
void mpz_mat_subrow(mpz_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_sub(mpz_mat_entry(M, i0, j),
                mpz_mat_entry(M, i0, j),
                mpz_mat_entry(M, i1, j));
    }
}

/* subtract row i1 to row i0 */
void mpq_mat_subrow(mpq_mat_ptr M, unsigned int i0, unsigned int i1)
{
    ASSERT_ALWAYS(i0 < M->m);
    ASSERT_ALWAYS(i1 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpq_sub(mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i0, j),
                mpq_mat_entry(M, i1, j));
    }
}

/* multiply row i0 by lambda */
void mpz_mat_mulrow(mpz_mat_ptr M, unsigned int i0, mpz_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_mul(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i0, j), lambda);
    }
}

void mpz_mat_mulrow_mod(mpz_mat_ptr M, unsigned int i0, mpz_srcptr lambda, mpz_srcptr p)
{
    ASSERT_ALWAYS(i0 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpz_mul(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i0, j), lambda);
        mpz_mod(mpz_mat_entry(M, i0, j), mpz_mat_entry(M, i0, j), p);
    }
}

/* multiply row i0 by lambda */
void mpq_mat_mulrow(mpq_mat_ptr M, unsigned int i0, mpq_srcptr lambda)
{
    ASSERT_ALWAYS(i0 < M->m);
    for(unsigned int j = 0 ; j < M->n ; j++) {
        mpq_mul(mpq_mat_entry(M, i0, j), mpq_mat_entry(M, i0, j), lambda);
    }
}

/* this computes an additive combination of n rows into row [didx] of the
 * initial matrix. We require that this destination row be cleared
 * initially.
 */
void mpz_mat_combinerows(mpz_mat_ptr M, unsigned int didx, unsigned int sidx,
        mpz_srcptr * lambda, unsigned int n)
{
    for(unsigned int j = 0 ; j < M->n ; j++) {
        ASSERT_ALWAYS(mpz_cmp_ui(mpz_mat_entry(M, didx, j), 0) == 0);
    }
    for(unsigned int i = 0 ; i < n ; i++) {
        mpz_mat_addmulrow(M, didx, sidx + i, lambda[i]);
    }
}

/* }}} */
/*{{{ I/O*/
void mpz_mat_fprint(FILE * stream, mpz_mat_srcptr M)
{
    for(unsigned int i = 0 ; i < M->m ; i++) {
        for(unsigned int j = 0 ; j < M->n ; j++) {
            gmp_fprintf(stream, " %Zd", mpz_mat_entry_const(M, i, j));
        }
        fprintf(stream, "\n");
    }
}

void mpq_mat_fprint(FILE * stream, mpq_mat_srcptr M)
{
    for(unsigned int i = 0 ; i < M->m ; i++) {
        for(unsigned int j = 0 ; j < M->n ; j++) {
            gmp_fprintf(stream, " %Qd", mpq_mat_entry_const(M, i, j));
        }
        fprintf(stream, "\n");
    }
}
/*}}}*/
/*{{{ multiplication */
void mpz_mat_multiply(mpz_mat_ptr C, mpz_mat_srcptr A, mpz_mat_srcptr B)
{
    ASSERT_ALWAYS(A->n == B->m);
    mpz_mat_realloc(C, A->m, B->n);
    for(unsigned int i = 0 ; i < A->m ; i++) {
        for(unsigned int j = 0 ; j < B->n ; j++) {
            mpz_set_ui(mpz_mat_entry(C, i, j), 0);
            for(unsigned int k = 0 ; k < B->m ; k++) {
                mpz_addmul(mpz_mat_entry(C, i, j),
                        mpz_mat_entry_const(A, i, k),
                        mpz_mat_entry_const(B, k, j));
            }
        }
    }
}

void mpq_mat_multiply(mpq_mat_ptr C, mpq_mat_srcptr A, mpq_mat_srcptr B)
{
    mpq_t z;
    mpq_init(z);
    ASSERT_ALWAYS(A->n == B->m);
    mpq_mat_realloc(C, A->m, B->n);
    for(unsigned int i = 0 ; i < A->m ; i++) {
        for(unsigned int j = 0 ; j < B->n ; j++) {
            mpq_set_ui(mpq_mat_entry(C, i, j), 0, 1);
            for(unsigned int k = 0 ; k < B->m ; k++) {
                mpq_add(z,
                        mpq_mat_entry_const(A, i, k),
                        mpq_mat_entry_const(B, k, j));
                mpq_add(mpq_mat_entry(C, i, j),
                        mpq_mat_entry(C, i, j),
                        z);
            }
        }
    }
    mpq_clear(z);
}
/*}}}*/
/* {{{ gaussian reduction over the rationals
 * this is a backend for row gaussian reduction. T receives the
 * transformation matrix. M is modified in place.
 * this includes reordering of the rows.
 *
 * this works reasonably well over Fp, but for rationals we suffer from
 * coefficient blowup.
 */
void mpq_gauss_backend(mpq_mat_ptr M, mpq_mat_ptr T)
{
    unsigned int m = M->m;
    unsigned int n = M->n;
    mpq_mat_realloc(T, m, m);
    mpq_mat_set_ui(T, 1);
    unsigned int rank = 0;
    mpq_t tmp1, tmp2;
    mpq_init(tmp1);
    mpq_init(tmp2);
    for(unsigned int j = 0 ; j < n ; j++) {
        unsigned int i1;
        for(i1 = rank ; i1 < M->m ; i1++) {
            if (mpq_cmp_ui(mpq_mat_entry(M, i1, j), 0, 1) != 0) break;
        }
        if (i1 == M->m) /* this column is zero */
            continue;
        mpq_mat_swaprows(M, rank, i1);
        mpq_mat_swaprows(T, rank, i1);
        i1 = rank;
        /* canonicalize this row */
        mpq_inv(tmp1, mpq_mat_entry(M, rank, j));
        mpq_mat_mulrow(M, rank, tmp1);
        mpq_mat_mulrow(T, rank, tmp1);
        for(unsigned int i0 = 0 ; i0 < m ; i0++) {
            if (i0 == rank) continue;
            /* we've normalized row rank, so all it takes is to multiply by
             * M[i0, j]
             */
            mpq_set(tmp2, mpq_mat_entry(M, i0, j));
            mpq_mat_submulrow(M, i0, rank, tmp2);
            mpq_mat_submulrow(T, i0, rank, tmp2);
        }
        rank++;
    }
    mpq_clear(tmp1);
    mpq_clear(tmp2);
}
/* }}} */
/* {{{ gaussian reduction over Z/pZ
 *
 * We sort of return something even in the case where p is not prime.
 */
void mpz_gauss_backend_mod(mpz_mat_ptr M, mpz_mat_ptr T, mpz_srcptr p)
{
    unsigned int m = M->m;
    unsigned int n = M->n;
    mpz_mat_realloc(T, m, m);
    mpz_mat_set_ui(T, 1);
    unsigned int rank = 0;
    mpz_t gcd, tmp2, tmp3;
    mpz_init(gcd);
    mpz_init(tmp2);
    mpz_init(tmp3);
    for(unsigned int j = 0 ; j < n ; j++) {
        unsigned int i1 = M->m;
        mpz_set(gcd, p);
        for(unsigned int i = rank ; i < M->m ; i++) {
            mpz_gcd(tmp2, mpz_mat_entry(M, i, j), p);
            if (mpz_cmp(tmp2, gcd) < 0) {
                i1 = i;
                mpz_set(gcd, tmp2);
                if (mpz_cmp_ui(gcd, 1) == 0) break;
            }
        }
        if (i1 == M->m) /* this column is zero */
            continue;
        mpz_mat_swaprows(M, rank, i1);
        mpz_mat_swaprows(T, rank, i1);
        i1 = rank;
        /* canonicalize this row */
        /* gcd is gcd(M[rank, j], p) */
        mpz_divexact(tmp2, mpz_mat_entry(M, rank, j), gcd);
        mpz_divexact(tmp3, p, gcd);
        mpz_invert(tmp2, tmp2, tmp3);
        mpz_mat_mulrow_mod(M, rank, tmp2, p);
        mpz_mat_mulrow_mod(T, rank, tmp2, p);
        ASSERT_ALWAYS(mpz_cmp(mpz_mat_entry(M, rank, j), gcd) == 0);
        for(unsigned int i0 = 0 ; i0 < m ; i0++) {
            if (i0 == rank) continue;
            /* we've normalized row rank, so all it takes is to multiply by
             * M[i0, j]
             */
            mpz_fdiv_q(tmp2, mpz_mat_entry(M, i0, j), gcd);
            /* for i0 > rank, we should have exact quotient */
            mpz_mat_submulrow_mod(M, i0, rank, tmp2, p);
            mpz_mat_submulrow_mod(T, i0, rank, tmp2, p);
            ASSERT_ALWAYS(mpz_cmp_ui(mpz_mat_entry(M, i0, j), 0) == 0);
        }
        rank++;
    }
    mpz_clear(gcd);
    mpz_clear(tmp2);
    mpz_clear(tmp3);
}
/* }}} */
/* {{{ integer heap routines */
/* input: heap[0..n-1[ a heap, and heap[n-1] unused.
 * output: heap[0..n[ a heap, includes x
 */
typedef int (*cmp_t)(void *, unsigned int, unsigned int);

void push_heap(unsigned int * heap, unsigned int x, unsigned int n, cmp_t cmp, void * arg)
{
    unsigned int i = n - 1;
    for(unsigned int i0; i; i = i0) {
        i0 = (i-1) / 2;
        if (cmp(arg, x, heap[i0]) > 0)
            heap[i] = heap[i0];
        else
            break;
    }
    heap[i] = x;
}

/* put heap[0] in x, and keep the heap property on the resulting
 * heap[0..n-1[ ; heap[n-1] is unused on output.
 */
void pop_heap(unsigned int * x, unsigned int * heap, unsigned int n, cmp_t cmp, void * arg)
{
    *x = heap[0];
    unsigned int y = heap[n-1];
    /* make heap[0..n-1[ a heap, now */
    /* couldn't there be various strategies here ? */
    unsigned int i = 0;
    for(unsigned int i1 ; 2*i+2 <  n - 1 ; i = i1) {
        unsigned int left = 2*i + 1;
        unsigned int right = 2*i + 2;
        if (cmp(arg, heap[left], heap[right]) > 0)
            i1 = left;
        else
            i1 = right;
        if (cmp(arg, y, heap[i1]) > 0)
            break;
        heap[i] = heap[i1];
    }
    /* there we know that i has no right child, maybe even no left child.
     */
    if (2*i + 1 < n - 1 && cmp(arg, y, heap[2*i+1]) <= 0) {
        heap[i] = heap[2*i+1];
        i=2*i+1;
    }
    heap[i] = y;
}

/* make heap[0..n[ a heap. */
void make_heap(unsigned int * heap, unsigned int n, cmp_t cmp, void * arg)
{
    for(unsigned int i = 1 ; i <= n ; i++) {
        unsigned int t = heap[i-1];
        push_heap(heap, t, i, cmp, arg);
    }
}

/* must be a heap already */
void sort_heap(unsigned int * heap, unsigned int n, cmp_t cmp, void * arg)
{
    for(unsigned int i = n ; i ; i--) {
        unsigned int t;
        pop_heap(&t, heap, i, cmp, arg);
        heap[i-1] = t;
    }
}

void sort_heap_inverse(unsigned int * heap, unsigned int n, cmp_t cmp, void * arg)
{
    sort_heap(heap, n, cmp, arg);
    for(unsigned int i = 0, j = n-1 ; i < j ; i++, j--) {
        int a = heap[i];
        int b = heap[j];
        heap[i] = b;
        heap[j] = a;
    }
}
/* }}} */
/* {{{ Hermite normal form over the integers
 * The algorithm here is randomly cooked from a stupid idea I had. The
 * transformation matrices produced seem to be reasonably good, somewhat
 * better than what magma gives (for overdetermined matrices, of course
 * -- otherwise they're unique). However we do so in an embarrassingly
 * larger amount of time (more than 1000* for 100x100 matrices).
 * Fortunately, our intent is not to run this code on anything larger
 * than 30*30, in which case it's sufficient.
 */
/*{{{ hnf helper algorithm (for column matrices) */
struct mpz_hnf_helper_heap_aux {
    double * dd;
    mpz_mat_ptr A;
};

int mpz_hnf_helper_heap_aux_cmp(struct mpz_hnf_helper_heap_aux * data, unsigned int a, unsigned int b)
{
    /* The heap will have the largest elements on top according to this
     * measure. Thus the comparison on the ->a field must be as per
     * mpz_cmp
     */
    mpz_srcptr xa = mpz_mat_entry_const(data->A, a, 0);
    mpz_srcptr xb = mpz_mat_entry_const(data->A, b, 0);

    assert(mpz_sgn(xa) >= 0);
    assert(mpz_sgn(xb) >= 0);
    int r = mpz_cmp(xa, xb);
    if (r) return r;
    /* among combinations giving the same result, we want the "best" to
     * be the one with *smallest* norm. So instead of the sign of da-db,
     * we want the sign of db-da, here.
     */
    double da = data->dd[a];
    double db = data->dd[b];
    return (db > da) - (da > db);
}

/* this is quite the same as computing the hnf on a column matrix, with
 * the added functionality that entries in a[0..n0[ are reduced with
 * respect to the entries in a[n0..n[. This is equivalent to saying that
 * we apply a transformation matrix which is:
 *       I_n0 R
 * T ==  0    S
 * where S is unimodular. The resulting vector T*A has entries [n0..n[
 * equal to (g,0,...,0), while entries [0..n0[ are reduced modulo g.
 */
void mpz_hnf_helper(mpz_mat_ptr T, mpz_mat_ptr dT, mpz_ptr * a, unsigned int n0, unsigned int n)
{
    mpz_mat_realloc(dT, n, n);
    mpz_mat_set_ui(dT, 1);

    if (n == n0) return;

    mpz_t q, r2;
    mpz_mat A;
    mpz_mat_init(A, n, 1);
    mpz_init(q);
    mpz_init(r2);
    unsigned int * heap = malloc(n * sizeof(unsigned int));
    struct mpz_hnf_helper_heap_aux cmpdata[1];
    cmpdata->A = A;
    cmpdata->dd = malloc(n * sizeof(double));
    cmp_t cmp = (cmp_t) &mpz_hnf_helper_heap_aux_cmp;
    for(unsigned int i = 0 ; i < n ; i++) {
        mpz_ptr Ai = mpz_mat_entry(A, i, 0);
        mpz_set(Ai, a[i]);
        cmpdata->dd[i] = 0;
        for(unsigned int j = 0 ; j < n ; j++) {
            double d = mpz_get_d(mpz_mat_entry(T, i, j));
            cmpdata->dd[i] += d * d;
        }
        heap[i] = i;
        if (i < n0)
            continue;
        if (mpz_sgn(Ai) == -1) {
            mpz_neg(Ai, Ai);
            mpz_set_si(mpz_mat_entry(dT, i, i), -1);
            for(unsigned int j = 0 ; j < n ; j++) {
                mpz_neg(mpz_mat_entry(T, i, j), mpz_mat_entry(T, i, j));
            }
        }
    }
    make_heap(heap + n0, n - n0, cmp, cmpdata);
    unsigned int head;
    for( ; ; ) {
        /* extract the head */
        pop_heap(&head, heap + n0, n - n0, cmp, cmpdata);
        mpz_ptr r0 = mpz_mat_entry(A, head, 0);
        if (mpz_cmp_ui(r0, 0) == 0) {
            /* we're done ! */
            push_heap(heap + n0, head, n - n0, cmp, cmpdata);
            break;
        }
        /* reduce A[0..n0[ wrt r0 == A[head] */
        for(unsigned int i = 0 ; i < n0 ; i++) {
            mpz_fdiv_qr(q, r2, mpz_mat_entry(A, i, 0), r0);
            mpz_swap(r2, mpz_mat_entry(A, i, 0));
            mpz_mat_submulrow(T, i, head, q);
            mpz_mat_submulrow(dT, i, head, q);
        }
        if (n0 == n - 1) {
            /* we're actually computing the gcd of one single integer.
             * That makes no sense of course.
             */
            push_heap(heap + n0, head, n - n0, cmp, cmpdata);
            break;
        }
        mpz_ptr r1 = mpz_mat_entry(A, heap[n0], 0);
        if (mpz_cmp_ui(r1, 0) == 0) {
            /* we're done ! */
            push_heap(heap + n0, head, n - n0, cmp, cmpdata);
            break;
        }
        mpz_fdiv_qr(q, r2, r0, r1);
        mpz_swap(r0, r2);
        mpz_mat_submulrow(T, head, heap[n0], q);
        mpz_mat_submulrow(dT, head, heap[n0], q);
        /* adjust the norm of the row in T */
        cmpdata->dd[head] = 0;
        for(unsigned int j = 0 ; j < n ; j++) {
            double d = mpz_get_d(mpz_mat_entry(T, head, j));
            cmpdata->dd[head] += d * d;
        }
        push_heap(heap + n0, head, n - n0, cmp, cmpdata);
    }
    sort_heap_inverse(heap + n0, n - n0, cmp, cmpdata);
    mpz_mat_permuterows(T, heap);
    mpz_mat_permuterows(dT, heap);
    mpz_mat_permuterows(A, heap);
    for(unsigned int i = 0 ; i < n ; i++) {
        mpz_ptr Ai = mpz_mat_entry(A, i, 0);
        mpz_swap(a[i], Ai);
    }
    free(cmpdata->dd);
    mpz_mat_clear(A);
    mpz_clear(q);
    mpz_clear(r2);
    free(heap);
}
/* {{{ this computes the row hnf on a column C.
 * The result is always of the form C'=(gcd(C),0,0,...,0). The transform
 * matrix T such that C'=T*C is most interesting.  a is modified in
 * place.
 */
void mpz_gcd_many(mpz_mat_ptr dT, mpz_ptr * a, unsigned int n)
{
    mpz_mat T;
    mpz_mat_init(T, n, n);
    mpz_mat_set_ui(T, 1);
    mpz_hnf_helper(T, dT, a, 0, n);
    mpz_mat_clear(T);
}
/*}}}*/
/*}}}*/
void mpz_hnf_backend(mpz_mat_ptr M, mpz_mat_ptr T)
{
    /* Warning ; for this to work, we need to have extra blank rows in M
     * and T, so we require on input M and T to be overallocated (twice
     * as many rows).
     */
    unsigned int m = M->m;
    unsigned int n = M->n;
    mpz_mat_realloc(T, m, m);
    mpz_mat_set_ui(T, 1);
    unsigned int rank = 0;
    mpz_t quo;
    mpz_init(quo);
    mpz_ptr * colm = malloc(m * sizeof(mpz_t));
    mpz_mat dT, Mx, My;
    mpz_mat_init(dT, 0, 0);
    mpz_mat_init(Mx, 0, 0);
    mpz_mat_init(My, 0, 0);
    for(unsigned int j = 0 ; j < n && rank < m; j++) {
        for(unsigned int i = 0 ; i < m ; i++) {
            colm[i] = mpz_mat_entry(M, i, j);
        }
        mpz_hnf_helper(T, dT, colm, rank, m);

        /* apply dT to the submatrix of size m * (n - 1 - j) of M */
        mpz_mat_realloc(Mx, m, n - 1 - j);
        mpz_mat_submat_swap(Mx, 0, 0, M, 0, j + 1, m, n - 1 - j);
        mpz_mat_multiply(My, dT, Mx);
        mpz_mat_submat_swap(M, 0, j + 1, My, 0, 0, m, n - 1 - j);

        mpz_srcptr gcd = colm[rank];
        if (mpz_cmp_ui(gcd, 0) == 0) /* this column is zero -- nothing to do */
            continue;
        rank++;
    }
    free(colm);
    mpz_clear(quo);
    mpz_mat_clear(dT);
    mpz_mat_clear(Mx);
    mpz_mat_clear(My);
}
/* }}} */

/*{{{ timer*/
double
seconds (void)
{
    struct rusage res[1];
    getrusage(RUSAGE_SELF, res);
    uint64_t r;
    r = (uint64_t) res->ru_utime.tv_sec;
    r *= (uint64_t) 1000000UL;
    r += (uint64_t) res->ru_utime.tv_usec;
    return r * 1.0e-6;
}
/*}}}*/


// Prints the polynom
void print_polynom(mpz_t* f, int degree){
	int i;
	for(i = degree ; i >= 0 ; i--){
		if(mpz_get_ui(f[i]) != 0){
			gmp_printf("%Zd",f[i]);
			if(i > 0){
				printf("*x^%d + ",i);
			}
		}
	}
	printf("\n");
}

// We assume that M is a square Matrix and that its size is not 0
mpz_ptr mpz_mat_trace(mpz_mat_ptr M){
	mpz_t* p = malloc(sizeof(mpz_t));
	mpz_init(*p);
	unsigned int i;

	for(i = 0 ; i < (M->n) ; i++){
		mpz_add(*p,*p,mpz_mat_entry(M,i,i));
	}
	return *p;
}

int main(int argc, char * argv[])/*{{{*/
{
	/*
    unsigned int m = 8;
    unsigned int n = 5;
    if (argc == 3) {
        m = strtoul(argv[1], NULL, 0);
        n = strtoul(argv[2], NULL, 0);
    }
    gmp_randstate_t state;
    gmp_randinit_default(state);

    mpq_mat M;
    mpq_mat T;
    mpz_mat Mz;
    mpz_mat Tz;
    mpz_t p;

    mpq_mat_init(M, m, n);
    mpq_mat_init(T, m, m);

    mpz_mat_init(Mz, m, n);
    mpz_mat_init(Tz, m, m);

    mpz_init(p);

    mpz_set_ui(p, 19);

    if (0) {
	printf("\n\nCas 0.1\n\n");
        mpq_mat_urandomm(M, state, p);
        mpq_mat_fprint(stdout, M);
        printf("\n");
        mpq_gauss_backend(M, T);
        mpq_mat_fprint(stdout, M);
        printf("\n");
        mpq_mat_fprint(stdout, T);
        printf("\n");
    }

    if (0) {
	printf("\n\nCas 0.2\n\n");
        mpz_mat_urandomm(Mz, state, p);
        mpz_mat_fprint(stdout, Mz);
        printf("\n");
        mpz_gauss_backend_mod(Mz, Tz, p);
        mpz_mat_fprint(stdout, Mz);
        printf("\n");
        mpz_mat_fprint(stdout, Tz);
        printf("\n");
    }

    if (1) {
	printf("\n\nCas 1\n\n");
        mpz_mat_realloc(Mz, m, n);
        mpz_mat_urandomm(Mz, state, p);
        mpz_mat_fprint(stdout, Mz); printf("\n");
        double t = seconds();
        mpz_hnf_backend(Mz, Tz);
        t = seconds()-t;
        mpz_mat_fprint(stdout, Mz); printf("\n");
        mpz_mat_fprint(stdout, Tz); printf("\n");

        printf("%1.4f\n", t);
    }

    mpz_clear(p);
    mpq_mat_clear(M);
    mpq_mat_clear(T);
    mpz_mat_clear(Mz);
    mpz_mat_clear(Tz);
    gmp_randclear(state);
	*/


	// Here starts my personal work
	// We assume that the polynom was given in a command line, by giving its coefficient, in reversed order (first the constant coefficient, etc. And the head coefficient in the end);

	if(argc > 1){

		// Initialisation
		unsigned int degree = argc-2;
		mpz_t f[degree+1];
		mpz_mat mul_alpha, id;
		mpz_mat M, N;
		mpz_t p;
		mpz_t minus;

		// Storing the coefficients obtained in the command line in the polynom
		unsigned int i,j, k;
		for(i = 0 ; i <= degree ; i++){
			mpz_init(f[i]);
			mpz_set_str(f[i],argv[i+1],10);
		}
		printf("\n\n");
		print_polynom(f,degree);

		//Initialising each matrix
		mpz_mat_init(mul_alpha,degree,degree);
		mpz_mat_init(id,degree,degree);
		mpz_mat_init(M,degree,degree);
		mpz_mat_init(N,degree,degree);
		mpz_init(p);
		mpz_init(minus);
		mpz_mat_set_ui(id,1);
		mpz_set_si(minus,-1);
		

		// Filling the coefficients in mul_alpha, the companion matrix of f
		for(i = 0 ; i < degree ; i++){
			// Setting the left part of the companion matrix
			for(j = 0 ; j < degree-1 ; j++){
				if(i == j+1){ mpz_set_ui(mpz_mat_entry(mul_alpha,i,j),1); }
				else{ mpz_set_ui(mpz_mat_entry(mul_alpha,i,j),0); }
			}

			// Computing the coefficients for the column on the right
			mpz_set(p,f[i]);
			mpz_mul(p,p,minus);
			for(k = 1 ; k < degree-1-j ; k++){
				mpz_mul(p,p,f[degree]);
			}
			mpz_set(mpz_mat_entry(mul_alpha,i,j),p);
		}


		
	}
}
/*}}}*/
