OS=$(shell uname)

includes=-I$(OS) -I.
CFLAGS=$(includes)

extents : extents.o fail.o $(OS)/fiemap.o

extents.o : extents.c

fail.o : fail.c

$(OS)/fiemap.o :
	(cd $(OS); make)

install: 
	install -c extents /usr/local/bin

test:
	./test

clean:
	rm -f *.o extents
	(cd $(OS); make clean)
