.POSIX:

VERSION = 1.9
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

CFLAGS = -Wall -Wextra -pedantic -std=c99 -O2 -I/usr/X11R6/include
CPPFLAGS = -DVERSION=\"${VERSION}\"
LDFLAGS = -lX11 -lXrandr -lm -L/usr/X11R6/lib -s

all: sct

.c.o:
	$(CC) -o $@ -c $< $(CFLAGS) $(CPPFLAGS)

.o:
	$(CC) -o $@ $< $(LDFLAGS)

install: all
	# installing executable
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f sct "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/sct"
	#installing manual page
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man1"
	sed "s:VERSION:$(VERSION)/g" < sct.1 > "$(DESTDIR)$(MANPREFIX)/man1/sct.1"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/sct"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/sct.1"

clean:
	rm -f sct $(OBJ)

.PHONY: all clean install uninstall
