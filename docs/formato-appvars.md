# Formato de ROM en AppVars (versión 1)

La calculadora no puede guardar una ROM de SNES (256 KB a 4 MB) en una sola
variable: una AppVar admite como mucho unos 64 KB. Por eso el conversor
(`tools/rom-converter.html`) parte la ROM en **trozos de 32 KB** y genera:

| AppVar            | Contenido                                                      |
|-------------------|----------------------------------------------------------------|
| `<P>IDX`          | Índice: cabecera de la ROM, mapa de memoria y tabla de trozos  |
| `<P>Cnn`          | Trozo nº `nn` (hex) clasificado como **código** (programa)      |
| `<P>Gnn`          | Trozo nº `nn` clasificado como **gráficos**                     |
| `<P>Dnn`          | Trozo nº `nn` clasificado como **datos** (audio, tablas, comprimidos…) |
| `<P>SAV`          | SRAM de la partida (ceros al crearla), solo si el cartucho tiene |

`<P>` es un prefijo de 1 a 5 caracteres (`A-Z`, `0-9`, empieza por letra),
derivado del título de la ROM o elegido por el usuario. `nn` es el número de
trozo en hexadecimal con dos cifras: el trozo `nn` contiene los bytes
`nn × 32768 … nn × 32768 + 32767` de la ROM (sin la cabecera de copiadora de
512 bytes de los `.smc`). La letra C/G/D es solo informativa: el emulador
localiza cada trozo **siempre a través de la tabla del índice**, nunca por el
nombre.

Los trozos en los que todos los bytes son iguales (relleno `00`/`FF`) **no se
guardan**: el índice indica el byte de relleno y el emulador lo devuelve sin
leer Flash. Todas las AppVars se marcan como archivadas para que vayan a Flash
al enviarlas.

Los trozos son bytes crudos de la ROM, sin cabecera propia: el byte de ROM en
el desplazamiento `off` está en `trozo[off >> 15][off & 0x7FFF]`. Con
`ti_GetDataPtr()` de `fileioc` se obtiene un puntero directo a Flash, sin
copiar nada a RAM.

- El **último trozo puede ser más corto** que 32 KB si el tamaño de la ROM no
  es múltiplo de 32 KB; su longitud real es el campo "bytes guardados" del
  índice.
- `N` es un byte, así que caben como mucho 255 trozos (~8 MB), más que
  cualquier ROM de SNES. En la práctica el límite es la Flash libre de la
  calculadora (~3 MB): el conversor avisa cuando las variables no caben.
- El emulador aplica el espejado de las ROMs cuyo tamaño no es potencia de 2
  (por ejemplo 3 MB = 2 MB + 1 MB repetido).

## Índice `<P>IDX`

Todos los enteros son little-endian.

| Desp. | Tam. | Campo |
|------:|-----:|-------|
| 0     | 4    | Firma `"SNCE"` (permite encontrar ROMs con `ti_Detect(&pos, "SNCE")`) |
| 4     | 1    | Versión del formato = `1` |
| 5     | 1    | Mapa: `0` LoROM, `1` HiROM, `2` ExHiROM |
| 6     | 1    | Byte de cabecera `$FFD5` (modo de mapa y velocidad) |
| 7     | 1    | Byte de cabecera `$FFD6` (chips del cartucho) |
| 8     | 4    | Tamaño real de la ROM en bytes |
| 12    | 4    | Tamaño de la SRAM en bytes (0 si no tiene) |
| 16    | 1    | log2 del tamaño de trozo (`15` → 32768) |
| 17    | 1    | `N`: número de trozos (= ⌈tamaño ROM / 32768⌉) |
| 18    | 1    | Región (`$FFD9`) |
| 19    | 1    | Flags: bit0 FastROM, bit1 se quitó cabecera de copiadora, bit2 checksum correcto |
| 20    | 2    | Checksum (`$FFDE`) |
| 22    | 2    | Complemento del checksum (`$FFDC`) |
| 24    | 21   | Título ASCII (relleno con espacios) |
| 45    | 3    | 0 |
| 48    | 20   | Vectores u16. Nativo: COP, BRK, ABORT, NMI, IRQ. Emulación: COP, ABORT, NMI, RESET, IRQ/BRK |
| 68    | 8    | Nombre de la AppVar de SRAM (relleno con 0; todo 0 si no hay) |
| 76    | 4    | Reservado (0) |
| 80    | N×12 | Tabla de trozos (ver abajo) |

Entrada de la tabla de trozos (12 bytes):

| Desp. | Tam. | Campo |
|------:|-----:|-------|
| 0     | 8    | Nombre de la AppVar (relleno con 0). Todo 0 si el trozo es de relleno y no se guardó |
| 8     | 1    | Tipo: `0` vacío/relleno, `1` código, `2` gráficos, `3` datos |
| 9     | 1    | Byte de relleno (solo si tipo 0) |
| 10    | 2    | Bytes guardados en la AppVar (32768, menos en el último trozo; 0 si es relleno) |

## Mapa de memoria (para el emulador)

Desplazamiento en la ROM a partir de una dirección SNES `banco:dir`:

- **LoROM**: `dir >= $8000` en cualquier banco (y `$0000-$7FFF` en `$40-$6F`,
  `$C0-$EF` como espejo) → `((banco & $7F) << 15) | (dir & $7FFF)`.
  SRAM en bancos `$70-$7D` (y `$F0-$FF`), `dir < $8000`.
- **HiROM**: bancos `$C0-$FF` y `$40-$7D` completos, y `dir >= $8000` en
  `$00-$3F`/`$80-$BF` → `((banco & $3F) << 16) | dir`.
  SRAM en `$20-$3F`/`$A0-$BF`, `dir $6000-$7FFF`.
- **ExHiROM**: como HiROM, pero los bancos `$C0-$FF`/`$80-$BF` dan la primera
  mitad de 4 MB y `$40-$7D`/`$00-$3F` suman `$400000`.

En todos los casos el resultado se reduce módulo el tamaño de la ROM
(espejos). La WRAM (`$7E-$7F`, 128 KB) no está en la ROM y la reserva el
emulador.

## Sobre la clasificación código / gráficos / datos

En una ROM de SNES el código, los gráficos y el resto de datos están
mezclados y a menudo comprimidos, y no hay ninguna marca que diga qué es cada
cosa. El conversor lo estima así:

1. **Código**: recorre el programa desde los vectores de reset, NMI e IRQ
   siguiendo saltos, llamadas y ramas (desensamblado recursivo con seguimiento
   de `REP`/`SEP`). Lo alcanzado es código seguro. Para el resto mira la
   frecuencia de opcodes típicos del 65C816.
2. **Gráficos**: los tiles de SNES (2/4/8 bpp) guardan cada fila en bytes
   alternos, así que bytes a distancia 2 se parecen mucho. Se mide ese
   parecido y la entropía.
3. **Datos**: lo demás. Con entropía muy alta suele ser información comprimida
   (que puede contener gráficos que el juego descomprime en tiempo real).

La clasificación es orientativa: se puede corregir a mano en la página antes de
generar las variables, y no afecta al funcionamiento del emulador.
