VERSION = 1.0.0

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/include
X11LIB = /usr/lib

TARGET = ueberzug-xc

CC = cc
STDFLAG = -std=c11

INCS = -Iinclude -I${X11INC}
LIBS = -L${X11LIB} -lX11 -lm

BASE_CFLAGS = ${STDFLAG} -Wall -Wextra -Wpedantic -Wshadow -Werror

# Default mode is PROD
MODE = PROD

# Flag definitions for each mode
PROD_CFLAGS = ${BASE_CFLAGS} -O2 -DNDEBUG -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE
PROD_LDFLAGS = ${LIBS} -pie -s -Wl,-z,relro,-z,now

DEV_CFLAGS = ${BASE_CFLAGS} -g -O0 -fno-omit-frame-pointer
DEV_LDFLAGS = ${LIBS}

ASAN_CFLAGS = ${BASE_CFLAGS} -g -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all
ASAN_LDFLAGS = ${LIBS} -fsanitize=address -fsanitize=undefined

# Nested macro expansion
CFLAGS = ${${MODE}_CFLAGS}
LDFLAGS = ${${MODE}_LDFLAGS}
