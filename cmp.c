/*
 * Generating indices for cmp(1)
 *
 * Walks through a pair of files, one extent at a time (in logical order).  Reports regions which may differ;
 * suppresses the report when two regions share a physical extent.  Can be directed to start at any offset in either file
 * using -i, and to limit the size of the region being compared (-b).
 */

#include <stdlib.h>
#include <stdio.h>

#include "cmp.h"
#include "extents.h"
#include "lists.h"
#include "opts.h"
#include "print.h"

typedef struct ecmp ecmp;
struct ecmp {
    list *lst; // list of extent*
    extent *e;
    off_t skip;
    unsigned i;
} f1, f2;

static void swap() { ecmp tmp= f1; f1= f2; f2= tmp; }

static bool at_end(ecmp *ec) { return ec->e == NULL; }

static bool advance(ecmp *ec) {
    if (ec->i < n_elems(ec->lst)) {
        extent *e= get(ec->lst, ec->i++);
        ec->e= e;
        off_t max_off= ec->skip + max_cmp;
        off_t l= e->l;
        if (l >= max_off)
            ec->e= NULL;
        else {
            if (l + e->len > max_off)
                e->len= max_off - l;
            e->l -= ec->skip;
        }
    } else
        ec->e= NULL;
    return ec->e != NULL;
}

static void init(ecmp *ec, fileinfo *info) {
    ec->lst= new_list((int)info->n_exts);
    for (unsigned i= 0; i < info->n_exts; ++i)
        append(ec->lst, &info->exts[i]);
    ec->skip= info->skip;
    ec->i= 0;
    while (advance(ec) && end_l(ec->e) <= 0)
        ;
    off_t l;
    if (!at_end(ec) && (l= ec->e->l, l < 0)) { // trim off before skip
        ec->e->l= 0;
        ec->e->len += l;
        ec->e->p -= l;
    }
}

static off_t last_start= -1, last_len; // used to merge contiguous regions

static void print_last() {
    if (last_start >= 0) print_cmp(last_start, last_len);
}

static void report(off_t len) {
    off_t start1= f1.e->l;
    if (last_start >= 0 && last_start + last_len == start1)
        last_len += len;
    else {
        print_last();
        last_start= start1;
        last_len= len;
    }
}

// trunc at max_cmp
void generate_cmp_output() {
    check_all_extents_are_sane();
    if (max_cmp < 0) {
        off_t size1= info[0].size - info[0].skip,
                size2= info[1].size - info[1].skip;
        max_cmp= size1 > size2 ? size1 : size2;
    }
    init(&f1, &info[0]);
    init(&f2, &info[1]);
    while (!at_end(&f1) && !at_end(&f2)) {
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
                if (at_end(&f1) || at_end(&f2)) break;
            }
        }
    }
    if (at_end(&f1)) f1= f2;
    while (!at_end(&f1)) {
        report(f1.e->len);
        advance(&f1);
    }
    print_last();
}