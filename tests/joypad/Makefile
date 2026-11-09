# Tests del mando en el PC (no necesitan el CE Toolchain).
CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Werror -Istub

test: joypad_test
	./joypad_test

joypad_test: joypad_test.c ../../src/input/joypad.c ../../src/input/joypad.h stub/keypadc.h
	$(CC) $(CFLAGS) -o $@ joypad_test.c ../../src/input/joypad.c

clean:
	rm -f joypad_test

.PHONY: test clean
