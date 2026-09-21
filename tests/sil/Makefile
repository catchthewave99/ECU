CFLAGS ?= -O2 -Wall
LDLIBS ?= -lsimavr -lelf

sil_runner: sil_runner.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f sil_runner

.PHONY: clean
