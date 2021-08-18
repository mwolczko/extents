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

#include "fail.h"
#include "fiemap.h"

void usage(char *p) {
  fail("usage: %s [-f] [-h] [-s|-u] FILE1 FILE2 ...\n", p);
}

bool print_flags= true, print_shared= true, print_unshared= true, no_header = false; 

void print(int n, __u64 log, __u64 ph, __u64 len, int flags)
{
  bool is_shared = (flags & FIEMAP_EXTENT_SHARED) != 0;
  if ((print_shared && is_shared) || (print_unshared && !is_shared) ) {
    if (!no_header) 
      printf("%4d %12lld %12lld %12lld", n, log, ph, len);
    else
      printf("%lld %lld %lld", log, ph, len);
    if (print_flags) {
      char *fs = flags2str(flags);
      printf("   %s\n", fs);
    } else
      putchar('\n');
  }
}

void print_header() {
  fputs("No.       Logical     Physical       Length", stdout);
  if (print_flags)
    fputs("   Flags", stdout);
  puts("\n           Offset      Offset");
}

int *fds, block_size, nfiles;

void open_files(char *fn[])
{
  fds = calloc(nfiles, sizeof(int));
  if (fds == NULL) fail("calloc failed!\n");
  for (int i = 0; i < nfiles; ++i) {
    char *name = fn[i];
    int fd= open(name, O_RDONLY);
    if (fd < 0)
      fail("Can't open file %s : %s\n", name, strerror(errno));
    fds[i] = fd;
    struct stat sb;
    if (fstat(fd, &sb) < 0)
      fail("Can't stat %s : %s\n", name, strerror(errno));
    if ((sb.st_mode & S_IFMT) != S_IFREG)
      fail("%s: Not a regular file\n", name);
    if (i == 0)
      block_size= sb.st_blksize;
    else if (block_size != sb.st_blksize)
      fail("block size weirdness! %d v %d\n", block_size, sb.st_size);
  }
}

struct fiemap_extent **exts;

void get_all_extents()
{
  exts = calloc(nfiles, sizeof(struct fiemap_extent *));
  if (exts == NULL) fail("calloc failed\n");
  for (int i = 0; i < nfiles; ++i) {
    exts[i] = get_extents(fds[i], block_size);
  }
}


int main(int argc, char *argv[])
{
  for (int c; c= getopt(argc, argv, "fhsu"), c != -1; )
    switch (c) {
    case 'f': print_flags=    false; break;
    case 'h': no_header =      true; break;
    case 's': print_unshared= false; break;
    case 'u': print_shared=   false; break;
    default: usage(argv[0]);
    }
  nfiles = argc - optind;
  if (nfiles < 1) usage(argv[0]);

  open_files(&argv[optind]);

  get_all_extents();

  for (int i = 0; i < nfiles; i++) {
    if (!no_header) print_header();
    int e = 1;
    struct fiemap_extent *fme= exts[i];
    do {
      __u64 l= fme->fe_logical;
      __u64 p= fme->fe_physical;
      __u64 len= fme->fe_length;
      print(e++, l, p, len, fme->fe_flags);
    } while ((fme++->fe_flags & FIEMAP_EXTENT_LAST) == 0);
  }
  return 0;
}
