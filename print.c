/*
 * Printing
 */

#include <stdio.h>

#include "extents.h"
#include "fiemap.h"
#include "opts.h"
#include "print.h"
#include "sharing.h"
#include "sorting.h"

#define LINENO_FMT "%-6"
#define FIELD_WIDTH 15
#define FIELD_WIDTH_S "15"
#define STRING_FIELD_FMT "%" FIELD_WIDTH_S "s"
#define FIELD_W "%" FIELD_WIDTH_S OFF_T
#define FILENO_WIDTH 4
#define FILENO_WIDTH_S "4"
#define SEP "  "
#define CSEP " | "

static void print_lineno(unsigned n) { printf(LINENO_FMT "d ", n); }

static void print_lineno_s(char *s)  { printf(LINENO_FMT "s ", s); }

static void sep() { fputs(SEP, stdout); }

static void csep() { fputs(no_headers ? " " : CSEP, stdout); }

static void print_fileno(unsigned n) {
    printf(no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", n);
}

static void print_fileno_header(char *s) {
    printf("%" FILENO_WIDTH_S "s ", s);
}

static void print_off_t(off_t o) {
    printf(no_headers ? FIELD " " : FIELD_W " ", o);
}

static void print_off_t_hdr(char *s) {
    printf(STRING_FIELD_FMT " ", s);
}

static void print_extent(extent *e) {
    print_off_t(e->l);
    if (print_phys_addr) print_off_t(e->p);
    print_off_t(e->len);
}

static void print_sh_ext(off_t p, off_t len, extent *owner) {
    off_t l= p - owner->p + owner->l;
    print_off_t(l);
    if (print_phys_addr) print_off_t(p);
    print_off_t(len);
}

static unsigned hdr_line;

static char *h(char *s1, char *s2) { return hdr_line==1 ? s1 : s2; }

static void print_header_for_file(unsigned i) {
    printf("(%d) %s\n", i+1, info[i].name);
    for (hdr_line= 1; hdr_line <= 2; hdr_line++) {
        print_lineno_s(h("#", ""));
        print_off_t_hdr(h("Logical", "Offset"));
        if (print_phys_addr) print_off_t_hdr(h("Physical", "Offset"));
        print_off_t_hdr(h("Length", ""));
        if (print_flags) fputs(h("  Flags", ""), stdout);
        putchar('\n');
    }
}

static char flagbuf[200];

char *flag_pr(unsigned flags) {
    flags2str(flags, flagbuf, sizeof(flagbuf));
    return flagbuf;
}

void print_extents_by_file() {
    for (unsigned i= 0; i < nfiles; i++) {
        if (!no_headers) print_header_for_file(i);
        for (unsigned e= 0; e < info[i].n_exts; ++e) {
            extent *ext= &info[i].exts[e];
            if (!no_headers) print_lineno(e + 1);
            print_extent(ext);
            if (print_flags) printf(" %s", flag_pr(ext->flags));
            putchar('\n');
        }
    }
}

void print_shared_extents() {
    if (!no_headers) {
        if (!print_shared_only) puts("Shared: ");
        for (hdr_line= 1; hdr_line <= 2; hdr_line++) {
            print_lineno_s(h("#", ""));
            print_off_t_hdr(h("Length", ""));
            if (print_phys_addr) print_off_t_hdr(h("Physical", "Offset"));
            csep();
            for (unsigned i= 0; i < max_n_shared; ++i) {
                print_fileno_header(h("File", ""));
                print_off_t_hdr(h("Logical", "Offset"));
                if (i < max_n_shared - 1) csep();
            }
            putchar('\n');
        }
    }
    unsigned e= 1;
    ITER(shared, sh_ext*, s_e, {
        if (!no_headers) print_lineno(e++);
        print_off_t(s_e->len);
        if (print_phys_addr) print_off_t(s_e->p);
        csep();
        ITER(s_e->owners, extent*, owner, {
            print_fileno(owner->info->argno + 1);
            print_off_t(s_e->p - owner->p + owner->l);
            if (owner != last(s_e->owners)) csep();
        })
        putchar('\n');
        if (print_flags) {
            if (!no_headers) print_lineno_s("Flags:");
            bool first= true;
            ITER(s_e->owners, extent*, owner, {
                char *f= flag_pr(owner->flags);
                if (no_headers) {
                    if (!first) { putchar(','); putchar(' '); }
                    fputs(f, stdout);
                    first= false;
                } else {
                    printf("%-*s", FILENO_WIDTH + FIELD_WIDTH, f);
                    if (owner != last(s_e->owners)) csep();
                }
            })
            putchar('\n');
        }
    })
}

void print_unshared_extents() {
    if (total_unshared == 0) return;
    if (!no_headers && !print_unshared_only) puts("Not Shared:");
    for (unsigned i= 0; i < nfiles; i++) {
        list *unsh= info[i].unsh;
        if (!is_empty(unsh)) {
            log_sort(unsh);
            if (!no_headers) print_header_for_file(i);
            unsigned n= 1;
            ITER(info[i].unsh, sh_ext*, sh, {
                if (!no_headers) print_lineno(n++);
                extent *owner= only(sh->owners);
                print_sh_ext(sh->p, sh->len, owner);
                if (print_flags) {
                    csep(); fputs(flag_pr(owner->flags), stdout);
                }
                putchar('\n');
            })
        }
    }
}

void print_cmp(off_t start, off_t len) {
    printf(FIELD " " FIELD " " FIELD "\n", start + skip1, start + skip2, len);
}

