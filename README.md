# snes-ti84ce

Emulador original de Super Nintendo (SNES) para la calculadora **TI-84 Plus CE / CE Python Edition** (eZ80 a 48 MHz, ~154 KB de RAM, pantalla 320×240).

> Proyecto experimental y educativo. El código es original; no se incluye ninguna ROM, BIOS ni recurso con copyright. Usa únicamente copias de seguridad de juegos que poseas legalmente o ROMs homebrew de dominio público.

## Estado

🚧 Esqueleto inicial: el programa arranca, muestra una pantalla y espera una tecla. El núcleo de emulación (CPU 65C816, PPU, APU SPC700) está por hacer.

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

## Estructura

```
src/
  main.c        Punto de entrada: inicializa gráficos y el bucle principal
  cpu/65c816.c  Núcleo de la CPU 65C816 (pendiente)
  cpu/65c816.h
  input/joypad.c  Mando SNES: teclado -> registros $4016/$4017 y $4218-$421F
  input/joypad.h
tests/joypad/   Tests del mando en el PC (`make -C tests/joypad`)
makefile        Configuración del CE C Toolchain
```

## Controles

| SNES | Calculadora |
|------|-------------|
| Cruceta | flechas |
| Y | `[2nd]` |
| X | `[mode]` |
| B | `[alpha]` |
| A | `[X,T,θ,n]` |
| L | `[y=]` |
| R | `[graph]` |
| Select | `[+]` |
| Start | `[enter]` |
| Salir del emulador | `[clear]` |

Y, X, B y A ocupan el bloque 2×2 a la izquierda de las flechas con la misma posición relativa que en el mando de la SNES (Y/X arriba, B/A abajo; Y/B a la izquierda, X/A a la derecha), así se juega con el pulgar izquierdo en los botones y el derecho en la cruceta, o al revés. L y R están en las teclas de los extremos de la fila superior. Pulsar dos direcciones opuestas a la vez se ignora, como en una cruceta real. La tabla está en `src/input/joypad.c` (`key_map`) y se puede cambiar fácilmente.

El bus de la CPU debe enviar a `joypad_read`/`joypad_write` las direcciones `$4016`, `$4017` y `$4218-$421F`, y llamar a `joypad_poll()` + `joypad_auto_read()` al empezar cada VBlank cuando `$4200` bit 0 está activo. Solo se emula el mando 1; los mandos 2-4 se leen como desconectados.

## Hoja de ruta

1. Núcleo CPU 65C816 con decodificador de opcodes.
2. Mapa de memoria (LoROM/HiROM) leyendo la ROM desde AppVars en Flash.
3. PPU simplificada (modo 1, escalado 256×224 → 320×240).
4. ~~Entrada con `keypadc`.~~ Hecho (ver Controles).
5. Audio: probablemente omitido (la calculadora no tiene salida de sonido).

## Licencia

Por decidir.
