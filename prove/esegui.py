"""Lancia tutte le prove (prove/*.txt) con storm --prova e dice OK / FALLITO per ognuna.

Uso: python prove/esegui.py [--lunghe] [--uscite CARTELLA] [percorso di storm]
Senza percorso cerca l'eseguibile in build/. WAV e foto vanno in una cartella temporanea,
cancellata alla fine; con --uscite restano in CARTELLA (nuova o vuota), con CARTELLA/md5.txt (md5 di tutti i
.wav e .png, righe "md5 *./prova/file" ordinate) da confrontare con diff fra due macchine.
--lunghe aggiunge le prove di prove/lunghe/ (10 minuti di suono ciascuna, qualche secondo di calcolo).
Ogni prova ha il suo file delle scene (--scene) nella cartella temporanea: vuoto, o copia di
prove/<nome>.scene; <nome>_riapri.txt gira dopo <nome>.txt e riceve il file che quella ha lasciato.
Esce con 1 se almeno una prova fallisce.
"""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

PROVE = pathlib.Path(__file__).resolve().parent
RADICE = PROVE.parent

ARG = argparse.ArgumentParser()
ARG.add_argument("--lunghe", action="store_true")
ARG.add_argument("--uscite")
ARG.add_argument("storm", nargs="?")
ARGOMENTI = ARG.parse_args()


def trova_storm():
    if ARGOMENTI.storm:
        return pathlib.Path(ARGOMENTI.storm)
    for nome in ("storm.exe", "storm", "Release/storm.exe", "storm.app/Contents/MacOS/storm"):
        p = RADICE / "build" / nome
        if p.is_file():
            return p
    sys.exit("storm non trovato in build/: compila prima o passa il percorso")


def main():
    storm = trova_storm()
    fallite = 0
    with tempfile.TemporaryDirectory() as tmp:
        if ARGOMENTI.uscite:
            tmp = ARGOMENTI.uscite
            pathlib.Path(tmp).mkdir(parents=True, exist_ok=True)
        # <nome>_riapri subito dopo <nome>
        prove = sorted(PROVE.glob("*.txt"), key=lambda p: (p.stem.removesuffix("_riapri"), p.stem.endswith("_riapri")))
        if ARGOMENTI.lunghe:
            prove += sorted((PROVE / "lunghe").glob("*.txt"))
        for prova in prove:
            cartella = pathlib.Path(tmp) / prova.stem
            cartella.mkdir()
            mappa = prova.with_suffix(".mappa")  # una prova puo' avere la sua mappa MIDI
            scene = cartella / "scene.txt"
            prima = prova.with_suffix(".scene")
            if prova.stem.endswith("_riapri"):  # chiudi e riapri: il file lasciato dalla prova di prima
                prima = pathlib.Path(tmp) / prova.stem.removesuffix("_riapri") / "scene.txt"
            if prima.is_file():
                shutil.copyfile(prima, scene)
            r = subprocess.run([str(storm), "--prova", str(prova), "--wav", str(cartella / "uscita.wav"),
                                "--foto", str(cartella), "--seme", "1", "--scene", str(scene)] +
                               (["--mappa", str(mappa)] if mappa.is_file() else []), capture_output=True, text=True)
            ok = r.returncode == 0
            print(f"{'OK      ' if ok else 'FALLITO '} {prova.name}" + ("" if ok else f" (uscita {r.returncode})"))
            if not ok:
                fallite += 1
                sys.stdout.write(r.stdout + r.stderr)
        if ARGOMENTI.uscite:
            file = sorted(f.relative_to(tmp).as_posix() for f in pathlib.Path(tmp).rglob("*")
                          if f.suffix in (".wav", ".png"))
            righe = [f"{hashlib.md5((pathlib.Path(tmp) / f).read_bytes()).hexdigest()} *./{f}\n" for f in file]
            (pathlib.Path(tmp) / "md5.txt").write_text("".join(righe), newline="\n")
    print(f"{fallite} fallite" if fallite else "tutte OK")
    return 1 if fallite else 0


if __name__ == "__main__":
    sys.exit(main())
