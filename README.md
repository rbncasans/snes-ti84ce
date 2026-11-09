# snes-ti84ce

Emulador original de Super Nintendo (SNES) para la calculadora **TI-84 Plus CE / CE Python Edition** (eZ80 a 48 MHz, ~154 KB de RAM, pantalla 320×240).

> Proyecto experimental y educativo. El código es original; no se incluye ninguna ROM, BIOS ni recurso con copyright. Usa únicamente copias de seguridad de juegos que poseas legalmente o ROMs homebrew de dominio público.

## Estado

🚧 En desarrollo.

- ✅ **CPU 65C816**: intérprete completo (256 opcodes, modos 8/16 bits, modo emulación, aritmética decimal, NMI/IRQ/BRK/COP, WAI/STP, MVN/MVP). Verificado contra la batería [SingleStepTests](https://github.com/SingleStepTests/65816) (registros y RAM; los ciclos son aproximados).
- ✅ **Mapa de memoria**: LoROM, HiROM y ExHiROM con espejos (también ROMs que no miden una potencia de 2), WRAM de 128 KB con sus espejos y el puerto `$2180`, SRAM, registros `$42xx` (multiplicación, división, NMI/IRQ, RDNMI/TIMEUP/HVBJOY), DMA de propósito general y detección de la cabecera del cartucho. La ROM se lee directamente de las AppVars archivadas en Flash (sin copiarla a RAM). Ver `src/mem/mem.h`.
- ⏳ PPU, entrada. El audio (SPC700) probablemente se omitirá (hay una APU falsa que responde a los juegos para que no se cuelguen).

Ahora mismo el programa de la calculadora ejecuta un autotest (CPU + mapa LoROM), busca ROMs convertidas con `tools/rom-converter.html`, carga la elegida, comprueba que la cabecera se ve en `$00:FFC0` y ejecuta la CPU mostrando PC, instrucciones por segundo y fotogramas por segundo (todavía sin imagen).

## Requisitos

- [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain) (`cedev`) instalado y en el `PATH`.
- Bibliotecas CE (`graphx`, `keypadc`, `fileioc`) en la calculadora (paquete [clibs](https://tiny.cc/clibs)).
- Opcional: emulador [CEmu](https://ce-programming.github.io/CEmu/) para probar sin la calculadora.

## Compilar

```sh
make            # genera bin/SNESCE.8xp
make clean
```

Envía `bin/SNESCE.8xp` a la calculadora con TI Connect CE o TILP y ejecútalo desde el menú de programas (o con un shell como Cesium).

## Tests en PC

```sh
make -C tests          # tests propios, rápidos, sin dependencias
make -C tests sst      # batería SingleStepTests completa (descarga ~2 GB por partes)
make -C tests sst OPS="a9 69"   # solo algunos opcodes
```

## Estructura

```
src/
  main.c        Punto de entrada: ejecuta el autotest y muestra resultados
  bus.h         Interfaz del bus de 24 bits que usa la CPU
  mem/mem.c     Mapa de memoria SNES (bus_read/bus_write, E/S, DMA)
  mem/cart.c    Detección de la cabecera (LoROM/HiROM/ExHiROM)
  mem/rom_ce.c  Carga de la ROM desde AppVars y reparto de la WRAM en la CE
  demo_rom.h    Programa de autotest de la CPU
  cpu/65c816.c  Intérprete de la CPU 65C816
  cpu/65c816.h
tests/
  test_cpu.c    Tests de la CPU en PC
  test_mem.c    Tests del mapa de memoria en PC
  sst/          Ejecutor de la batería SingleStepTests
makefile        Configuración del CE C Toolchain
```

## Hoja de ruta

1. ~~Núcleo CPU 65C816~~ ✅
2. ~~Mapa de memoria (LoROM/HiROM) leyendo la ROM desde AppVars en Flash~~ ✅
3. PPU simplificada (modo 1, escalado 256×224 → 320×240).
4. Entrada con `keypadc`.
5. Audio: probablemente omitido (la calculadora no tiene salida de sonido).

## Licencia

Por decidir.
