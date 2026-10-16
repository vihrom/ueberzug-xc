include config.mk

SRC = main.c x11_backend.c json_parser.c image_cache.c gif_loader.c stb_impl.c
OBJ = ${SRC:.c=.o}

TEST_SRC = tests.c x11_backend.c json_parser.c image_cache.c gif_loader.c stb_impl.c
TEST_OBJ = ${TEST_SRC:.c=.o}
TEST_TARGET = test_runner

all: ${TARGET}

${OBJ}: config.mk
${TEST_OBJ}: config.mk

.c.o:
	${CC} -c ${CFLAGS} ${INCS} $<

stb_impl.o: stb_impl.c
	${CC} -c ${CFLAGS} -Wno-unused-but-set-variable ${INCS} $< -o $@

${TARGET}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

${TEST_TARGET}: ${TEST_OBJ}
	${CC} -o $@ ${TEST_OBJ} ${LDFLAGS}

test: all ${TEST_TARGET}
	./${TEST_TARGET}
	chmod +x integration_test.sh
	./integration_test.sh

clean:
	rm -f ${TARGET} ${OBJ} ${TEST_TARGET} ${TEST_OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${TARGET} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${TARGET}
	ln -sf ${TARGET} ${DESTDIR}${PREFIX}/bin/ueberzug
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f ${TARGET}.1 ${DESTDIR}${MANPREFIX}/man1 2>/dev/null || true
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/${TARGET}.1 2>/dev/null || true

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${TARGET}
	rm -f ${DESTDIR}${PREFIX}/bin/ueberzug
	rm -f ${DESTDIR}${MANPREFIX}/man1/${TARGET}.1

.PHONY: all clean install uninstall test
