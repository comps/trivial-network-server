CFLAGS += -std=gnu99 -pedantic -I$(CURDIR)
LDFLAGS += -lselinux

all: ns2
ns2: shared.c main.c client.c locking.c cleanup.c cmds/*.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: install
install: ns2
	install -o root -g root -m 0644 ns2.service /etc/systemd/system
	systemctl daemon-reload
	systemctl enable ns2
	systemctl start ns2

.PHONY: clean
clean:
	rm -f ns2
