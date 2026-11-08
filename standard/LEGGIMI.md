# Standard dei controlli per synth software

Questo standard descrive come un synth del progetto (il primo e' **storm**) si suona con
**tastiera MIDI, mouse, tastiera del PC e gamepad**, e come gira su **Windows, macOS e Linux**.
E' scritto per essere riusato: un altro synth che lo segue ottiene gli stessi ingressi, gli stessi
gesti, lo stesso formato delle mappe MIDI e lo stesso modo prova, cambiando solo la propria
tabella dei parametri.

## Idea in una riga
Ogni dispositivo traduce i suoi eventi in poche **azioni comuni** (apri pagina, gira manopola,
imposta valore, suona nota, azione speciale). Il synth conosce solo le azioni e la sua
**tabella dei parametri**; non sa da quale dispositivo arrivano.

```
 tastiera MIDI ─┐
 mouse ─────────┤                 ┌──────────────────────┐      ┌───────────────┐
 tastiera PC ───┼──> traduttori ─>│ azioni comuni        │ ───> │ synth         │
 gamepad ───────┤   (uno per      │ (controlli/azioni.md)│      │ (tabella dei  │
 file di prova ─┘    dispositivo) └──────────────────────┘      │  parametri)   │
                                                                └───────────────┘
```

## Cartelle
| Cartella | Cosa definisce |
|---|---|
| [controlli/](controlli/) | Tabella dei parametri, pagine e 4 manopole, azioni comuni, scene su file: il cuore dello standard |
| [midi/](midi/) | Note, CC, soft takeover, MIDI learn, clock, program change, formato del file di mappa |
| [mouse/](mouse/) | Clic, trascinamento, rotella, doppio clic, tasto destro, zone minime |
| [tastiera/](tastiera/) | Tastiera del PC (per posizione dei tasti, vale per ogni layout) |
| [gamepad/](gamepad/) | Uno o due gamepad con la grammatica dei pad PS1 |
| [piattaforme/](piattaforme/) | Librerie, compilazione, audio, percorsi dei file su Windows, macOS, Linux |
| [prove/](prove/) | Modo prova senza finestra: eventi da file, uscita WAV e PNG, controlli automatici |
| [esempi/](esempi/) | Synth scritti su carta sotto lo standard, per vedere come si adotta (il primo: nucleo, un synth a sequenza) |

## Come adottarlo in un nuovo synth
1. Scrivi la tabella dei parametri ([controlli/parametri.md](controlli/parametri.md)): ogni
   parametro ha un id, una pagina e un posto 1-4.
2. Elenca le azioni speciali del synth (es. MUTE, FREEZE) con un tasto per ogni dispositivo
   ([controlli/azioni.md](controlli/azioni.md)); i tasti del PC li dai tu, fuori da quelli
   riservati ([tastiera/](tastiera/)).
3. Scrivi la mappa MIDI predefinita (`mappe/<synth>.txt`, formato in [midi/](midi/)).
4. Riusa i traduttori dei dispositivi senza cambiarli; se uno standard non basta, si cambia qui
   prima che nel codice.
5. Fai girare le prove di [prove/](prove/) su tutte le piattaforme.

Un esempio completo di questi passi, per un synth diverso da storm, e' in
[esempi/nucleo.md](esempi/nucleo.md).

## Versioni
| Versione | Data | Cosa |
|---|---|---|
| 0.1 | 2026-11-06 | Prima stesura, da storm PS1. Da confermare con il primo porting. |
| 0.2 | 2026-11-06 | Dopo tastiera e gamepad di storm. Solo aggiunte e precisazioni, i file della 0.1 restano validi: pagina 10 col tasto `-`; Shift (lato 2 e passi da 10, senza scorrimento) e Ctrl (un passo; Ctrl+S e Ctrl+Q vincono); Invio ed Esc nei menu; tocco = meno di 300 ms; pad nativo per i synth nati sulla PS1; campo `anche` per un parametro su due pagine; regole per `pulsante`, `bipolare`, `scena`; interruttori `cambia` / `acceso` / `spento`, HOLD di storm e sustain tenuto; nel modo prova nomi dei tasti e del pad, `leva`, `guide`, `controlla pagina` e `controlla` degli interruttori |
| 0.3 | 2026-11-07 | Dopo il mouse di storm. Solo aggiunte e precisazioni, i file della 0.2 restano validi: pulsanti, nomi delle pagine e aiuto del mouse compaiono dal primo movimento sul disegno; clic = tocco di 300 ms anche col mouse; pagina a riposo portata davanti (`PAGINA lato 0 tieni` poi `fissa`); clic fuori = come Esc; doppio clic = due clic sulla stessa zona entro 400 ms; `scelta`: clic in giro, rotella ferma agli estremi; tasto destro anche sulle `scelta`, voci senza MIDI grigie; testata su una o due righe, LOCK tenuto anche per sbloccare; un pulsante da tenere conta solo se lo lasci sopra il pulsante (lasciato fuori o con la finestra che perde il fuoco non fa niente); ogni pagina apribile con un clic (nomi in basso); trascinamento `MANOPOLA` per cio' che non e' nella tabella; striscia di PLAY con velocita' 100; nel modo prova `mouse destro X Y`, `mouse su X Y` e il controllo delle zone sotto 12x12 |
| 0.4 | 2026-11-07 | Dopo il MIDI di storm. **Cambio incompatibile per le prove**: un'azione vale dal campione in cui arriva (nella finestra dal blocco audio dopo, al massimo ~6 ms), non piu' dal fotogramma dopo; tasti del pad e levette restano a ogni fotogramma. I WAV delle prove con eventi a meta' fotogramma cambiano. Il resto sono aggiunte: nel modo prova le righe `midi` (`cc`, `nota`, `programma`, `piega`, `pressione`, `canale C`) e `clock tick`, mappa da `--mappa` e learn solo in quel file; pitch bend = intonazione propria sopra FINE (`piega = N` semitoni nella mappa); accordo = note premute entro 50 ms, se no legato; note MIDI anche con PLAY spento; cartella dei dati senza autore (`%APPDATA%\storm\`); nella mappa `pressione = SINK` (gesto della macro 2 che ricade) e `cc N = spento` (toglie un CC della mappa predefinita, lo scrive CLEAR) |
| 0.5 | 2026-11-07 | Dopo le scene e i testi di storm. Solo aggiunte, i file della 0.4 restano validi: formato di `scene.txt` ([controlli/scene.md](controlli/scene.md)); azioni secche `AVANTI` / `INDIETRO` del tutorial (PagGiu' / PagSu', pulsanti `<` `>` del mouse, nella mappa MIDI `zona N = AVANTI`; la zona accetta anche `SALVA`); GUIDE e TUTORIAL spiegano i comandi del dispositivo usato per ultimo (il mouse non toglie il MIDI, all'avvio pad); nel modo prova `--scene FILE`, tasti `pagsu` / `paggiu`, `controlla dispositivo` e `controlla passo`, regole di `esegui.py` per le scene (`<nome>.scene`, `<nome>_riapri.txt`); precisazione di nome: la voce della guida per `SALVA` si chiama `GUIDE > SAVE SCENE` (le scene sono le vecchie patch), stesso comportamento |
| 0.6 | 2026-11-07 | Piattaforme, solo precisazioni, i file della 0.5 restano validi: Windows con MSYS2 UCRT64 (gcc), MSVC non provato, exe senza console (messaggi con `> esito.txt 2>&1`); macOS: app universale arm64 + x86_64, macOS 11 o piu', prima apertura con "Apri comunque"; Linux: elenco dei pacchetti, RtMidi solo ALSA, consegna in `.tar.gz` compilata su Ubuntu 22.04; stesso seme = stessi WAV e PNG su tutte le piattaforme |
| 0.7 | 2026-11-07 | Piattaforme, solo correzioni del testo, i file della 0.6 restano validi: per compilare serve CMake 3.24 o piu' (su Ubuntu 22.04 con `pip install cmake` o dal repository di Kitware); la frequenza interna di storm e' 44100 Hz come la SPU della PS1, non 22050 |
| 0.8 | 2026-11-07 | Dopo la prova su un secondo synth ([esempi/nucleo.md](esempi/nucleo.md)). Solo aggiunte e precisazioni, i file della 0.7 restano validi e storm non cambia comportamento: i tasti del PC delle azioni speciali li da' il synth (restano dello standard F1/Tab, F11, Esc, Ctrl+S, Ctrl+Q, Ctrl+L, PagSu'/PagGiu'; storm da' i suoi di oggi); azione comune `AVVIA` (la sequenza gira), Spazio = `AVVIA` nei synth che ce l'hanno (storm tiene Spazio = FREEZE); scene, GUIDE, TUTORIAL, VOLUME, PLAY, LOCK, MUTE facoltativi; CC 64 = l'interruttore che tiene del synth; MIDI: colonna "synth a sequenza" e `nota = sequenza`, clock Start/Continue/Stop = `AVVIA`, passi agganciati ai tick, `pulsante` e azioni secche del synth su `cc N =` e `zona N =`, `pressione = ID somma`; mouse: trascinamento di tutta la gamma in ~100 pixel (0-99 resta 1 pixel = 1 passo); parametri: gamma che dipende da un altro parametro, precisione interna piu' fine della manopola |
