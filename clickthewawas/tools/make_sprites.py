#!/usr/bin/env python3
"""
Genera src/sprites.c y src/sprites.h a partir de las imágenes de assets/.

  assets/wawa.png       foto del wawa (se recorta el gato y se quita el fondo)
  assets/petrovich.png  Petrovich (PNG con transparencia)

Uso (desde la carpeta clickthewawas):  python3 tools/make_sprites.py
Necesita Pillow. Los .c/.h generados se suben al repo, así que la
compilación normal (make) no necesita Python.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
SRC = ROOT / "src"

# Número de colores fijos de la paleta (enum C_* de wawas.h). Los colores de
# las imágenes van a continuación.
FIXED_COLORS = 29
MAX_EXTRA = 256 - FIXED_COLORS

GB = [(15, 56, 15), (48, 98, 48), (139, 172, 15), (155, 188, 15)]

# Tintes de los wawas de colores (mismo orden que el enum W_* de wawas.h)
VARIANTS = [
    ("normal", None),
    ("red", (235, 50, 50)),
    ("blue", (70, 120, 240)),
    ("green", (70, 200, 80)),
    ("yellow", (250, 215, 40)),
    ("purple", (170, 80, 220)),
    ("gb", "gb"),
]


def wawa_cutout():
    """Recorta el gato de la foto y quita el fondo con una máscara."""
    im = Image.open(ASSETS / "wawa.png").convert("RGBA")
    mask = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(mask)
    d.ellipse([392, 282, 1180, 900], fill=255)      # cabeza
    d.ellipse([368, 560, 1245, 1385], fill=255)     # cuerpo
    d.ellipse([448, 1270, 650, 1430], fill=255)     # patas
    d.ellipse([785, 1330, 990, 1500], fill=255)
    d.ellipse([920, 1260, 1080, 1470], fill=255)
    im.putalpha(mask)
    im = im.crop((360, 270, 1260, 1505))
    rgb = ImageEnhance.Contrast(im.convert("RGB")).enhance(1.25)
    out = rgb.convert("RGBA")
    out.putalpha(im.getchannel("A"))
    return out


def fit(img, w, h, bottom=True):
    """Escala la imagen para que quepa en w x h, centrada y apoyada abajo."""
    s = img.copy()
    s.thumbnail((w, h), Image.LANCZOS)
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    y = h - s.height if bottom else (h - s.height) // 2
    canvas.paste(s, ((w - s.width) // 2, y), s)
    return canvas


def harden(img):
    """Alfa binario y contorno negro de 1 px (para que se vea sobre blanco)."""
    w, h = img.size
    px = img.load()
    solid = [[px[x, y][3] >= 110 for x in range(w)] for y in range(h)]
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    for y in range(h):
        for x in range(w):
            if solid[y][x]:
                r, g, b, _ = px[x, y]
                o[x, y] = (r, g, b, 255)
            else:
                near = any(
                    0 <= x + dx < w and 0 <= y + dy < h and solid[y + dy][x + dx]
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                )
                if near:
                    o[x, y] = (0, 0, 0, 255)
    return out


def tint(img, how):
    if how is None:
        return img
    out = img.copy()
    p = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = p[x, y]
            if not a:
                continue
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if how == "gb":
                p[x, y] = GB[min(3, lum * 4 // 256)] + (a,)
            else:
                tr, tg, tb = how
                f = 0.35 + 0.65 * lum / 255
                p[x, y] = (int(tr * f), int(tg * f), int(tb * f), a)
    return out


def dead(img):
    """Wawa muerto: rojo y aplastado."""
    w, h = img.size
    flat = tint(img, (230, 40, 40)).resize((w, max(3, h // 3)), Image.LANCZOS)
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    canvas.paste(flat, (0, h - flat.height), flat)
    return canvas


def main():
    cat = wawa_cutout()
    petro_src = Image.open(ASSETS / "petrovich.png").convert("RGBA")

    sprites = []  # (nombre C, imagen RGBA)
    for size in (16, 24):
        base = fit(cat, size, size)
        for name, how in VARIANTS:
            sprites.append((f"wawa{size}_{name}", harden(tint(base, how))))
        sprites.append((f"dead{size}", harden(dead(base))))
    sprites.append(("wawa64", harden(fit(cat, 64, 64))))
    petro = petro_src.copy()
    petro.thumbnail((40, 64), Image.LANCZOS)
    sprites.append(("petro", harden(petro)))

    # Paleta común para todas las imágenes
    opaque = []
    for _, im in sprites:
        opaque += [p[:3] for p in im.getdata() if p[3]]
    strip = Image.new("RGB", (len(opaque), 1))
    strip.putdata(opaque)
    palimg = strip.quantize(colors=MAX_EXTRA, method=Image.Quantize.MEDIANCUT)
    pal = palimg.getpalette()[: MAX_EXTRA * 3]
    ncolors = len(set(palimg.getdata()))
    ncolors = max(palimg.getdata()) + 1

    c = ["/* Generado por tools/make_sprites.py. No editar a mano. */",
         '#include "sprites.h"', ""]
    c.append(f"const uint8_t sprite_palette[{ncolors}][3] = {{")
    for i in range(ncolors):
        c.append(f"    {{{pal[i*3]}, {pal[i*3+1]}, {pal[i*3+2]}}},")
    c.append("};")
    c.append("")

    h = ["/* Generado por tools/make_sprites.py. No editar a mano. */",
         "#ifndef SPRITES_H", "#define SPRITES_H", "#include <stdint.h>", "",
         f"#define SPRITE_PAL_FIRST {FIXED_COLORS}",
         f"#define SPRITE_PAL_COUNT {ncolors}",
         "extern const uint8_t sprite_palette[SPRITE_PAL_COUNT][3];", ""]

    for name, im in sprites:
        q = im.convert("RGB").quantize(palette=palimg, dither=Image.Dither.NONE)
        idx = list(q.getdata())
        alpha = list(im.getchannel("A").getdata())
        data = [0 if not a else FIXED_COLORS + i for i, a in zip(idx, alpha)]
        w, hh = im.size
        c.append(f"const uint8_t spr_{name}_data[{2 + w*hh}] = {{")
        c.append(f"    {w}, {hh},")
        for y in range(hh):
            row = data[y * w:(y + 1) * w]
            c.append("    " + ", ".join(str(v) for v in row) + ",")
        c.append("};")
        c.append("")
        h.append(f"extern const uint8_t spr_{name}_data[{2 + w*hh}];")
        if name == "petro":
            h.append(f"#define PETRO_W {w}")
            h.append(f"#define PETRO_H {hh}")

    h += ["", "#endif", ""]
    (SRC / "sprites.c").write_text("\n".join(c))
    (SRC / "sprites.h").write_text("\n".join(h))
    print(f"{len(sprites)} sprites, {ncolors} colores extra")


if __name__ == "__main__":
    main()
