.POSIX:
CC     = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wno-missing-field-initializers -I.

all: tests/pretty tests/stream tests/tests

tests/pretty: tests/pretty.o pdjson.o pdjson_stream.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/tests: tests/tests.o pdjson.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/stream: tests/stream.o pdjson.o pdjson_stream.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

pdjson.o: pdjson.c pdjson.h
pdjson_stream.o: pdjson_stream.c pdjson.h
tests/pretty.o: tests/pretty.c pdjson.h
tests/tests.o: tests/tests.c pdjson.h
tests/stream.o: tests/stream.c pdjson.h

test: check
check: tests/tests
	tests/tests

clean:
	rm -f tests/pretty tests/tests tests/stream
	rm -f pdjson.o tests/pretty.o tests/tests.o tests/stream.o

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<
