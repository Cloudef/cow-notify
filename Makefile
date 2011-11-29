PREFIX := /usr
CFLAGS := -std=c99 -D_GNU_SOURCE -Wall -O3 -s
TARGET := cow-notify

all:
	${CC} ${CFLAGS} -DUSE_NOTIFY `pkg-config --cflags --libs dbus-1` cow-notify.c -o ${TARGET} notify.c

clean:
	rm -f ${TARGET}

install:
	mkdir -p ${DESTDIR}${PREFIX}/bin/
	cp ${TARGET} ${DESTDIR}${PREFIX}/bin/
	chmod +x ${DESTDIR}${PREFIX}/bin/${TARGET}

uninstall:
	rm ${DESTDIR}${PREFIX}/bin/${TARGET}
