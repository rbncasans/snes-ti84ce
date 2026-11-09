#!/usr/bin/env python3
"""
Ejecuta la batería SingleStepTests del 65816 (https://github.com/SingleStepTests/65816)
contra nuestra CPU. Descarga cada fichero JSON, lo prueba y lo borra (son ~2 GB en total).

Uso:  make -C tests sst              # todos los opcodes
      python3 tests/sst/run_sst.py tests/sst/libsst.so a9 ea   # solo algunos

Compara registros y RAM final (MVN/MVP hasta terminar el bloque); no compara ciclos (nuestro conteo es aproximado).
"""
import ctypes, json, os, sys, tempfile, urllib.request

URL = "https://raw.githubusercontent.com/SingleStepTests/65816/main/v1/{}.json"
REGS = ["pc", "s", "p", "a", "x", "y", "dbr", "d", "pbr", "e"]


def run_file(lib, path):
    out = (ctypes.c_int * 10)()
    block_move = os.path.basename(path)[:2] in ("44", "54")
    tests = json.load(open(path))
    fails, first = 0, None
    for t in tests:
        ini, fin = t["initial"], t["final"]
        for a, v in ini["ram"]:
            lib.poke(a, v)
        lib.set_state(*[ini[k] for k in REGS])
        lib.step()
        lib.get_state(out)
        # MVN/MVP mueven un byte por paso (se pueden interrumpir); los tests
        # esperan el bloque completo, así que repetimos hasta que PC avance.
        while block_move and out[0] == ini["pc"]:
            lib.step()
            lib.get_state(out)
        bad = [f"{k}={out[n]:#x} esperado {fin[k]:#x}" for n, k in enumerate(REGS) if out[n] != fin[k]]
        bad += [f"[{a:#x}]={lib.peek(a):#x} esperado {v:#x}" for a, v in fin["ram"] if lib.peek(a) != v]
        for a, _ in ini["ram"] + fin["ram"]:
            lib.poke(a, 0)
        if bad:
            fails += 1
            first = first or (t["name"], bad[:4])
    return fails, len(tests), first


def main():
    lib = ctypes.CDLL(os.path.abspath(sys.argv[1]))
    ops = sys.argv[2:] or [f"{i:02x}" for i in range(256)]
    total_fail = 0
    with tempfile.TemporaryDirectory() as tmp:
        for op in ops:
            for mode in ("n", "e"):
                name = f"{op}.{mode}"
                path = os.path.join(tmp, name + ".json")
                urllib.request.urlretrieve(URL.format(name), path)
                fails, n, first = run_file(lib, path)
                os.remove(path)
                total_fail += fails
                print(name, "OK" if not fails else f"FALLO {fails}/{n} {first}", flush=True)
    sys.exit(1 if total_fail else 0)


if __name__ == "__main__":
    main()
