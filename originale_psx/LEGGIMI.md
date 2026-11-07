# storm – synth per drone PS1 (PAL)

Disco pronto da masterizzare: `storm.bin` e `storm.cue` in questa cartella (masterizzare dal
`.cue`). Programma nuovo, non un mod: non serve nessun disco originale. Progetto completo in
`DESIGN.md`, testi a schermo (in inglese) in `script/testi.txt`.

## Cosa fa
- Un **drone** che non si ferma mai: la CPU della PS1 calcola il suono principale a 22050 Hz.
  Un oscillatore con 4 forme insieme (SINE, SQUARE, SAW, SUB) e un secondo oscillatore (OSC B) che
  lo modula (FM, AM, PWM). Poi FOLD, rumore, FEEDBACK, DRIVE, un filtro LP/BP/HP con risonanza.
- Sopra suonano **4 strati** (LAYERS) col chip audio della PS1: un'ottava sopra, un po' stonati,
  con le BELLS (metallo) e gli accordi (CHORD). Poi WIND (vento), THUMP (colpo basso a tempo) e il
  riverbero della PS1 in **stereo**, con la coda che si può congelare (TAIL 99 = HOLD).
- **Si muove da solo**: 3 "mani" (MOD A, B, C) girano le manopole a velocità che non si
  ripetono mai; 4 **macro** girano 4 manopole ciascuna. Un sequencer (SEQ) cambia la nota sulla
  scala, due ritmi euclidei (PULSE) danno colpi a tempo.
- **Si suona**: modo PLAY sul d-pad (gonfia, affonda, cambia nota), le levette DualShock, le
  **scene** che scivolano una nell'altra fino a 2 minuti.
- **VISUAL**: Start + Select toccati = una tempesta in mare vista dalla prua di un peschereccio; ogni manopola
  muove qualcosa (pioggia, luce, onde, fulmini, uragano).
- Menu **GUIDE** con una pagina per blocco e un **TUTORIAL** che costruisce un drone partendo da
  una sinusoide.
- **Memory card** nella porta 1: 8 scene (file "STORM DRONE PATCHES", 1 blocco).

## Comandi
Regola: **tieni un tasto = pagina, frecce e dorsali = 4 manopole**. Su, Destra, R1, R2 alzano;
Giù, Sinistra, L1, L2 abbassano. Tenendo una freccia il valore scorre. Il pannello della pagina
viene avanti e mostra i 4 valori; in basso c'è sempre l'aiuto della pagina.
Due tocchi veloci del tasto di una pagina la lasciano aperta (cornice gialla); un altro tocco la chiude.

### Controller 1 — sorgenti e forma
| Tenendo | Su / Giù | Destra / Sinistra | R1 / L1 | R2 / L2 |
|---|---|---|---|---|
| niente = SHAPE | CUTOFF | RES | MODE: LP · BP · HP | DRIVE |
| X = OSC | SINE | SQUARE | SAW | SUB |
| Quadrato = GRAIN | FOLD | NOISE | WIDTH (larghezza della quadra) | FEEDBACK |
| Triangolo = NOTE | ROOT (nota) | OCTAVE | GLIDE | — |
| Cerchio = LAYERS | DETUNE | BELLS | CHORD | COLOR |
| Select = OSC B | FM | AM | RATIO | PWM |
| Select + X = TEXTURE | THUMP (colpo basso sui colpi di PULSE 1) | WIND (vento) | PEDAL: OFF · SUB · LAYERS · BOTH (restano sulla nota di casa) | FINE (±50 cent) |

**Modo PLAY** (Select toccato e lasciato subito, riquadro PLAY in alto): il d-pad si suona.
| Tasto | Cosa fa |
|---|---|
| Su tenuto | SWELL: alza la macro 1 finché lo tieni, poi ricade |
| Giù tenuto | SINK: alza la macro 2 finché lo tieni, poi ricade |
| Sinistra / Destra | nota giù / su sulla scala |
| L1 / R1 | ottava giù / su |
| L2 / R2 | accordo degli strati |

### Controller 2 — movimento, spazio, tempo
| Tenendo | Su / Giù | Destra / Sinistra | R1 / L1 | R2 / L2 |
|---|---|---|---|---|
| niente = MIX | M1 RISE | REVERB | LAYERS (volume strati) | VOLUME |
| X / Quadrato / Triangolo = MOD A / B / C | RATE (velocità) | DEPTH (quanto) | WAVE: SIN · TRI · BREATH · RANDOM · NOISE · PULSE1 · PULSE2 | DEST (cosa muove: una manopola, PITCH, PAN, una macro, DEPTH o RATE di un'altra MOD) |
| Cerchio = SPACE | WIDE (oltre 50 più largo delle casse) | TAIL (99 = HOLD: la coda resta) | ROOM: SMALL · STUDIO · HALL · ECHO · SPACE | DAMP |
| Select = MACROS | M1 RISE | M2 DEEP | M3 UNREST | M4 HAZE |
| Select + X = SEQ | STEPS (OFF, 2-8) | MUTATE | SCALE: MINOR · DORIAN · PENTA · FIFTHS · OCTAVES · PHRYGIAN · LYDIAN · MIXOLYD · MAJOR | EVERY (1-16 battiti per passo) |
| Select + Quadrato = PULSE | HITS 1 | STEPS 1 | HITS 2 | STEPS 2 |
| Select + Triangolo = CLOCK | BPM (40-200) | RANGE (1 o 2 ottave) | NUDGE: battito avanti / indietro | TAP tempo (batti R2 o L2 a tempo) |
| Select + Cerchio = EDIT | posto 1-16 (4 per macro) | quantità -9…+9 | destinazione | — |
| Start tenuto = SCENE | scena 1-8 | MORPH: 2 · 5 · 10 · 20 · 40 · 80 · 120 s | R1 GO (va alla scena) · L1 HOME (torna alla scena di partenza, macro a 0) | R2 NUDGE (sposta un po' 6 valori) · L2 AUTO (scene in fila da sole) |

### Tasti speciali
| Tasto | Cosa fa |
|---|---|
| Controller 1 Start (tocco) | MUTE: il suono sfuma in 3 s; ancora = torna in 3 s |
| Controller 1 Select (tocco) | PLAY acceso / spento |
| Controller 2 Start (tocco) | FREEZE: le mani (MOD), il SEQ e il clock si fermano |
| Start + Select toccati (uno dei due controller) | VISUAL (tempesta); ancora = si torna ai pannelli |
| Start + Select tenuti 1 s | GUIDE (menu); Su/Giù sceglie, X conferma, Triangolo chiude |
| Controller 1 Start + X tenuti 2 s | LOCK: il pad ignora tutto tranne MUTE (riquadro LOCK); ancora = sblocca |
| Start + R1 / L1 (tutorial acceso) | passo avanti / indietro |

### Levette (DualShock con LED rosso)
| Levetta | Controller 1 | Controller 2 |
|---|---|---|
| sinistra su / giù | CUTOFF | M1 RISE |
| sinistra destra / sinistra | FOLD | M2 DEEP |
| destra su / giù | FM | M3 UNREST |
| destra destra / sinistra | WIDTH | M4 HAZE |

## Scene e memory card
- Si salva dalla GUIDE: **SAVE PATCH**, Sinistra/Destra sceglie il posto (1-8), X salva; se il
  posto è pieno chiede conferma. Durante il salvataggio il suono sparisce per ~2 s (la card blocca
  la console) e poi torna in 3 s: meglio salvare tra un pezzo e l'altro.
- Dal vivo si richiamano con la pagina **SCENE** (tieni Start del controller 2). Le scene
  scivolano nel tempo MORPH; se giri una manopola durante la scivolata, quella resta dove la metti.
- Senza memory card SAVE dice "NO MEMORY CARD OR CARD FULL", ma la scena resta in memoria fino
  allo spegnimento.

## Schermo
- In alto: nota che suona, filtro, riquadri PLAY / FREEZE / MUTE / LOCK / AUTO, punto del battito,
  riga **NOW** (valori che suonano adesso) e **OUT** (livello d'uscita; giallo chiaro = vicino al
  massimo).
- Sotto il nome di ogni manopola una barretta azzurra mostra il valore che suona davvero (con
  MOD e macro).
- Nel VISUAL, mentre tieni una pagina, una riga tenue in basso mostra i suoi valori; in alto a
  destra le lettere P F M L A = PLAY, FREEZE, MUTE, LOCK, AUTO.

## Note
- PAL 320x256 a 50 Hz. Provato in DuckStation: CPU del suono 28-33%, fotogramma fino a ~15 ms su 20.
- Da provare sulla PS1 vera (prova dell'utente): suono, card, leggibilità sul TV a tubo.
- Versione **NTSC** (60 Hz, licenza americana) in `ntsc/`: `python giochi_nuovi/build.py storm ntsc`. Disegna la stessa schermata a 256 righe e manda al TV le 240 righe dalla 6 alla 245 (il TV NTSC ne mostra ~224). Provata in DuckStation col BIOS SCPH-1001: fotogramma 10-16 ms su 16,7, B0.
- Compilare: `python giochi_nuovi/build.py storm` (rigenera anche i testi e i suoni da `script/prepara.py`).
- `SVILUPPO` in `main.c`: 0 = dal vivo (nessuna riga di misure in cima); 1 = riga dei tempi per le prove.
