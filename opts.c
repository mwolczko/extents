// Parsing args for opts etc.

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "opts.h"
#include "fail.h"
#include "print.h"
#include "extents.h"

bool
    print_flags        = false,
    print_extents_only = false,
    print_shared_only  = false,
    print_unshared_only= false,
    no_headers         = false,
    print_phys_addr    = false,
    cmp_output         = false;

off_t max_cmp= -1, skip1= 0, skip2= 0;

#define USAGE "usage: %s -P [-f] [-n] [-p] FILE1 [FILE2 ...]\n"               \
              "or:    %s [-s|-u] [-f] [-n] [-p] FILE1 [FILE2 ...]\n"          \
	          "or:    %s -c [-b LIMIT] [-i SKIP1[:SKIP2]] [-v] FILE1 FILE2\n" \
	          "or:    %s -h\n"

static void usage(char *p) { fail(USAGE, p, p, p, p); }

static void print_help(char *progname) {
    printf("%s: Print extent information for files\n\n", progname);
    printf(USAGE, progname, progname, progname, progname);
    printf("\nWith -P, prints information about each extent.\n");
    printf("With -c, prints indices of regions which may differ (used to drive ccmp).\n");
    printf("Otherwise, determines which extents are shared and prints information about shared and unshared extents.\n");
    printf("An extent is a contiguous area of physical storage and is described by:\n");
    printf("  n if it belongs to FILEn (omitted for only a single file);\n");
    printf("  the logical offset in the file at which it begins;\n");
    printf("  the physical offset on the underlying device at which it begins (if -p is specified);\n");
    printf("  its length.\nOffsets and lengths are in bytes.\n");
    printf("OS-specific flags are also printed (with -f). Flags are available only on Linux and are described in /usr/include/linux/fiemap.h.\n\n");
    printf("Options and their long forms:\n");
    printf("-b --bytes LIMIT                   Compare at most LIMIT bytes (-c only)\n");
    printf("-c --cmp                           (two files only) Output unshared regions to be compared by ccmp. Fails silently unless -v follows.\n");
    printf("-f --flags                         Print OS-specific flags for each extent\n");
    printf("-h --help                          Print help (this message)\n");
    printf("-i --ignore-initial SKIP1[:SKIP2}  Skip first SKIP1 bytes of file1 (optionally, SKIP2 of file2) -- (-c)\n");
    printf("-n --no_headers                    Don't print human-readable headers and line numbers, output is easier to parse.\n");
    printf("-P --print_extents_only            Print extents for each file\n");
    printf("-p --print_phys_addr               Print physical address of extents\n");
    printf("-s --print_shared_only             Print only shared extents\n");
    printf("-u --print_unshared_only           Print only unshared extents\n");
    printf("-v --dont_fail_silently            Don't fail silently (use only after -c)\n");
    printf("\nMario Wolczko, Oracle, Sep 2021\n");
    exit(0);
}

void args(int argc, char *argv[])
{
    struct option longopts[]= {
            { "bytes",          required_argument, NULL, 'b' },
            { "cmp",                  no_argument, NULL, 'c' },
            { "flags"     ,           no_argument, NULL, 'f' },
            { "help",                 no_argument, NULL, 'h' },
            { "ignore-initial", required_argument, NULL, 'i' },
            { "no_headers",           no_argument, NULL, 'n' },
            { "print_extents_only",   no_argument, NULL, 'P' },
            { "print_phys_addr",      no_argument, NULL, 'p' },
            { "print_shared_only",    no_argument, NULL, 's' },
            { "print_unshared_only",  no_argument, NULL, 'u' },
            { "dont_fail_silently",   no_argument, NULL, 'v' },
    };
    for (int c; c= getopt_long(argc, argv, "cfhnpPsuvb:i:", longopts, NULL), c != -1; ) {
        switch (c) {
            case 'b':
                if (sscanf(optarg, FIELD, &max_cmp) != 1 || max_cmp <= 0)
                    fail("arg to -b|--bytes must be positive integer\n");
                break;
            case 'i':
                if (sscanf(optarg, FIELD ":" FIELD, &skip1, &skip2) != 2) {
                    if (sscanf(optarg, FIELD, &skip1) == 1)
                        skip2= skip1;
                    else
                        skip1= -1;
                }
                if (skip1 < 0 || skip2 < 0)
                    fail("arg to -i must be N or N:M (N,M non-negative integers)\n");
                break;
            case 'c': cmp_output=          true;
                      fail_silently=       true; break;
            case 'f': print_flags=         true; break;
            case 'n': no_headers=          true; break;
            case 'P': print_extents_only=  true; break;
            case 'p': print_phys_addr=     true; break;
            case 's': print_shared_only=   true; break;
            case 'u': print_unshared_only= true; break;
            case 'v': fail_silently=      false; break;
            case 'h': print_help(argv[0]); break;
            default : usage(argv[0]);
        }
    }
    nfiles= (unsigned)(argc - optind);
    if (nfiles < 1) usage(argv[0]);
    if (print_shared_only && print_unshared_only)
        fail("Must choose only one of -s (--print_shared_only) and -u (--print_unshared_only)\n");
    if (cmp_output && nfiles != 2)
        fail("Must have two files with -c (--cmp_output)\n");
    if (cmp_output && print_extents_only)
        fail("Choose at most one of -c and -P\n");
    if (cmp_output && (print_shared_only || print_unshared_only || print_phys_addr))
        fail("Can't use -c with -s, -u or -p\n");
}
