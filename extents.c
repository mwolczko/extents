/*
  extents : print extent information for files

  Mario Wolczko, Sep 2021

 */


#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdbool.h>

#include "fail.h"
#include "mem.h"
#include "fiemap.h"
#include "extents.h"
#include "lists.h"
#include "print.h"
#include "cmp.h"
#include "opts.h"
#include "sharing.h"
#include "sorting.h"

static dev_t device;
blksize_t blk_sz;
unsigned n_ext= 0;

unsigned nfiles;
fileinfo *info;

list *extents;

off_t end_l(extent *e) { return e->l + e->len; }

static off_t end_p(extent *e) { return e->p + e->len; }

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
        if (cmp_output || print_extents_only) {
            if (i == 0) info[0].skip= skip1;
            else if (i == 1) info[1].skip= skip2;
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

void check_all_extents_are_sane() {
    ITER(extents, extent*, e, {
        if (!flags_are_sane(e->flags))
	  fail("Extent in file %s has unexpected flag: %s\n", e->info->name, flag_pr(e->flags, false));
    })
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
        //log_sort(shared);
        bool pr_sh= !print_unshared_only && !is_empty(shared);
	    bool pr_unsh= !print_shared_only && total_unshared > 0;
        if (pr_sh) {
            if (!no_headers) print_file_key();
            print_shared_extents();
        }
	    if (pr_sh && pr_unsh || no_headers) putchar('\n');
        if (pr_unsh) print_unshared_extents();
    }
    return 0;
}
