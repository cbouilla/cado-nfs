#include "cado.h"

#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <array>
#include <memory>

#include "las-config.h"
#include "cxx_mpz.hpp"
#include "las-info.hpp"
#include "las-debug.hpp"
#include "las-coordinates.hpp"
#include "las-threads-work-data.hpp"    /* trace_per_sq_init needs this */
#include "portability.h"

using namespace std;
#if defined(__GLIBC__)
#include <execinfo.h>   /* For backtrace. Since glibc 2.1 */
#include <signal.h>     /* we use it only with glibc */
#endif
#ifdef HAVE_CXXABI_H
/* We use that to demangle C++ names */
#include <cxxabi.h>
#endif

#if defined(__GLIBC__)
static void signal_handling (int signum)/*{{{*/
{
   fprintf (stderr, "*** Error: caught signal \"%s\"\n", strsignal (signum));

   int sz = 100, i;
   void *buffer [sz];
   char** text;

   sz = backtrace (buffer, sz);
   text = backtrace_symbols (buffer, sz);

   fprintf(stderr, "======= Backtrace: =========\n");
   for (i = 0; i < sz; i++)
       fprintf (stderr, "%s\n", text [i]);

   signal (signum, SIG_DFL);
   raise (signum);
}/*}}}*/
#endif

void las_install_sighandlers()
{
#ifdef __GLIBC__
    signal (SIGABRT, signal_handling);
    signal (SIGSEGV, signal_handling);
#else
    verbose_output_print(0, 0, "# Cannot catch signals, lack glibc support\n");
#endif
}



/* The trivial calls for when TRACE_K is *not* defined are inlines in
 * las-debug.h */


/* recall that TRACE_K requires TRACK_CODE_PATH ; so we may safely use
 * all where_am_I types here */

trace_Nx_t trace_Nx { 0, UINT_MAX};
trace_ab_t trace_ab { 0, 0 };
trace_ij_t trace_ij { 0, UINT_MAX, };

/* Those are from the parameter list. */
std::unique_ptr<trace_ab_t> pl_ab;
std::unique_ptr<trace_ij_t> pl_ij;
std::unique_ptr<trace_Nx_t> pl_Nx;

int have_trace_ab = 0, have_trace_ij = 0, have_trace_Nx = 0;

/* two norms of the traced (a,b) pair */
std::array<cxx_mpz, 2> traced_norms;

void init_trace_k(cxx_param_list & pl)
{
    struct trace_ab_t ab;
    struct trace_ij_t ij;
    struct trace_Nx_t Nx;
    int have_trace_ab = 0, have_trace_ij = 0, have_trace_Nx = 0;

    const char *abstr = param_list_lookup_string(pl, "traceab");
    if (abstr != NULL) {
        if (sscanf(abstr, "%" SCNd64",%" SCNu64, &ab.a, &ab.b) == 2)
            have_trace_ab = 1;
        else {
            fprintf (stderr, "Invalid value for parameter: -traceab %s\n",
                     abstr);
            exit (EXIT_FAILURE);
        }
    }

    const char *ijstr = param_list_lookup_string(pl, "traceij");
    if (ijstr != NULL) {
        if (sscanf(ijstr, "%d,%u", &ij.i, &ij.j) == 2) {
            have_trace_ij = 1;
        } else {
            fprintf (stderr, "Invalid value for parameter: -traceij %s\n",
                     ijstr);
            exit (EXIT_FAILURE);
        }
    }

    const char *Nxstr = param_list_lookup_string(pl, "traceNx");
    if (Nxstr != NULL) {
        if (sscanf(Nxstr, "%u,%u", &Nx.N, &Nx.x) == 2)
            have_trace_Nx = 1;
        else {
            fprintf (stderr, "Invalid value for parameter: -traceNx %s\n",
                     Nxstr);
            exit (EXIT_FAILURE);
        }
    }
    if (have_trace_ab) pl_ab = std::unique_ptr<trace_ab_t>(new trace_ab_t(ab));
    if (have_trace_ij) pl_ij = std::unique_ptr<trace_ij_t>(new trace_ij_t(ij));
    if (have_trace_Nx) pl_Nx = std::unique_ptr<trace_Nx_t>(new trace_Nx_t(Nx));
}

/* This fills all the trace_* structures from the main one. The main
 * structure is the one for which a non-NULL pointer is passed.
 */
void trace_per_sq_init(nfs_work const & ws)
{
    int logI = ws.conf.logI;
    unsigned int J = ws.J;
    qlattice_basis const & Q(ws.Q);

#ifndef TRACE_K
    if (pl_Nx || pl_ab || pl_ij) {
        fprintf (stderr, "Error, relation tracing requested but this siever "
                 "was compiled without TRACE_K.\n");
        exit(EXIT_FAILURE);
    }
    return;
#endif
    /* At most one of the three coordinates must be specified */
    ASSERT_ALWAYS((pl_Nx != NULL) + (pl_ab != NULL) + (pl_ij != NULL) <= 1);

    if (pl_ab) {
      trace_ab = *pl_ab;
      /* can possibly fall outside the q-lattice. We have to check for it */
      if (ABToIJ(trace_ij.i, trace_ij.j, trace_ab.a, trace_ab.b, Q)) {
          IJToNx(trace_Nx.N, trace_Nx.x, trace_ij.i, trace_ij.j, logI);
      } else {
          verbose_output_print(TRACE_CHANNEL, 0, "# Relation (%" PRId64 ",%" PRIu64 ") to be traced "
                  "is outside of the current q-lattice\n",
                  trace_ab.a, trace_ab.b);
          trace_ij.i=0;
          trace_ij.j=UINT_MAX;
          trace_Nx.N=0;
          trace_Nx.x=UINT_MAX;
          return;
      }
    } else if (pl_ij) {
        trace_ij = *pl_ij;
        IJToAB(trace_ab.a, trace_ab.b, trace_ij.i, trace_ij.j, Q);
        IJToNx(trace_Nx.N, trace_Nx.x, trace_ij.i, trace_ij.j, logI);
    } else if (pl_Nx) {
        trace_Nx = *pl_Nx;
        if (trace_Nx.x < ((size_t) 1 << LOG_BUCKET_REGION)) {
            NxToIJ(trace_ij.i, trace_ij.j, trace_Nx.N, trace_Nx.x, logI);
            IJToAB(trace_ab.a, trace_ab.b, trace_ij.i, trace_ij.j, Q);
        } else {
            fprintf(stderr, "Error, tracing requested for x=%u but"
                    " this siever was compiled with LOG_BUCKET_REGION=%d\n",
                    trace_Nx.x, LOG_BUCKET_REGION);
            exit(EXIT_FAILURE);
        }
    }

    if ((trace_ij.j < UINT_MAX && trace_ij.j >= J)
         || (trace_ij.i < -(1L << (logI-1)))
         || (trace_ij.i >= (1L << (logI-1))))
    {
        verbose_output_print(TRACE_CHANNEL, 0, "# Relation (%" PRId64 ",%" PRIu64 ") to be traced is "
                "outside of the current (i,j)-rectangle (i=%d j=%u)\n",
                trace_ab.a, trace_ab.b, trace_ij.i, trace_ij.j);
        trace_ij.i=0;
        trace_ij.j=UINT_MAX;
        trace_Nx.N=0;
        trace_Nx.x=UINT_MAX;
        return;
    }
    if (trace_ij.i || trace_ij.j < UINT_MAX) {
        verbose_output_print(TRACE_CHANNEL, 0, "# Tracing relation (a,b)=(%" PRId64 ",%" PRIu64 ") "
                "(i,j)=(%d,%u), (N,x)=(%u,%u)\n",
                trace_ab.a, trace_ab.b, trace_ij.i, trace_ij.j, trace_Nx.N,
                trace_Nx.x);
    }

    for(int side = 0 ; side < 2 ; side++) {
        int i = trace_ij.i;
        unsigned j = trace_ij.j;
        adjustIJsublat(i, j, Q.sublat);
        ws.sides[side].lognorms.norm(traced_norms[side], i, j);
    }
}

#ifdef TRACE_K
int test_divisible(where_am_I& w)
{
    /* we only check divisibility for the given (N,x) value */
    if (!trace_on_spot_Nx(w.N, w.x))
        return 1;

    /* Note that when we are reaching here through apply_one_bucket, we
     * do not know the prime number. */
    fbprime_t p = w.p;
    if (p==0) return 1;

    const unsigned int logI = w.logI;
    const unsigned int I = 1U << logI;

    const unsigned long X = w.x + (w.N << LOG_BUCKET_REGION);
    long i = (long) (X & (I-1)) - (long) (I/2);
    unsigned long j = X >> logI;
    fbprime_t q;

    q = fb_is_power (p, NULL);
    if (q == 0)
        q = p;

    int rc = mpz_divisible_ui_p (traced_norms[w.side], (unsigned long) q);

    if (rc)
        mpz_divexact_ui (traced_norms[w.side], traced_norms[w.side], (unsigned long) q);
    else
        verbose_output_vfprint(TRACE_CHANNEL, 0, gmp_vfprintf, "# FAILED test_divisible(p=%" FBPRIME_FORMAT
                ", N=%d, x=%u, side %d): i = %ld, j = %u, norm = %Zd\n",
                w.p, w.N, w.x, w.side, (long) i, j, (mpz_srcptr) traced_norms[w.side]);

    return rc;
}

/* {{{ helper: sieve_increase */

string remove_trailing_address_suffix(string const& a, string& suffix)
{
    size_t pos = a.find('+');
    if (pos == a.npos) {
        suffix.clear();
        return a;
    }
    suffix = a.substr(pos);
    return a.substr(0, pos);
}

string get_parenthesized_arg(string const& a, string& prefix, string& suffix)
{
    size_t pos = a.find('(');
    if (pos == a.npos) {
        prefix=a;
        suffix.clear();
        return string();
    }
    size_t pos2 = a.find(')', pos + 1);
    if (pos2 == a.npos) {
        prefix=a;
        suffix.clear();
        return string();
    }
    prefix = a.substr(0, pos);
    suffix = a.substr(pos2 + 1);
    return a.substr(pos+1, pos2-pos-1);
}

/* Do this so that the _real_ caller is always 2 floors up */
void sieve_increase_logging_backend(unsigned char *S, const unsigned char logp, where_am_I& w)
{
    if (!trace_on_spot_Nx(w.N, w.x))
        return;

    ASSERT_ALWAYS(test_divisible(w));

    string caller;

#ifdef __GLIBC__
    {
        void * callers_addresses[3];
        char ** callers = NULL;
        backtrace(callers_addresses, 3);
        callers = backtrace_symbols(callers_addresses, 3);
        caller = callers[2];
        free(callers);
    }

    string xx,yy,zz;
    yy = get_parenthesized_arg(caller, xx, zz);
    if (!yy.empty()) caller = yy;

    if (caller.empty()) {
        caller="<no symbol (static?)>";
    } else {
#ifdef HAVE_CXXABI_H
        string address_suffix;
        caller = remove_trailing_address_suffix(caller, address_suffix);
        int demangle_status;
        {
            char * freeme = abi::__cxa_demangle(caller.c_str(), 0, 0, &demangle_status);
            if (demangle_status == 0) {
                caller = freeme;
                free(freeme);
            }
        }

        /* Get rid of the type signature, it rather useless */
        yy = get_parenthesized_arg(caller, xx, zz);
        caller = xx;

        /* could it be that we have the return type in the name
         * as well ? */

        caller+=address_suffix;
#endif
    }
#endif
    if (w.p) 
        verbose_output_print(TRACE_CHANNEL, 0, "# Add log(%" FBPRIME_FORMAT ",side %d) = %hhu to "
            "S[%u] = %hhu, from BA[%u] -> %hhu [%s]\n",
            w.p, w.side, logp, w.x, *S, w.N, (unsigned char)(*S+logp), caller.c_str());
    else
        verbose_output_print(TRACE_CHANNEL, 0, "# Add log(hint=%lu,side %d) = %hhu to "
            "S[%u] = %hhu, from BA[%u] -> %hhu [%s]\n",
            (unsigned long) w.h, w.side, logp, w.x, *S, w.N, (unsigned char)(*S+logp), caller.c_str());
}

/* Produce logging as sieve_increase() does, but don't actually update
   the sieve array. */
void sieve_increase_logging(unsigned char *S, const unsigned char logp, where_am_I& w)
{
    sieve_increase_logging_backend(S, logp, w);
}

/* Increase the sieve array entry *S by logp, with underflow checking
 * and tracing if desired. w is used only for trace test and output */

void sieve_increase(unsigned char *S, const unsigned char logp, where_am_I& w)
{
    sieve_increase_logging_backend(S, logp, w);
#ifdef CHECK_UNDERFLOW
    sieve_increase_underflow_trap(S, logp, w);
#endif
    *S += logp;
}

#endif  /* TRACE_K */

/* This function is useful both with and without TRACE_K, as the flag
 * controlling it is CHECK_UNDERFLOW
 */
#ifdef CHECK_UNDERFLOW
void sieve_increase_underflow_trap(unsigned char *S, const unsigned char logp, where_am_I& w)
{
    int i;
    unsigned int j;
    int64_t a;
    uint64_t b;
    static unsigned char maxdiff = ~0;

    NxToIJ(&i, &j, w.N, w.x, w.logI);
    IJToAB(&a, &b, i, j, *w.Q);
    if ((unsigned int) logp + *S > maxdiff)
      {
        maxdiff = logp - *S;
        verbose_output_print(TRACE_CHANNEL, 0, "# Error, underflow at (N,x)=(%u, %u), "
                "(i,j)=(%d, %u), (a,b)=(%ld, %lu), S[x] = %hhu, log(%"
                FBPRIME_FORMAT ") = %hhu\n",
                w.N, w.x, i, j, a, b, *S, w.p, logp);
      }
    /* arrange so that the unconditional increase which comes next
     * has the effect of taking the result to maxdiff */
    *S = maxdiff - logp;
}
#endif


dumpfile_t::~dumpfile_t() {
    if (f) fclose(f);
}

void dumpfile_t::close() {
    if (f) fclose(f);
}

void dumpfile_t::open(const char *filename_stem, las_todo_entry const & doing, int side)
{
    ASSERT_ALWAYS(!f);
    if (filename_stem != NULL) {
        char *filename;
        int rc = gmp_asprintf(&filename, "%s.%d.sq%Zd.rho%Zd.side%d.dump",
            filename_stem,
            doing.side,
            (mpz_srcptr) doing.p, 
            (mpz_srcptr) doing.r, 
            side);
        ASSERT_ALWAYS(rc > 0);
        f = fopen(filename, "w");
        if (f == NULL) {
            perror("Error opening dumpfile");
        }
        free(filename);
    }
}

size_t dumpfile_t::write(const unsigned char * const data, const size_t size) const {
    if (!f) return 0;

    size_t rc = fwrite(data, sizeof(unsigned char), size, f);
    ASSERT_ALWAYS(rc == size);
    return rc;
}
/* }}} */
