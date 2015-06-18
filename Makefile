CFLAGS += -g -Wall -Wextra -Werror -std=gnu99 -pedantic -I$(CURDIR)
LDFLAGS += -lselinux

all: ns2
ns2: shared.c main.c client.c locking.c cleanup.c cmds/*.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f ns2
