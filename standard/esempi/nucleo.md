# Esempio: il synth nucleo per PS1 sotto lo standard 0.8

Questo documento prova lo standard su un secondo synth, scritto solo su carta: **nucleo**, un synth
generativo per PS1 (ritmo euclideo + registro a scorrimento, 2 VCO, filtro, 2 inviluppi, LFO,
delay, motore DSP). Segue i passi di "Come adottarlo" di [../LEGGIMI.md](../LEGGIMI.md). Valori,
gamme e valori iniziali sono presi dal codice del synth nucleo per PS1 (stato dopo l'avvio).

Dove lo standard 0.7 non bastava, il punto e' segnato con **G1-G9** e **B10-B15** e spiegato in
[Cosa ha cambiato la prova](#cosa-ha-cambiato-la-prova). Quello che serviva e' entrato nella 0.8;
alcune regole della 0.8 i traduttori di oggi non le fanno ancora: sono in
[Cosa cambia nei traduttori](#cosa-cambia-nei-traduttori).

## 1. Pagine dei due lati

Come sulla PS1: lato 1 = controller 1 (**generatore**), lato 2 = controller 2 (**timbro**). Il
numero e' il tasto della tastiera del PC (lato 2 con Shift) e l'ordine delle pagine per i CC 20 e
25. I nomi delle pagine sono testi a schermo in inglese (la PS1 e' in italiano: vedi
[Testi: PS1 e PC](#5-testi-ps1-e-pc)).

| Lato | Numero | Pagina | Posto 1 | Posto 2 | Posto 3 | Posto 4 | Pad PS1 (pad nativo) |
|---|---|---|---|---|---|---|---|
| 1 | riposo | MAIN | BPM | MUTATE | ROOT | SCALE | niente tenuto |
| 1 | 1 | RHYTHM | HITS | STEPS | GATE | ACCENT | tieni X |
| 1 | 2 | MELODY | OCTAVES | LENGTH | GLIDE | — | tieni Quadrato |
| 1 | 3 | VCO2 | INTERV | DIVIDE | OCT2 | — | tieni Triangolo |
| 1 | 4 | TEMPO | BPM (anche) | — | — | TAP | nessuna (sulla PS1 TAP e' Select) |
| 2 | riposo | MIX | MIX | NOISE | DELAY | REPEAT | niente tenuto |
| 2 | 1 | VCO | SHAPE1 | SHAPE2 | RANGE1 | RANGE2 | tieni X |
| 2 | 2 | FILTER | CUTOFF | RES | MODE | ENV2AMT | tieni Quadrato |
| 2 | 3 | ENV1 | ATT | DECAY | SUST | REL | tieni Triangolo, ENV1 scelto (B2) |
| 2 | 4 | ENV2 | ATT | DECAY | SUST | REL | tieni Triangolo, ENV2 scelto (B2) |
| 2 | 5 | LFO | RATE | DEPTH | SHAPE | DEST | tieni Cerchio |
| 2 | 6 | DELAY | DLYTYPE | DELAY (anche) | REPEAT (anche) | — | nessuna: C2 Start a riposo = tipo di delay |
| 2 | 7 | ENGINE | ENGINE | SYNC | DRIVE | — | nessuna: combinazioni dei tasti (B2) |

Lato 1: 4 pagine oltre al riposo; lato 2: 7. I posti vuoti restano vuoti.

## 2. Tabella dei parametri

Campi di [../controlli/parametri.md](../controlli/parametri.md). Le ultime due colonne dicono come
si raggiunge ogni parametro **col solo mouse** e **con la tastiera MIDI + mouse**: tutti stanno in
una pagina, quindi col mouse si apre la pagina (clic sul pannello o sul nome in basso) e si gira la
manopola; col MIDI si sceglie la pagina col CC 20 (lato 1) o 25 (lato 2) e si gira il CC del posto
(21-24 lato 1, 26-29 lato 2), oppure il CC fisso. Col mouse ogni manopola fa tutta la gamma in
circa 100 pixel del disegno ([../mouse/mouse.md](../mouse/mouse.md)): le gamme 0-8 vanno a 12 pixel
per passo, 0-2 a 50, 0-3 a 33, `CUTOFF` e `BPM` a 1.

| id | nome | lato | pagina | posto | min | max | iniziale | tipo | etichette | bipolare | scena | anche | Solo mouse | MIDI + mouse |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `BPM` | BPM | 1 | MAIN | 1 | 30 | 300 | 120 | continuo | | no | no | `TEMPO 1` | manopola (B11) | CC 21; clock MIDI; TAP |
| `MUTATE` | MUTATE | 1 | MAIN | 2 | 0 | 8 | 2 | continuo | | no | si' | | manopola | CC 22; CC 1 |
| `ROOT` | ROOT | 1 | MAIN | 3 | 0 | 11 | 9 | scelta | `C C# D D# E F F# G G# A A# B` | no | si' | | scelta | CC 23; nota (G3) |
| `SCALE` | SCALE | 1 | MAIN | 4 | 0 | 5 | 0 | scelta | `MINOR DORIAN PHRYG PENTA FIFTHS CHROM` | no | si' | | scelta | CC 24 |
| `HITS` | HITS | 1 | RHYTHM | 1 | 0 | 16 | 5 | continuo | | no | si' | | manopola (B10: al massimo STEPS) | CC 20 + 21 |
| `STEPS` | STEPS | 1 | RHYTHM | 2 | 2 | 16 | 8 | continuo | | no | si' | | manopola | CC 20 + 22 |
| `GATE` | GATE | 1 | RHYTHM | 3 | 1 | 8 | 5 | continuo | | no | si' | | manopola (a schermo `5/8`) | CC 20 + 23 |
| `ACCENT` | ACCENT | 1 | RHYTHM | 4 | 0 | 16 | 0 | continuo | | no | si' | | manopola (B10: al massimo HITS) | CC 20 + 24 |
| `OCTAVES` | OCTAVES | 1 | MELODY | 1 | 1 | 3 | 2 | continuo | | no | si' | | manopola | CC 20 + 21 |
| `LENGTH` | LENGTH | 1 | MELODY | 2 | 2 | 16 | 8 | continuo | | no | si' | | manopola | CC 20 + 22 |
| `GLIDE` | GLIDE | 1 | MELODY | 3 | 0 | 8 | 0 | continuo | | no | si' | | manopola | CC 20 + 23; CC 5 |
| `INTERVAL` | INTERV | 1 | VCO2 | 1 | 0 | 3 | 2 | scelta | `UNISON 3RD 5TH OCTAVE` | no | si' | | scelta | CC 20 + 21 |
| `DIVIDE` | DIVIDE | 1 | VCO2 | 2 | 1 | 4 | 1 | continuo | | no | si' | | manopola (a schermo `/1`) | CC 20 + 22 |
| `VCO2OCT` | OCT2 | 1 | VCO2 | 3 | -1 | 1 | 0 | continuo | | si' | si' | | manopola | CC 20 + 23 |
| `TAP` | TAP | 1 | TEMPO | 4 | 0 | 0 | 0 | pulsante | | no | no | | clic a tempo | zona 38 (o `cc N = TAP`) |
| `VCOMIX` | MIX | 2 | MIX | 1 | 0 | 8 | 3 | continuo | | no | si' | | manopola | CC 26 |
| `NOISE` | NOISE | 2 | MIX | 2 | 0 | 8 | 0 | continuo | | no | si' | | manopola | CC 27 |
| `DLYMIX` | DELAY | 2 | MIX | 3 | 0 | 8 | 3 | continuo | | no | si' | `DELAY 2` | manopola | CC 28; CC 91 |
| `REPEATS` | REPEAT | 2 | MIX | 4 | 0 | 8 | 6 | continuo | | no | si' | `DELAY 3` | manopola | CC 29 |
| `SHAPE1` | SHAPE1 | 2 | VCO | 1 | 0 | 4 | 2 | scelta | `SIN TRI SAW SQUARE PULSE` | no | si' | | scelta | CC 25 + 26 |
| `SHAPE2` | SHAPE2 | 2 | VCO | 2 | 0 | 4 | 3 | scelta | `SIN TRI SAW SQUARE PULSE` | no | si' | | scelta | CC 25 + 27 |
| `RANGE1` | RANGE1 | 2 | VCO | 3 | 0 | 2 | 2 | scelta | `32' 16' 8'` | no | si' | | scelta | CC 25 + 28 |
| `RANGE2` | RANGE2 | 2 | VCO | 4 | 0 | 2 | 2 | scelta | `32' 16' 8'` | no | si' | | scelta | CC 25 + 29 |
| `CUTOFF` | CUTOFF | 2 | FILTER | 1 | 0 | 112 | 70 | continuo | | no | si' | | manopola (a schermo 0-99) | CC 74 |
| `RES` | RES | 2 | FILTER | 2 | 0 | 2 | 1 | continuo | | no | si' | | manopola | CC 71 |
| `FILTMODE` | MODE | 2 | FILTER | 3 | 0 | 2 | 0 | scelta | `LP BP HP` | no | si' | | scelta | CC 25 + 28 |
| `ENV2AMT` | ENV2AMT | 2 | FILTER | 4 | 0 | 8 | 3 | continuo | | no | si' | | manopola | CC 25 + 29 |
| `ATT1` | ATT | 2 | ENV1 | 1 | 0 | 8 | 1 | continuo | | no | si' | | manopola | CC 25 + 26; CC 73 |
| `DEC1` | DECAY | 2 | ENV1 | 2 | 0 | 8 | 4 | continuo | | no | si' | | manopola | CC 25 + 27; CC 75 |
| `SUS1` | SUST | 2 | ENV1 | 3 | 0 | 8 | 6 | continuo | | no | si' | | manopola | CC 25 + 28 |
| `REL1` | REL | 2 | ENV1 | 4 | 0 | 8 | 3 | continuo | | no | si' | | manopola | CC 25 + 29; CC 72 |
| `ATT2` | ATT | 2 | ENV2 | 1 | 0 | 8 | 0 | continuo | | no | si' | | manopola | CC 25 + 26 |
| `DEC2` | DECAY | 2 | ENV2 | 2 | 0 | 8 | 3 | continuo | | no | si' | | manopola | CC 25 + 27 |
| `SUS2` | SUST | 2 | ENV2 | 3 | 0 | 8 | 0 | continuo | | no | si' | | manopola | CC 25 + 28 |
| `REL2` | REL | 2 | ENV2 | 4 | 0 | 8 | 2 | continuo | | no | si' | | manopola | CC 25 + 29 |
| `LFORATE` | RATE | 2 | LFO | 1 | 0 | 8 | 3 | continuo | | no | si' | | manopola | CC 25 + 26; CC 76 |
| `LFODEPTH` | DEPTH | 2 | LFO | 2 | 0 | 8 | 0 | continuo | | no | si' | | manopola | CC 25 + 27; CC 77; pressione |
| `LFOSHAPE` | SHAPE | 2 | LFO | 3 | 0 | 4 | 0 | scelta | `SIN TRI SAW SQUARE S&H` | no | si' | | scelta | CC 25 + 28 |
| `LFODEST` | DEST | 2 | LFO | 4 | 0 | 3 | 1 | scelta | `PITCH CUTOFF MIX VOLUME` | no | si' | | scelta | CC 25 + 29 |
| `DLYTYPE` | DLYTYPE | 2 | DELAY | 1 | 0 | 5 | 0 | scelta | `OFF ROOM STUDIO HALL ECHO SPACE` | no | si' | | scelta | CC 25 + 26 |
| `ENGINE` | ENGINE | 2 | ENGINE | 1 | 0 | 1 | 0 | scelta | `SPU DSP` | no | si' | | scelta | CC 25 + 26 |
| `SYNC` | SYNC | 2 | ENGINE | 2 | 0 | 1 | 0 | scelta | `OFF ON` | no | si' | | scelta (B14) | CC 25 + 27 |
| `DRIVE` | DRIVE | 2 | ENGINE | 3 | 0 | 3 | 0 | continuo | | no | si' | | manopola (B14) | CC 25 + 28 |

Note sui valori:
- `BPM`: sulla PS1 il tempo e' tenuto in decimi (30,0-300,0, a schermo `120.0`); la manopola fa
  passi da 1 BPM, tap, clock e levetta vanno piu' fini (B11).
- `CUTOFF`: la manopola e' il passo del pad della PS1 (Su / Giu' = 16 su 1792): 0-112, valore
  interno = manopola x 16. All'avvio 70, cioe' 1120 esatto come sulla PS1. A schermo si legge 0-99
  come sulla PS1 (interno x 99 / 1792: all'avvio 61). Le levette del pad nativo vanno piu' fini
  (B11).
- `RES` 0-2: col CC 71 sono 3 zone larghe. Sulla PS1 cambiare la risonanza a nota tenuta puo' dare
  un piccolo scatto: e' un limite noto, resta.
- `HITS` non supera `STEPS`, `ACCENT` non supera `HITS` (B10): quando `STEPS` e' basso la parte
  alta della manopola (col CC e col mouse) non fa niente.
- `RANGE1` / `RANGE2`: sulla PS1 R1 = "piu' acuto"; nella tabella le etichette vanno dal grave
  all'acuto, cosi' "alza" resta R1 / Su come vuole la grammatica. All'avvio 8'.
- `DLYTYPE`, `ENGINE`, `SYNC`, `DRIVE` sulla PS1 non hanno una manopola ma un tasto o una
  combinazione (B2). `SYNC` e `DRIVE` contano solo con `ENGINE` = DSP (B14).
- `DLYTYPE` parte da `OFF`, come sulla PS1: finche' non si sceglie un tipo nella pagina DELAY,
  `DLYMIX` (e il CC 91) non si sentono.
- Il synth nucleo non ha scene: la colonna `scena` dice cosa salverebbe se le avesse (G6).

## 3. Azioni speciali

Colonne di [../controlli/azioni.md](../controlli/azioni.md). Il nome e' la chiave delle mappe e
delle prove; il testo del pulsante in testata e' in inglese (al massimo 7 caratteri). I tasti del
PC li da' il synth (G1), fuori da quelli riservati dello standard.

| Nome | Tipo | Pad PS1 | Tastiera PC | Mouse | MIDI predefinito |
|---|---|---|---|---|---|
| `BLOCCA` (il registro non muta, il giro si ripete; la mutazione resta e torna allo sblocco) | interruttore che tiene, spento all'avvio | C1 Cerchio | H (come HOLD) | pulsante `LOOP` in testata | CC 64 (pedale sustain, G5); zona 37 |
| `AVVIA` (la sequenza gira; spegnendo, tutte le voci si lasciano; riaccendendo riparte dal passo 1) | interruttore, acceso all'avvio | C1 Start | Spazio (G4) | pulsante `RUN` in testata | clock Start / Continue = acceso, Stop = spento (G4); zona 36 |
| `INIZIALE` (tutto il timbro del lato 2, delay compreso, come all'avvio; `ENGINE` non cambia) | azione secca | C2 Select, nessuna pagina tenuta | Ctrl + I | pulsante `INIT` in testata | zona 47 (B13) |
| `TAP` (non e' un'azione speciale: e' l'`id` pulsante della pagina TEMPO, come `TAP` di storm) | pulsante della tabella | C1 Select, nessuna pagina tenuta | pagina TEMPO (4), poi R o F a tempo | clic a tempo sulla zona TAP | zona 38 (B12) |

- Ctrl + I: I non e' un tasto di manopola, quindi non e' mai "un passo solo" di qualcosa; servono
  due tasti insieme, nessuna conferma (dal vivo deve essere immediato come sulla PS1).
- Comandi del pad PS1 che restano solo nel pad nativo (sul PC sono parametri delle pagine): C2
  Start = tipo di delay; C2 Select tenendo X = motore; C2 Start tenendo X = sync; C2 Start tenendo
  Quadrato = overdrive; C2 Select tenendo Triangolo = quale inviluppo modifica la pagina INVILUPPO
  (sul PC due pagine, ENV1 e ENV2); levette = cutoff, risonanza, velocita' e profondita' LFO,
  mutazione, BPM fine (G8).
- Il synth nucleo non ha `MUTE`, `PLAY` (gesti), `FREEZE`, `VISUAL`, `GUIDE`, `LOCK`, `HOLD`,
  `SALVA`, `AVANTI`, `INDIETRO` (G6). I tasti riservati per queste azioni (F1, Tab, Ctrl+L, Ctrl+S,
  PagSu', PagGiu') non fanno niente; M, P e V restano liberi.
- Tap tempo come sulla PS1: la media degli ultimi 4 intervalli (2-5 tocchi); un tocco dopo una
  pausa di piu' di 3 s riallinea il battito "1" senza cambiare il BPM.

## 4. Mappa MIDI predefinita

Formato di [../midi/midi.md](../midi/midi.md).

```
# mappa predefinita del synth nucleo
canale      omni
cc 74       = CUTOFF
cc 71       = RES
cc 91       = DLYMIX        # delay (il riverbero della PS1): si sente solo con un DLYTYPE scelto
cc 1        = MUTATE        # mod wheel = quanto cambia il giro
cc 5        = GLIDE         # portamento
cc 73       = ATT1          # attacco
cc 75       = DEC1          # decay
cc 72       = REL1          # release
cc 76       = LFORATE       # velocita' del vibrato = velocita' dell'LFO
cc 77       = LFODEPTH      # profondita' del vibrato = quanto si muove l'LFO
cc 64       = BLOCCA        # pedale giu' = il giro si ripete (interruttore che tiene)
cc 20-29    = pagine        # CC 20 pagina lato 1, 21-24 manopole; CC 25 lato 2, 26-29
piega       = 2             # +-2 semitoni sopra il glide e l'LFO, su VCO1 e VCO2
pressione   = LFODEPTH somma  # la pressione apre l'LFO e ricade quando lasci
nota        = sequenza      # nota = ROOT, la sequenza si sposta
zona        36-47 spenta    # zona comandi: accesa / spenta
zona 36     = AVVIA
zona 37     = BLOCCA
zona 38     = TAP           # tasto bianco: si batte piu' volte di fila
zona 47     = INIZIALE      # l'ultimo tasto, lontano da AVVIA: non si tocca per sbaglio
# niente scene: il program change si ignora (nessuna riga "programma")
```

Note per chi suona:
- **Nota**: la nota premuta (senza l'ottava) diventa `ROOT`; la sequenza continua e si sposta dal
  colpo dopo, la nota che suona resta. `ROOT` vive tra C2 e B2 come sulla PS1 (il registro lo
  decidono `OCTAVES`, `RANGE1` / `RANGE2`, `OCT2`): da A a C la sequenza **scende** di una sesta,
  come con L1 / R1 sulla PS1. La velocita' non cambia niente (gli accenti li fa il ritmo). Le note
  spostano `ROOT` anche con `AVVIA` spento: si prepara la tonalita' prima di ripartire. Accordo:
  nessuno, conta la nota piu' bassa.
- **Zona comandi**: in nucleo accenderla (`zona 36-47 accesa`) non costa niente, perche' l'ottava
  della nota non conta: la tonica si sceglie con qualsiasi altra ottava.
- **Mod wheel e piega**: CC 1 = `MUTATE`, 9 valori su 128, scatti larghi; col soft takeover la
  rotella a riposo a 0 non blocca il giro per sbaglio. La piega e' sopra il glide (risponde subito)
  e sopra l'LFO, su VCO1 e VCO2 insieme, non sul rumore; con la rotella al centro il suono e' quello
  della PS1.
- **Pressione**: con `somma` la pressione si aggiunge a `LFODEPTH` e ricade quando lasci il tasto;
  la profondita' messa col mouse o col CC 77 resta. Senza `somma` (`pressione = LFODEPTH`) la
  pressione scriverebbe il valore: in nucleo si premono tasti di continuo, e una profondita' messa
  a mano si azzererebbe alla prima nota.
- **BPM col CC**: col CC 21 il BPM va a passi di circa 2 e non arriva ad alcuni tempi esatti (120,
  il tempo di avvio, e 80, 110, 125, 140, 150, 174, 180: si salta da 119 a 121). Per un tempo esatto:
  clock MIDI, `TAP` (zona 38), mouse o tastiera del PC (1 BPM a passo), oppure `cc N = BPM relativo`
  con un encoder (1 BPM a scatto) o i 14 bit.
- **Clock MIDI**: il `BPM` segue il clock (con la `E` a schermo); Start / Continue accendono
  `AVVIA` e il prossimo tick e' il passo 1; Stop spegne `AVVIA`. Ogni 6 tick un passo (un
  sedicesimo), agganciato al tick come in storm: la sequenza non scivola rispetto alla drum machine.
- Per un controller con 8 manopole bastano i CC 20-29; i CC 70-77 (le 8 manopole di molti
  controller piccoli) vanno su filtro, inviluppo e LFO come sopra. Il CC 70 non e' usato: `DRIVE`
  conta solo col motore DSP (B14).

## 5. Testi: PS1 e PC

I testi a schermo della PS1 di nucleo sono in italiano; sul PC sono in inglese, come vuole lo
standard (le note a lettere sono anche quelle delle tastiere MIDI e dei programmi musicali). Il
suono non cambia.

| PS1 | PC | Dove |
|---|---|---|
| GENERALE, RITMO, MELODIA, VCO2 | MAIN, RHYTHM, MELODY, VCO2 | pagine del lato 1 |
| MIX, VCO, FILTRO, ENV1 VOLUME / ENV2 FILTRO, LFO | MIX, VCO, FILTER, ENV1 / ENV2, LFO | pagine del lato 2 (sul PC anche DELAY, ENGINE) |
| MUTAZ, TONICA, SCALA | MUTATE, ROOT, SCALE | pagina MAIN |
| COLPI, PASSI, GATE, ACCENT | HITS, STEPS, GATE, ACCENT | pagina RHYTHM |
| OTTAVE, PASSI, GLIDE | OCTAVES, LENGTH, GLIDE | pagina MELODY |
| INTERV, DIVIS, OTTAVA | INTERV, DIVIDE, OCT2 | pagina VCO2 |
| MIX, RUMORE, DELAY, RIPET | MIX, NOISE, DELAY, REPEAT | pagina MIX |
| FORMA1, FORMA2, RANGE1, RANGE2 | SHAPE1, SHAPE2, RANGE1, RANGE2 | pagina VCO |
| CUTOFF, RES, MODO, ENV2 | CUTOFF, RES, MODE, ENV2AMT | pagina FILTER (`ENV2AMT` per non confondersi con la pagina ENV2) |
| VEL, PROF, FORMA, DEST | RATE, DEPTH, SHAPE, DEST | pagina LFO |
| DO DO# RE RE# MI FA FA# SOL SOL# LA LA# SI | C C# D D# E F F# G G# A A# B | `ROOT` |
| MINORE DORICA FRIGIA PENTATONICA QUINTE CROMATICA | MINOR DORIAN PHRYG PENTA FIFTHS CHROM | `SCALE` |
| UNISONO 3A 5A OTTAVA | UNISON 3RD 5TH OCTAVE | `INTERVAL` |
| SINUSOIDE TRIANGOLO SEGA QUADRA IMPULSO | SIN TRI SAW SQUARE PULSE | `SHAPE1`, `SHAPE2` |
| SIN TRI SEGA QUADRA S&H | SIN TRI SAW SQUARE S&H | `LFOSHAPE` |
| TONO CUTOFF MIX VOLUME | PITCH CUTOFF MIX VOLUME | `LFODEST` |
| SPENTO STANZA STUDIO SALA ECO SPACE ECHO | OFF ROOM STUDIO HALL ECHO SPACE | `DLYTYPE` |
| BLOCCA, PLAY (Start), INIZIALE | LOOP, RUN, INIT | pulsanti in testata |

## Cosa ha cambiato la prova

Ogni buco trovato con nucleo ha la sua decisione: **0.8** (entrato nello standard come aggiunta o
precisazione compatibile: storm non cambia), **dopo** (con il primo synth che ne ha bisogno),
**no** (con il perche').

| # | Buco | Decisione | Stato |
|---|---|---|---|
| G1 | I tasti del PC delle azioni speciali erano fissi nel traduttore della tastiera, coi nomi di storm: `BLOCCA`, `AVVIA`, `INIZIALE` non avrebbero avuto un tasto | Il synth da' l'elenco `tasto -> azione speciale`; restano dello standard F1/Tab, F11, Esc, Ctrl+S, Ctrl+Q, Ctrl+L, PagSu'/PagGiu'. Storm da' i suoi tasti di oggi | 0.8 |
| G2 (B2) | Parametri che sulla PS1 hanno un tasto o una combinazione e non una manopola: tipo di delay, motore, hard sync, overdrive; e la pagina INVILUPPO della PS1 che modifica ENV1 o ENV2 a scelta | Nessun cambio: sul PC pagine DELAY e ENGINE e due pagine ENV1 / ENV2; il pad nativo tiene le combinazioni della PS1. L'ordine delle pagine del PC non coincide coi tasti del pad, che restano quelli della PS1 | no (basta la tabella) |
| G3 | Note MIDI: il synth nucleo non e' ne' drone ne' a note, e' una sequenza | Colonna "synth a sequenza" in `midi.md` e parola `nota = sequenza` nel file di mappa (il traduttore manda le note come con `note`) | 0.8 |
| G4 | Trasporto: Start del C1 = play / stop della sequenza; `PLAY` di storm vuol dire "gesti" | Azione comune `AVVIA`; clock Start / Continue = acceso, Stop = spento (un drone senza `AVVIA`: si fermano solo i sequencer, come prima); Spazio = `AVVIA` nei synth che ce l'hanno, storm tiene Spazio = `FREEZE` | 0.8 |
| G5 | Pedale sustain = `HOLD` di storm; il synth nucleo non ha HOLD | CC 64 = l'interruttore che tiene del synth (qui `BLOCCA`), anche sul tasto H | 0.8 |
| G6 | Synth senza scene, GUIDE, TUTORIAL, VOLUME, PLAY (gesti), LOCK, MUTE | Sono facoltativi; senza scene `SCENA`, `SALVA` e program change non fanno niente; senza GUIDE F1 / Tab non fanno niente | 0.8 |
| G7 | TAP: sulla PS1 e' Select del C1 a riposo | Resta l'`id` pulsante `TAP` come in storm, nella pagina TEMPO (posto 4, come la pagina CLOCK di storm), con `BPM` anche al posto 1. Non e' un'azione speciale (il nome sarebbe uguale a un `id`) | no (basta la tabella) |
| G8 | Levette della PS1 (cutoff, risonanza, velocita' e profondita' LFO, mutazione, BPM fine) | Pad nativo: gia' coperte. MIDI: CC fissi sugli stessi `id`. "BPM fine" e' una velocita', non un valore: col MIDI c'e' il clock | no (gia' coperto) |
| G9 | Gamme piccole (0-8, 0-2, 0-3): col mouse 1 pixel = 1 passo faceva tutta la gamma in 3-9 pixel, e la mano che trema non posa un valore | Trascinamento di tutta la gamma in circa 100 pixel: pixel per passo = 100 / (max - min), minimo 1. Le gamme 0-99 di storm restano 1 pixel = 1 passo | 0.8 |
| B10 | Gamma che dipende da un altro parametro: `HITS` al massimo `STEPS`, `ACCENT` al massimo `HITS` | `min` e `max` sono la gamma piu' larga; il synth stringe il valore e lo scrive nelle note; il soft takeover guarda il valore stretto | 0.8 (precisazione) |
| B11 | Il synth tiene dentro piu' precisione della manopola: `BPM` in decimi | `min` / `max` sono la gamma della manopola; la parte fine la cambiano solo tap, clock e levette del pad nativo; il numero a schermo lo sceglie il synth. `CUTOFF` non ne ha piu' bisogno (manopola 0-112, a schermo 0-99) | 0.8 (precisazione) |
| B12 | Un `pulsante` della tabella (`TAP`) non si poteva dare a un CC o a una nota della zona | `cc N = TAP` e `zona N = TAP`: un colpo quando il CC passa da sotto 64 a 64 o piu', o quando la nota si preme. Sui CC di pagina un `pulsante` resta senza effetto | 0.8 |
| B13 | La zona comandi accettava solo le azioni secche dello standard (`SALVA`, `AVANTI`, `INDIETRO`), non quelle del synth (`INIZIALE`) | La zona (e `cc N =`) accetta ogni azione speciale del synth, interruttori e azioni secche | 0.8 |
| B14 | Parametri che contano solo in un modo del synth (`SYNC`, `DRIVE` solo con `ENGINE` = DSP) | Restano nella tabella e si girano sempre; il synth puo' disegnarli scuri quando non contano | no (e' una scelta di disegno del synth) |
| B15 | Le `scelta` del pad nativo della PS1 vanno in giro (dopo B viene C, dopo l'ultima scala la prima) | Nessuna regola nuova: la tastiera del PC fa come il pad del synth (storm: `ROOT` va in giro, le altre `scelta` si fermano agli estremi), il clic del mouse va in giro e la rotella si ferma, come dice gia' [../mouse/mouse.md](../mouse/mouse.md). In nucleo tastiera e pad fanno come la PS1: vanno in giro `ROOT`, `SCALE`, `INTERVAL`, `SHAPE1`, `SHAPE2`, `FILTMODE`, `LFOSHAPE`, `LFODEST`, `DLYTYPE` | no (lo standard basta) |
| — | Pressione su un `id` scrive il valore e azzera le regolazioni fatte a mano | `pressione = ID somma`: gesto che si somma e ricade, come `SINK` di storm | 0.8 |
| — | Clock: per un synth a sequenza il passo deve cadere sui tick, non solo seguire il BPM mediato | Precisazione in `midi.md`: ogni 6 tick un sedicesimo, rimesso a posto se fuori di piu' di mezzo sedicesimo (storm fa gia' cosi') | 0.8 (precisazione) |
| — | Una nota che avvia la sequenza (come negli arpeggiatori) | No come comportamento di base: non c'e' sulla PS1 e impedirebbe di scegliere la tonica a sequenza ferma. `AVVIA` ha gia' tasto, pedale, clock e zona | no |
| — | Motore DSP: sulla PS1 ha 80 ms di ritardo e il sequencer gira avanti per compensare | Sul PC il ritardo puo' sparire (differenza dalla PS1, a favore del MIDI) | dopo (quando nucleo gira su PC) |

## Cosa cambia nei traduttori

Lo standard vuole traduttori uguali per tutti i synth. Le regole della 0.8 qui sotto i traduttori
di oggi non le fanno ancora: servono a un synth che le usa, come nucleo. Per storm non cambia
niente: stessi tasti, stessa mappa, stessi WAV e PNG.

| Traduttore | Cosa cambia | Perche' | Storm |
|---|---|---|---|
| Tastiera del PC | I tasti delle azioni speciali (oggi M, P, Spazio, V, H scritti nel traduttore) li da' il synth come elenco; restano fissi solo i tasti riservati dello standard | G1: senza questo `BLOCCA`, `AVVIA`, `INIZIALE` non hanno un tasto | da' i suoi tasti di oggi |
| MIDI, file di mappa | Parola `sequenza` dopo `nota =` (stesso trattamento di `note`) | G3 | la sua mappa dice `nota = drone` |
| MIDI, file di mappa | Un `id` di tipo `pulsante` a destra di `cc N =` e `zona N =`; nella zona e sui CC le azioni speciali del synth, non solo `SALVA`, `AVANTI`, `INDIETRO` | B12, B13 | nessuna riga cosi' nella sua mappa; i CC di pagina saltano i `pulsante` come oggi |
| MIDI, file di mappa | `pressione = ID somma` | la pressione non deve cancellare le regolazioni | la sua mappa dice `pressione = SINK` |
| Mouse | Pixel per passo del trascinamento ricavati da `min` e `max` | G9 | tutti i suoi `continuo` sono 0-99: 1 pixel = 1 passo come oggi |

Il gamepad non cambia: il pad nativo e' gia' del synth.
