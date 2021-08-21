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

int block_size, nfiles;

fileinfo *info; // ptr to array of files' info of size nfiles

bool print_flags= true, print_shared= true, print_unshared= true, no_headers= false; 

void usage(char *p) {
  fail("usage: %s [-f] [-h] [-s|-u] FILE1 FILE2 ...\n", p);
}

static int pp= 0;
void print(int n, extent *e)
{
  off_t log= e->l, ph= e->p, len= e->len;
  int flags= e->flags;
  //bool is_shared= (flags & FIEMAP_EXTENT_SHARED) != 0;
  if (1) { // ((print_shared && is_shared) || (print_unshared && !is_shared) ) {
    if (!no_headers) 
      printf("%4d %12ld %12ld %12ld", n, log, ph, len);
    else
      printf("%ld %ld %ld", log, ph, len);
    if (print_flags) {
      char *fs= flags2str(flags);
      printf("   %s\n", fs);
    } else
      putchar('\n');
  }
  //assert(pp++ < 100);
}

void print_header(int i) {
  puts(info[i].name); 
  fputs("No.       Logical     Physical       Length", stdout);
  if (print_flags)
    fputs("   Flags", stdout);
  puts("\n           Offset      Offset");
}

void read_ext(char *fn[])
{
  info= calloc_s(nfiles, sizeof(fileinfo));
  for (int i= 0; i < nfiles; ++i) {
    char *name= fn[i];
    int fd= open(name, O_RDONLY);
    if (fd < 0)
      fail("Can't open file %s : %s\n", name, strerror(errno));
    info[i].name= name; info[i].fd= fd;
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
    //info[i].exts= get_extents(info[i].fd, sb.st_size, &info[i].nexts);
    get_extents(&info[i]);
  }
}

/*void get_all_extents()
{
  //exts= calloc(nfiles, sizeof(struct fiemap_extent *));
  //if (exts == NULL) fail("calloc failed\n");
  for (int i= 0; i < nfiles; ++i) {
    info[i].exts= get_extents(info[i].fd, block_size);
  }
  }*/

void print_help()
{
  printf("help\n");
  exit(0);
}

void output_format()
{
}

void output()
{
  for (int i= 0; i < nfiles; i++) {
    if (!no_headers) print_header(i);
    for (int e= 0; e < info[i].nexts; ++e) {
      extent *ext= info[i].exts[e];
      if (ext->nxt_sh == NULL)
	print(e+1, ext);
    }
  }
}

void args(int argc, char *argv[])
{
  struct option longopts[]= { { "no_flags",   false, NULL, 'f' },
			      { "help",       false, NULL, 'h' },
			      { "no_headers", false, NULL, 'n' },
  };
  for (int c; c= getopt_long(argc, argv, "fhno:", longopts, NULL), c != -1; )
    switch (c) {
    case 'f': print_flags= false; break;
    case 'h': print_help()      ; break;
    case 'n': no_headers=   true; break;
      //case 's': print_unshared= false; break;
    case 'o': output_format(); break;
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
    qsort(info[i].exts, info[i].nexts, sizeof(extent *), (__compar_fn_t)&ext_cmp_phys);
  }
}

typedef struct extents {
  unsigned nelems;
  extent *elems[];
} extents;

typedef struct {
  extent *prev, *next;
} iter;

extent *new_iter() { return malloc_s(sizeof(iter)); }

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

#define ITER(es, elem, stmt)		     \
  for (int _i= 0; _i < (es)->nelems; ++_i) { \
    extent *elem= get((es), _i);	     \
    do { stmt; } while (0);		     \
  }

extents *new_exts() {
  extents *ps= malloc_s(sizeof(extents) + nfiles * sizeof(extent *));
  ps->nelems= 0;
  return ps;
}

void add(extents *ps, extent *e) {
  assert(card(ps) < nfiles);
  put(ps, ps->nelems++, e);
}

extents *all() {
  extents *ps= new_exts();
  for (int i= 0; i < nfiles; ++i)
    if (info[i].nexts > 0)
      add(ps, info[i].exts[0]);
  return ps;
}

typedef bool (*filter_t)(extent *);

extents *filter(extents *ps, filter_t f)
{
  extents *res = new_exts();
  ITER(ps, elem, 
      if ((*f)(elem)) {
	//print(-10, elem);
	add(res, elem);
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
  for (int i= 0; i < card(ps); ++i)
    print(i, get(ps, i));
}

extents *remove_elem(extents *ps, extent *e)
{
  extents *new= new_exts();
  ITER(ps, elem, if (elem != e) add(new, elem););
  assert(card(new) == card(ps) - 1);
  return new;
}

extents *move_next(extents *ps, extent *e)
{
  extents *del= remove_elem(ps, e);
  fileinfo *info= e->info;
  int n;
  for (n= 0; n < info->nexts; ++n)
    if (info->exts[n] == e)
      break;
  if (n < info->nexts - 1)
    add(del, info->exts[n+1]);
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
  puts("shared: ");
  int i= 0;
  extent *n= e;
  do {
    assert(n != NULL);
    print(i++, n);
    n= n->nxt_sh;
    assert(i <= nfiles);
  } while (n != e);
}
  
void find_shares()
{
  extents *curr= all();
  do {
    //puts("loop head: "); print_set(curr);
    extents *low= find_lowest_p(curr);
    //puts("low: "); print_set(low);
    if (is_singleton(low)) {
      extent *lowest= only(low);
      //puts("lowest: "); print(-6, lowest);
      extents *allbutlowest= remove_elem(curr, lowest);
      //puts("allbutlowest: "); print_set(allbutlowest);
      if (is_empty(allbutlowest)) { // last one
	lowest->nxt_sh= NULL;
	curr= move_next(curr, lowest);
      } else {
	extents *next_lowest= find_lowest_p(allbutlowest);
	//puts("next lowest: "); print_set(next_lowest);
	if (end_p(lowest) <= first(next_lowest)->p) { // lowest precedes all others
	  //puts("prec: "); print_set(low);
	  lowest->nxt_sh= NULL;
	  curr= move_next(curr, lowest);
	} else {
	  //puts("split: "); print_set(low);
	  extent *head= split(lowest, first(next_lowest)->p);
	  head->nxt_sh= NULL;
	}
      }
    } else {
      // first, take all the ones of the same length
      extents *shortest= find_shortest(low);
      //puts("shortest: "); print_set(shortest);
      extent *prev= NULL;
      extent *frst= first(shortest);
      //puts("first: "); print(-33, frst);
      off_t end= end_p(frst);
      ITER(shortest, s, {
	  curr= move_next(curr, s);
	  low= remove_elem(low, s);
	  s->nxt_sh= prev;
	  prev= s;
	});
      // next, split the longer ones
      //puts("new low: "); print_set(low);
      ITER(low, l, {
	  extent *head= split(l, end);
	  head->nxt_sh= prev;
	  prev= head;
	});
      frst->nxt_sh= prev; // close the ring
      validate(prev);
    }
  } while (!is_empty(curr));
}

int main(int argc, char *argv[])
{
  args(argc, argv);
  
  read_ext(&argv[optind]);

  if (nfiles > 1) {
    phys_sort();
    find_shares();
    // sort by logical
  }
  output();

  return 0;
}
