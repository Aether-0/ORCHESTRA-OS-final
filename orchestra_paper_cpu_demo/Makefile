CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror
CHECK_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror
HARDENING_CFLAGS ?= -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
HARDENING_LDFLAGS ?= -Wl,-z,relro,-z,now -Wl,-z,noexecstack -pie
LDLIBS ?= -lm

TARGET = orchestra_paper_cpu

all: $(TARGET)

$(TARGET): orchestra_paper_cpu.c
	$(CC) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) \
		-o $@ $< $(LDLIBS)

check:
	$(CC) $(CHECK_CFLAGS) -fsyntax-only orchestra_paper_cpu.c

clean:
	rm -f $(TARGET)

.PHONY: all check clean
