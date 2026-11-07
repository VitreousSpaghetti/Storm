# storm per PC

Versione per computer di **storm**, il synth per drone nato per la PS1 PAL. Si suona con una
**tastiera MIDI**, con il **mouse**, con la tastiera del PC o con uno o due gamepad, su
**Windows, macOS e Linux**.

## Cartelle
| Cartella | Contenuto |
|---|---|
| `originale_psx/` | Sorgenti della versione PS1 (riferimento di sola lettura: suono e comportamento da rispettare) |
| `standard/` | Standard dei controlli (MIDI, mouse, tastiera, gamepad), piattaforme e modo prova, riusabile per altri synth |
| `src/` | Sorgenti C della versione PC: `storm.c` (il programma della PS1, cambiato il minimo), `psx.h` + `psx_spu.c` + `psx_gpu.c` (le funzioni PS1 che usa, fatte in software), `controlli.h` + `controlli.c` (standard dei controlli: tabella dei parametri, azioni comuni, tastiera del PC, gamepad, mouse e MIDI con il file di mappa; niente di storm dentro), `midi.c` (porte MIDI con RtMidi), `main.c` (finestra, audio, modo prova) |
| `prove/` | File di eventi del modo prova e `esegui.py` che li lancia tutti; in `prove/lunghe/` le prove di 10 minuti |
| `mappe/` | Mappa MIDI predefinita (`storm.txt`, compilata dentro il programma) |
| `script/` | Testi della GUIDE e del TUTORIAL (`testi.txt`) e `testi.py`, che li misura e scrive `src/testi.h` |

## Compilare
In generale vedi [standard/piattaforme/piattaforme.md](standard/piattaforme/piattaforme.md).
SDL3 (release 3.4.20) e RtMidi 6.0.0 si scaricano da soli alla prima configurazione.

Su Windows con gcc di MSYS2 UCRT64 e cmake/ninja del PSn00bSDK (il PATH cambia solo dentro lo
script):
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```
Il risultato e' `build\storm.exe`, un exe unico che non chiede DLL di MSYS2 e non apre la
finestra nera della console.

Su macOS (11 o piu') servono gli Xcode Command Line Tools (`xcode-select --install`) e CMake:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```
Il risultato e' `build/storm.app`, universale (Apple Silicon e Intel) e firmata ad-hoc.

Su Linux (Ubuntu/Debian; compilato su Ubuntu 22.04 gira anche sulle distro di 2-3 anni fa):
```bash
sudo apt-get install build-essential cmake ninja-build pkg-config libasound2-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libibus-1.0-dev
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Il risultato e' `build/storm`. Senza `libasound2-dev` la configurazione si ferma (senza ALSA non
ci sarebbe il MIDI).

`.github/workflows/build.yml` fa le stesse build su GitHub a ogni push, fa girare le prove sulle
tre piattaforme (su macOS sia arm64 sia x86_64) e tiene i pacchetti: `storm-windows` (exe e questo
file), `storm-macos.zip` (l'app), `storm-linux-x86_64.tar.gz` (il tar tiene il permesso di
esecuzione), piu' gli `md5.txt` di WAV e foto, che devono essere uguali su tutte.

## Avviare
Il programma e' `build\storm.exe` su Windows, `build/storm.app` su macOS (dal terminale:
`build/storm.app/Contents/MacOS/storm`) e `build/storm` su Linux; gli esempi sotto sono per Windows,
con le stesse opzioni sulle altre.

| Comando | Cosa fa |
|---|---|
| `build\storm.exe` | Finestra 320x256 ingrandita (si ridimensiona; F11 = tutto schermo) e il drone di storm |
| `build\storm.exe --prova prove\avvio.txt --wav uscita.wav --foto cartella\ --seme 1` | Modo prova senza finestra ne' audio (vedi [standard/prove/prove.md](standard/prove/prove.md)) |
| `build\storm.exe --mappa mia.txt` | Usa questa mappa MIDI al posto di quella dell'utente (il MIDI learn scrive qui) |
| `build\storm.exe --scene mie.txt` | Legge e scrive le scene in questo file al posto di quello dell'utente |
| `build\storm.exe --parametri` | Stampa la tabella dei parametri in Markdown |
| `python prove\esegui.py` | Lancia tutte le prove e dice OK / FALLITO (`--lunghe`: anche quelle di 10 minuti; `--uscite CARTELLA`: tiene WAV e foto e scrive `CARTELLA/md5.txt`) |
| `python script\testi.py` | Dopo aver cambiato `script\testi.txt`: riscrive `src\testi.h` (poi si compila) |

- Windows: l'exe non ha la console, quindi nel Prompt dei comandi o in PowerShell non scrive niente a schermo; per vedere
  i messaggi: `build\storm.exe --parametri > esito.txt 2>&1` (vale anche per il modo prova).
- macOS, la prima volta che apri un'app scaricata (non notarizzata): da macOS 15 tasto destro > Apri
  non basta; apri l'app, poi Impostazioni di Sistema > Privacy e sicurezza > "Apri comunque".
  Oppure dal terminale: `xattr -dr com.apple.quarantine storm.app`.
- Linux: dopo aver scompattato il `.tar.gz`, `./storm` dalla sua cartella.

## Stato
| Parte | Stato |
|---|---|
| Build, finestra, modo prova (WAV, PNG, controlli) | fatto; Windows: build e modo prova verificati; macOS e Linux: build e CI scritte, non provati |
| Suono: motore DSP, SPU (24 voci, ADPCM, gaussiana, PMON, rumore, riverbero), MOTION, macro, SEQ, PULSE, CLOCK, scene in memoria con AUTO | fatto; verificato in modo prova (WAV); da confrontare a orecchio con la PS1 |
| Grafica: pannelli 3D, testi, GUIDE, TUTORIAL (testi per pad, tastiera, mouse e MIDI), VISUAL (GPU e GTE della PS1 in software: copertura, Gouraud, dithering, semitrasparenza, font 4 bit) | fatto; verificato in modo prova (PNG, `prove/grafica.txt`) e nella finestra |
| Tastiera del PC e gamepad (uno o due, anche collegati a programma acceso) | fatto; verificato in modo prova (`prove/tastiera.txt`, `prove/pad.txt`); tastiera e gamepad veri da provare |
| Mouse (tutto suonabile col solo mouse) | fatto; verificato in modo prova (`prove/mouse.txt`, `prove/mouse_casi.txt`, `prove/tutorial.txt`: anche il TUTORIAL col solo mouse); mouse vero da provare |
| MIDI: note, CC fissi e di pagina col soft takeover, encoder, 14 bit, pitch bend, pressione, pedale, program change, clock esterno, MIDI learn, porte collegate a programma acceso | fatto; verificato in modo prova (`prove/midi.txt`); tastiera MIDI vera da provare; macOS e Linux non provati |
| Scene su file (`scene.txt`) | fatto; verificato in modo prova (`prove/scene.txt`, `prove/scene_riapri.txt`, `prove/scene_file.txt`); nella finestra da provare |

Come suona: il codice del synth e' quello della PS1; sotto c'e' una SPU in software che fa le
stesse cose dell'hardware (stessa decodifica ADPCM, interpolazione, inviluppi, riverbero a 22050 Hz).
L'unica differenza voluta: il motore DSP non scrive piu' 50-80 ms avanti nell'anello della voce 0,
ma ogni blocco proprio quando la voce lo legge, quindi niente ritardo. Al posto della memory card
le scene stanno in un file di testo (vedi **Scene**), e salvare non ferma il suono.

## Scene
SAVE SCENE (GUIDE, o Ctrl + S) mette il suono in uno degli 8 posti e riscrive subito il file delle
scene: non c'e' niente da salvare all'uscita (Ctrl + Q esce subito). LOAD SCENE, la pagina SCENE
(GO, HOME, AUTO), il program change 1-8 e la zona comandi le richiamano, scivolando nel tempo di
MORPH. All'avvio il suono e' sempre quello di partenza: le scene non si caricano da sole.

| Sistema | File |
|---|---|
| Windows | `%APPDATA%\storm\scene.txt` |
| macOS | `~/Library/Application Support/storm/scene.txt` |
| Linux | `~/.local/share/storm/scene.txt` |

Il file si legge e si corregge con un editor di testo e si puo' copiare su un altro PC. Formato
completo in [standard/controlli/scene.md](standard/controlli/scene.md); in breve:
```
# scene di storm (versione 1)
[scena 2]
CUTOFF = 45
MODE = 0
macro 1 1 = CUTOFF 6
```
Una sezione `[scena N]` per ogni posto pieno; `ID = valore` per ogni parametro che sta nelle scene
(id e valori come in `storm --parametri`; le scelte col numero: `MODE = 0` e' LP); `macro M P = ID
quantita'` = posto P della macro M (destinazione e quantita' da -9 a 9). Una riga sbagliata si salta
con un avviso (`file:riga`), un valore fuori limiti si porta dentro, un parametro che manca prende il
valore di partenza. Se il file non si riesce a scrivere, a schermo compare SCENE FILE NOT WRITTEN e
il file di prima resta com'era. `--scene FILE` usa un altro file (anche per tenerne piu' d'uno).

## Aiuto a schermo
La GUIDE (pagina KEYS e le altre) e il TUTORIAL spiegano i comandi del dispositivo usato per
ultimo: pad, tastiera del PC, mouse o tastiera MIDI. Conta l'ultimo che ha premuto qualcosa (un
tasto, un clic o la rotella, una nota o un CC; non il clock ne' il solo movimento del mouse). Chi
suona col MIDI usa il mouse per i menu: un clic non toglie i testi del MIDI. All'avvio: pad.
Anche l'aiuto in basso dello schermo principale segue il dispositivo: tasti del pad, tasti del PC,
CC del MIDI (righe in `script/testi.txt`, sezione `[[AIUTO]]`); col mouse le pagine e l'aiuto del
puntatore.

Il TUTORIAL dice quanto girare (es. "ABOVE 30") e va avanti da solo 3 s dopo che il passo e' fatto; per saltare un passo o tornare indietro:
PagGiu' / PagSu' sulla tastiera, i pulsanti `>` / `<` sotto il numero del passo col mouse, Start +
R1 / L1 sul pad; dalla tastiera MIDI solo se la mappa lo dice (`zona 37 = AVANTI`,
`zona 36 = INDIETRO`). Nel TUTORIAL col mouse il pulsante PLAY sta in cima.

## Suonare con la tastiera del PC
I tasti valgono per posizione (nomi della tastiera americana: su una tastiera italiana sono gli
stessi tasti fisici). Le manopole agiscono sul lato dell'ultima pagina aperta (in testata,
dopo LP/BP/HP, `C1` o `C2`).

| Tasti | Cosa fa |
|---|---|
| `1`-`6` | pagine del lato 1: OSC, GRAIN, NOTE, LAYERS, OSC B, TEXTURE |
| Shift + `1`-`9`, Shift + `-` | pagine del lato 2: MOD A, MOD B, MOD C, SPACE, MACROS, SEQ, PULSE, CLOCK, SCENE, EDIT |
| tocco / tenuto | tocco = pagina fissa (cornice gialla; tocca di nuovo per chiuderla), tenuto = finche' tieni |
| `0` / Shift + `0` | lato 1 / lato 2 a riposo (SHAPE / MIX) |
| W S o Su Giu' | manopola 1 (Su = alza) |
| D A o Destra Sinistra | manopola 2 |
| E Q | manopola 3 (come R1 / L1) |
| R F | manopola 4 (come R2 / L2) |
| tenuto / Shift / Ctrl | tenuto = scorre come sul pad; Shift = 10 passi; Ctrl = un passo solo |
| M, P, Spazio, V | MUTE, PLAY, FREEZE, VISUAL |
| F1 o Tab | GUIDE (Su/Giu' scelgono, Invio conferma, Esc indietro) |
| Invio | Croce del pad (conferma nei menu; fuori dai menu tenuto apre la pagina della Croce) |
| H | HOLD: TAIL a 99 (a schermo `HOLD`), di nuovo = com'era; se giri TAIL, HOLD si spegne e TAIL resta li' |
| Ctrl + L | LOCK (tutto fermo tranne MUTE e Ctrl + L) |
| Ctrl + S | GUIDE su SAVE SCENE |
| PagGiu' / PagSu' | TUTORIAL: passo dopo / prima |
| Esc | chiude le pagine fisse e la GUIDE |
| F11, Ctrl + Q | tutto schermo, esci |

Con PLAY acceso, sul lato 1 a riposo, le stesse coppie sono i gesti del pad (Su = SWELL,
Giu' = SINK, Destra/Sinistra = nota, E/Q = ottava, R/F = accordo). Shift tenuto mentre si girano le
manopole = passi da 10: per tenere una pagina del lato 2 si lascia Shift e si tiene il numero.

## Suonare con il mouse
Appena il mouse passa sulla finestra compaiono i pulsanti in alto, i nomi delle pagine in basso e,
sotto, due righe che spiegano la cosa sotto il puntatore.

| Gesto | Dove | Cosa fa |
|---|---|---|
| clic | su un pannello o sul nome di una pagina in basso | apre la pagina **fissa** (cornice gialla); un altro clic la chiude |
| clic tenuto | su un pannello o su una pagina | la pagina resta aperta finche' tieni |
| clic | fuori dai pannelli | chiude le pagine fisse (e la GUIDE) |
| trascina su/giu' | su una manopola della pagina aperta | 1 pixel del disegno = 1 passo; con Shift 4 pixel = 1 passo; il puntatore resta fermo e nascosto, torna dove era quando lasci |
| rotella | su una manopola | 1 scatto = 1 passo, con Shift 10 |
| doppio clic | su una manopola | torna al valore iniziale |
| clic | su una scelta (MODE, ROOT, WAVE...) | etichetta dopo (in giro); rotella = su e giu' |
| tasto destro | su una manopola o scelta | menu LEARN / CLEAR / DEFAULT (LEARN e CLEAR grigi finche' non c'e' una porta MIDI) |
| clic | pulsanti in alto: VISUAL, GUIDE, LOCK; nella riga della nota PLAY, FREEZE, MUTE, HOLD | azioni speciali (acceso = colorato). LOCK va tenuto 1 s (anche per sbloccare) |
| clic | ovunque, in VISUAL | torna ai pannelli |
| clic | `<` `>` sotto il numero del passo del TUTORIAL | passo prima / dopo |
| doppio clic | sulla testata (STORM, nota, riga NOW) | tutto schermo (come F11) |

- **PLAY**: con PLAY acceso e il lato 1 a riposo, in basso c'e' una striscia di un'ottava (da LA a
  SOL# dell'OCTAVE attuale) con OCT- e OCT+. Clic su un tasto = quella nota (ROOT e OCTAVE). Tenendo
  premuto nella meta' alta di un tasto SWELL cresce, nella meta' bassa SINK (come Su/Giu' del pad).
- **GUIDE**: clic su una voce = la sceglie, un altro clic (o OK in basso) = conferma; BACK = indietro
  o chiude. In SAVE SCENE e LOAD SCENE un clic sceglie il posto, un altro salva o carica.
- **Scene**: si salvano dalla GUIDE (SAVE SCENE). Nella pagina SCENE: clic sulla prima riga = posto
  dopo, MORPH con clic o rotella, HOME / GO e AUTO / NUDGE sono due meta' della riga.
- **EDIT**: clic sul titolo = macro dopo; clic su una riga = sceglie il posto; nella riga scelta il
  nome cambia destinazione (clic o rotella), la barra a destra si trascina per la quantita'.
- **CLOCK**: TAP con dei clic a tempo; NUDGE: meta' sinistra = battito un po' prima, destra = dopo.

## Suonare con una tastiera MIDI
Tutte le porte MIDI d'ingresso sono aperte insieme; una tastiera collegata a programma acceso si
trova da sola entro 2 s (e una staccata si chiude). Su Windows una porta gia' usata da un altro
programma non si apre: storm lo scrive una volta e riprova da solo. Su Linux il MIDI passa dal
sequencer ALSA: `aconnect -l` mostra le porte (si apre anche "Midi Through", che non fa niente).

| Cosa | Cosa fa |
|---|---|
| nota | diventa la nota del drone (ROOT e OCTAVE, scivola col GLIDE), con PLAY acceso o spento; la velocita' gonfia SWELL finche' tieni il tasto; lasciata, il drone resta sull'ultima |
| piu' note entro 50 ms | la piu' bassa e' la nota, le altre scelgono l'accordo degli strati (CHORD: 5TH, OCTAVE, 2 OCT, MINOR, MAJOR, SUS4, MIN7); suonate una dopo l'altra = melodia (legato) |
| pitch bend | +-2 semitoni sopra FINE (FINE non cambia) |
| aftertouch di canale | gesto SINK: piu' premi, piu' la macro 2 sale sopra M2 DEEP; lasciato, ricade (la manopola non si muove) |
| CC 74, 71, 91, 7 | CUTOFF, RES, REVERB, VOLUME (anche a 14 bit con CC 39 dopo CC 7) |
| CC 1, 11, 5, 72, 93, 94 | M3 UNREST, M1 RISE, GLIDE, TAIL, LAYERS, DETUNE |
| CC 64 (pedale) | HOLD finche' e' giu' |
| CC 20 / 25 | pagina del lato 1 / 2 (il valore 0-127 diviso per il numero di pagine) |
| CC 21-24 / 26-29 | le 4 manopole della pagina aperta del lato 1 / 2 |
| CC 16 | FOLD (assoluto; per un encoder senza fine nella mappa: `cc 16 = FOLD relativo`) |
| program change 1-8 (0-7) | scene 1-8 (posto vuoto = niente) |
| clock | il CLOCK segue il tempo (BPM mediato, `E` dopo il BPM e vicino al punto del battito); Start = battito 1, Stop ferma SEQ e PULSE ma non il suono; senza clock per 2 s torna al suo BPM |

- **Soft takeover**: una manopola MIDI che non e' dove sta il valore non lo fa saltare: lo prende
  quando ci passa sopra. Finche' non l'ha preso, vicino alla manopola a schermo c'e' un punto grigio
  (anche dopo che il valore e' cambiato dal mouse, dalla tastiera o da una scena). Due CC sullo
  stesso parametro: ognuno lo deve prendere da se'.
- **MIDI learn**: tasto destro su una manopola > LEARN, poi muovi un controllo MIDI: e' suo. CLEAR
  toglie i controlli MIDI di quella manopola. Esc o un clic altrove annullano il LEARN.
- **Mappa**: quella predefinita e' `mappe/storm.txt` (formato in
  [standard/midi/midi.md](standard/midi/midi.md)). Le assegnazioni del learn vanno nella mappa
  dell'utente, che vince sulla predefinita e si puo' modificare a mano: `%APPDATA%\storm\mappa.txt`
  (Windows), `~/Library/Application Support/storm/mappa.txt` (macOS), `~/.local/share/storm/mappa.txt`
  (Linux). `cc N = spento` toglie un CC della mappa predefinita. La **zona comandi** (note 36-47
  che lanciano scene 1-8, PLAY, FREEZE, MUTE, VISUAL invece di suonare) si accende con
  `zona 36-47 accesa` nella mappa dell'utente.

## Suonare con il gamepad
Ogni gamepad (PlayStation, Xbox, Switch, generico) e' un pad PS1 con i comandi di storm: tieni
Croce/Quadrato/Triangolo/Cerchio = pagina, toccato due volte = fissa, frecce e dorsali = manopole
(L2/R2 = grilletti), Select e Start come sulla PS1, levette = manopole in piu'.

| Tasti | Cosa fa |
|---|---|
| pad 1 a riposo (SHAPE): Su/Giu', Sinistra/Destra, L1/R1, L2/R2 | CUTOFF, RES, MODE, DRIVE |
| pad 1: tieni Croce / Quadrato / Triangolo / Cerchio | OSC, GRAIN, NOTE, LAYERS |
| pad 1: Select tenuto / + Croce | OSC B / TEXTURE |
| pad 1: Select toccato | PLAY (le frecce suonano: Su = SWELL, Giu' = SINK, Sinistra/Destra = nota, L1/R1 = ottava, L2/R2 = accordo) |
| pad 1: Start | MUTE (sfuma) |
| pad 1: Start + Croce tenuti 2 s | LOCK |
| pad 2 a riposo (MIX): Su/Giu', Sinistra/Destra, L1/R1, L2/R2 | M1 RISE, REVERB, LAYERS, VOLUME |
| pad 2: tieni Croce / Quadrato / Triangolo / Cerchio | MOD A, MOD B, MOD C, SPACE |
| pad 2: Select tenuto, + Croce / Quadrato / Triangolo / Cerchio | MACROS; SEQ, PULSE, CLOCK, EDIT |
| pad 2: Start toccato / tenuto | FREEZE / SCENE (Su/Giu' scena, Sinistra/Destra MORPH, R1 GO, L1 HOME, R2 NUDGE, L2 AUTO) |
| Start + Select toccati / tenuti 1 s | VISUAL / GUIDE (Su/Giu' scelgono, Croce conferma, Triangolo indietro) |
| Start + R1 / L1 | TUTORIAL: passo dopo / prima |

I tasti vanno per
posizione: Croce = tasto in basso (A su Xbox), Cerchio = destra, Quadrato = sinistra, Triangolo =
in alto, Select = View/Back, Start = Menu/Start. Il primo gamepad collegato e' il controller 1, il
secondo il controller 2. Con un gamepad solo il tasto Guide (PS, Xbox, Home) passa dal controller 1
al 2 e viceversa (in testata `C1` o `C2`; con due gamepad non si mostra). Un terzo gamepad non guida
niente finche' uno dei primi due non si scollega: allora ne prende il posto.

## Modo prova
`tasto NOME giu|su` passa dalla stessa traduzione della tastiera vera (Shift e Ctrl sono tasti come
gli altri: `shift giu` prima del numero). `pad1` e `pad2` sono il primo e il secondo gamepad
(`pad2` si collega al suo primo evento): stessi nomi dei tasti PS1, piu' `guide`; `l2` e `r2` sono
i grilletti; `pad1 leva sx|dx X Y` (da -1 a 1, Y = -1 in alto) muove una levetta e la lascia li'.
Un'azione vale dal campione esatto del suo istante (nella finestra dal blocco audio dopo, al
massimo ~6 ms); i tasti del pad e le levette si leggono a ogni fotogramma (ogni 1/50 s), come
sulla PS1. `controlla NOME = V` legge il valore impostato
di un parametro (id in `storm --parametri`) o lo stato 0/1 di un interruttore (MUTE, PLAY, FREEZE,
VISUAL, GUIDE, LOCK, HOLD); `controlla pagina 1 = OSC` la pagina aperta di un lato. Piu' di 256
azioni nello stesso istante sono un errore (uscita 2). Gli eventi `mouse` (`giu X Y`, `su`, `muovi X Y`, `rotella X Y N`, coordinate nel
disegno) passano dallo stesso traduttore del mouse vero; in piu' `mouse destro X Y` = clic del
tasto destro. Il doppio clic sono due `giu` sulla stessa cosa entro 0,4 s; Shift col mouse = `tasto
shift giu`. Una prova fallisce anche se il disegno ha una zona cliccabile sotto 12x12 pixel. Gli
eventi `midi` (`cc N V`, `nota N V`, `programma N`, `piega V`, `pressione V`, con `canale C` facoltativo)
passano dallo stesso traduttore dei messaggi delle porte vere; `clock start|stop|continua|tick` sono i
messaggi di clock e `clock bpm X` manda i tick da solo, a campioni esatti, fino a `clock stop`. Nel
modo prova la mappa e' la predefinita o quella di `--mappa`, e il learn scrive solo in `--mappa`.
Le scene, nel modo prova, stanno solo nel file di `--scene` (senza, restano in memoria): il modo
prova non tocca mai la cartella dei dati. `esegui.py` da' a ogni prova un file delle scene suo
(vuoto, o copia di `prove/<nome>.scene`); `<nome>_riapri.txt` riceve quello lasciato da `<nome>.txt`
(= chiudi e riapri). I tasti `pagsu` e `paggiu` sono PagSu' e PagGiu'; `controlla dispositivo =
PAD|TASTIERA|MOUSE|MIDI` legge il dispositivo usato per ultimo, `controlla passo = N` il passo del
TUTORIAL (0 = chiuso).
