all: tns

EXEC_DIR=/usr/libexec/trivial-network-server
LOCK_DIR=/var/lock/trivial-network-server
BIN_DIR=/usr/local/bin

# maybe-uninitialized disabled due to numerous gcc bugs // false positives
tns: CFLAGS += -Wextra -std=gnu99 -pedantic -I$(CURDIR) -Wno-maybe-uninitialized
tns: CFLAGS += -DEXEC_DIR='"$(EXEC_DIR)"' -DLOCK_DIR='"$(LOCK_DIR)"'
tns: LDFLAGS += -lselinux

tns: shared.c main.c client.c cleanup.c cmds/*.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: install
install: tns
	install -o root -g root -m 0644 -t /etc/systemd/system trivial-network-server.service
	install -o root -g root -m 0755 -T tns "$(BIN_DIR)/trivial-network-server"
	restorecon -vF /etc/systemd/system/trivial-network-server.service
	restorecon -vF "$(BIN_DIR)/trivial-network-server"
	cd exec; find . -type d -exec install -v -o root -g root -m 0755 -d '$(EXEC_DIR)/{}' \;
	cd exec; find . -type f -exec install -v -o root -g root -m 0755 -D '{}' '$(EXEC_DIR)/{}' \;
	restorecon -RvF "$(EXEC_DIR)"
	install -o root -g root -m 0755 -d "$(LOCK_DIR)"
	systemctl daemon-reload

.PHONY: uninstall
uninstall:
	rm -f /etc/systemd/system/trivial-network-server.service
	rm -f "$(BIN_DIR)/trivial-network-server"
	rm -rf "$(EXEC_DIR)" "$(LOCK_DIR)"
	systemctl daemon-reload

.PHONY: clean
clean:
	rm -f tns
