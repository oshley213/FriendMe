PORT = 53654
CFLAGS = -DPORT=$(PORT) -g -std=gnu99 -Wall -Werror

all: friend_server

friend_server: friend_server.o friends.o
	gcc $(CFLAGS) -o $@ $^

%.o: %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f *.o friend_server
