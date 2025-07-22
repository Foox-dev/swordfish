CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= 

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
TARGET = swordfish
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
