# storm for PC

Computer version of **storm**, the drone synth born on the PAL PS1. You play it with a **MIDI
keyboard**, the **mouse**, the PC keyboard or one or two gamepads, on **Windows, macOS and Linux**.

English translation of [LEGGIMI.md](LEGGIMI.md). The linked documents in `standard/` are in Italian.
File names, test events and MIDI map keywords (`tasto`, `controlla`, `zona`, `spento`...) are the
real ones the program reads, so they stay as they are.

The executables for Windows, macOS and Linux can be downloaded from
<https://github.com/VitreousSpaghetti/Storm/releases>.

## Folders
| Folder | Contents |
|---|---|
| `originale_psx/` | Sources of the PS1 version (read-only reference: the sound and behaviour to respect) |
| `standard/` | Controls standard (MIDI, mouse, keyboard, gamepad), platforms and test mode, reusable for other synths |
| `src/` | C sources of the PC version: `storm.c` (the PS1 program, changed as little as possible), `psx.h` + `psx_spu.c` + `psx_gpu.c` (the PS1 functions it uses, done in software), `controlli.h` + `controlli.c` (controls standard: parameter table, common actions, PC keyboard, gamepad, mouse and MIDI with the map file; nothing specific to storm inside), `midi.c` (MIDI ports with RtMidi), `main.c` (window, audio, test mode) |
| `prove/` | Test mode event files and `esegui.py`, which runs them all; `prove/lunghe/` holds the 10-minute tests |
| `mappe/` | Default MIDI map (`storm.txt`, compiled into the program) |
| `script/` | Texts of the GUIDE and TUTORIAL (`testi.txt`) and `testi.py`, which measures them and writes `src/testi.h` |

## Building
For the general picture see [standard/piattaforme/piattaforme.md](standard/piattaforme/piattaforme.md).
You need CMake 3.24 or later (required by RtMidi and, on macOS, by SDL3).
SDL3 (release 3.4.20) and RtMidi 6.0.0 download themselves at the first configuration.

On Windows with MSYS2 UCRT64 gcc and the cmake/ninja from PSn00bSDK (the PATH changes only inside
the script):
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```
The result is `build\storm.exe`, a single exe that needs no MSYS2 DLLs and does not open the black
console window.

On macOS (11 or later) you need the Xcode Command Line Tools (`xcode-select --install`) and CMake:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```
The result is `build/storm.app`, universal (Apple Silicon and Intel) and ad-hoc signed.

On Linux (Ubuntu/Debian; built on Ubuntu 22.04 it also runs on distros that are 2-3 years old).
On Ubuntu 22.04 apt gives CMake 3.22, which is too old: get 3.24 or later with `pip install cmake`
or from the Kitware apt repository (apt.kitware.com).
```bash
sudo apt-get install build-essential cmake ninja-build pkg-config libasound2-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libibus-1.0-dev
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
The result is `build/storm`. Without `libasound2-dev` the configuration stops (without ALSA there
would be no MIDI).

`.github/workflows/build.yml` does the same builds on GitHub at every push, runs the tests on the
three platforms (on macOS both arm64 and x86_64) and keeps the packages: `storm-windows` (the exe
and the LEGGIMI.md file), `storm-macos.zip` (the app), `storm-linux-x86_64.tar.gz` (the tar keeps
the execute permission), plus the `md5.txt` files of WAVs and pictures, which must be identical on
all of them.

## Running
The program is `build\storm.exe` on Windows, `build/storm.app` on macOS (from the terminal:
`build/storm.app/Contents/MacOS/storm`) and `build/storm` on Linux; the examples below are for
Windows, with the same options on the others.

| Command | What it does |
|---|---|
| `build\storm.exe` | Enlarged 320x256 window (resizable; F11 = full screen) and the storm drone |
| `build\storm.exe --prova prove\avvio.txt --wav output.wav --foto folder\ --seme 1` | Test mode with no window and no audio (see [standard/prove/prove.md](standard/prove/prove.md)) |
| `build\storm.exe --mappa my.txt` | Uses this MIDI map instead of the user's one (MIDI learn writes here) |
| `build\storm.exe --scene my_scenes.txt` | Reads and writes the scenes in this file instead of the user's one |
| `build\storm.exe --parametri` | Prints the parameter table in Markdown |
| `python prove\esegui.py` | Runs all the tests and says OK / FALLITO (failed) (`--lunghe`: also the 10-minute ones; `--uscite FOLDER`: keeps WAVs and pictures and writes `FOLDER/md5.txt`) |
| `python script\testi.py` | After changing `script\testi.txt`: rewrites `src\testi.h` (then build) |

- Windows: the exe has no console, so in Command Prompt or PowerShell it prints nothing on screen;
  to see the messages: `build\storm.exe --parametri > output.txt 2>&1` (also works for test mode).
- macOS, the first time you open a downloaded (non-notarized) app: since macOS 15 right-click > Open
  is not enough; open the app, then System Settings > Privacy & Security > "Open Anyway".
  Or from the terminal: `xattr -dr com.apple.quarantine storm.app`.
- Linux: after unpacking the `.tar.gz`, run `./storm` from its folder.

## Status
| Part | Status |
|---|---|
| Build, window, test mode (WAV, PNG, checks) | done; Windows: build and test mode verified; macOS and Linux: build and CI written, not tested |
| Sound: DSP engine, SPU (24 voices, ADPCM, Gaussian, PMON, noise, reverb), MOTION, macros, SEQ, PULSE, CLOCK, in-memory scenes with AUTO | done; verified in test mode (WAV); still to compare by ear with the PS1 |
| Graphics: 3D panels, texts, GUIDE, TUTORIAL (texts for pad, keyboard, mouse and MIDI), VISUAL (PS1 GPU and GTE in software: coverage, Gouraud, dithering, semi-transparency, 4-bit font) | done; verified in test mode (PNG, `prove/grafica.txt`) and in the window |
| PC keyboard and gamepad (one or two, also plugged in while the program runs) | done; verified in test mode (`prove/tastiera.txt`, `prove/pad.txt`); real keyboard and gamepad still to test |
| Mouse (everything playable with the mouse alone) | done; verified in test mode (`prove/mouse.txt`, `prove/mouse_casi.txt`, `prove/tutorial.txt`: including the TUTORIAL with the mouse alone); real mouse still to test |
| MIDI: notes, fixed and page CCs with soft takeover, encoders, 14 bit, pitch bend, pressure, pedal, program change, external clock, MIDI learn, ports plugged in while the program runs | done; verified in test mode (`prove/midi.txt`); real MIDI keyboard still to test; macOS and Linux not tested |
| Scenes on file (`scene.txt`) | done; verified in test mode (`prove/scene.txt`, `prove/scene_riapri.txt`, `prove/scene_file.txt`); still to test in the window |

How it sounds: the synth code is the PS1 code; underneath there is a software SPU that does the
same things as the hardware (same ADPCM decoding, interpolation, envelopes, reverb at 22050 Hz).
The only intended difference: the DSP engine no longer writes 50-80 ms ahead in the ring of voice 0,
but each block exactly when the voice reads it, so there is no delay. Instead of the memory card
the scenes live in a text file (see **Scenes**), and saving does not stop the sound.

## Scenes
SAVE SCENE (GUIDE, or Ctrl + S) puts the sound in one of the 8 slots and rewrites the scene file
right away: there is nothing to save on exit (Ctrl + Q quits immediately). LOAD SCENE, the SCENE
page (GO, HOME, AUTO), program change 1-8 and the command zone recall them, gliding over the MORPH
time. At startup the sound is always the initial one: scenes do not load by themselves.

| System | File |
|---|---|
| Windows | `%APPDATA%\storm\scene.txt` |
| macOS | `~/Library/Application Support/storm/scene.txt` |
| Linux | `~/.local/share/storm/scene.txt` |

The file can be read and fixed with a text editor and copied to another PC. Full format in
[standard/controlli/scene.md](standard/controlli/scene.md); in short:
```
# scene di storm (versione 1)
[scena 2]
CUTOFF = 45
MODE = 0
macro 1 1 = CUTOFF 6
```
One `[scena N]` section for each full slot; `ID = value` for each parameter that belongs in scenes
(ids and values as in `storm --parametri`; choices by number: `MODE = 0` is LP); `macro M P = ID
amount` = slot P of macro M (destination and amount from -9 to 9). A wrong line is skipped with a
warning (`file:line`), an out-of-range value is brought inside the range, a missing parameter takes
its initial value. If the file cannot be written, SCENE FILE NOT WRITTEN appears on screen and the
previous file stays as it was. `--scene FILE` uses another file (also to keep more than one).

## On-screen help
The GUIDE (the KEYS page and the others) and the TUTORIAL explain the controls of the device used
last: pad, PC keyboard, mouse or MIDI keyboard. What counts is the last one that pressed something
(a key, a click or the wheel, a note or a CC; not the clock nor mouse movement alone). Whoever plays
MIDI uses the mouse for the menus: a click does not remove the MIDI texts. At startup: pad.
The help at the bottom of the main screen follows the device too: pad buttons, PC keys, MIDI CCs
(lines in `script/testi.txt`, section `[[AIUTO]]`); with the mouse, the pages and the pointer help.

The TUTORIAL says how far to turn (e.g. "ABOVE 30") and moves on by itself 3 s after the step is
done; to skip a step or go back: PageDown / PageUp on the keyboard, the `>` / `<` buttons under the
step number with the mouse, Start + R1 / L1 on the pad; from the MIDI keyboard only if the map says
so (`zona 37 = AVANTI`, `zona 36 = INDIETRO`). In the TUTORIAL with the mouse the PLAY button is at
the top.

## Playing with the PC keyboard
Keys work by position (names from the US keyboard: on an Italian keyboard they are the same physical
keys). The knobs act on the side of the last page opened (shown in the header, after LP/BP/HP, as
`C1` or `C2`).

| Keys | What they do |
|---|---|
| `1`-`6` | side 1 pages: OSC, GRAIN, NOTE, LAYERS, OSC B, TEXTURE |
| Shift + `1`-`9`, Shift + `-` | side 2 pages: MOD A, MOD B, MOD C, SPACE, MACROS, SEQ, PULSE, CLOCK, SCENE, EDIT |
| tap / hold | tap = pinned page (yellow frame; tap again to close it), hold = as long as you hold |
| `0` / Shift + `0` | side 1 / side 2 at rest (SHAPE / MIX) |
| W S or Up Down | knob 1 (Up = raise) |
| D A or Right Left | knob 2 |
| E Q | knob 3 (like R1 / L1) |
| R F | knob 4 (like R2 / L2) |
| hold / Shift / Ctrl | hold = scrolls like on the pad; Shift = 10 steps; Ctrl = a single step |
| M, P, Space, V | MUTE, PLAY, FREEZE, VISUAL |
| F1 or Tab | GUIDE (Up/Down choose, Enter confirms, Esc goes back) |
| Enter | pad Cross (confirms in menus; outside menus, held, opens the Cross page) |
| H | HOLD: TAIL to 99 (on screen `HOLD`), again = as it was; if you turn TAIL, HOLD turns off and TAIL stays there |
| Ctrl + L | LOCK (everything frozen except MUTE and Ctrl + L) |
| Ctrl + S | GUIDE on SAVE SCENE |
| PageDown / PageUp | TUTORIAL: next / previous step |
| Esc | closes the pinned pages and the GUIDE |
| F11, Ctrl + Q | full screen, quit |

With PLAY on, on side 1 at rest, the same pairs are the pad gestures (Up = SWELL, Down = SINK,
Right/Left = note, E/Q = octave, R/F = chord). Shift held while turning the knobs = steps of 10: to
hold a side 2 page, release Shift and keep holding the number.

## Playing with the mouse
As soon as the mouse passes over the window, the buttons at the top appear, the page names at the
bottom and, below them, two lines explaining what is under the pointer.

| Gesture | Where | What it does |
|---|---|---|
| click | on a panel or on a page name at the bottom | opens the page **pinned** (yellow frame); another click closes it |
| click and hold | on a panel or a page | the page stays open as long as you hold |
| click | outside the panels | closes the pinned pages (and the GUIDE) |
| drag up/down | on a knob of the open page | 1 pixel of the picture = 1 step; with Shift 4 pixels = 1 step; the pointer stays still and hidden, and returns where it was when you let go |
| wheel | on a knob | 1 notch = 1 step, with Shift 10 |
| double click | on a knob | back to the initial value |
| click | on a choice (MODE, ROOT, WAVE...) | next label (wraps around); wheel = up and down |
| right button | on a knob or choice | LEARN / CLEAR / DEFAULT menu (LEARN and CLEAR grey until there is a MIDI port) |
| click | buttons at the top: VISUAL, GUIDE, LOCK; on the note line PLAY, FREEZE, MUTE, HOLD | special actions (on = coloured). LOCK must be held 1 s (also to unlock) |
| click | anywhere, in VISUAL | back to the panels |
| click | `<` `>` under the TUTORIAL step number | previous / next step |
| double click | on the header (STORM, note, NOW line) | full screen (like F11) |

- **PLAY**: with PLAY on and side 1 at rest, at the bottom there is a one-octave strip (from A to
  G# of the current OCTAVE) with OCT- and OCT+. Click a key = that note (ROOT and OCTAVE). Holding
  in the top half of a key makes SWELL grow, in the bottom half SINK (like Up/Down on the pad).
- **GUIDE**: click an item = selects it, another click (or OK at the bottom) = confirms; BACK = goes
  back or closes. In SAVE SCENE and LOAD SCENE a click selects the slot, another saves or loads.
- **Scenes**: saved from the GUIDE (SAVE SCENE). On the SCENE page: click the first row = next slot,
  MORPH with click or wheel, HOME / GO and AUTO / NUDGE are two halves of the row.
- **EDIT**: click the title = next macro; click a row = selects the slot; on the selected row the
  name changes the destination (click or wheel), the bar on the right is dragged for the amount.
- **CLOCK**: TAP with clicks in time; NUDGE: left half = beat a bit earlier, right half = later.

## Playing with a MIDI keyboard
All MIDI input ports are open together; a keyboard plugged in while the program runs is found by
itself within 2 s (and an unplugged one is closed). On Windows a port already used by another
program does not open: storm says so once and retries by itself. On Linux MIDI goes through the ALSA
sequencer: `aconnect -l` shows the ports ("Midi Through" opens too, and does nothing).

| What | What it does |
|---|---|
| note | becomes the drone note (ROOT and OCTAVE, gliding with GLIDE), with PLAY on or off; velocity swells SWELL as long as you hold the key; on release the drone stays on the last note |
| several notes within 50 ms | the lowest is the note, the others choose the chord of the layers (CHORD: 5TH, OCTAVE, 2 OCT, MINOR, MAJOR, SUS4, MIN7); played one after the other = melody (legato) |
| pitch bend | +-2 semitones on top of FINE (FINE does not change) |
| channel aftertouch | SINK gesture: the harder you press, the more macro 2 rises above M2 DEEP; on release it falls back (the knob does not move) |
| CC 74, 71, 91, 7 | CUTOFF, RES, REVERB, VOLUME (also 14 bit with CC 39 after CC 7) |
| CC 1, 11, 5, 72, 93, 94 | M3 UNREST, M1 RISE, GLIDE, TAIL, LAYERS, DETUNE |
| CC 64 (pedal) | HOLD while down |
| CC 20 / 25 | side 1 / 2 page (value 0-127 divided by the number of pages) |
| CC 21-24 / 26-29 | the 4 knobs of the open page of side 1 / 2 |
| CC 16 | FOLD (absolute; for an endless encoder, in the map: `cc 16 = FOLD relativo`) |
| program change 1-8 (0-7) | scenes 1-8 (empty slot = nothing) |
| clock | CLOCK follows the tempo (averaged BPM, `E` after the BPM and near the beat dot); Start = beat 1, Stop stops SEQ and PULSE but not the sound; with no clock for 2 s it returns to its own BPM |

- **Soft takeover**: a MIDI knob that is not where the value is does not make it jump: it picks it
  up when it passes over it. Until it has picked it up, there is a grey dot near the knob on screen
  (also after the value was changed by the mouse, the keyboard or a scene). Two CCs on the same
  parameter: each has to pick it up by itself.
- **MIDI learn**: right button on a knob > LEARN, then move a MIDI control: it is now assigned. CLEAR
  removes the MIDI controls of that knob. Esc or a click elsewhere cancels LEARN.
- **Map**: the default one is `mappe/storm.txt` (format in
  [standard/midi/midi.md](standard/midi/midi.md)). Learn assignments go into the user map, which wins
  over the default one and can be edited by hand: `%APPDATA%\storm\mappa.txt` (Windows),
  `~/Library/Application Support/storm/mappa.txt` (macOS), `~/.local/share/storm/mappa.txt` (Linux).
  `cc N = spento` removes a CC from the default map. The **command zone** (notes 36-47 that launch
  scenes 1-8, PLAY, FREEZE, MUTE, VISUAL instead of playing) is turned on with `zona 36-47 accesa`
  in the user map.

## Playing with the gamepad
Each gamepad (PlayStation, Xbox, Switch, generic) is a PS1 pad with the storm controls: hold
Cross/Square/Triangle/Circle = page, tapped twice = pinned, d-pad and shoulder buttons = knobs
(L2/R2 = triggers), Select and Start as on the PS1, sticks = extra knobs.

| Buttons | What they do |
|---|---|
| pad 1 at rest (SHAPE): Up/Down, Left/Right, L1/R1, L2/R2 | CUTOFF, RES, MODE, DRIVE |
| pad 1: hold Cross / Square / Triangle / Circle | OSC, GRAIN, NOTE, LAYERS |
| pad 1: Select held / + Cross | OSC B / TEXTURE |
| pad 1: Select tapped | PLAY (the d-pad plays: Up = SWELL, Down = SINK, Left/Right = note, L1/R1 = octave, L2/R2 = chord) |
| pad 1: Start | MUTE (fades) |
| pad 1: Start + Cross held 2 s | LOCK |
| pad 2 at rest (MIX): Up/Down, Left/Right, L1/R1, L2/R2 | M1 RISE, REVERB, LAYERS, VOLUME |
| pad 2: hold Cross / Square / Triangle / Circle | MOD A, MOD B, MOD C, SPACE |
| pad 2: Select held, + Cross / Square / Triangle / Circle | MACROS; SEQ, PULSE, CLOCK, EDIT |
| pad 2: Start tapped / held | FREEZE / SCENE (Up/Down scene, Left/Right MORPH, R1 GO, L1 HOME, R2 NUDGE, L2 AUTO) |
| Start + Select tapped / held 1 s | VISUAL / GUIDE (Up/Down choose, Cross confirms, Triangle goes back) |
| Start + R1 / L1 | TUTORIAL: next / previous step |

Buttons go by position: Cross = bottom button (A on Xbox), Circle = right, Square = left, Triangle =
top, Select = View/Back, Start = Menu/Start. The first gamepad connected is controller 1, the second
controller 2. With a single gamepad the Guide button (PS, Xbox, Home) switches from controller 1 to
2 and back (`C1` or `C2` in the header; with two gamepads it is not shown). A third gamepad drives
nothing until one of the first two is disconnected: then it takes its place.

## Test mode
`tasto NAME giu|su` goes through the same translation as the real keyboard (Shift and Ctrl are keys
like the others: `shift giu` before the number). `pad1` and `pad2` are the first and second gamepad
(`pad2` connects at its first event): same PS1 button names, plus `guide`; `l2` and `r2` are the
triggers; `pad1 leva sx|dx X Y` (from -1 to 1, Y = -1 up) moves a stick and leaves it there.
An action takes effect from the exact sample of its instant (in the window from the next audio
block, at most ~6 ms); pad buttons and sticks are read at every frame (every 1/50 s), as on the PS1.
`controlla NAME = V` reads the set value of a parameter (ids in `storm --parametri`) or the 0/1 state
of a switch (MUTE, PLAY, FREEZE, VISUAL, GUIDE, LOCK, HOLD); `controlla pagina 1 = OSC` the open page
of a side. More than 256 actions at the same instant are an error (exit code 2). `mouse` events
(`giu X Y`, `su`, `muovi X Y`, `rotella X Y N`, coordinates in the picture) go through the same
translator as the real mouse; also `mouse destro X Y` = right-button click. A double click is two
`giu` on the same thing within 0.4 s; Shift with the mouse = `tasto shift giu`. A test also fails if
the picture has a clickable zone smaller than 12x12 pixels. `midi` events (`cc N V`, `nota N V`,
`programma N`, `piega V`, `pressione V`, with optional `canale C`) go through the same translator as
messages from real ports; `clock start|stop|continua|tick` are the clock messages and `clock bpm X`
sends the ticks by itself, at exact samples, until `clock stop`. In test mode the map is the default
one or the one from `--mappa`, and learn writes only to `--mappa`.
Scenes, in test mode, live only in the `--scene` file (without it, they stay in memory): test mode
never touches the data folder. `esegui.py` gives each test its own scene file (empty, or a copy of
`prove/<name>.scene`); `<name>_riapri.txt` receives the one left by `<name>.txt` (= close and
reopen). The keys `pagsu` and `paggiu` are PageUp and PageDown; `controlla dispositivo =
PAD|TASTIERA|MOUSE|MIDI` reads the device used last, `controlla passo = N` the TUTORIAL step (0 =
closed).
