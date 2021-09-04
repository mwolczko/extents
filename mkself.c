//
// Linux program to make self-sharing clonefiles.
//
// Mario Wolczko, Sep 2021
//

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "fail.h"

void usage() { fail("usage: mkself #blocks #repeats\n"); }

int main(int argc, char *argv[])
{
  if (argc < 3) usage();
  int n_b, n_r;
  if (sscanf(argv[1], "%d", &n_b) != 1 || sscanf(argv[2], "%d", &n_r) != 1) usage();
    char *filename= "self.dat";

    off_t sz= 4096L * n_b;

    int fd= open(filename, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

    if (fd < 0)
        fail("open of %s failed!\n", filename);
    int rnd= open("/dev/random", O_RDONLY);
    if (rnd < 0)
        fail("Cannot open /dev/random\n");

    char buf[sz];
    read(rnd, buf, sz);
    write(fd, buf, sz);

    struct file_clone_range range= { fd, 0L, sz, sz };

    for (unsigned n= 0; n < n_r; n++) {
        int res= ioctl(fd, FICLONERANGE, &range);
        if (res < 0)
            fail("ioctl failed at %d with %s\n", n, strerror(errno));
        range.dest_offset += sz;
    }
    return 0;
}
