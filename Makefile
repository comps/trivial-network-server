TOPDIR		= ../..

include $(TOPDIR)/rules.mk

ALL_EXE		= ns2

# maybe-uninitialized disabled due to numerous gcc bugs // false positives
ns2: CFLAGS += -Wextra -std=gnu99 -pedantic -I$(CURDIR) -Wno-maybe-uninitialized
ns2: LDFLAGS += -lselinux

ns2: shared.c main.c client.c cleanup.c cmds/*.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

executables: $(ALL_EXE)

install: ns2
	install -o root -g root -m 0644 ns2.service /etc/systemd/system
	systemctl daemon-reload
	systemctl enable ns2
	systemctl start ns2
