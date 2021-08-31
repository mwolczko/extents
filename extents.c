/*
  extents : print extent information for files

  Mario Wolczko, Aug 2021

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

static list *extents; // list of extent*

static bool
  print_flags        = true,
  print_shared_only  = false,
  print_unshared_only= false,
  no_headers         = false,
  print_phys_addr    = false,
  cmp_output         = false;

static list *shared; // list of sh_ext*

#define ITER(es, elem, stmt)		          \
  for (unsigned _i= 0; _i < (es)->nelems; ++_i) { \
    extent *elem= get((es), _i);	          \
    do { stmt; } while (0);		          \
  }

#ifdef linux
#define OFF_T "%ld"
#else
#define OFF_T "%lld"
#endif

#define USAGE "usage: %s [--bytes LIMIT] [-c|--cmp] [-f|flags] [-h|--help] [[-i|--ignore-initial] SKIP1[:SKIP2]] [-n|--no_headers]\n  [-p|--print_phys_addr] [[-s|--print_shared_only]|[-u|--print_unshared_only]] FILE1 ...\n"

static void usage(char *p) { fail(USAGE, p); }

static void print_help(char *progname)
{
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
  printf("--bytes LIMIT                      compare at most LIMIT bytes (must be used with -c)\n");
  printf("-c --cmp                           (two files only) output unshared regions to be compared by cmp\n");
  printf("-f --flags                         don't print (1 file) or print (>1 file) OS-specific flags\n");
  printf("-h --help                          print help (this message)\n");
  printf("-i --ignore-initial SKIP1[:SKIP2}  skip first SKIP1 bytes of file1 (optionally, SKIP2 of file2) -- must be used with -c\n");
  printf("-n --no_headers                    don't print human-readable headers and line numbers\n");
  printf("-p --print_phys_addr               print physical address of extents\n");
  printf("-s --print_shared_only             print only shared extents (>1 file)\n");
  printf("-u --print_unshared_only           print only unshared extents (>1 file)\n");
  printf("\nMario Wolczko, Oracle, Aug 2021\n");
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
          { "print_phys_addr",      no_argument, NULL, 'p' },
          { "print_shared_only",    no_argument, NULL, 's' },
          { "print_unshared_only",  no_argument, NULL, 'u' },
  };
  for (int c; c= getopt_long(argc, argv, "cfhnpsub:i:", longopts, NULL), c != -1; )
    switch (c) {
    case 'b':
      if (sscanf(optarg, OFF_T, &max_cmp) != 1 || max_cmp <= 0)
	fail("arg to -b|--bytes must be positive integer\n");
      break;
    case 'i':
      if (sscanf(optarg, OFF_T ":" OFF_T, &skip1, &skip2) != 2) {
	if (sscanf(optarg, OFF_T, &skip1) == 1) {
	  skip2= skip1;
	} else
	  skip1= -1;
      }
      if (skip1 < 0 || skip2 < 0)
	fail("arg to -i must be N or N:M (N,M non-negative integers)\n");
      break;
    case 'c': cmp_output=          true; break;
    case 'f': print_flags=        false; break;
    case 'h': print_help(argv[0])      ; break;
    case 'n': no_headers=          true; break;
    case 'p': print_phys_addr=     true; break;
    case 's': print_shared_only=   true; break;
    case 'u': print_unshared_only= true; break;
    default: usage(argv[0]);
    }
  nfiles= (unsigned)(argc - optind);
  if (nfiles < 1) usage(argv[0]);
  if (print_shared_only && print_unshared_only)
    fail("Must choose only one of -s (--print_shared_only) and -u (--print_unshared_only)\n");
  if (cmp_output && nfiles != 2)
    fail("Must have two files with -c (--cmp_output)\n");
  if (nfiles > 1)
    print_flags= !print_flags;
}

static unsigned n_elems(list *ps) { return ps->nelems; }

#define GET(ps, i) ((ps)->elems[i])

static void *get(list *ps, unsigned i) { assert(n_elems(ps) > i); return GET(ps, i); }

static void put(list *ps, unsigned i, void *e) { assert(n_elems(ps) > i); GET(ps, i)= e; }

static bool is_empty(list *ps) { return n_elems(ps) == 0; }

static bool is_singleton(list *ps) { return n_elems(ps) == 1; }

static bool is_multiple(list *ps) { return n_elems(ps) > 1; }

static void *first(list *ps) { assert(!is_empty(ps)); return GET(ps, 0); }

static void *only(list *ps) { assert(is_singleton(ps)); return first(ps); }

static void *last(list *ps) { return GET(ps, n_elems(ps) - 1); }

static off_t end_l(extent *e) { return e->l + e->len; }

static off_t end_p(extent *e) { return e->p + e->len; }

// -ve max_sz means growable, abs value is initial size
static list *new_list(int max_sz) {
  assert(max_sz != 0);
  unsigned max_sz_abs = max_sz < 0 ? -max_sz : max_sz;
  list *ps= malloc_s(sizeof(list) + max_sz_abs * sizeof(void *));
  ps->nelems= 0;
  ps->max_sz= max_sz;
  return ps;
}

static list *append(list *ps, void *e) {
  assert(ps->max_sz < 0 || n_elems(ps) < ps->max_sz);
  if (ps->max_sz < 0 && n_elems(ps) == -ps->max_sz) {
    ps->max_sz *= 2;
    ps= realloc(ps, sizeof(list) - ps->max_sz * sizeof(void *));
  }
  put(ps, ps->nelems++, e);
  return ps;
}

#define LINENO_FMT "%-6"
#define FIELD_WIDTH 15
#define FIELD_WIDTH_S "15"
#define FILENO_WIDTH 4
#define FILENO_WIDTH_S "4"
#define SEP "  "

static void print_extent(extent *e)
{
  char filenobuf[10];
  off_t log= e->l, ph= e->p, len= e->len;
  char *fileno= nfiles == 1 ? ""
    : (sprintf(filenobuf, no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", e->info->argno+1),
       filenobuf);
  if (no_headers) {
    char *fmt= print_phys_addr ? "%s%ld %ld %ld" : "%s%ld %4$ld";
    printf(fmt, fileno, log, ph, len);
  } else {
    char *fmt= print_phys_addr
      ? "%s%" FIELD_WIDTH_S "ld %" FIELD_WIDTH_S "ld %" FIELD_WIDTH_S "ld "
      : "%s%" FIELD_WIDTH_S "ld %4$" FIELD_WIDTH_S "ld ";
      printf(fmt, fileno, log, ph, len);
  }
}

static void print_sh_ext(off_t p, off_t len, extent *owner)
{
  char filenobuf[10];
  off_t l= p - owner->p + owner->l;
  char *fileno= nfiles == 1 ? ""
    : (sprintf(filenobuf, no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", owner->info->argno+1),
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
  if (print_flags && f)
    fputs("  Flags", stdout);
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

static char *flag_pr(unsigned flags)
{
  flags2str(flags, flagbuf, sizeof(flagbuf));
  return flagbuf;
}

static void print_extents_by_file()
{
  for (unsigned i= 0; i < nfiles; i++) {
    if (!no_headers) print_header(i, "#");
    for (unsigned e= 0; e < info[i].n_exts; ++e) {
      extent *ext= &info[i].exts[e];
      if (!no_headers) printf("%" FILENO_WIDTH_S "d ", e+1);
      print_extent(ext);
      if (print_flags) printf("  %s", flag_pr(ext->flags));
      putchar('\n');
    }
  }
}

static unsigned max_n_shared= 0;

static void print_shared_extents()
{
  if (!no_headers) {
    puts("Shared: ");
    printf(LINENO_FMT "s ", "");
    for (unsigned i= 0; i < max_n_shared; ++i) {
      print_header1("File", false); fputs(SEP, stdout);
    }
    putchar('\n');
    printf(LINENO_FMT "s ", "");
    for (unsigned i= 0; i < max_n_shared; ++i) {
      print_header2(); fputs(SEP, stdout);
    }
    putchar('\n');
  }
  unsigned e= 1;
  ITER(shared, sh_elem, {
      sh_ext *s_e= (sh_ext*) sh_elem;
      if (!no_headers) printf(LINENO_FMT "d ", e++);
      ITER((list*)s_e->owners, owner, {
	  print_sh_ext(s_e->p, s_e->len, owner); fputs(SEP, stdout);
	});
      putchar('\n');
      if (print_flags) {
	printf(LINENO_FMT "s ", "FLAGS");
	ITER((list*)s_e->owners, owner, {
	    printf("%-*s", FILENO_WIDTH
		   + FIELD_WIDTH * (print_phys_addr ? 3 : 2)
		   + (print_phys_addr ? 6 : 5),
		   flag_pr(owner->flags)); 
	  });
	putchar('\n');
      }
    })
}

static void print_unshared_extents()
{
  putchar('\n');
  if (!no_headers)
    puts("Not Shared:");
  for (unsigned i= 0; i < nfiles; i++) {
    if (!is_empty(info[i].unsh)) {
      if (!no_headers) print_header(i, "File");
      ITER(info[i].unsh, ext, {
	  print_extent(ext); putchar('\n');
	})
    }
  }
}

// ???
#ifdef DEBUG
void print_set(list *ps)
{
  for (int i= 0; i < n_elems(ps); ++i) {
    print_extent(get(ps, i)); fputs("  ", stdout);
  }
}
#endif

static void read_ext(char *fn[])
{
  info= calloc_s(nfiles, sizeof(fileinfo));
  for (unsigned i= 0; i < nfiles; ++i) {
    char *name= fn[i];
    int fd= open(name, O_RDONLY);
    if (fd < 0)
      fail("Can't open file %s : %s\n", name, strerror(errno));
    info[i].name=       name;
    info[i].fd= (unsigned)fd;
    info[i].argno=         i;
    info[i].exts=       NULL;
    info[i].unsh= new_list(-4); // ???;
    struct stat sb;
    if (fstat(fd, &sb) < 0)
      fail("Can't stat %s : %s\n", name, strerror(errno));
    if ((sb.st_mode & S_IFMT) != S_IFREG)
      fail("%s: Not a regular file\n", name);
    if (i == 0) {
      device= sb.st_dev; blk_sz= sb.st_blksize;
    } else if (blk_sz != sb.st_blksize)
      fail("block size weirdness! %d v %d\n", blk_sz, sb.st_blksize);
    else if (device != sb.st_dev)
      fail("Error: All files must be on the same filesystem!\n");
    info[i].size= sb.st_size;
    get_extents(&info[i]); 
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
  extents= new_list(-n_ext);
  for (unsigned i= 0; i < nfiles; ++i) 
    for (unsigned e= 0; e < info[i].n_exts; ++e)
      append(extents, &info[i].exts[e]);
}

static int ext_cmp_phys(extent **pa, extent **pb)
{
  extent *a= *pa, *b= *pb;
  return
      a->p > b->p ? 1
    : a->p < b->p ? -1
    : a->len > b->len ? 1
    : a->len < b->len ? -1
    : 0;
}

#ifndef linux
typedef int (* _Nonnull __compar_fn_t)(const void *, const void *);
#endif

static void phys_sort()
{
  qsort(&GET(extents, 0), n_ext, sizeof(extent*), (__compar_fn_t)&ext_cmp_phys);
}

static void append_unshared(extent *e)
{
  append(e->info->unsh, e);
}

// the first extent from each file
/*list *all() {
  list *ps= new_list((int)nfiles);
  for (unsigned i= 0; i < nfiles; ++i)
    if (info[i].n_exts > 0)
      ps= append(ps, info[i].exts[0]);
  return ps;
}

typedef bool (*filter_t)(extent *);

list *filter(list *ps, filter_t f)
{
  list *res = new_list((int)nfiles);
  ITER(ps, elem, 
      if ((*f)(elem)) {
	res= append(res, elem);
      });
  return res;
}

off_t minp;
bool min_p(extent *e)
{
  if (e->p <= minp) {
    minp= e->p;
    return true;
  } else
    return false;
}

list *find_lowest_p(list *a)
{
  if (is_empty(a)) return a;
  minp= 0x7fffffffffffffffL;
  list *min1= filter(a, min_p);
  assert(!is_empty(min1));
  list *min= filter(min1, min_p);
  assert(!is_empty(min));
  return min;
}

off_t minlen;
bool min_len(extent *e)
{
  if (e->len <= minlen) {
    minlen= e->len;
    return true;
  } else
    return false;
}

list *find_shortest(list *a)
{
  if (is_empty(a)) return a;
  minlen= 0x7fffffffffffffffL;
  list *min1= filter(a, min_len);
  assert(!is_empty(min1));
  list *min= filter(min1, min_len);
  assert(!is_empty(min));
  return min;
}

list *remove_elem(list *ps, extent *e)
{
  list *new= new_list((int)nfiles);
  ITER(ps, elem, if (elem != e) new= append(new, elem););
  assert(n_elems(new) == n_elems(ps) - 1);
  return new;
}

list *move_next(list *ps, extent *e)
{
  list *del= remove_elem(ps, e);
  fileinfo *info= e->info;
  unsigned n;
  for (n= 0; n < info->n_exts; ++n)
    if (info->exts[n] == e)
      break;
  if (n < info->n_exts - 1)
    del= append(del, info->exts[n+1]);
  return del;
}

extent *split(extent *e, off_t end)
{
  assert(end > e->p && end < end_p(e));
  extent *head= malloc_s(sizeof(extent));
  head->info= e->info;
  head->l= e->l;
  head->p= e->p;
  head->len= end - e->p;
  head->flags= e->flags; // ? make no sense for, eg, LAST
  head->nxt_sh= NULL;
  e->p= end;
  e->l += head->len;
  e->len -= head->len;
  return head;
}

#ifdef DEBUG
#define DBG_PRINT(m, e) {puts(m); print_extent(e); putchar('\n');}
#define DBG_PRINTS(m, ps) {puts(m); print_set(ps); putchar('\n');}
#else
#define DBG_PRINT(m, e)
#define DBG_PRINTS(m, ps)
#endif
*/

static int ext_cmp_fileno(extent **a, extent **b)
{
  return (*a)->info->argno > (*b)->info->argno ? 1
    : (*a)->info->argno < (*b)->info->argno ? -1 : 0;
}

static void fileno_sort(list *ps)
{
  qsort(ps->elems, ps->nelems, sizeof(void *), (__compar_fn_t)&ext_cmp_fileno);
}

// components of the current shared extent being processed
static off_t start, len, end;
static list *share; // list of extent*
static extent *cur_e;

// next extent under consideration
static unsigned ei;
static extent *nxt_e;

static void append_share(extent *e)
{
  append(share, e);
}

static void new_share(extent *e)
{
  share= new_list(-4);
  append_share(e);
}

static void begin_next_e()
{
  cur_e= get(extents, ei++);
  new_share(cur_e);
  start= cur_e->p;  len= cur_e->len;  end= start + len;
  if (ei < n_elems(extents))
    nxt_e= get(extents, ei);
  else
    nxt_e= NULL;
}

static sh_ext *new_sh_ext(off_t p, off_t len)
{
  sh_ext *res= malloc_s(sizeof(sh_ext));
  res->p=          p;
  res->len=      len;
  res->owners= share;
  return res;
}

static void advance_e(off_t start, off_t len)
{
  assert(!is_empty(share));
  if (is_singleton(share)) 
    append_unshared(only(share));
  else {
    sh_ext *ps= new_sh_ext(start, len);
    append(shared, ps);
  }
  if (ei < n_elems(extents)) begin_next_e();
}

static void swap_e(extent **a, extent **b) { extent *t= *a; *b= *a; *b= t; }

// add a new extent to the list in the right place
static void insert(extent *e)
{
  unsigned i, n= n_elems(extents);
  for (i= ei; i < n && ext_cmp_phys(&e, (extent**)&GET(extents, i)) > 0; ++i)
    ;
  if (i == n) 
    append(extents, e);
  else {
    extent *lst= get(extents, n);
    memmove(&GET(extents, i+1), &GET(extents, i), n - i);
    put(extents, i, e);
    append(extents, lst);
  }
}

// the extent at ei has changed; move it to the right place to maintain sort order
static void re_sort()
{
  for (unsigned i= ei; i < n_elems(extents) - 1 && ext_cmp_phys((extent**)&GET(extents, i), (extent**)&GET(extents, i+1)) > 0; ++i)
    swap_e((extent**)&GET(extents, i), (extent**)&GET(extents, i+1));
}

static extent *new_extent(fileinfo *pfi, off_t l, off_t p, off_t len, unsigned flags)
{
  extent *res= malloc_s(sizeof(extent));
  res->info=    pfi;
  res->l=         l;
  res->p=         p; 
  res->len=     len;
  res->flags= flags;
  return res;
}

static void find_shares()
{
  shared= new_list(-10); // ?? size
  if (n_ext == 0) return;
  ei= 0;
  begin_next_e();
  while (nxt_e != NULL) {
    off_t start_nxt= nxt_e->p;
    if (start < start_nxt) {
      if (end > start_nxt) {
	len= start_nxt - start;
	off_t tail_len= end - start_nxt;
	ITER(share, owner, {
	    extent *e= new_extent(owner->info, owner->l + len, start_nxt, tail_len, owner->flags);
	    insert(e);
	  })
      }
      advance_e(start, len);
    } else {
      off_t len_nxt= nxt_e->len;
      if (len < len_nxt) {
	off_t tail_len= len_nxt - len;
	nxt_e->l += len;  nxt_e->p += len;  nxt_e->len= tail_len; re_sort();
      }
      append_share(get(extents, ei));
      begin_next_e();
    }
  }
  advance_e(start, len);
}

static int ext_list_cmp_log(list **a, list **b)
{
  extent *fa= first(*a), *fb = first(*b);
  return fa->l > fb->l ? 1 : fa->l < fb->l ? -1 : 0;
}

static void log_sort(list *ll)
{
  qsort(ll->elems, ll->nelems, sizeof(void *), (__compar_fn_t)&ext_list_cmp_log);
}

struct ecmp {
  list *lst;
  extent e;
  off_t skip;
  unsigned i;
  bool at_end;
} f1, f2;

static void swap() { struct ecmp tmp= f1; f1= f2; f2= tmp; }

static bool advance(struct ecmp *ec)
{
  if (ec->i < n_elems(ec->lst)) {
    ec->e= *(extent *)get(ec->lst, ec->i++);
    off_t max_off= ec->skip + max_cmp;
    if (ec->e.l >= max_off)
      ec->at_end= true;
    else {
      ec->at_end= false;
      if (end_l(&ec->e) > max_off)
	ec->e.len= max_off - ec->e.l;
    }
  } else
    ec->at_end= true;
  return !ec->at_end;
}

static void init(struct ecmp *ec, list *l, off_t skip, off_t size)
{
  ec->lst= l; ec->skip= skip; ec->i= 0;
  while (advance(ec) && end_l(&ec->e) <= ec->skip) 
    ;
  if (!ec->at_end && ec->e.l < ec->skip) { // trim off before skip
    off_t head= ec->skip - ec->e.l;
    ec->e.l += head;
    ec->e.len -= head;
  }
}

static void report(off_t strt, off_t ln)
{
  printf(OFF_T " " OFF_T "\n", strt, ln);
}

// trunc at max_cmp
static void generate_cmp_output()
{
  if (max_cmp < 0) {
      off_t size1= info[0].size - skip1, size2= info[1].size - skip2;     
      max_cmp= size1 > size2 ? size1 : size2;
  }
  init(&f1, info[0].unsh, skip1, info[0].size);
  init(&f2, info[1].unsh, skip2, info[1].size);
  while (!f1.at_end && !f2.at_end) {
    if (f1.e.l > f2.e.l) swap();
    if (end_l(&f1.e) <= f2.e.l) { 
      report(f1.e.l, f1.e.len);
      if (!advance(&f1)) break;
    } else if (f1.e.l < f2.e.l) { 
      off_t head= f2.e.l - f1.e.l;
      report(f1.e.l, head);
      f1.e.l= f2.e.l; f1.e.len -= head;
    } else {
      if (f1.e.len > f2.e.len) swap();
      if (f1.e.len < f2.e.len) {
        report(f1.e.l, f1.e.len);
        f2.e.l= end_l(&f1.e); f2.e.len -= f1.e.len;
        if (!advance(&f1)) break;
      } else { // same start and len
        report(f1.e.l, f1.e.len);
	advance(&f1); advance(&f2);
	if (f1.at_end || f2.at_end) break;
      }
    }
  }
  if (f1.at_end) f1= f2;
  while (!f1.at_end) {
    report(f1.e.l, f1.e.len);
    advance(&f1);
  }
}

int main(int argc, char *argv[])
{
  args(argc, argv);
  read_ext(&argv[optind]);
  print_extents_by_file();
  phys_sort();
  find_shares();
  ITER(shared, sh_list, fileno_sort((list*)sh_list))
  log_sort(shared);
  if (cmp_output) 
    generate_cmp_output();
  else {
    if (!print_unshared_only) print_shared_extents();
    if (!print_shared_only) print_unshared_extents();
  }
  return 0;
}
