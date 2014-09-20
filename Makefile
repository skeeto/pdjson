CFLAGS = -std=c99 -pedantic -Wall -Wextra

json : json.o main.o

clean :
	$(RM) json *.o

run : json
	./$^
