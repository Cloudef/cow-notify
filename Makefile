CC=gcc
CFLAGS= -std=c99 -pedantic -Wall -Os -s cow-notify.c -o cow-notify

all:
	${CC} ${CFLAGS} -DUSE_NOTIFY `pkg-config --cflags --libs dbus-1` notify.c

clean:
	rm cow-notify

install:
	cp cow-notify /usr/local/bin/
