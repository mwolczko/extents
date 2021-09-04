/*
  extents : print extent information for files

  Mario Wolczko, Sep 2021

 */


#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <getopt.h>
#include <assert.h>

#include "fail.h"
#include "mem.h"
#include "fiemap.h"
#include "extents.h"

static dev_t device;
blksize_t blk_sz;
static unsigned nfiles, n_ext= 0;
static off_t max_cmp= -1, skip1= 0, skip2= 0;

static fileinfo *info; // ptr to array of files' info of size nfiles

static list *extents; // list of all extent* from all files

static bool
  print_flags        = false,
  print_extents_only = false,
  print_shared_only  = false,
  print_unshared_only= false,
  no_headers         = false,
  print_phys_addr    = false,
  cmp_output         = false;

// an extent with its sharing info
typedef struct sh_ext sh_ext;
struct sh_ext {
    off_t p;         // physical offset on device
    off_t len;
    list *owners;    // the original extent*s from whence it came
};

static list *shared; // list of sh_ext*

#define ITER(l, EL_T, elem, stmt) {             \
  list *_l= (l);                                \
  for (unsigned _i= 0; _i < _l->nelems; ++_i) { \
    EL_T (elem)= get(_l, _i);                   \
    do { stmt; } while (0);                     \
  }}

#ifdef linux
#define OFF_T "ld"
#else
#define OFF_T "lld"
#endif

#define USAGE "usage: %s [--bytes LIMIT] [-c|--cmp] [-f|flags] [-h|--help] [[-i|--ignore-initial] SKIP1[:SKIP2]] [-n|--no_headers]\n"\
              "          [-P|--print_extents_only] [-p|--print_phys_addr] [[-s|--print_shared_only]|[-u|--print_unshared_only]]\n"\
              "          [-v|--dont_fail_silently] FILE1 [FILE2 ...]\n"

static void usage(char *p) { fail(USAGE, p); }

static void print_help(char *progname) {
    printf("%s: print extent information for files\n\n", progname);
    printf(USAGE, progname);
    printf("\nWith a single FILE, prints information about each extent.\n");
    printf("For multiple FILEs, determines which extents are shared and prints information about shared and unshared extents.\n");
    printf("An extent is a contiguous area of physical storage and is described by:\n");
    printf("  n if it belongs to FILEn (omitted for only a single file);\n");
    printf("  the logical offset in the file at which it begins;\n");
    printf("  the physical offset on the underlying device at which it begins (if -p or --print_phys_addr is specified);\n");
    printf("  its length.\nOffsets and lengths are in bytes.\n");
    printf("OS-specific flags are also printed (unless suppressed with -f or --no_flags). Flags are available only on Linux and are described in /usr/include/linux/fiemap.h.\n\n");
    printf("Options:\n");
    printf("-b --bytes LIMIT                   Compare at most LIMIT bytes (must be used with -c)\n");
    printf("-c --cmp                           (two files only) Output unshared regions to be compared by cmp. Fails silently unless -v is specified.\n");
    printf("-f --flags                         Print OS-specific flags\n");
    printf("-h --help                          Print help (this message)\n");
    printf("-i --ignore-initial SKIP1[:SKIP2}  Skip first SKIP1 bytes of file1 (optionally, SKIP2 of file2) -- must be used with -c\n");
    printf("-n --no_headers                    Don't print human-readable headers and line numbers\n");
    printf("-P --print_extents_only            Just print extents for each file, don't analyze sharing behavior");
    printf("-p --print_phys_addr               Print physical address of extents\n");
    printf("-s --print_shared_only             Print only shared extents (>1 file)\n");
    printf("-u --print_unshared_only           Print only unshared extents (>1 file)\n");
    printf("-v --dont_fail_silently            Don't fail silently (use only after -c)\n");
    printf("\nMario Wolczko, Oracle, Sep 2021\n");
    exit(0);
}

static void args(int argc, char *argv[])
{
    struct option longopts[]= {
            { "bytes",          required_argument, NULL, 'b' },
            { "cmp",                  no_argument, NULL, 'c' },
            { "flags"     ,           no_argument, NULL, 'f' },
            { "help",                 no_argument, NULL, 'h' },
            { "ignore-initial", required_argument, NULL, 'i' },
            { "no_headers",           no_argument, NULL, 'n' },
            { "print_extents_only",   no_argument, NULL, 'P' },
            { "print_phys_addr",      no_argument, NULL, 'p' },
            { "print_shared_only",    no_argument, NULL, 's' },
            { "print_unshared_only",  no_argument, NULL, 'u' },
            { "dont_fail_silently",   no_argument, NULL, 'v' },
        };
    for (int c; c= getopt_long(argc, argv, "cfhnpPsuvb:i:", longopts, NULL), c != -1; )
        switch (c) {
        case 'b':
            if (sscanf(optarg, "%" OFF_T, &max_cmp) != 1 || max_cmp <= 0)
                fail("arg to -b|--bytes must be positive integer\n");
            break;
        case 'i':
            if (sscanf(optarg, "%" OFF_T ":%" OFF_T, &skip1, &skip2) != 2) {
                if (sscanf(optarg, "%" OFF_T, &skip1) == 1) {
                    skip2= skip1;
                } else
                    skip1= -1;
            }
            if (skip1 < 0 || skip2 < 0)
                fail("arg to -i must be N or N:M (N,M non-negative integers)\n");
            break;
        case 'c': cmp_output=          true;
                  fail_silently=       true; break;
        case 'f': print_flags=         true; break;
        case 'n': no_headers=          true; break;
        case 'P': print_extents_only=  true; break;
        case 'p': print_phys_addr=     true; break;
        case 's': print_shared_only=   true; break;
        case 'u': print_unshared_only= true; break;
        case 'v': fail_silently=      false; break;
        case 'h': print_help(argv[0]); break;
        default : usage(argv[0]);
    }
    nfiles= (unsigned)(argc - optind);
    if (nfiles < 1) usage(argv[0]);
    if (print_shared_only && print_unshared_only)
        fail("Must choose only one of -s (--print_shared_only) and -u (--print_unshared_only)\n");
    if (cmp_output && nfiles != 2)
        fail("Must have two files with -c (--cmp_output)\n");
    if (cmp_output && print_extents_only)
        fail("Choose at most one of -c and -P");
    if (cmp_output && (print_shared_only || print_unshared_only || print_phys_addr))
        fail("Can't use -c with -s, -u or -p");
}

//functions on lists
static unsigned n_elems(list *ps) { return ps->nelems; }

#define GET(ps, i) ((ps)->elems[i])

static void *get(list *ps, unsigned i) { assert(n_elems(ps) > i); return GET(ps, i); }

static void put(list *ps, unsigned i, void *e) { assert(n_elems(ps) > i); GET(ps, i)= e; }

static bool is_empty(list *ps)     { return n_elems(ps) == 0; }

static bool is_singleton(list *ps) { return n_elems(ps) == 1; }

static bool is_multiple(list *ps)  { return n_elems(ps) > 1; }

static void *first(list *ps) { assert(!is_empty(ps)); return GET(ps, 0); }

static void *only(list *ps) { assert(is_singleton(ps)); return first(ps); }

static void *last(list *ps) { return GET(ps, n_elems(ps) - 1); }

static off_t end_l(extent *e) { return e->l + e->len; }

static off_t end_p(extent *e) { return e->p + e->len; }

// -ve max_sz means growable, abs value is initial size
// Caution: if growable, elems[] can be realloc'ed when growing, so don't keep pointers into the array.
static list *new_list(int max_sz) {
    assert(max_sz != 0);
    unsigned max_sz_abs= max_sz < 0 ? -max_sz : max_sz;
    list *ps= malloc_s(sizeof(list));
    ps->nelems= 0;
    ps->max_sz= max_sz;
    ps->elems= calloc_s(max_sz_abs, sizeof(void *));
    return ps;
}

static list *append(list *ps, void *e) {
    assert(ps->max_sz < 0 || n_elems(ps) < ps->max_sz);
    if (ps->max_sz < 0 && n_elems(ps) == -ps->max_sz) {
        ps->max_sz *= 2;
        ps->elems= realloc(ps->elems, (-ps->max_sz) * sizeof(void *));
    }
    put(ps, ps->nelems++, e);
    return ps;
}

#ifndef linux
typedef int (* _Nonnull __compar_fn_t)(const void *, const void *);
#endif

static int sh_ext_list_cmp_log(sh_ext **a, sh_ext **b) {
    extent *fa= first((*a)->owners), *fb= first((*b)->owners);
    return fa->l > fb->l ? 1 : fa->l < fb->l ? -1 : 0;
}

static void log_sort(list *l) {
    qsort(l->elems, l->nelems, sizeof(void *), (__compar_fn_t) &sh_ext_list_cmp_log);
}

static int extent_list_cmp_phys(extent **pa, extent **pb)
{
    extent *a= *pa, *b= *pb;
    return a->p > b->p ? 1
         : a->p < b->p ? -1
         : a->len > b->len ? 1
         : a->len < b->len ? -1
         : 0;
}

static void phys_sort() {
    qsort(&GET(extents, 0), n_ext, sizeof(extent *), (__compar_fn_t) &extent_list_cmp_phys);
}

static int extent_list_cmp_fileno(extent **a, extent **b) {
    return (*a)->info->argno > (*b)->info->argno ?  1
         : (*a)->info->argno < (*b)->info->argno ? -1
         : 0;
}

static void fileno_sort(list *ps) {
    qsort(ps->elems, ps->nelems, sizeof(void *), (__compar_fn_t) &extent_list_cmp_fileno);
}

#define LINENO_FMT "%-6"
#define FIELD_WIDTH 15
#define FIELD_WIDTH_S "15"
#define FIELD "%" OFF_T
#define FIELD_W "%" FIELD_WIDTH_S OFF_T
#define FILENO_WIDTH 4
#define FILENO_WIDTH_S "4"
#define SEP "  "

static void print_extent(extent *e) {
    off_t log= e->l, ph= e->p, len= e->len;
    unsigned fileno= e->info->argno + 1;
    if (no_headers)
        if (nfiles == 1)
            if (print_phys_addr)
                printf(FIELD " " FIELD " " FIELD " ", log, ph, len);
            else
                printf(FIELD " " FIELD " ", log, len);
        else if (print_phys_addr)
            printf("%d " FIELD " " FIELD " " FIELD " ", fileno, log, ph, len);
        else
            printf("%d " FIELD " " FIELD " ", fileno, log, len);
    else
        if (print_phys_addr)
            printf(FIELD_W " " FIELD_W " " FIELD_W " ", log, ph, len);
        else
            printf(FIELD_W " " FIELD_W " ", log, len);
}

static void print_sh_ext(off_t p, off_t len, extent *owner) {
    char filenobuf[10];
    off_t l= p - owner->p + owner->l;
    char *fileno= nfiles == 1 ? ""
            : (sprintf(filenobuf, no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", owner->info->argno + 1),
                    filenobuf);
    if (no_headers) {
        char *fmt= print_phys_addr ? "%s%ld %ld %ld" : "%s%ld %4$ld";
        printf(fmt, fileno, l, p, len);
    } else {
        char *fmt= print_phys_addr
                    ? "%s%" FIELD_WIDTH_S "ld %" FIELD_WIDTH_S "ld %" FIELD_WIDTH_S "ld "
                    : "%s%" FIELD_WIDTH_S "ld %4$" FIELD_WIDTH_S "ld ";
        printf(fmt, fileno, l, p, len);
    }
}

#define STRING_FIELD_FMT "%" FIELD_WIDTH_S "s"

static void print_header1(char *s, bool f) {
    printf("%" FILENO_WIDTH_S "s " STRING_FIELD_FMT " ", s ? s : "File", "Logical");
    if (print_phys_addr) printf(STRING_FIELD_FMT " ", "Physical");
    printf(STRING_FIELD_FMT " ", "Length");
    if (print_flags && f) fputs("  Flags", stdout);
}

static void print_header2() {
    printf("%" FILENO_WIDTH_S "s " STRING_FIELD_FMT " ", "", "Offset");
    if (print_phys_addr) printf(STRING_FIELD_FMT " ", "Offset");
    printf(STRING_FIELD_FMT " ", "");
}

static void print_header(unsigned i, char *h) {
    puts(info[i].name);
    print_header1(h, true);
    putchar('\n');
    print_header2();
    putchar('\n');
}

static char flagbuf[200];

static char *flag_pr(unsigned flags) {
    flags2str(flags, flagbuf, sizeof(flagbuf));
    return flagbuf;
}

static void print_extents_by_file() {
    for (unsigned i= 0; i < nfiles; i++) {
        if (!no_headers) print_header(i, "#");
        for (unsigned e= 0; e < info[i].n_exts; ++e) {
            extent *ext= &info[i].exts[e];
            if (!no_headers) printf("%" FILENO_WIDTH_S "d ", e + 1);
            print_extent(ext);
            if (print_flags) printf("  %s", flag_pr(ext->flags));
            putchar('\n');
        }
    }
}

static unsigned max_n_shared= 0;

static void print_shared_extents() {
    if (!no_headers) {
        puts("Shared: ");
        printf(LINENO_FMT "s ", "");
        for (unsigned i= 0; i < max_n_shared; ++i) {
            print_header1("File", false);
            fputs(SEP, stdout);
        }
        putchar('\n');
        printf(LINENO_FMT "s ", "");
        for (unsigned i= 0; i < max_n_shared; ++i) {
            print_header2();
            fputs(SEP, stdout);
        }
        putchar('\n');
    }
    unsigned e= 1;
    ITER(shared, sh_ext*, s_e, {
        if (!no_headers) printf(LINENO_FMT "d ", e++);
        ITER(s_e->owners, extent*, owner, {
                print_sh_ext(s_e->p, s_e->len, owner); fputs(SEP, stdout);
        });
        putchar('\n');
        if (print_flags) {
            printf(LINENO_FMT "s ", "FLAGS");
            ITER(s_e->owners, extent*, owner, {
                    printf("%-*s", FILENO_WIDTH
                                   + FIELD_WIDTH * (print_phys_addr ? 3 : 2)
                                   + (print_phys_addr ? 6 : 5),
                           flag_pr(owner->flags));
            });
            putchar('\n');
        }
    })
}

static void print_unshared_extents() {
    if (!no_headers) puts("Not Shared:");
    for (unsigned i= 0; i < nfiles; i++) {
        list *unsh= info[i].unsh;
        if (!is_empty(unsh)) {
            log_sort(unsh);
            if (!no_headers) print_header(i, nfiles==1 ? "#" : "File");
            unsigned e= 0;
            ITER(info[i].unsh, sh_ext*, sh, {
                if (!no_headers) printf("%" FILENO_WIDTH_S "d ", e++ + 1);
                print_sh_ext(sh->p, sh->len, only(sh->owners));
                putchar('\n');
            })
        }
    }
}

static void read_ext(char *fn[]) {
    info= calloc_s(nfiles, sizeof(fileinfo));
    for (unsigned i= 0; i < nfiles; ++i) {
        char *name= fn[i];
        int fd= open(name, O_RDONLY);
        if (fd < 0) fail("Can't open file %s : %s\n", name, strerror(errno));
        info[i].name= name;
        info[i].fd= (unsigned) fd;
        info[i].argno= i;
        info[i].exts= NULL;
        info[i].unsh= new_list(-4); // SWAG
        struct stat sb;
        if (fstat(fd, &sb) < 0) fail("Can't stat %s : %s\n", name, strerror(errno));
        if ((sb.st_mode & S_IFMT) != S_IFREG) fail("%s: Not a regular file\n", name);
        if (i == 0) {
            device= sb.st_dev;
            blk_sz= sb.st_blksize;
            if (cmp_output && (skip1 - skip2) % blk_sz != 0)
                fail("Skip distances must differ by a multiple of the block size (%d).\n", blk_sz);
        } else if (blk_sz != sb.st_blksize) fail("block size weirdness! %d v %d\n", blk_sz, sb.st_blksize);
        else if (device != sb.st_dev) fail("Error: All files must be on the same filesystem!\n");
        info[i].size= sb.st_size;
        info[i].skip= 0;
        if (cmp_output) {
            if (i == 0) info[0].skip = skip1;
            else if (i == 1) info[1].skip = skip2;
        }
        get_extents(&info[i], max_cmp);
        unsigned n= info[i].n_exts;
        n_ext += n;
        if (n > 0) {
            extent *last_e= &info[i].exts[n - 1];
            off_t end_last= end_l(last_e);
            if (end_last > sb.st_size) // truncate last extent to file size
                last_e->len -= (end_last - sb.st_size);
        }
        close(fd);
    }
    extents= new_list(-(int)n_ext);
    for (unsigned i= 0; i < nfiles; ++i)
        for (unsigned e= 0; e < info[i].n_exts; ++e)
            append(extents, &info[i].exts[e]);
}

/*
 * determine extent sharing -- find_shares()
 *
 * This algorithm takes a single list of all extents (variable: extents), and sorts it by physical address.
 * It then works through the list, comparing the current sh_ext (expressed in terms of its components) with the
 * next extent.  This can either (a) cause the current sh_ext to be finished (if it completely precedes the next extent),
 * (b) if the current sh_ext overlaps the next extent then it is split into the part which precedes it and the remainder,
 * (c) if the current sh_ext begins at the same offset as the next extent but is shorter, the next extent is split and
 * the first part merged into the current sh_ext, or (d) the current sh_ext and next extent are the same, and the next
 * is merged into the current.
 */

// components of the current shared extent being processed
static off_t start, len, end;
static list *owners; // list of extent*
static extent *cur_e;

static void append_owner(extent *e) {
    append(owners, e);
}

static void new_owner(extent *e) {
    owners= new_list(-4);
    append_owner(e);
}

// next extent under consideration
static unsigned ei;
static extent *nxt_e; // = get(extents, ei), or NULL if at end

static void next_extent() {
    nxt_e = ++ei < n_elems(extents) ? get(extents, ei) : NULL;
}

static void begin_next() {
    cur_e= get(extents, ei);
    new_owner(cur_e);
    start= cur_e->p;
    len= cur_e->len;
    end= start + len;
    next_extent();
}

static sh_ext *new_sh_ext() {
    sh_ext *res= malloc_s(sizeof(sh_ext));
    res->p= start;
    res->len= len;
    res->owners= owners;
    return res;
}

static void add_to_shared(sh_ext *s) {
  append(shared, s);
  unsigned n= n_elems(shared);
  if (n > max_n_shared) max_n_shared= n;
}

static void add_to_unshared(sh_ext *sh) {
    if (is_singleton(sh->owners)) {
        extent *owner = only(sh->owners);
        append(owner->info->unsh, sh);
    }
}

static void process_current() {
    assert(!is_empty(owners));
    sh_ext *s= new_sh_ext();
    bool is_sing= is_singleton(owners);
    if (is_sing || (cmp_output && skip1 != skip2))
        add_to_unshared(s);
    if (!is_sing)
        add_to_shared(s);
    if (ei < n_elems(extents)) begin_next();
}

static void swap_e(extent **a, extent **b) { extent *t= *a; *b= *a; *b= t; }

// add a new extent to the list in the right place
static void insert(extent *e) {
    unsigned i, n= n_elems(extents);
    for (i= ei; i < n && extent_list_cmp_phys(&e, (extent **) &GET(extents, i)) > 0; ++i)
        ;
    if (i == n) append(extents, e);
    else {
        extent *lst= last(extents);
        memmove(&GET(extents, i + 1), &GET(extents, i), (n - i - 1) * sizeof(extent *));
        put(extents, i, e);
        append(extents, lst);
    }
}

// the extent at ei has changed; move it to the right place to maintain sort order
static void re_sort() {
    extent **a, **b;
    for (unsigned i= ei;
         i < n_elems(extents) - 1
         && (a= (extent **) &GET(extents, i), b= (extent **) &GET(extents, i + 1), extent_list_cmp_phys(a, b) > 0);
         ++i)
        swap_e(a, b);
}

static extent *new_extent(fileinfo *pfi, off_t l, off_t p, off_t len, unsigned flags) {
    extent *res= malloc_s(sizeof(extent));
    res->info=    pfi;
    res->l=         l;
    res->p=         p;
    res->len=     len;
    res->flags= flags;
    return res;
}

static void find_shares() {
    shared= new_list(-10); // SWAG
    if (n_ext == 0) return;
    ei= 0; begin_next();
    while (nxt_e != NULL) {
        off_t start_nxt= nxt_e->p;
        //printf("%ld %ld  %ld %ld  ", start, len, start_nxt, nxt_e->len);
        if (start < start_nxt) {
            if (end > start_nxt) {
                //puts("overlap");
                len= start_nxt - start;
                off_t tail_len= end - start_nxt;
                ITER(owners, extent*, owner, {
                    extent *e= new_extent(owner->info, owner->l + len, start_nxt, tail_len, owner->flags);
                    insert(e);
                })
            } //else puts("precedes");
            process_current();
        } else { // same start
            append_owner(nxt_e);
            off_t len_nxt= nxt_e->len;
            if (len < len_nxt) {
                //puts("shorter");
                len_nxt -= len;
                nxt_e->l += len;
                nxt_e->p += len;
                nxt_e->len= len_nxt;
                re_sort();
                nxt_e= get(extents, ei);
            } else { // same len
                //puts("same");
                next_extent();
            }
        }
    }
    process_current();
}

/*
 * Generating indices for cmp(1) -- generate_cmp_output()
 *
 * Walks through a pair of files, one extent at a time (in logical order).  Reports regions which may differ;
 * suppresses the report when two regions share a physical extent.  Can be directed to start at any offset in either file
 * using -i, and to limit the size of the region being compared (-b).
 */

typedef struct ecmp ecmp;
struct ecmp {
    list *lst; // list of extent*
    extent *e;
    off_t skip;
    unsigned i;
    bool at_end;
} f1, f2;

static void swap() { ecmp tmp= f1; f1= f2; f2= tmp; }

static bool advance(ecmp *ec) {
    if (ec->i < n_elems(ec->lst)) {
        extent *e= get(ec->lst, ec->i++);
        ec->e= e;
        off_t max_off= ec->skip + max_cmp;
        off_t l= e->l;
        if (l >= max_off)
            ec->at_end= true;
        else {
            ec->at_end= false;
            if (l + e->len > max_off)
                e->len= max_off - l;
            e->l -= ec->skip;
        }
    } else
        ec->at_end= true;
    return !ec->at_end;
}

static void init(ecmp *ec, fileinfo *info, off_t skip) {
    ec->lst= new_list((int)info->n_exts);
    for (unsigned i= 0; i < info->n_exts; ++i)
        append(ec->lst, &info->exts[i]);
    ec->skip= skip;
    ec->i= 0;
    while (advance(ec) && end_l(ec->e) <= 0)
        ;
    off_t l;
    if (!ec->at_end && (l= ec->e->l, l < 0)) { // trim off before skip
        ec->e->l= 0;
        ec->e->len += l;
        ec->e->p -= l;
    }
}

static off_t last_start= -1, last_len; // used to merge contiguous regions

static void print_last() {
    if (last_start >= 0) printf("%"OFF_T " %" OFF_T " %" OFF_T "\n", last_start + skip1, last_start + skip2, last_len);
}

static void report(off_t len) {
    off_t start1= f1.e->l;
    if (last_start >= 0 && last_start + last_len == start1)
        last_len += len;
    else {
        print_last();
        last_start = start1;
        last_len = len;
    }
}

// trunc at max_cmp
static void generate_cmp_output() {
    if (max_cmp < 0) {
        off_t size1= info[0].size - skip1, size2= info[1].size - skip2;
        max_cmp= size1 > size2 ? size1 : size2;
    }
    init(&f1, &info[0], skip1);
    init(&f2, &info[1], skip2);
    while (!f1.at_end && !f2.at_end) {
        if (f1.e->l > f2.e->l) swap();
        if (end_l(f1.e) <= f2.e->l) {
            report(f1.e->len);
            if (!advance(&f1)) break;
        } else if (f1.e->l < f2.e->l) {
            off_t head= f2.e->l - f1.e->l;
            report(head);
            f1.e->l= f2.e->l;
            f1.e->p += head;
            f1.e->len -= head;
        } else { // same start
            if (f1.e->len > f2.e->len) swap();
            if (f1.e->len < f2.e->len) {
                if (f1.e->p != f2.e->p) report(f1.e->len);
                f2.e->l= end_l(f1.e);
                f2.e->p += f1.e->len;
                f2.e->len -= f1.e->len;
                if (!advance(&f1)) break;
            } else { // same start and len
                if (f1.e->p != f2.e->p) report(f1.e->len);
                advance(&f1);
                advance(&f2);
                if (f1.at_end || f2.at_end) break;
            }
        }
    }
    if (f1.at_end) f1= f2;
    while (!f1.at_end) {
        report(f1.e->len);
        advance(&f1);
    }
    print_last();
}

int main(int argc, char *argv[]) {
    args(argc, argv);
    read_ext(&argv[optind]);
    if (print_extents_only)
        print_extents_by_file();
    else if (cmp_output)
        generate_cmp_output();
    else {
        phys_sort();
        find_shares();
        ITER(shared, sh_ext*, sh_e, fileno_sort(((sh_ext *)sh_e)->owners))
        log_sort(shared);
        if (!print_unshared_only && !is_empty(shared)) {
	  print_shared_extents();
	  putchar('\n');
	} else if (no_headers)
	  putchar('\n');
        if (!print_shared_only) print_unshared_extents();
    }
    return 0;
}
