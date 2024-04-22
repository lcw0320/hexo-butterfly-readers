CC = gcc
CFLAGS = -g -Wall -I .

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: ../public/tiny cgi

../public/tiny: tiny.c csapp.o
	$(CC) $(CFLAGS) -o ../public/tiny tiny.c csapp.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

cgi:
	(cd cgi-bin; make)

clean:
	rm -f *.o tiny *~
	(cd cgi-bin; make clean)

