/*
  extents : print extent information for a file (Linux specific)

  For each extent in the file (search for FIEMAP for description), print (in bytes):
  starting logical offset
  starting physical offset
  length
  extent flags (see /usr/include/linux/fiemap.h)

  Options:
  -f don't print flags
  -h don't print human-readable header and line numbers
  -s print only shared extents
  -u print only unshared extents  

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

bool print_flags= true, print_shared= true, print_unshared= true, no_headers= false, print_phys_addr= false; 

extents *shared; // one per ring of shared extents

#define ITER(es, elem, stmt)		     \
  for (int _i= 0; _i < (es)->nelems; ++_i) { \
    extent *elem= get((es), _i);	     \
    do { stmt; } while (0);		     \
  }

unsigned card(extents *ps) { return ps->nelems; }

extent *get(extents *ps, int i) { assert(card(ps) > i); return ps->elems[i]; }

void put(extents *ps, int i, extent *e) { assert(card(ps) > i); ps->elems[i]= e; }

bool is_empty(extents *s) { return card(s) == 0; }

bool is_singleton(extents *s) { return card(s) == 1; }

bool is_multiple(extents *s) { return card(s) > 1; }

extent *first(extents *s) { assert(!is_empty(s)); return s->elems[0]; }

extent *only(extents *s) { assert(is_singleton(s)); return first(s); }

extent *last(extents *s) { return s->elems[card(s) - 1]; }

off_t end_p(extent *e) { return e->p + e->len; }

// -ve max_sz means growable, abs value is initial size
extents *new_exts(int max_sz) {
  unsigned max_sz_abs = max_sz < 0 ? -max_sz : max_sz;
  extents *ps= malloc_s(sizeof(extents) + max_sz_abs * sizeof(extent *));
  ps->nelems= 0;
  ps->max_sz= max_sz;
  return ps;
}

void usage(char *p) {
  fail("usage: %s [-f] [-h] [-p] [-s|-u] FILE1 FILE2 ...\n", p);
}

#define LINENO_FMT "%-6"
#define FIELD_WIDTH "15"

void print(int n, extent *e)
{
  char linenobuf[10], filenobuf[10];
  off_t log= e->l, ph= e->p, len= e->len;
  int flags= e->flags;
  char *filename= nfiles == 1 ? "" : (sprintf(filenobuf, no_headers ? "%d " : "%4d ", e->info->argno), filenobuf);
  if (no_headers) {
    char *fmt= print_phys_addr ? "%s%ld %ld %ld" : "%s%ld %ld";
    printf(fmt, filename, log, ph, len);
  } else {
    char *lineno;
    if (n > 0) {
      sprintf(linenobuf, LINENO_FMT "d ", n);
      lineno= linenobuf;
    } else
      lineno= "";
    char *fmt = print_phys_addr
      ? "%s%s%3$" FIELD_WIDTH "ld %4$" FIELD_WIDTH "ld %5$" FIELD_WIDTH "ld" : "%s%s%3$" FIELD_WIDTH "ld %5$" FIELD_WIDTH "ld";
    printf(fmt, lineno, filename, log, ph, len);
  }
  if (print_flags) {
    char *fs= flags2str(flags);
    printf("   %s", fs);
  }
}

#define STRING_FIELD_FMT "%" FIELD_WIDTH "s"

void print_header(int i) {
  if (i >= 0) puts(info[i].name); 
  printf(LINENO_FMT "s " STRING_FIELD_FMT " ", "No.", "Logical");
  if (print_phys_addr) printf(STRING_FIELD_FMT " ", "Physical");
  printf(STRING_FIELD_FMT " ", "Length");
  if (print_flags)
    fputs("  Flags", stdout);
  printf("\n" LINENO_FMT "s " STRING_FIELD_FMT, "", "Offset");
  if (print_phys_addr) printf(STRING_FIELD_FMT, "Offset");
  putchar('\n');
}

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
    //info[i].exts= get_extents(info[i].fd, sb.st_size, &info[i].n_exts);
    get_extents(&info[i]);
    n_ext += info[i].n_exts;
    info[i].unsh= new_exts(-info[i].n_exts);
  }
}

void print_help()
{
  printf("help\n");
  exit(0);
}

void output_format()
{
}

void do_print_shared(int lineno, extent *e)
{
  int i= 0;
  extent *n= e;
  printf("%d: ", lineno);
  do {
    assert(n != NULL);
    print(0, n); fputs("  ", stdout);
    n= n->nxt_sh;
    assert(i <= nfiles);
  } while (n != e);
}

void output_unshared()
{
  for (int i= 0; i < nfiles; i++) {
    if (!no_headers) print_header(i);
    for (int e= 0; e < info[i].n_exts; ++e) {
      extent *ext= info[i].exts[e];
      if (ext->nxt_sh == NULL) {
	print(e+1, ext); putchar('\n');
      }
    }
  }
}

void output()
{
  if (nfiles > 1) {
    puts("Shared: ");
    if (!no_headers) print_header(-1);
    int e= 1;
    ITER(shared, sh, {
	do_print_shared(e++, sh);
	putchar('\n');
      });
  }
  puts("Not Shared:");
  for (int i= 0; i < nfiles; i++) {
    if (!no_headers) print_header(i);
    ITER(info[i].unsh, ext, {
	print(0, ext); putchar('\n');
      });
  }
}

void args(int argc, char *argv[])
{
  struct option longopts[]= { { "no_flags",        false, NULL, 'f' },
			      { "help",            false, NULL, 'h' },
			      { "no_headers",      false, NULL, 'n' },
			      { "print_phys_addr", false, NULL, 'p'},
  };
  for (int c; c= getopt_long(argc, argv, "fhno:p", longopts, NULL), c != -1; )
    switch (c) {
    case 'f': print_flags=    false; break;
    case 'h': print_help()         ; break;
    case 'n': no_headers=      true; break;
    case 'o': output_format()      ; break;
    case 'p': print_phys_addr= true; break;
    default: usage(argv[0]);
    }
  nfiles= argc - optind;
  if (nfiles < 1) usage(argv[0]);
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
  assert(ps->max_sz < 0 || card(ps) < ps->max_sz);
  if (ps->max_sz < 0 && card(ps) == -ps->max_sz) {
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
	//print(elem);
	res= add(res, elem);
      });
  return res;
}

off_t minp;
bool min_p(extent *e)
{
  if (e->p <= minp) {
    minp= e->p; //print(-1, e);
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
    minlen= e->len; //print(-2, e);
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

void print_set(extents *ps)
{
  for (int i= 0; i < card(ps); ++i) {
    print(0, get(ps, i)); fputs("  ", stdout);
  }
}

extents *remove_elem(extents *ps, extent *e)
{
  extents *new= new_exts(nfiles);
  ITER(ps, elem, if (elem != e) new= add(new, elem););
  assert(card(new) == card(ps) - 1);
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

void find_shares()
{
  extents *curr= all();
  shared= new_exts(n_ext);
  do {
    //puts("loop head: "); print_set(curr); putchar('\n');
    extents *low= find_lowest_p(curr);
    //puts("low: "); print_set(low); putchar('\n');
    if (is_singleton(low)) {
      extent *lowest= only(low);
      //puts("lowest: "); print(lowest); putchar('\n');
      extents *allbutlowest= remove_elem(curr, lowest);
      //puts("allbutlowest: "); print_set(allbutlowest); putchar('\n');
      if (is_empty(allbutlowest)) { // last one
	lowest->nxt_sh= NULL;
	add_unshared(lowest);
	curr= move_next(curr, lowest);
      } else {
	extents *next_lowest= find_lowest_p(allbutlowest);
	//puts("next lowest: "); print_set(next_lowest); putchar('\n');
	if (end_p(lowest) <= first(next_lowest)->p) { // lowest precedes all others
	  //puts("prec: "); print_set(low); putchar('\n');
	  lowest->nxt_sh= NULL;
	  add_unshared(lowest);
	  curr= move_next(curr, lowest);
	} else {
	  //puts("split: "); print_set(low); putchar('\n');
	  extent *head= split(lowest, first(next_lowest)->p);
	  head->nxt_sh= NULL;
	  add_unshared(head);
	}
      }
    } else {
      // first, take all the ones of the same length
      extents *shortest= find_shortest(low);
      //puts("shortest: "); print_set(shortest); putchar('\n');
      extent *prev= NULL;
      extent *frst= first(shortest);
      //puts("first: "); print(frst); putchar('\n');
      off_t end= end_p(frst);
      ITER(shortest, s, {
	  curr= move_next(curr, s);
	  low= remove_elem(low, s);
	  s->nxt_sh= prev; 
	  prev= s;
	});
      // next, split the longer ones
      //puts("new low: "); print_set(low); putchar('\n');
      ITER(low, l, {
	  extent *head= split(l, end);
	  head->nxt_sh= prev; 
	  prev= head;
	});
      frst->nxt_sh= prev; // close the ring
      shared= add(shared, prev);
      validate(prev);
    }
  } while (!is_empty(curr));
}

int main(int argc, char *argv[])
{
  args(argc, argv);
  
  read_ext(&argv[optind]);

  if (nfiles == 1)
    output_unshared();
  else {
    phys_sort();
    find_shares();
    // sort by logical
    output();
  }

  return 0;
}
