OS := $(shell uname)

export O_CFLAGS := $(CFLAGS)
CFLAGS := -I$(OS) -I. -O

all : extents

extents : extents.o fail.o mem.o $(OS)/fiemap.o lists.o cmp.o sharing.o opts.o print.o sorting.o

extents.o : extents.c extents.h fail.h mem.h fiemap.h lists.h cmp.h sharing.h opts.h print.h sorting.h

fail.o : fail.c

mem.o : mem.c

lists.o : lists.c

cmp.o : cmp.c

sharing.o : sharing.c

opts.o : opts.c

print.o : print.c

sorting.o : sorting.c

#$(OS)/fiemap.o : $(OS)/fiemap.c

$(OS):
	${MAKE} -C $@ # CFLAGS=$(O_CFLAGS) $@ 

install: 
	install -c extents /usr/local/bin

test:
	./test

clean:
	rm -f *.o extents
	make -C $(OS) clean

parfait:
	export CC=/Users/mario/Work/parfait-tools-5.2/bin/parfait-clang
	export PARFAIT_NATIVECLANG=`which cc`
	parfait -e all -W -p --disable=unchecked-result-call-stdc-ped-printf --disable=unchecked-result-call-stdc-ped-io `pwd`

mkself: mkself.o fail.o

mkself.o : mkself.c fail.h
