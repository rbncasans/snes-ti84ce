# Tests de PC (no usan el CE Toolchain): make -C tests
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
SRC = ../src/cpu/65c816.c

MEM_SRC = ../src/mem/mem.c ../src/mem/cart.c

test: test_cpu test_mem
	./test_cpu
	./test_mem

test_mem: test_mem.c $(SRC) $(MEM_SRC) ../src/mem/mem.h ../src/mem/cart.h ../src/bus.h ../src/demo_rom.h
	$(CC) $(CFLAGS) -I../src test_mem.c $(SRC) $(MEM_SRC) -o $@

test_cpu: test_cpu.c $(SRC) ../src/cpu/65c816.h ../src/bus.h ../src/demo_rom.h
	$(CC) $(CFLAGS) -I../src test_cpu.c $(SRC) -o $@

# Batería externa SingleStepTests (descarga ~2 GB poco a poco; tarda un rato)
sst: sst/libsst.so
	python3 sst/run_sst.py sst/libsst.so $(OPS)

sst/libsst.so: sst/harness.c $(SRC) ../src/cpu/65c816.h
	$(CC) -O2 -shared -fPIC -I../src sst/harness.c $(SRC) -o $@

clean:
	rm -f test_cpu test_mem sst/libsst.so

.PHONY: test sst clean
