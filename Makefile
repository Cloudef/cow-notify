CFLAGS= -std=c99 -D_GNU_SOURCE -Wall -O3 -s cow-notify.c -o cow-notify

all:
	${CC} ${CFLAGS} -DUSE_NOTIFY `pkg-config --cflags --libs dbus-1` notify.c

clean:
	rm cow-notify

install:
	cp cow-notify /usr/bin/
	chmod +x /usr/bin/cow-notify

uninstall:
	rm /usr/bin/cow-notify
