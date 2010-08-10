#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE		/* asprintf */
#define _DARWIN_C_SOURCE	/* asprintf ; getpagesize */
#define _XOPEN_SOURCE   600     /* sometimes useful for posix_memalign */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "cado_config.h"
#include "macros.h"
#include "misc.h"

/* Not every libc has this, and providing a workalike is very easy */

char *cado_strndup(const char *a, size_t n)
{
    char *r = malloc(n+1);
    memcpy(r, a, MIN(strlen(a),n)+1);
    r[n] = '\0';
    return r;
}

void *malloc_check(const size_t x)
{
    void *p;
    p = malloc(x);
    ASSERT_ALWAYS(p != NULL);
    return p;
}

/* Not everybody has posix_memalign. In order to provide a viable
 * alternative, we need an ``aligned free'' matching the ``aligned
 * malloc''. We rely on posix_memalign if it is available, or else fall
 * back on ugly pointer arithmetic so as to guarantee alignment. Note
 * that not providing the requested alignment can have some troublesome
 * consequences. At best, a performance hit, at worst a segv (sse-2
 * movdqa on a pentium4 causes a GPE if improperly aligned).
 */

void *malloc_aligned(size_t size, size_t alignment)
{
#ifdef HAVE_POSIX_MEMALIGN
    void *res = NULL;
    int rc = posix_memalign(&res, alignment, size);
    ASSERT_ALWAYS(rc == 0);
    return res;
#else
    char * res;
    res = malloc(size + sizeof(size_t) + alignment);
    res += sizeof(size_t);
    size_t displ = alignment - ((unsigned long) res) % alignment;
    res += displ;
    memcpy(res - sizeof(size_t), &displ, sizeof(size_t));
    ASSERT_ALWAYS((((unsigned long) res) % alignment) == 0);
    return (void*) res;
#endif
}

void free_aligned(void * p, size_t size MAYBE_UNUSED, size_t alignment MAYBE_UNUSED)
{
#ifdef HAVE_POSIX_MEMALIGN
    free(p);
#else
    char * res = (char *) p;
    ASSERT_ALWAYS((((unsigned long) res) % alignment) == 0);
    size_t displ;
    memcpy(&displ, res - sizeof(size_t), sizeof(size_t));
    res -= displ;
    ASSERT_ALWAYS(displ == alignment - ((unsigned long) res) % alignment);
    res -= sizeof(size_t);
    free(res);
#endif
}

void *malloc_pagealigned(size_t sz)
{
    void *p = malloc_aligned(sz, getpagesize());
    ASSERT_ALWAYS(p != NULL);
    return p;
}

void free_pagealigned(void * p, size_t sz)
{
    free_aligned(p, sz, getpagesize());
}

int has_suffix(const char * path, const char * sfx)
{
    unsigned int lp = strlen(path);
    unsigned int ls = strlen(sfx);
    if (lp < ls) return 0;
    return strcmp(path + lp - ls, sfx) == 0;
}

// given a path to a file (prefix), and a suffix called (what), returns
// if the ext parameter is NULL, a malloced string equal to
// (prefix).(what) ; if ext is non-null AND (ext) is already a suffix of
// (prefix), say we have (prefix)=(prefix0)(ext), then we return
// (prefix0).(what)(ext)
// It is typical to use ".bin" or ".txt" as ext parameters.
char * derived_filename(const char * prefix, const char * what, const char * ext)
{
    char * dup_prefix;
    dup_prefix=strdup(prefix);

    if (ext && has_suffix(dup_prefix, ext)) {
        dup_prefix[strlen(dup_prefix)-strlen(ext)]='\0';
    }
    char * str;
    int rc = asprintf(&str, "%s.%s%s", dup_prefix, what, ext ? ext : "");
    if (rc<0) abort();
    free(dup_prefix);
    return str;
}

