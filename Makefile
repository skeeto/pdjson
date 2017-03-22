.POSIX:
CC     = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra

json: json.o main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ json.o main.o $(LDLIBS)

clean:
	rm -f json json.o main.o

run: json
	./json
