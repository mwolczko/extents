OS := $(shell uname)

export O_CFLAGS := $(CFLAGS)
CFLAGS := -I$(OS) -I. -O

all : extents

extents : extents.o fail.o mem.o $(OS)/fiemap.o

extents.o : extents.c extents.h fail.h mem.h fiemap.h

fail.o : fail.c

mem.o : mem.c

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
	export CC=/Users/mario/Work/parfait-tools-5.2/bin/parfait-gcc
	export PARFAIT_NATIVEGCC=`which cc`
	parfait -e all -W -p --disable=unchecked-result-call-stdc-ped-printf --disable=unchecked-result-call-stdc-ped-io `pwd`

mkself: mkself.o fail.o

mkself.o : mkself.c fail.h
