# Azioni comuni

Tutti i dispositivi producono solo queste azioni. Il synth le esegue senza sapere da dove
arrivano. Nel codice sono una `struct` piccola messa in una coda tra il thread degli ingressi e
quello dell'audio.

| Azione | Argomenti | Cosa fa |
|---|---|---|
| `PAGINA` | lato, pagina, `tieni` / `lascia` / `fissa` | Apre o chiude una pagina |
| `MANOPOLA` | lato, posto 1-4, passi (+/-) | Gira di N passi la manopola della pagina aperta |
| `VALORE` | id, valore 0.0-1.0 | Imposta un parametro (MIDI, mouse trascinato): il synth lo porta tra `min` e `max` |
| `NOTA` | nota MIDI, velocita' 0-127 (0 = lasciata) | Suona o lascia una nota |
| `PIEGA` | -1.0..+1.0 | Pitch bend |
| `CLOCK` | `tick` / `start` / `stop` / `continua` | Clock esterno (24 tick per battito) |
| `SCENA` | 1-8 | Va alla scena (morph col tempo della scena) |
| `SPECIALE` | nome, `cambia` / `acceso` / `spento` | Azione propria del synth (tabella sotto). Senza il secondo argomento vale `cambia` |

## Azioni speciali di storm
| Nome | Pad PS1 | Tastiera PC | Mouse | MIDI predefinito |
|---|---|---|---|---|
| `MUTE` | C1 Start | M | pulsante MUTE in testata | — |
| `PLAY` | C1 Select tocco | P | pulsante PLAY in testata | — |
| `FREEZE` | C2 Start | Spazio | pulsante FREEZE in testata | nota della zona comandi |
| `VISUAL` | Start + Select | V | pulsante VISUAL in testata; in VISUAL un clic ovunque | — |
| `GUIDE` | Start + Select tenuti 1 s | F1 o Tab | pulsante GUIDE in testata | — |
| `LOCK` | C1 Start + X 2 s | Ctrl + L | pulsante LOCK in testata (tenere 1 s, anche per sbloccare) | — |
| `HOLD` (coda del riverbero ferma) | SPACE / TAIL 99 | H | pulsante HOLD in testata | CC 64 (pedale sustain) |
| `SALVA` | GUIDE > SAVE SCENE | Ctrl + S | GUIDE > SAVE SCENE | — |
| `AVANTI` (passo dopo del TUTORIAL) | Start + R1 | PagGiu' | pulsante `>` nella testata del tutorial | solo dalla mappa (`zona N = AVANTI`) |
| `INDIETRO` (passo prima del TUTORIAL) | Start + L1 | PagSu' | pulsante `<` nella testata del tutorial | solo dalla mappa (`zona N = INDIETRO`) |

### Interruttori e azioni secche
- Un **interruttore** (MUTE, PLAY, FREEZE, VISUAL, GUIDE, LOCK, HOLD) ha uno stato acceso/spento.
  Tasto del PC, tasto del pad e clic mandano `cambia`. Un comando che si tiene giu' (pedale
  sustain, nota della zona comandi tenuta) manda `acceso` quando si preme e `spento` quando si
  lascia.
- Lo stato di ogni interruttore si vede sempre a schermo (pulsante acceso nella testata o
  scritta sul pannello).
- Un'**azione secca** (SALVA, AVANTI, INDIETRO) non ha stato: fa una cosa e basta; `acceso` vale come `cambia`,
  `spento` non fa niente.
- Con `LOCK` acceso passano solo `MUTE` e `LOCK`.
- `AVANTI` e `INDIETRO` (dalla 0.5) spostano di un passo un tutorial aperto, anche un passo che
  il synth farebbe superare solo cosi' (il primo e l'ultimo); con la GUIDE aperta o senza
  tutorial non fanno niente. Il tasto del pad e' scelto dal synth.

### Aiuto a schermo
I testi che spiegano i comandi (GUIDE, TUTORIAL) parlano del **dispositivo usato per ultimo**:
pad, tastiera, mouse o MIDI. Dalla 0.5.
- Conta l'ultimo dispositivo che ha **premuto** qualcosa: un tasto, un tasto del pad o una levetta
  oltre la zona morta, un clic o la rotella, una nota o un CC (anche piega e pressione). Non
  contano il clock, il solo movimento del mouse e i tasti lasciati quando la finestra perde il
  fuoco.
- Il mouse **non** toglie il MIDI: chi suona con la tastiera MIDI usa il mouse per i menu, e i
  testi restano quelli del MIDI (che dicono anche cosa si fa col mouse).
- All'avvio: pad.
- Riguarda solo i testi: pulsanti e nomi delle pagine del mouse compaiono e restano dal primo
  movimento, come dice [../mouse/mouse.md](../mouse/mouse.md).

### HOLD di storm
- Acceso: TAIL va a 99 (la coda del riverbero non si spegne piu'; sullo schermo TAIL mostra `HOLD`).
- Spento: TAIL torna al valore che aveva prima di HOLD.
- Se TAIL si muove mentre HOLD e' acceso (manopola, mouse, MIDI, scena), HOLD si considera spento
  e TAIL resta dove l'hai messo.

Un nuovo synth riempie questa tabella con le sue azioni; le colonne (i dispositivi) restano
uguali, e i tasti gia' usati qui (M, P, Spazio, V, F1, H, Ctrl+L, Ctrl+S) mantengono lo stesso
significato quando il synth ha un'azione simile.
