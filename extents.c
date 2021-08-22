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

list *shared; // list of list of extents

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
  printf("-f                        don't print (1 file) or print (>1 file) OS-specific flags\n");
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

unsigned n_elems(list *ps) { return ps->nelems; }

void *get(list *ps, int i) { assert(n_elems(ps) > i); return ps->elems[i]; }

void put(list *ps, int i, void *e) { assert(n_elems(ps) > i); ps->elems[i]= e; }

bool is_empty(list *s) { return n_elems(s) == 0; }

bool is_singleton(list *s) { return n_elems(s) == 1; }

bool is_multiple(list *s) { return n_elems(s) > 1; }

void *first(list *s) { assert(!is_empty(s)); return s->elems[0]; }

void *only(list *s) { assert(is_singleton(s)); return first(s); }

void *last(list *s) { return s->elems[n_elems(s) - 1]; }

off_t end_p(extent *e) { return e->p + e->len; }

// -ve max_sz means growable, abs value is initial size
list *new_list(int max_sz) {
  unsigned max_sz_abs = max_sz < 0 ? -max_sz : max_sz;
  list *ps= malloc_s(sizeof(list) + max_sz_abs * sizeof(void *));
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

void print_shared_extent(int lineno, list *l)
{
  if (!no_headers) printf(LINENO_FMT "d ", lineno);
  /*  extent *n= e;
  do {
    assert(n != NULL);
    print(n); fputs(SEP, stdout);
    n= n->nxt_sh;
    } while (n != e);*/
  ITER(l, elem, {
      print(elem); fputs(SEP, stdout);
    });
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
  ITER(shared, sh_list, {
      print_shared_extent(e++, (list *)sh_list); putchar('\n');
      if (print_flags) {
	printf(LINENO_FMT "s ", "FLAGS");
	ITER((list*)sh_list, sh, {
	    printf("%-*s", FILENO_WIDTH
		   + FIELD_WIDTH * (print_phys_addr ? 3 : 2)
		   + (print_phys_addr ? 6 : 5),
		   flags2str(sh->flags)); 
	  });
	/*	for (unsigned i= 0; i < max_n_shared; ++i) {
	  printf("%-*s", FILENO_WIDTH
		 + FIELD_WIDTH * (print_phys_addr ? 3 : 2)
		 + (print_phys_addr ? 6 : 5),
		 flags2str(sh->flags)); 
		 }*/
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
    info[i].unsh= new_list(-info[i].n_exts);
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

list *add(list *ps, void *e) {
  assert(ps->max_sz < 0 || n_elems(ps) < ps->max_sz);
  if (ps->max_sz < 0 && n_elems(ps) == -ps->max_sz) {
    ps->max_sz *= 2;
    ps= realloc(ps, sizeof(list) - ps->max_sz * sizeof(void *));
  }
  put(ps, ps->nelems++, e);
}

void add_unshared(extent *e)
{
  add(e->info->unsh, e);
}

// the first extent from each file
list *all() {
  list *ps= new_list(nfiles);
  for (int i= 0; i < nfiles; ++i)
    if (info[i].n_exts > 0)
      ps= add(ps, info[i].exts[0]);
  return ps;
}

typedef bool (*filter_t)(extent *);

list *filter(list *ps, filter_t f)
{
  list *res = new_list(nfiles);
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
  list *new= new_list(nfiles);
  ITER(ps, elem, if (elem != e) new= add(new, elem););
  assert(n_elems(new) == n_elems(ps) - 1);
  return new;
}

list *move_next(list *ps, extent *e)
{
  list *del= remove_elem(ps, e);
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

int ext_cmp_fileno(extent **a, extent **b)
{
  return (*a)->info->argno > (*b)->info->argno ? 1
    : (*a)->info->argno < (*b)->info->argno ? -1 : 0;
}

void fileno_sort(list *ps)
{
  qsort(ps->elems, ps->nelems, sizeof(void *), (__compar_fn_t)&ext_cmp_fileno);
}

void find_shares()
{
  list *curr= all();
  shared= new_list(n_ext);
  do {
    DBG_PRINTS("loop head:", curr);
    list *low= find_lowest_p(curr);
    DBG_PRINTS("low:", low);
    if (is_singleton(low)) {
      extent *lowest= only(low);
      DBG_PRINT("lowest:", lowest);
      list *allbutlowest= remove_elem(curr, lowest);
      DBG_PRINT("allbutlowest:", allbutlowest);
      if (is_empty(allbutlowest)) { // last one
	lowest->nxt_sh= NULL;
	add_unshared(lowest);
	curr= move_next(curr, lowest);
      } else {
	list *next_lowest= find_lowest_p(allbutlowest);
	DBG_PRINTS("next lowest:", next_lowest);
	extent *second= (extent *)first(next_lowest);
	if (end_p(lowest) <= second->p) { // lowest precedes all others
	  DBG_PRINTS("prec:", low);
	  lowest->nxt_sh= NULL;
	  add_unshared(lowest);
	  curr= move_next(curr, lowest);
	} else {
	  DBG_PRINTS("split:", low);
	  extent *head= split(lowest, second->p);
	  head->nxt_sh= NULL;
	  add_unshared(head);
	}
      }
    } else {
      // first, take all the ones of the same length
      list *shortest= find_shortest(low);
      DBG_PRINTS("shortest:", shortest);
      extent *prev= NULL;
      extent *frst= first(shortest);
      DBG_PRINT("first:", frst);
      off_t end= end_p(frst);
      unsigned ring_len= 0;
      list *share= new_list(nfiles); // list of extent*
      ITER(shortest, s, {
	  curr= move_next(curr, s);
	  low= remove_elem(low, s);
	  s->nxt_sh= prev; 
	  prev= s;
	  add(share, s);
	  ring_len++;
	});
      // next, split the longer ones
      DBG_PRINTS("new low:", low);
      ITER(low, l, {
	  extent *head= split(l, end);
	  head->nxt_sh= prev; 
	  prev= head;
	  add(share, l);
	  ring_len++;
	});
      frst->nxt_sh= prev; // close the ring
      shared= add(shared, share);
      validate(prev);
      
      if (ring_len > max_n_shared)
	max_n_shared= ring_len;
    }
  } while (!is_empty(curr));
}

int ext_list_cmp_log(list **a, list **b)
{
  extent *fa= first(*a), *fb = first(*b);
  return fa->l > fb->l ? 1 : fa->l < fb->l ? -1 : 0;
}

void log_sort(list *ll)
{
  qsort(ll->elems, ll->nelems, sizeof(void *), (__compar_fn_t)&ext_list_cmp_log);
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
    ITER(shared, sh_list, fileno_sort((list*)sh_list););
    log_sort(shared);
    if (cmp_output) {


    } else {
      if (!print_unshared_only) print_shared_extents();
      if (!print_shared_only) print_unshared_extents();
    }
  }

  return 0;
}
