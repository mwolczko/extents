//
// Created by Mario Wolczko on 9/9/21.
//

#ifndef EXTENTS_SHARING_H
#define EXTENTS_SHARING_H

// an extent with its sharing info
typedef struct sh_ext sh_ext;
struct sh_ext {
    off_t p;          // physical offset on device
    off_t len;
    list *owners;     // the original extent*s from whence it came
    bool self_shared; // mapped to two or more logical extents in the same file
};

extern list *shared; // list of sh_ext*

extern unsigned total_unshared, total_self_shared, max_self_shared;

extern void find_shares();
extern extent *find_owner(sh_ext *s, unsigned i);
extern void find_self_shares();

#endif //EXTENTS_SHARING_H
