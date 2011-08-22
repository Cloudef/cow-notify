PREFIX := /usr
CFLAGS := -std=c99 -D_GNU_SOURCE -Wall -O3 -s
TARGET := cow-notify

all:
	${CC} ${CFLAGS} -DUSE_NOTIFY `pkg-config --cflags --libs dbus-1` cow-notify.c -o ${TARGET} notify.c

clean:
	rm ${TARGET}

install:
	cp ${TARGET} ${PREFIX}/bin/
	chmod +x ${PREFIX}/bin/${TARGET}

uninstall:
	rm ${PREFIX}/bin/${TARGET}
