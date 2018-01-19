.POSIX:
CC     = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra

pretty: pretty.c pdjson.c pdjson.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ pretty.c pdjson.c $(LDLIBS)

clean:
	rm -f pretty
