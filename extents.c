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

int block_size, nfiles, n_ext= 0;

fileinfo *info; // ptr to array of files' info of size nfiles

bool print_flags        = true,
     print_shared_only  = false,
     print_unshared_only= false,
     no_headers         = false,
     print_phys_addr    = false,
     cmp_output         = false;

extents *shared; // one per ring of shared extents

#define ITER(es, elem, stmt)		     \
  for (int _i= 0; _i < (es)->nelems; ++_i) { \
    extent *elem= get((es), _i);	     \
    do { stmt; } while (0);		     \
  }

#define USAGE "usage: %s [-c|--cmp] [-f|--no_flags] [-h|--help] [-p|--print_phys_addr] [[-s|--print_shared_only]|[-u|--print_unshared_only]] FILE1 FILE2 ...\n"

void usage(char *p) {
  fail(USAGE, p);
}

void print_help(char *progname)
{
  printf("%s: print extent information for files\n\n", progname);
  printf(USAGE, progname);
  printf("\nWith a single FILE, prints information about each extent.\n");
  printf("For multiple FILEs, determines which extents are shared and prints information about shared and unshared extents.\n");
  printf("An extent is described by:\n");
  printf("  n if it belongs to FILEn (omitted for only a single file);\n");
  printf("  The logical offset in the file at which it begins;\n");
  printf("  The physical offset on the underlying device at which it begins (if -p or --print_phys_addr is specified);\n");
  printf("  Length.\nOffsets and length are in bytes.\n");
  printf("OS-specific flags are also printed (unless suppressed with -f or --no_flags). Flags are available only on Linux and are described in /usr/include/linux/fiemap.h.\n\n");
  printf("Options:\n");
  printf("-c --cmp                  (two files only) output unshared regions to be compared by cmp\n");
  printf("-f --no_flags             don't print OS-specific flags\n");
  printf("-h --help                 print help (this message)\n");
  printf("-n --no_headers           don't print human-readable header and line numbers\n");
  printf("-s --print_shared_only    print only shared extents\n");
  printf("-u --print_unshared_only  print only unshared extents\n");
  printf("\nMario Wolczko, Oracle, Aug 2021\n");
  exit(0);
}

void args(int argc, char *argv[])
{
  struct option longopts[]= { { "cmp_output",          false, NULL, 'c' },
			      { "no_flags",            false, NULL, 'f' },
			      { "help",                false, NULL, 'h' },
			      { "no_headers",          false, NULL, 'n' },
			      { "print_phys_addr",     false, NULL, 'p'},
			      { "print_shared_only",   false, NULL, 's' },
			      { "print_unshared_only", false, NULL, 'u' },
  };
  for (int c; c= getopt_long(argc, argv, "cfhnpsu", longopts, NULL), c != -1; )
    switch (c) {
    case 'c': cmp_output=          true; break;
    case 'f': print_flags=        false; break;
    case 'h': print_help(argv[0])      ; break;
    case 'n': no_headers=          true; break;
    case 'p': print_phys_addr=     true; break;
    case 's': print_shared_only=   true; break;
    case 'u': print_unshared_only= true; break;
    default: usage(argv[0]);
    }
  nfiles= argc - optind;
  if (nfiles < 1) usage(argv[0]);
  if (print_shared_only && print_unshared_only)
    fail("Must choose only one of -s (--print_shared_only) and -u (--print_unshared_only)\n");
  if (cmp_output && nfiles != 2)
    fail("Must have two files with -c (--cmp_output)\n");
  if (nfiles > 1)
    print_flags= !print_flags;
}

unsigned n_elems(extents *ps) { return ps->nelems; }

extent *get(extents *ps, int i) { assert(n_elems(ps) > i); return ps->elems[i]; }

void put(extents *ps, int i, extent *e) { assert(n_elems(ps) > i); ps->elems[i]= e; }

bool is_empty(extents *s) { return n_elems(s) == 0; }

bool is_singleton(extents *s) { return n_elems(s) == 1; }

bool is_multiple(extents *s) { return n_elems(s) > 1; }

extent *first(extents *s) { assert(!is_empty(s)); return s->elems[0]; }

extent *only(extents *s) { assert(is_singleton(s)); return first(s); }

extent *last(extents *s) { return s->elems[n_elems(s) - 1]; }

off_t end_p(extent *e) { return e->p + e->len; }

// -ve max_sz means growable, abs value is initial size
extents *new_exts(int max_sz) {
  unsigned max_sz_abs = max_sz < 0 ? -max_sz : max_sz;
  extents *ps= malloc_s(sizeof(extents) + max_sz_abs * sizeof(extent *));
  ps->nelems= 0;
  ps->max_sz= max_sz;
  return ps;
}

#define LINENO_FMT "%-6"
#define FIELD_WIDTH 15
#define FIELD_WIDTH_S "15"
#define FILENO_WIDTH 4
#define FILENO_WIDTH_S "4"
#define SEP "  "

void print(extent *e)
{
  char filenobuf[10];
  off_t log= e->l, ph= e->p, len= e->len;
  char *fileno= nfiles == 1 ? "" : (sprintf(filenobuf, no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", e->info->argno+1), filenobuf);
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

void print_flags_for_extent(extent *e)
{
  int flags= e->flags;
  char *fs= flags2str(flags);
  printf("  %s", fs);
}

#define STRING_FIELD_FMT "%" FIELD_WIDTH_S "s"

void print_header1(char *s, bool f) {
  printf("%" FILENO_WIDTH_S "s " STRING_FIELD_FMT " ", s ? s : "File", "Logical");
  if (print_phys_addr) printf(STRING_FIELD_FMT " ", "Physical");
  printf(STRING_FIELD_FMT " ", "Length");
  if (print_flags && f)
    fputs("  Flags", stdout);
}

void print_header2() {
  printf("%" FILENO_WIDTH_S "s " STRING_FIELD_FMT " ", "", "Offset");
  if (print_phys_addr) printf(STRING_FIELD_FMT " ", "Offset");
  printf(STRING_FIELD_FMT " ", "");
}

void print_header(int i, char *h) {
  puts(info[i].name); 
  print_header1(h, true);
  putchar('\n');
  print_header2();
  putchar('\n');
}

void print_extents_by_file()
{
  for (int i= 0; i < nfiles; i++) {
    if (!no_headers) print_header(i, "#");
    for (int e= 0; e < info[i].n_exts; ++e) {
      extent *ext= info[i].exts[e];
      if (ext->nxt_sh == NULL) {
	if (!no_headers)
	  printf("%" FILENO_WIDTH_S "d ", e+1);
	print(ext);
	if (print_flags) print_flags_for_extent(ext);
	putchar('\n');
      }
    }
  }
}

void print_shared_extent(int lineno, extent *e)
{
  extent *n= e;
  if (!no_headers) printf(LINENO_FMT "d ", lineno);
  do {
    assert(n != NULL);
    print(n); fputs(SEP, stdout);
    n= n->nxt_sh;
  } while (n != e);
}

unsigned max_n_shared= 0;

void print_shared_extents()
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
  int e= 1;
  ITER(shared, sh, {
      print_shared_extent(e++, sh); putchar('\n');
      if (print_flags) {
	printf(LINENO_FMT "s ", "FLAGS");
	for (unsigned i= 0; i < max_n_shared; ++i) {
	  printf("%-*s", FILENO_WIDTH + FIELD_WIDTH * (print_phys_addr ? 3 : 2) + (print_phys_addr ? 6 : 5), flags2str(sh->flags)); 
	}
	putchar('\n');
      }
    });
}

void print_unshared_extents()
{
  if (no_headers)
    putchar('\n');
  else
    puts("\nNot Shared:");
  for (int i= 0; i < nfiles; i++) {
    if (!is_empty(info[i].unsh)) {
      if (!no_headers) print_header(i, "File");
      ITER(info[i].unsh, ext, {
	  print(ext); putchar('\n');
	});
    }
  }
}

#ifdef DEBUG
void print_set(extents *ps)
{
  for (int i= 0; i < n_elems(ps); ++i) {
    print(get(ps, i)); fputs("  ", stdout);
  }
}
#endif

void read_ext(char *fn[])
{
  info= calloc_s(nfiles, sizeof(fileinfo));
  for (int i= 0; i < nfiles; ++i) {
    char *name= fn[i];
    int fd= open(name, O_RDONLY);
    if (fd < 0)
      fail("Can't open file %s : %s\n", name, strerror(errno));
    info[i].name= name; info[i].fd= fd; info[i].argno= i;
    struct stat sb;
    if (fstat(fd, &sb) < 0)
      fail("Can't stat %s : %s\n", name, strerror(errno));
    if ((sb.st_mode & S_IFMT) != S_IFREG)
      fail("%s: Not a regular file\n", name);
    if (i == 0)
      block_size= sb.st_blksize;
    else if (block_size != sb.st_blksize)
      fail("block size weirdness! %d v %d\n", block_size, sb.st_blksize);
    info[i].size= sb.st_size;
    get_extents(&info[i]);
    n_ext += info[i].n_exts;
    info[i].unsh= new_exts(-info[i].n_exts);
  }
}

int ext_cmp_phys(extent **a, extent **b)
{
  return (*a)->p > (*b)->p ? 1 : (*a)->p < (*b)->p ? -1 : 0;
}

void phys_sort()
{
  for (int i= 0; i < nfiles; ++i) {
    qsort(info[i].exts, info[i].n_exts, sizeof(extent *), (__compar_fn_t)&ext_cmp_phys);
  }
}

extents *add(extents *ps, extent *e) {
  assert(ps->max_sz < 0 || n_elems(ps) < ps->max_sz);
  if (ps->max_sz < 0 && n_elems(ps) == -ps->max_sz) {
    ps->max_sz *= 2;
    ps= realloc(ps, sizeof(extents) - ps->max_sz * sizeof(extent *));
  }
  put(ps, ps->nelems++, e);
}

void add_unshared(extent *e)
{
  add(e->info->unsh, e);
}

// the first extent from each file
extents *all() {
  extents *ps= new_exts(nfiles);
  for (int i= 0; i < nfiles; ++i)
    if (info[i].n_exts > 0)
      ps= add(ps, info[i].exts[0]);
  return ps;
}

typedef bool (*filter_t)(extent *);

extents *filter(extents *ps, filter_t f)
{
  extents *res = new_exts(nfiles);
  ITER(ps, elem, 
      if ((*f)(elem)) {
	res= add(res, elem);
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

extents *find_lowest_p(extents *a)
{
  if (is_empty(a)) return a;
  minp= 0x7fffffffffffffffL;
  extents *min1= filter(a, min_p);
  assert(!is_empty(min1));
  extents *min= filter(min1, min_p);
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

extents *find_shortest(extents *a)
{
  if (is_empty(a)) return a;
  minlen= 0x7fffffffffffffffL;
  extents *min1= filter(a, min_len);
  assert(!is_empty(min1));
  extents *min= filter(min1, min_len);
  assert(!is_empty(min));
  return min;
}

extents *remove_elem(extents *ps, extent *e)
{
  extents *new= new_exts(nfiles);
  ITER(ps, elem, if (elem != e) new= add(new, elem););
  assert(n_elems(new) == n_elems(ps) - 1);
  return new;
}

extents *move_next(extents *ps, extent *e)
{
  extents *del= remove_elem(ps, e);
  fileinfo *info= e->info;
  int n;
  for (n= 0; n < info->n_exts; ++n)
    if (info->exts[n] == e)
      break;
  if (n < info->n_exts - 1)
    del= add(del, info->exts[n+1]);
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
  head->flags= e->flags; // ?
  head->nxt_sh= NULL;
  e->p= end;
  e->l += head->len;
  e->len -= head->len;
  return head;
}

// check that e is on a ring 
void validate(extent *e)
{
  int i= 0;
  extent *n= e;
  do {
    assert(n != NULL);
    n= n->nxt_sh;
    assert(i <= nfiles);
  } while (n != e);
}

#ifdef DEBUG
#define DBG_PRINT(m, e) {puts(m); print(e); putchar('\n');}
#define DBG_PRINTS(m, ps) {puts(m); print_set(ps); putchar('\n');}
#else
#define DBG_PRINT(m, e)
#define DBG_PRINTS(m, ps)
#endif

void find_shares()
{
  extents *curr= all();
  shared= new_exts(n_ext);
  do {
    DBG_PRINTS("loop head:", curr);
    extents *low= find_lowest_p(curr);
    DBG_PRINTS("low:", low);
    if (is_singleton(low)) {
      extent *lowest= only(low);
      DBG_PRINT("lowest:", lowest);
      extents *allbutlowest= remove_elem(curr, lowest);
      DBG_PRINT("allbutlowest:", allbutlowest);
      if (is_empty(allbutlowest)) { // last one
	lowest->nxt_sh= NULL;
	add_unshared(lowest);
	curr= move_next(curr, lowest);
      } else {
	extents *next_lowest= find_lowest_p(allbutlowest);
	DBG_PRINTS("next lowest:", next_lowest);
	if (end_p(lowest) <= first(next_lowest)->p) { // lowest precedes all others
	  DBG_PRINTS("prec:", low);
	  lowest->nxt_sh= NULL;
	  add_unshared(lowest);
	  curr= move_next(curr, lowest);
	} else {
	  DBG_PRINTS("split:", low);
	  extent *head= split(lowest, first(next_lowest)->p);
	  head->nxt_sh= NULL;
	  add_unshared(head);
	}
      }
    } else {
      // first, take all the ones of the same length
      extents *shortest= find_shortest(low);
      DBG_PRINTS("shortest:", shortest);
      extent *prev= NULL;
      extent *frst= first(shortest);
      DBG_PRINT("first:", frst);
      off_t end= end_p(frst);
      unsigned ring_len= 0;
      ITER(shortest, s, {
	  curr= move_next(curr, s);
	  low= remove_elem(low, s);
	  s->nxt_sh= prev; 
	  prev= s;
	  ring_len++;
	});
      // next, split the longer ones
      DBG_PRINTS("new low:", low);
      ITER(low, l, {
	  extent *head= split(l, end);
	  head->nxt_sh= prev; 
	  prev= head;
	  ring_len++;
	});
      frst->nxt_sh= prev; // close the ring
      shared= add(shared, prev);
      validate(prev);
      if (ring_len > max_n_shared)
	max_n_shared= ring_len;
    }
  } while (!is_empty(curr));
}

int main(int argc, char *argv[])
{
  args(argc, argv);
  
  read_ext(&argv[optind]);

  if (nfiles == 1)
    print_extents_by_file();
  else {
    phys_sort();
    find_shares();
    // sort by logical XXX?
    // sort shared rings by file#?
    // flag printing mess
    if (cmp_output) {
    } else {
      if (!print_unshared_only) print_shared_extents();
      if (!print_shared_only) print_unshared_extents();
    }
  }

  return 0;
}
