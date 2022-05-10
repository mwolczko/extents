Implementation of the "extents" and "ccmp" utilities for macOS and Linux as described in
https://labs.oracle.com/pls/apex/f?p=94065:10:112592311651585:8049 (also at http://www.wolczko.com/nvm-blog/Clonefiles.pdf).

extents: analyses the sharing relationships between a collection of clonefiles (macOS) / reflinks (Linux).

ccmp: a clone-aware version of cmp that is much faster for clonefiles which share a lot.
