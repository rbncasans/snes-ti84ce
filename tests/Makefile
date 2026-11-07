# Tests de PC (no usan el CE Toolchain): make -C tests
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
SRC = ../src/cpu/65c816.c

PPU_SRC = ../src/ppu/ppu.c ../src/ppu/ppu_demo.c

test: test_cpu test_ppu
	./test_cpu
	./test_ppu

test_ppu: test_ppu.c $(PPU_SRC) ../src/ppu/ppu.h ../src/ppu/ppu_demo.h
	$(CC) $(CFLAGS) -I../src test_ppu.c $(PPU_SRC) -o $@

# Guarda las escenas de demostración en tests/out/modoN.ppm
demos: test_ppu
	mkdir -p out
	./test_ppu --dump

test_cpu: test_cpu.c $(SRC) ../src/cpu/65c816.h ../src/bus.h ../src/demo_rom.h
	$(CC) $(CFLAGS) -I../src test_cpu.c $(SRC) -o $@

# Batería externa SingleStepTests (descarga ~2 GB poco a poco; tarda un rato)
sst: sst/libsst.so
	python3 sst/run_sst.py sst/libsst.so $(OPS)

sst/libsst.so: sst/harness.c $(SRC) ../src/cpu/65c816.h
	$(CC) -O2 -shared -fPIC -I../src sst/harness.c $(SRC) -o $@

clean:
	rm -rf test_cpu test_ppu out sst/libsst.so

.PHONY: test demos sst clean
