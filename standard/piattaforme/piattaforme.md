# Piattaforme: Windows, macOS, Linux

Un solo codice C11 per le tre piattaforme. Le differenze stanno in `CMakeLists.txt` e dentro le
due librerie; nel codice del synth non ci sono `#ifdef` di piattaforma.

## Librerie
| Libreria | Versione | Per cosa | Come arriva |
|---|---|---|---|
| SDL3 | ultima release stabile, tag fisso scritto in `CMakeLists.txt` | finestra, disegno, audio, mouse, tastiera, gamepad, cartelle dei dati | `FetchContent` di CMake, collegata statica |
| RtMidi | 6.0.0 | ingresso MIDI (API C `rtmidi_c.h`) | `FetchContent`, statica |

Nessun'altra dipendenza senza decisione esplicita.

## Compilare
Serve CMake 3.24 o piu' (lo chiedono RtMidi e, su macOS, SDL3).
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

| | Windows | macOS | Linux |
|---|---|---|---|
| Compilatore | MSYS2 UCRT64 (gcc); MSVC non provato | Xcode Command Line Tools (clang) | gcc (Ubuntu 22.04) |
| MIDI (dentro RtMidi) | WinMM | CoreMIDI | ALSA sequencer (JACK spento) |
| Audio (dentro SDL) | WASAPI | CoreAudio | PipeWire / PulseAudio / ALSA |
| Pacchetti da installare | — | — | vedi sotto |
| Consegna | `storm.exe` unico, senza console | `storm.app` universale arm64 + x86_64 (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`), macOS 11 o piu', firmata ad-hoc (`codesign -s -`) | `storm` in un `.tar.gz` (lo zip perde il permesso di esecuzione); compilato su Ubuntu 22.04, gira con glibc 2.35 o piu' |
| Porte MIDI virtuali | no (serve loopMIDI) | si' | si' |

Note:
- Windows: l'exe non apre la console; per vedere i messaggi in un terminale:
  `storm --parametri > esito.txt 2>&1`.
- macOS: un'app scaricata e non notarizzata, la prima volta: da macOS 15 tasto destro > Apri non
  basta, serve Impostazioni di Sistema > Privacy e sicurezza > "Apri comunque" (oppure
  `xattr -dr com.apple.quarantine storm.app`). Il MIDI non chiede permessi.
- Linux: l'utente deve poter usare il sequencer ALSA (di solito si'). Pacchetti per compilare
  (Ubuntu/Debian). Su Ubuntu 22.04 apt da' CMake 3.22, troppo vecchio: prendere il 3.24 o piu'
  con `pip install cmake` oppure dal repository apt di Kitware (apt.kitware.com).
  ```bash
  sudo apt-get install build-essential cmake ninja-build pkg-config libasound2-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libibus-1.0-dev
  ```

## Audio
- Frequenza del dispositivo (44100 o 48000 Hz): il synth calcola alla sua frequenza interna
  (storm: 44100 Hz come la SPU della PS1) e SDL converte.
- Buffer piccolo: obiettivo **meno di 15 ms dal tasto MIDI al suono**. Il thread audio non alloca
  memoria, non apre file, non aspetta lock: riceve le azioni da una coda senza blocchi.

## File dell'utente
Nella cartella dati che da' SDL (`SDL_GetPrefPath("", "<synth>")`, senza cartella dell'autore; storm:
`SDL_GetPrefPath("", "storm")`):
| Piattaforma | Cartella |
|---|---|
| Windows | `%APPDATA%\storm\` |
| macOS | `~/Library/Application Support/storm/` |
| Linux | `~/.local/share/storm/` |

Contenuto: `scene.txt` (le 8 scene, al posto della memory card; formato in
[../controlli/scene.md](../controlli/scene.md)), `mappa.txt` (mappa MIDI
dell'utente), `finestra.txt` (dimensione, tutto schermo, ultima porta MIDI vista).
I file sono testo, scritti in un file temporaneo e poi rinominati (mai un file a meta').

## Schermo
Disegno interno a bassa risoluzione in un buffer in memoria, poi una texture SDL scalata a
fattore intero con filtro "nearest". Alta densita' (Retina, 4K) gestita da SDL.
