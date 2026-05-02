CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pthread

all: agent_loop

agent_loop: agent_loop.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f agent_loop
