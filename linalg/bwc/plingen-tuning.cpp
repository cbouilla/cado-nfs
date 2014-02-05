#include "cado.h"

#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include "portability.h"
#include "macros.h"
#include "utils.h"
#include "abase.h"
#include "lingen-polymat.h"
#include "lingen-matpoly.h"
#include "lingen-bigpolymat.h"
#ifdef  HAVE_MPIR
#include "lingen-matpoly-ft.h"
#endif
#include "plingen.h"
#include "plingen-tuning.h"

#include <vector>
#include <utility>
#include <map>
#include <string>
#include <ostream>
#include <iostream>
#include <sstream>


using namespace std;

/* {{{ cutoff_array. Should be merged with polymat_cutoff_info, maybe */
/* This is just an array, but compressed as a cut-off list should be */
struct cutoff_array {
    vector<pair<unsigned int, unsigned int> > x;
    cutoff_array(pair<unsigned int, unsigned int> base = make_pair(1,0)) {
        x.push_back(base);
    }
    int push(unsigned int n, unsigned int winner) {
        if (winner == x.back().second) return n - x.back().first;
        x.push_back(make_pair(n, winner));
        return 0;
    }
    /* XXX this is really horrible and must be removed */
    void export_to_cutoff_info(polymat_cutoff_info * dst) const {
        dst->cut = x.back().first;
        dst->subdivide = x.back().first;
        dst->table_size = x.size();
        dst->table = (unsigned int (*)[2]) realloc(dst->table,
                dst->table_size * sizeof(unsigned int[2]));
        // cout << "cutoffs for kara:";
        for(unsigned int v = 0 ; v < dst->table_size ; v++) {
            if (x[v].second == 1 && x[v].first < dst->subdivide) {
                dst->subdivide = x[v].first;
            }
            dst->table[v][0] = x[v].first;
            dst->table[v][1] = x[v].second;
            // cout << " " << x[v].first << " " << x[v].second << ",";
        };
        // cout <<"\n";
    }

    friend ostream& operator<<(ostream& o, cutoff_array const& A);
};
ostream& operator<<(ostream& o, cutoff_array const& A) {
    o << "{";
    for(auto y : A.x) {
        o << " { " << y.first << ", " << y.second << " },";
    }
    o << " }";
    return o;
}
/* }}} */

struct timer_rusage {
    inline double operator()() const { return seconds(); }
};
struct timer_wct {
    inline double operator()() const { return wct_seconds(); }
};
/* It is important, when doing MPI benches, that we use this algorithm */
struct timer_wct_synchronized {
    inline double operator()() const { 
        double d = wct_seconds();
        MPI_Allreduce(MPI_IN_PLACE, &d, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        return d;
    }
};

/* {{{ cutoff_finder and its child small_bench */
template<typename T0 = timer_rusage>
struct cutoff_finder {
    int enough_repeats;
    double minimum_time;
    double enough_time;
    double scale;
    int stable_cutoff_break;
    cutoff_array cutoffs;
    unsigned int ntests;
    int stable;
    vector<double> benches;
    vector<bool> meaningful;
    vector<pair<unsigned int, pair<vector<double>, int> > > all_results;
    map<int, string> method_names;

    cutoff_finder(unsigned int ntests, unsigned int min_length = 1)
        : cutoffs(make_pair(min_length, 0)),
        ntests(ntests),
        benches(ntests),
        meaningful(ntests, true)
    {
        /* Fill defaults */
        enough_repeats = 1000;
        minimum_time = 0.5;
        enough_time = 1.0;
        scale = 1.1;
        stable_cutoff_break = 4;
        stable = 0;
    }

    inline void set_method_name(int k, string const& s) {
        method_names.insert(make_pair(k, s));
    }
    inline string method_name(int i) const {
        ostringstream v;
        map<int, string>::const_iterator z = method_names.find(i);
        if (z == method_names.end()) {
            v << "method " << i;
            return v.str();
        }
        return z->second;
    }

    inline unsigned int next_length(unsigned int k) const {
        return MAX(k+1, scale*k);
    }

    inline int done() const {
        int nactive=0;
        for(int i = 0 ; i < ntests ; i++) {
            nactive += meaningful[i];
        }
        if (nactive > 1) return 0;
        return !(stable < stable_cutoff_break);
    }

    inline bool still_meaningful_to_test(int i) { return meaningful[i]; }

    string summarize_for_this_length(unsigned int k)
    {
        const double slowness_ratio = 2;
        const int age_slow_discard = 5;
        std::ostringstream comments;
        int best = -1;
        for(int i = 0 ; i < ntests ; i++) {
            if (meaningful[i] && (best < 0 || benches[i] < benches[best])) best = i;
        }
        ASSERT_ALWAYS(best >= 0);
        all_results.push_back(make_pair(k,
                    make_pair(benches, best)));

        if (cutoffs.x.empty() || best != cutoffs.x.back().second) {
            stable = 0;
        } else {
            stable++;
        }

        cutoffs.push(k, best);

        vector<bool> was_meaningful = meaningful;

        /* try to discard those which are slow */
        for(int i = 0 ; i < best ; i++) {
            /* already dead ? */
            if (!meaningful[i]) continue;

            /* for how long has it been slow ? */
            unsigned int age_slow = 0;
            for( ; age_slow < all_results.size() ; age_slow++) {
                vector<double> const& these(all_results[all_results.size()-1-age_slow].second.first);
                int best_there = all_results[all_results.size()-1-age_slow].second.second;
                /* not so slow */
                if (these[i] <= these[best_there] * slowness_ratio) break;
            }
            if (age_slow >= age_slow_discard) {
                /* ok, discard */
                comments << "  ; discarding " << method_name(i);
                meaningful[i] = false;
            }
        }
        ostringstream s;
        s << k;
        for(int i = 0 ; i < ntests ; i++) {
            if (was_meaningful[i]) {
                s << " " << benches[i];
                if (i==best) s << '#';
            } else {
                s << " *";
            }
            benches[i] = 999999;
        }
        s << " " << method_name(best) << comments.str();
        return s.str();
    }

    cutoff_array const& result() const { return cutoffs; }
    /*
    void export_to_cutoff_info(polymat_cutoff_info* dst, pair<unsigned int, int> final_step)
    {
        cutoffs.export_to_cutoff_info(dst, final_step);
    }
    void export_to_cutoff_info(polymat_cutoff_info* dst)
    {
        cutoffs.export_to_cutoff_info(dst);
    }
    */


    template<typename T = T0>
    struct small_bench {
        cutoff_finder const& dad;
        double& tfinal;
        double tt;
        int n;
        explicit small_bench(cutoff_finder const& dad, double& t) :
            dad(dad), tfinal(t) {tt=n=0;}
        inline operator int() {
            if (tt < dad.enough_time) {
                if (tt < dad.minimum_time || n < dad.enough_repeats)
                    return 1;
            }
            tfinal = tt / n;
            return 0;
        }
        inline small_bench& operator++() { n++; return *this; }
        inline void pop() { tt += T()(); }
        inline void push() { tt -= T()(); }
    };

    template<typename T = T0>
    small_bench<T> micro_bench(int i) {
        return small_bench<T>(*this, benches[i]);
    }

};
/* }}} */

/* This code will first try to bench the basic operations (first
 * local; global are later) of middle product E*pi and product pi*pi.
 * The goal is to see where is the good value for the thresholds:
 * polymat_mul_kara_threshold
 * polymat_mp_kara_threshold
 */

void plingen_tune_mul(abdst_field ab, unsigned int m, unsigned int n)/*{{{*/
{
    gmp_randstate_t rstate;
    gmp_randinit_default(rstate);
    gmp_randseed_ui(rstate, 1);
    /* arguments to the ctor:
     * 3 : we are benching 3 methods (numbered 0, 1, 2)
     * 1 : size makes sense only for >=1
     */
    /* TODO: support multiple values in my cutoff table finder ? */
#ifdef HAVE_MPIR
#define FINDER_NMETHODS 4
#else  /* HAVE_MPIR */
#define FINDER_NMETHODS 3
#endif  /* HAVE_MPIR */

    cutoff_finder<timer_rusage> finder(FINDER_NMETHODS, 1);
    finder.set_method_name(0, "polymat-basecase");
    finder.set_method_name(1, "polymat-karatsuba");
    finder.set_method_name(2, "matpoly-kronecker-schönhage");
#ifdef  HAVE_MPIR
    finder.set_method_name(3, "matpoly-ft-kronecker-schönhage-caching");
#endif

    polymat_cutoff_info always_basecase[1];
    polymat_cutoff_info improved[1];

    polymat_cutoff_info_init(always_basecase);
    polymat_cutoff_info_init(improved);

    cout << "# Tuning "<<(m+n)<<"*"<<(m+n)<<"*"<<(m+n)<<" products\n";
    /* Beware, k is a length, not a degree. Hence length 1 clearly makes
     * no sense */
    cout << "# inputlength, ncoeffs";
    for(unsigned int i = 0 ; i < finder.ntests ; i++) {
        cout << ", " << finder.method_name(i);
    }
    cout << "\n";
    cout << "# Note: for input length k within plingen, we use mcoeffs,k*m/(m+n) = "<<(double)m/(m+n)<<"*k\n";

    /* Note: we are benching degree k, but the degree we are
     * interested in for pi is k*m/(m+n) */
    for(unsigned int k = 2 ; !finder.done() ; k=finder.next_length(k)) {
        polymat pi, piL, piR;
        polymat_init(ab, piL, m+n, m+n, k);
        polymat_init(ab, piR, m+n, m+n, k);
        polymat_init(ab, pi, m+n, m+n, 2*k-1);
        polymat_fill_random(ab, piL, k, rstate);
        polymat_fill_random(ab, piR, k, rstate);


        if (finder.still_meaningful_to_test(0)) {
            /* disable kara for a minute */
            polymat_set_mul_kara_cutoff(always_basecase, NULL);
            for(auto x = finder.micro_bench(0); x; ++x) {
                x.push();
                polymat_mul(ab, pi, piL, piR);
                x.pop();
            }
        }

        if (finder.still_meaningful_to_test(1)) {
            /* This temporarily sets the cutoff table to enable karatsuba for
             * length >=k (hence for this test) at least, possibly using
             * karatsuba one or more times in the recursive calls depending
             * on what has been measured as best so far.
             */
            cutoff_array tmp_cutoffs = finder.cutoffs;
            tmp_cutoffs.push(k, 1);
            tmp_cutoffs.export_to_cutoff_info(improved);
            polymat_set_mul_kara_cutoff(improved, NULL);
            for(auto x = finder.micro_bench(1); x; ++x) {
                x.push();
                polymat_mul(ab, pi, piL, piR);
                x.pop();
            }
        }

        if (finder.still_meaningful_to_test(2)) {
            /* The matpoly layer is just completetly different -- and gets
             * faster quite early on... */
            for(auto x = finder.micro_bench(2); x; ++x) {
                x.push();
                matpoly_mul(ab, (matpoly_ptr) pi, (matpoly_ptr) piL, (matpoly_ptr) piR);
                x.pop();
            }
        }

#ifdef HAVE_MPIR
        if (finder.still_meaningful_to_test(3)) {
            matpoly_ft tpi, tpiL, tpiR;
            mpz_t p;
            mpz_init(p);
            abfield_characteristic(ab, p);
            struct fft_transform_info fti[1];
            fft_get_transform_info_fppol(fti, p, piL->size, piR->size, piL->n);
            for(auto x = finder.micro_bench(3); x; ++x) {
                x.push();
                matpoly_ft_init(ab, tpiL, piL->m, piL->n, fti);
                matpoly_ft_init(ab, tpiR, piR->m, piR->n, fti);
                matpoly_ft_init(ab, tpi, piL->m, piR->n, fti);
                matpoly_ft_dft(ab, tpiL, (matpoly_ptr) piL, fti);
                matpoly_ft_dft(ab, tpiR, (matpoly_ptr) piR, fti);
                matpoly_ft_mul(ab, tpi, tpiL, tpiR, fti);
                matpoly_ft_ift(ab, (matpoly_ptr) pi, tpi, fti);
                matpoly_ft_clear(ab, tpiL, fti);
                matpoly_ft_clear(ab, tpiR, fti);
                matpoly_ft_clear(ab, tpi,  fti);
                x.pop();
            }
            mpz_clear(p);
        }
#endif

        cout << (int) ((m+n)*k/m) << " " << finder.summarize_for_this_length(k) << "\n";

#if 0
        printf("%d %1.6f %1.6f %1.6f %1.1f\n", k, ttb, ttk, ttm, ttk/ttb);

        int best = 0;
        if (ttk < ttb) { best++; if (ttm < ttk) best++; }
        finder.new_winner(k, best); /* < : kara wins: 1 */
#endif

        polymat_clear(ab, piL);
        polymat_clear(ab, piR);
        polymat_clear(ab, pi);
    }


    finder.cutoffs.export_to_cutoff_info(improved);
    polymat_set_mul_kara_cutoff(improved, NULL);

    cout << "/* Cutoffs for "<<(m+n)<<"*"<<(m+n)<<"*"<<(m+n)<<" products: */\n";
    cout << "#define MUL_CUTOFFS_" <<(m+n)<<"_"<<(m+n)<<"_"<<(m+n)
        << " " << finder.result() << endl;
    gmp_randclear(rstate);

    polymat_cutoff_info_clear(always_basecase);
    polymat_cutoff_info_clear(improved);
}/*}}}*/

#if 0
void plingen_tune_mp(abdst_field ab, unsigned int m, unsigned int n)/*{{{*/
{
    gmp_randstate_t rstate;
    gmp_randinit_default(rstate);
    gmp_randseed_ui(rstate, 1);
    /* arguments to the ctor:
     * 2 : we are benching 2 methods
     * 1 : size makes sense only for >=1
     */
    cutoff_finder<timer_rusage> finder(2, 1);

    polymat_cutoff_info always_basecase[1];
    polymat_cutoff_info improved[1];

    polymat_cutoff_info_init(always_basecase);
    polymat_cutoff_info_init(improved);

    /* Beware, k is a length, not a degree. Hence length 1 clearly makes
     * no sense */
    for(unsigned int k = 2 ; !finder.done() ; k=finder.next_length(k)) {
        /* For a 2\ell lingen call, we need a MP which is
         *
         * (2-n/(m+n))\ell times m/(m+n)\ell --> \ell
         *
         * The small argument is m/(m+n)\ell. It's the degree of piL here.
         * The corresponding degree for E, or the interesting fragment of
         * it, is:
         *   (2-n/(m+n))\ell = (2m+n)/(m+n)\ell = (2+n/m)*k
         *
         * while the result gives has length \ell = (1+n/m)*k
         *
         */
        /* Note: we are benching degree k, but the degree we are
         * interested in for ER is k*m/(m+n) */
        polymat ER, E, piL;
        polymat_init(ab, E, m, m+n, 2*k + n*k/m - 1);
        polymat_init(ab, piL, m+n, m+n, k);
        polymat_init(ab, ER, m, m+n, k + n*k/m);
        polymat_fill_random(ab, E, 2*k + n*k/m - 1, rstate);
        polymat_fill_random(ab, piL, k, rstate);
        double ttb;
        /* disable kara for a minute */
        polymat_set_mp_kara_cutoff(always_basecase, NULL);
        for(auto x = finder.micro_bench(ttb); x; ++x) {
            x.push();
            polymat_mp(ab, ER, E, piL);
            x.pop();
        }

        double ttk;
        /* This yields exactly *one* kara recursion step */
        finder.export_to_cutoff_info(improved, k);
        polymat_set_mp_kara_cutoff(improved, NULL);
        for(auto x = finder.micro_bench(ttk); x; ++x) {
            x.push();
            polymat_mp(ab, ER, E, piL);
            x.pop();
        }
        printf("%d %1.6f %1.6f %1.1f\n", k, ttb, ttk, ttk/ttb);
        finder.new_winner(k, ttk < ttb); /* < : kara wins: 1 */

        polymat_clear(ab, E);
        polymat_clear(ab, piL);
        polymat_clear(ab, ER);
    }

    finder.export_to_cutoff_info(improved);
    polymat_set_mp_kara_cutoff(improved, NULL);

    cout << "/* Cutoffs for "<<(m+n)<<"*"<<(m+n)<<"*"<<(m+n)<<" middle products: */\n";
    cout << "#define MP_CUTOFFS_" <<(m+n)<<"_"<<(m+n)<<"_"<<(m+n)
        << " " << finder.result() << endl;
    gmp_randclear(rstate);

    polymat_cutoff_info_clear(always_basecase);
    polymat_cutoff_info_clear(improved);
}/*}}}*/

void plingen_tune_bigmul(abdst_field ab, unsigned int m, unsigned int n, unsigned int m1, unsigned int n1, MPI_Comm comm)/*{{{*/
{
    int rank;
    MPI_Comm_rank(comm, &rank);
    gmp_randstate_t rstate;
    gmp_randinit_default(rstate);
    gmp_randseed_ui(rstate, 1);
    /* arguments to the ctor:
     * 2 : we are benching 2 methods
     * 1 : size makes sense only for >=1
     */
    cutoff_finder<> finder(2, 1);

    bigpolymat model;
    bigpolymat_init_model(model, comm, m1, n1);

    /* Beware, k is a length, not a degree. Hence length 1 clearly makes
     * no sense */
    for(unsigned int k = 2 ; !finder.done() ; k=finder.next_length(k)) {
        /* Note: we are benching degree k, but the degree we are
         * interested in for pi is k*m/(m+n) */
        polymat pi, piL, piR;
        bigpolymat bpi, bpiL, bpiR;
        polymat_init(ab, piL, m+n, m+n, k);
        polymat_init(ab, piR, m+n, m+n, k);
        polymat_init(ab, pi, m+n, m+n, 2*k-1);
        bigpolymat_init(ab, bpiL, model, m+n, m+n, k);
        bigpolymat_init(ab, bpiR, model, m+n, m+n, k);
        bigpolymat_init(ab, bpi, model, m+n, m+n, 2*k-1);
        polymat_fill_random(ab, piL, k, rstate);
        polymat_fill_random(ab, piR, k, rstate);
        polymat_fill_random(ab, bigpolymat_my_cell(bpiL), k, rstate);
        polymat_fill_random(ab, bigpolymat_my_cell(bpiR), k, rstate);

        double ttloc;
        if (rank == 0) {
            for(auto x = finder.micro_bench<timer_wct>(ttloc); x; ++x) {
                x.push();
                polymat_mul(ab, pi, piL, piR);
                x.pop();
            }
        }
        MPI_Bcast(&ttloc, 1, MPI_DOUBLE, 0, comm);

        double ttmpi;
        for(auto x = finder.micro_bench<timer_wct_synchronized>(ttmpi); x; ++x) {
            x.push();
            bigpolymat_mul(ab, bpi, bpiL, bpiR);
            x.pop();
        }
        if (rank == 0)
            printf("%d %1.6f %1.6f %1.1f\n", k, ttloc, ttmpi, ttmpi/ttloc);
        finder.new_winner(k, ttmpi < ttloc); /* < : mpi wins: 1 */

        polymat_clear(ab, piL);
        polymat_clear(ab, piR);
        polymat_clear(ab, pi);
        bigpolymat_clear(ab, bpiL);
        bigpolymat_clear(ab, bpiR);
        bigpolymat_clear(ab, bpi);
    }

    bigpolymat_clear_model(model);

    if (rank == 0) {
        cout << "/* Cutoffs for "<<(m+n)<<"*"<<(m+n)<<"*"<<(m+n)<<" MPI products: */\n";
        cout << "#define MUL_MPI_CUTOFFS_" <<(m+n)<<"_"<<(m+n)<<"_"<<(m+n)
            << " " << finder.result() << endl;
    }
    gmp_randclear(rstate);
}/*}}}*/
#endif

void plingen_tuning(abdst_field ab, unsigned int m, unsigned int n, MPI_Comm comm, param_list_ptr pl)
{
    mpz_t p;
    int thr[2] = {1,1};
    int mpi[2] = {1,1};
    int rank;

    MPI_Comm_rank(comm, &rank);

    mpz_init(p);
    abfield_characteristic(ab, p);
    param_list_parse_intxint(pl, "mpi", mpi);
    param_list_parse_intxint(pl, "thr", thr);
    if (rank == 0) {
        printf("# Tuning for m=%d n=%d p=[%zu %u-bits words]"
                " mpi=%dx%d thr=%dx%d\n",
                m, n, mpz_size(p), GMP_LIMB_BITS,
                mpi[0], mpi[1], thr[0], thr[1]);
    }
    mpz_clear(p);

    plingen_tune_mul(ab, m, n);

#if 0
    /* This should normally be reasonably quick, and running it every
     * time can be considered as an option */
    if (rank == 0) {
        plingen_tune_mul(ab, m, n);
        plingen_tune_mp(ab, m, n);
    }
    extern polymat_cutoff_info polymat_mul_kara_cutoff;
    extern polymat_cutoff_info polymat_mp_kara_cutoff;
    bigpolymat_bcast_polymat_cutoff(&polymat_mul_kara_cutoff, 0, comm);
    bigpolymat_bcast_polymat_cutoff(&polymat_mp_kara_cutoff, 0, comm);

    plingen_tune_bigmul(ab, m, n, mpi[0]*thr[0], mpi[1]*thr[1], comm);
#endif

#if 0
    int tune_bm_basecase = 1;
    int tune_mp = 1;
    /* record the list of cutoffs, and the method which wins from there
     */

    if (tune_mp) {
        /* Now for benching mp and plain mul */
        unsigned int maxtune = 10000 / (m*n);
        /* Bench the products which would come together with a k-steps
         * basecase algorithm. IOW a 2k, one-level recursive call incurs
         * twice the k-steps basecase, plus once the timings counted here
         * (presently, this has Karatsuba complexity)
         */
        for(unsigned int k = 10 ; k < maxtune ; k+= k/10) {
            polymat E, piL, piR, pi, Er;
            unsigned int sE = k*(m+2*n)/(m+n);
            unsigned int spi = k*m/(m+n);
            polymat_init(E, m, m+n, sE);
            polymat_init(piL, m+n, m+n, spi);
            polymat_init(piR, m+n, m+n, spi);
            polymat_init(pi, m+n, m+n, spi*2);
            polymat_init(Er, m, m+n, sE-spi+1);
            E->size = sE;
            for(unsigned int v = 0 ; v < E->m * E->n * E->size ; v++) {
                abrandom(ab, E->x[v], rstate);
            }
            piL->size = spi;
            piR->size = spi;
            for(unsigned int v = 0 ; v < piL->m * piL->n * piL->size ; v++) {
                abrandom(ab, piL->x[v], rstate);
                abrandom(ab, piR->x[v], rstate);
            }
            double ttmp = 0, ttmul = 0;
            ttmp -= seconds();
            polymat_mp(ab, Er, E, piL);
            ttmp += seconds();
            ttmul -= seconds();
            polymat_mul(ab, pi, piL, piR);
            ttmul += seconds();
            double ttmpq = ttmp / (k*k);
            double ttmulq = ttmul / (k*k);
            double ttmpk = ttmp / pow(k, 1.58);
            double ttmulk = ttmul / pow(k, 1.58);
            printf("%u [%.2e+%.2e = %.2e] [%.2e+%.2e = %.2e]\n",
                    k,
                    ttmpq, ttmulq, ttmpq + ttmulq,
                    ttmpk, ttmulk, ttmpk + ttmulk
                    );
            // (seconds()-tt) / (k*k)); // ((sE-spi) * spi) / (m*(m+n)*(m+n)));
            // printf("%zu %.2e\n", E->size, (seconds()-tt) / (k*k)); // (spi * spi) / ((m+n)*(m+n)*(m+n)));
            polymat_clear(E);
            polymat_clear(piL);
            polymat_clear(piR);
            polymat_clear(pi);
            polymat_clear(Er);
        }
    }
    if (tune_bm_basecase) {
        unsigned int maxtune = 10000 / (m * n);
        for(unsigned int k = 10 ; k < maxtune ; k += k/10) {
            unsigned int * delta = malloc((m + n) * sizeof(unsigned int));
            polymat E, pi;
            polymat_init(pi, 0, 0, 0);
            polymat_init(E, m, m+n, maxtune);
            E->size = k;
            for(unsigned int v = 0 ; v < E->m * E->n * E->size ; v++) {
                abrandom(ab, E->x[v], rstate);
            }
            double tt = seconds();
            for(unsigned int j = 0 ; j < m + n ; delta[j++]=1);
            bm->t = 1;
            bw_lingen_basecase(bm, pi, E, delta);
            printf("%zu %.2e\n", E->size, (seconds()-tt) / (k * k));
            polymat_clear(pi);
            polymat_clear(E);
            free(delta);
        }
    }


#endif

    return;
}

