OS := $(shell uname)

export O_CFLAGS := $(CFLAGS)
CFLAGS := -I$(OS) -I. -g

extents : extents.o fail.o mem.o $(OS)/fiemap.o

extents.o : extents.c extents.h fail.h mem.h $(OS)/fiemap.h

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
