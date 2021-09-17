/*
 * Printing
 */

#include <stdio.h>
#include <stdarg.h>

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
#define FILENO_WIDTH 6
#define FILENO_WIDTH_S "6"
#define SEP "  "
#define CSEP " | "

static void print_lineno(unsigned n) { printf(LINENO_FMT "d ", n); }

static void print_lineno_s(char *s)  { printf(LINENO_FMT "s ", s); }

static void print_fileno(unsigned n) {
    printf(no_headers ? "%d " : "%" FILENO_WIDTH_S "d ", n);
}

static void print_fileno_header(char *s) {
    printf("%" FILENO_WIDTH_S "s ", s);
}

static void sep() { if (!no_headers) fputs(SEP, stdout); }

static void print_off_t(off_t o) {
    printf(no_headers ? FIELD " " : FIELD_W " ", o);
}

static void print_off_t_s(char *s) {
    printf(no_headers ? "%s" : STRING_FIELD_FMT " ", s);
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

// return hdr_line'th arg
static char *h(char *s0, ...) {
    va_list args;
    unsigned n= 0;
    char *a= s0;
    va_start(args, s0);
    do 
	if (n++ == hdr_line)
	    break;
    while ((a= va_arg(args, char*)) != NULL);
    va_end(args);
    return a;
}

static void print_header_for_file(unsigned i) {
    printf("(%d) %s\n", i + 1, info[i].name);
    for (hdr_line= 1; hdr_line <= 2; hdr_line++) {
        print_lineno_s(h("", "#", ""));
        print_off_t_s(h("", "Logical", "Offset"));
        if (print_phys_addr) print_off_t_s(h("", "Physical", "Offset"));
        print_off_t_s(h("", "Length", ""));
        if (print_flags) fputs(h("", "  Flags", ""), stdout);
        putchar('\n');
    }
}

static char flagbuf[200];

char *flag_pr(unsigned flags, bool sharing) {
    flags2str(flags, flagbuf, sizeof(flagbuf), sharing);
    return flagbuf;
}

void print_extents_by_file() {
    for (unsigned i= 0; i < nfiles; i++) {
        if (!no_headers) print_header_for_file(i);
        for (unsigned e= 0; e < info[i].n_exts; ++e) {
            extent *ext= &info[i].exts[e];
            if (!no_headers) print_lineno(e + 1);
            print_extent(ext);
            if (print_flags) printf(" %s", flag_pr(ext->flags, false));
            putchar('\n');
        }
    }
}

void print_shared_extents_no_header() {
    ITER(shared, sh_ext*, s_e, {
	    print_off_t(s_e->len);
	    if (print_phys_addr) print_off_t(s_e->p);
	    ITER(s_e->owners, extent*, owner, {
	        printf("%d ", owner->info->argno + 1);
	        print_off_t(s_e->p - owner->p + owner->l);
	    })
	    putchar('\n');
	    if (print_flags) {
	        bool first= true;
	        ITER(s_e->owners, extent*, owner, {
		        char *f= flag_pr(owner->flags, true);
		        if (!first) { putchar(','); putchar(' '); }
		        fputs(f, stdout);
		        first= false;
	        })
	        putchar('\n');
	    }
    })
}

void print_shared_extents() {
    if (n_elems(shared) == total_self_shared)
        return;
    if (!no_headers) {
        if (!print_shared_only) puts("Shared: ");
        for (hdr_line= 0; hdr_line <= 2; hdr_line++) {
            print_lineno_s(h("File#:", "#", ""));
            print_off_t_s(h("", "Length", ""));
            if (print_phys_addr) print_off_t_s(h("", "Physical", "Offset"));
            sep();
            for (unsigned i= 0; i < nfiles; ++i) {
                if (hdr_line == 0) {
                    char buf[FIELD_WIDTH + 1];
                    sprintf(buf, FIELD_W, (off_t) (i + 1));
                    print_off_t_s(buf);
                } else
                    print_off_t_s(h("", "Logical", "Offset"));
                if (i < nfiles - 1) sep();
            }
            putchar('\n');
        }
    }
    unsigned e= 1;
    ITER(shared, sh_ext*, s_e, {
        if (!s_e->self_shared) {
            if (!no_headers) print_lineno(e++);
            print_off_t(s_e->len);
            if (print_phys_addr) print_off_t(s_e->p);
            sep();
            for (unsigned i= 0; i < nfiles; ++i) {
                extent *owner= find_owner(s_e, i);
                if (owner != NULL)
                    print_off_t(s_e->p - owner->p + owner->l);
                else
                    print_off_t_s(no_headers ? "- " : "");
                if (i < nfiles - 1) sep();
            }
            putchar('\n');
            if (print_flags) {
                if (!no_headers) print_lineno_s("Flags:");
                print_off_t_s("");
                if (print_phys_addr) print_off_t_s("");
                sep();
                bool first= true;
                for (unsigned i= 0; i < nfiles; ++i) {
                    extent *owner= find_owner(s_e, i);
                    char *f= owner == NULL ? "" : flag_pr(owner->flags, true);
                    if (no_headers) {
                        if (!first) {
                            putchar(',');
                            if (!no_headers) putchar(' ');
                        }
                        fputs(f, stdout);
                        first= false;
                    } else {
                        print_off_t_s(f);
                        if (i < nfiles - 1) sep();
                    }
                }
                putchar('\n');
            }
        }
    })
}

void print_self_shared_extents() {
    if (!no_headers) {
        if (!print_shared_only) puts("Self Shared: ");
        for (hdr_line= 0; hdr_line <= 1; hdr_line++) {
            print_lineno_s(h("#", ""));
            print_off_t_s(h("Length", ""));
            if (print_phys_addr) print_off_t_s(h("Physical", "Offset"));
            sep();
            for (unsigned i= 0; i < max_self_shared; ++i) {
                print_fileno_header(h("File#", ""));
                print_off_t_s(h("Logical", "Offset"));
            }
            putchar('\n');
        }
    }
    unsigned e= 1;
    ITER(shared, sh_ext*, s_e, {
        if (s_e->self_shared) {
            if (!no_headers) print_lineno(e++);
            print_off_t(s_e->len);
            if (print_phys_addr) print_off_t(s_e->p);
            sep();
            ITER(s_e->owners, extent * , owner, {
                print_fileno(owner->info->argno + 1);
                print_off_t(s_e->p - owner->p + owner->l);
            })
            putchar('\n');
            if (print_flags) {
                if (!no_headers) {
                    print_lineno_s("Flags:");
                    print_off_t_s("");
                    if (print_phys_addr) print_off_t_s("");
                    sep();
                }
                bool first= true;
                ITER(s_e->owners, extent * , owner, {
                    char *f= flag_pr(owner->flags, true);
                    if (no_headers) {
                        if (!first) { putchar(','); putchar(' '); }
                        fputs(f, stdout);
                        first= false;
                    } else {
                        printf("%-*s", FILENO_WIDTH + FIELD_WIDTH, f);
                        if (owner != last(s_e->owners)) sep();
                    }
                })
                putchar('\n');
            }
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
            ITER(unsh, sh_ext*, sh, {
                if (!no_headers) print_lineno(n++);
                extent *owner= only(sh->owners);
                print_sh_ext(sh->p, sh->len, owner);
                if (print_flags) { sep(); fputs(flag_pr(owner->flags, true), stdout); }
                putchar('\n');
            })
        }
    }
}

void print_cmp(off_t start, off_t len) {
    printf(FIELD " " FIELD " " FIELD "\n", start + skip1, start + skip2, len);
}

void print_file_key() {
    for (unsigned i= 0; i < nfiles; ++i)
        printf("(%d) %s\n", i + 1, info[i].name);
    putchar('\n');
}

void debug_print_extents(unsigned ei, extent *cur, list *owners) {
    putchar('{');
    if (owners != NULL) ITER(owners, extent*, owner, printf("%d,", owner->info->argno))
    putchar('}');
    if (cur != NULL) print_extent(cur);
    putchar('!');
    for (unsigned i= ei; i < n_elems(extents); ++i) {
        extent *e= get(extents, i);
        printf("%d: ", e->info->argno);
        print_extent(e);
        putchar(';');
    }
    putchar('\n');
}

