# Tests de PC (no usan el CE Toolchain): make -C tests
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
SRC = ../src/cpu/65c816.c

MEM_SRC = ../src/mem/mem.c ../src/mem/cart.c
PPU_SRC = ../src/ppu/ppu.c ../src/ppu/ppu_demo.c

EMU_SRC = ../src/emu.c ../src/input/joypad.c $(MEM_SRC) ../src/ppu/ppu.c $(SRC)

test: test_cpu test_mem test_ppu test_emu
	./test_cpu
	./test_mem
	./test_ppu
	./test_emu

# HDMA y bucle principal (usa el keypadc.h falso de los tests del mando)
test_emu: test_emu.c $(EMU_SRC) ../src/emu.h ../src/mem/mem.h ../src/ppu/ppu.h ../src/input/joypad.h
	$(CC) $(CFLAGS) -I../src -Ijoypad/stub test_emu.c $(EMU_SRC) -o $@

test_mem: test_mem.c $(SRC) $(MEM_SRC) ../src/mem/mem.h ../src/mem/cart.h ../src/bus.h ../src/demo_rom.h
	$(CC) $(CFLAGS) -I../src test_mem.c $(SRC) $(MEM_SRC) -o $@

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
	rm -rf test_cpu test_mem test_ppu test_emu out sst/libsst.so

.PHONY: test demos sst clean
