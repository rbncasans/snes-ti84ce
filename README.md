# snes-ti84ce

Emulador original de Super Nintendo (SNES) para la calculadora **TI-84 Plus CE / CE Python Edition** (eZ80 a 48 MHz, ~154 KB de RAM, pantalla 320×240).

> Proyecto experimental y educativo. El código es original; no se incluye ninguna ROM, BIOS ni recurso con copyright. Usa únicamente copias de seguridad de juegos que poseas legalmente o ROMs homebrew de dominio público.

## Estado

🚧 En desarrollo.

- ✅ **CPU 65C816**: intérprete completo (256 opcodes, modos 8/16 bits, modo emulación, aritmética decimal, NMI/IRQ/BRK/COP, WAI/STP, MVN/MVP). Verificado contra la batería [SingleStepTests](https://github.com/SingleStepTests/65816) (registros y RAM; los ciclos son aproximados).
- ✅ **PPU** (`src/ppu/`): registros $2100-$213F, VRAM/CGRAM/OAM, modos 0-6, modo 7 (incluido EXTBG), sprites, ventanas en la pantalla principal y mosaico. Ver [PPU](#ppu).
- ⏳ Mapa de memoria SNES, entrada. El audio (SPC700) probablemente se omitirá.

Ahora mismo el programa de la calculadora ejecuta un pequeño autotest de la CPU (`src/demo_rom.h`, ensamblado a mano) y muestra los resultados. Con [enter] se abre la demo de la PPU: una escena generada por código para cada modo gráfico ([<] y [>] cambian de modo) con un contador de fotogramas por segundo.

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
make -C tests          # tests propios, rápidos, sin dependencias (CPU y PPU)
make -C tests demos    # guarda las escenas de la PPU en tests/out/modoN.ppm
make -C tests sst      # batería SingleStepTests completa (descarga ~2 GB por partes)
make -C tests sst OPS="a9 69"   # solo algunos opcodes
```

## Estructura

```
src/
  main.c        Punto de entrada: ejecuta el autotest y muestra resultados
  bus.h / bus.c Bus de memoria (provisional: RAM mínima + programa de autotest)
  demo_rom.h    Programa de autotest de la CPU
  cpu/65c816.c  Intérprete de la CPU 65C816
  cpu/65c816.h
  ppu/ppu.c     PPU: registros, VRAM/CGRAM/OAM y render por líneas (modos 0-7)
  ppu/ppu.h     Interfaz con el bus y el bucle de fotograma
  ppu/ppu_demo.c Escenas de prueba de cada modo (gráficos generados por código)
tests/
  test_cpu.c    Tests de la CPU en PC
  test_ppu.c    Tests de la PPU en PC (registros y píxeles conocidos)
  sst/          Ejecutor de la batería SingleStepTests
makefile        Configuración del CE C Toolchain
```

## Hoja de ruta

1. ~~Núcleo CPU 65C816~~ ✅
2. Mapa de memoria (LoROM/HiROM) leyendo la ROM desde AppVars en Flash.
3. ~~PPU (modos 0-7, sprites)~~ ✅ (falta optimizar, ver abajo)
4. Entrada con `keypadc`.
5. Audio: probablemente omitido (la calculadora no tiene salida de sonido).

## PPU

La PPU renderiza **línea a línea**, así que los cambios a mitad de fotograma (HDMA de scroll, la perspectiva del modo 7, cambios de modo) se ven bien. Cada línea se compone en un búfer de 256 píxeles con un valor de prioridad por píxel y se copia a la pantalla.

| Modo | Capas | Estado |
|------|-------|--------|
| 0 | 4 BG de 2 bpp (paleta propia por BG) | ✅ |
| 1 | BG1/BG2 4 bpp, BG3 2 bpp, bit de prioridad de BG3 | ✅ |
| 2 | BG1/BG2 4 bpp con offset por tile | ✅ |
| 3 | BG1 8 bpp (256 colores), BG2 4 bpp | ✅ (sin color directo) |
| 4 | BG1 8 bpp, BG2 2 bpp, offset por tile | ✅ (sin color directo) |
| 5 | Hires 512 px: BG1 4 bpp, BG2 2 bpp | ✅ a 256 px (se muestra la pantalla principal) |
| 6 | Hires, BG1 4 bpp, offset por tile | ✅ a 256 px |
| 7 | Rotación/escala 128×128 tiles, EXTBG, repetición, volteos | ✅ |

También: tiles de 8×8 y 16×16, mapas de 32/64, volteos, scroll, sprites (128, todos los tamaños de OBSEL, límite de 32 por línea, rotación de prioridad), ventanas 1/2 con lógica en la pantalla principal, mosaico, brillo y forced blank, multiplicador $2134-$2136, contadores H/V y lecturas de VRAM/CGRAM/OAM.

**Cómo se muestra en la calculadora.** El LCD está en modo de paleta de 8 bpp: cada píxel es directamente un índice de CGRAM y la CGRAM (BGR555) se copia a la paleta del LCD (1555) aplicando el brillo. La imagen de 256×224 se dibuja 1:1 centrada en 320×240, sin escalar. La VRAM de la SNES (64 KB) vive en la segunda mitad de la RAM del LCD, así que no consume RAM de usuario; por eso no se puede usar `gfx_SetDrawBuffer()`.

**Limitaciones conocidas** (por la pantalla de 256 colores o por coste):
- **Sin matemática de color** (transparencias, sumas/restas con la subpantalla): solo se dibuja la pantalla principal. Las capas que un juego pone solo en la subpantalla no se ven.
- La paleta se aplica una vez por fotograma: los degradados hechos cambiando la CGRAM a mitad de pantalla no se ven.
- Sin modo de color directo (modos 3, 4 y 7), sin entrelazado ni el límite de 34 tiles de sprites por línea.
- Los modos hires (5 y 6) muestran solo los píxeles impares (la pantalla principal) a 256 de ancho.

**Rendimiento.** Es el punto débil. Un modo 1 típico son unos 4 recorridos de 256 píxeles por línea en C sobre el eZ80, unas 224 líneas por fotograma. Se espera una velocidad de **pocos fotogramas por segundo solo para la PPU**, antes de contar la CPU emulada; la demo muestra los fps reales. Para jugar hará falta saltar fotogramas (no llamar a `ppu_render_line` en los fotogramas saltados) y pasar a ensamblador eZ80 las rutinas internas (decodificación de tiles y composición).

## Licencia

Por decidir.
