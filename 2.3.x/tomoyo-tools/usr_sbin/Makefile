include ../Include.make

BUILD_FILES = tomoyo-domainmatch tomoyo-queryd tomoyo-pstree \
	tomoyo-checkpolicy tomoyo-editpolicy tomoyo-findtemp tomoyo-ld-watch \
	tomoyo-loadpolicy tomoyo-patternize tomoyo-savepolicy tomoyo-setlevel \
	tomoyo-setprofile tomoyo-sortpolicy tomoyo-diffpolicy \
	tomoyo-selectpolicy

all: libtomoyotools.so $(BUILD_FILES)

$(BUILD_FILES): libtomoyotools.so

/usr/include/curses.h:
	@echo "/usr/include/curses.h is missing."
	@echo "Run 'yum install ncurses-devel' or 'apt-get install libncurses-dev'"
	sleep 10

libtomoyotools.so: tomoyotools.c tomoyotools.h
	$(CC) $(CFLAGS) -fPIC tomoyotools.c -shared -Wl,-soname,libtomoyotools.so.1 -o libtomoyotools.so.1.0.1
	ln -sf libtomoyotools.so.1.0.1 libtomoyotools.so

.c:
	$(CC) $(CFLAGS) -o $@ $< -ltomoyotools -L. 

tomoyo-editpolicy: tomoyotools.h editpolicy*.c readline.h /usr/include/curses.h libtomoyotools.so
	$(CC) $(CFLAGS) -o tomoyo-editpolicy editpolicy*.c -lncurses -ltomoyotools -L. -DCOLOR_ON

tomoyo-queryd: tomoyotools.h tomoyo-queryd.c readline.h /usr/include/curses.h libtomoyotools.so
	$(CC) $(CFLAGS) -o tomoyo-queryd tomoyo-queryd.c -lncurses -ltomoyotools -L.

install: all
	mkdir -p -m 0755 $(INSTALLDIR)$(USRLIBDIR)
	$(INSTALL) -m 0755 libtomoyotools.so.1.0.1 $(INSTALLDIR)$(USRLIBDIR)
	ln -sf libtomoyotools.so.1.0.1 $(INSTALLDIR)$(USRLIBDIR)/libtomoyotools.so.1
ifeq ($(INSTALLDIR),)
	ldconfig || true
endif
	mkdir -p -m 0755 $(INSTALLDIR)$(USRSBINDIR)
	$(INSTALL) -m 0755 $(BUILD_FILES) $(INSTALLDIR)$(USRSBINDIR)

clean:
	rm -f -- $(BUILD_FILES) libtomoyotools.so*

.PHONY: clean install
