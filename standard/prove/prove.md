# Modo prova

Il programma si puo' provare **senza finestra e senza scheda audio**: legge eventi da un file,
calcola il suono piu' veloce del tempo reale e scrive un WAV e delle foto dello schermo. Serve per
le prove automatiche e per provare MIDI e mouse senza strumenti veri, uguale su tutte le
piattaforme.

## Riga di comando
```bash
storm --prova eventi.txt --wav uscita.wav --foto cartella/ --seme 1
storm --parametri > parametri.md
```
| Opzione | Cosa fa |
|---|---|
| `--prova FILE` | Esegue gli eventi del file, poi esce. Nessuna finestra, nessun dispositivo |
| `--wav FILE` | Scrive tutto il suono (stereo, 16 bit, frequenza del synth o 48000) |
| `--foto CARTELLA` | Salva le foto chieste dal file (PNG alla risoluzione del disegno) |
| `--seme N` | Fissa il caso: stessi eventi + stesso seme = stessi WAV e PNG, byte per byte, su tutte le piattaforme |
| `--parametri` | Stampa la tabella dei parametri in Markdown ed esce |
| `--mappa FILE` | Usa questa mappa MIDI invece di quella dell'utente |
| `--scene FILE` | Legge e scrive le scene in questo file invece di quello dell'utente (formato in [../controlli/scene.md](../controlli/scene.md)). Vale anche nella finestra. Dalla 0.5 |

Nel modo prova la mappa e' quella predefinita del synth, o quella di `--mappa` (che vince, come
quella dell'utente nella finestra). Il MIDI learn scrive solo nel file di `--mappa`; senza
`--mappa` l'assegnazione resta in memoria. Il modo prova non legge e non scrive mai la cartella
dei dati dell'utente. Dalla 0.4.

Le scene, nel modo prova, si leggono e si scrivono solo nel file di `--scene` (se non c'e', si
parte con i posti vuoti e lo si crea al primo `SALVA`). Senza `--scene` restano in memoria.
Dalla 0.5.

Il codice di uscita e' 0 se tutto e' andato, 1 se un `controlla` e' fallito, 2 se il file ha
errori.

## File degli eventi
Una riga per evento: secondi dall'inizio, dispositivo, evento. `#` = commento. Gli eventi passano
dagli **stessi traduttori** dei dispositivi veri.

```
# secondi  dispositivo  evento
0.0   midi   cc 74 100
0.5   midi   nota 48 100
2.0   midi   nota 48 0
2.5   midi   programma 3
3.0   mouse  giu 160 120          # coordinate nel disegno
3.1   mouse  muovi 160 100
3.2   mouse  su
3.5   mouse  rotella 160 120 +3
4.0   tasto  2 giu
4.2   tasto  2 su
4.5   pad1   croce giu
5.0   pad1   croce su
5.5   foto   dopo_croce
6.0   clock  start
6.0   clock  bpm 120              # genera i tick da solo fino a "clock stop"
10.0  controlla CUTOFF = 79       # fallisce se il valore impostato non e' quello
10.0  controlla livello > 0.05    # livello d'uscita (RMS dell'ultimo secondo)
12.0  fine
```

Dispositivi: `midi`, `mouse`, `tasto` (nomi per posizione come in [../tastiera/](../tastiera/)),
`pad1`, `pad2`, `clock`, `foto`, `controlla`, `fine`.

| Evento | Forma | Note |
|---|---|---|
| tasto | `tasto NOME giu` / `tasto NOME su` | NOME: lettere e cifre, `su`, `giu`, `sinistra`, `destra`, `esc`, `spazio`, `invio`, `shift`, `ctrl`, `alt`, `cmd`, `f1`..`f12`, `tab`, `-`, `pagsu`, `paggiu` (dalla 0.5). Shift e Ctrl sono tasti come gli altri: `shift giu` prima del numero |
| pad | `pad1 NOME giu` / `pad1 NOME su` | NOME: `croce`, `cerchio`, `quadrato`, `triangolo`, `su`, `giu`, `sinistra`, `destra`, `l1`, `r1`, `l2`, `r2` (grilletti), `l3`, `r3`, `select`, `start`, `guide` |
| mouse | `mouse muovi X Y`, `mouse giu X Y`, `mouse su` / `mouse su X Y`, `mouse rotella X Y N`, `mouse destro X Y` | X, Y nel disegno. `giu` e `su` sono il tasto sinistro; `destro` = clic del tasto destro (dalla 0.3); `su`, `rotella` e `destro` prima spostano il puntatore in X Y. Doppio clic = due `giu` sulla stessa zona entro 0,4 s. Shift col mouse = `tasto shift giu` prima. Il primo evento di una prova del mouse e' un `muovi`, cosi' compaiono pulsanti e pagine come col mouse vero |
| midi | `midi cc N V`, `midi nota N V`, `midi programma N`, `midi piega V`, `midi pressione V`, ognuna con `canale C` facoltativo in fondo | N e V 0-127; `nota N 0` = nota lasciata; `piega` da -8192 a 8191 (0 = al centro); `pressione` = aftertouch di canale; C da 1 a 16, di base 1. Es. `midi nota 48 100 canale 2`. Passano dal traduttore MIDI come i messaggi veri. Dalla 0.4 |
| clock | `clock start` / `stop` / `continua` / `tick` / `bpm X` | `tick` = un tick a mano (24 per battito); `bpm X` genera i tick da solo, a campioni esatti, fino a `clock stop` |
| levetta | `pad1 leva sx X Y` / `pad1 leva dx X Y` | X e Y da -1 a 1 (Y = -1 in alto); la levetta resta li' fino al prossimo evento |
| secondo gamepad | `pad2 ...` | Si collega al suo primo evento: prima c'e' un gamepad solo |
| valore | `controlla ID = V` | Valore impostato (intero) del parametro |
| livello | `controlla livello > X` / `< X` | RMS dell'ultimo secondo, 0..1 |
| pagina | `controlla pagina L = NOME` | Pagina aperta del lato L (1 o 2); la pagina a riposo si chiama col suo nome (storm: SHAPE, MIX). Dalla 0.2 |
| interruttore | `controlla NOME = 1` / `= 0` | Stato di un'azione speciale con stato (MUTE, HOLD...). Dalla 0.2 |
| dispositivo | `controlla dispositivo = PAD` / `TASTIERA` / `MOUSE` / `MIDI` | Dispositivo usato per ultimo, quello di cui parlano i testi a schermo (vedi [../controlli/azioni.md](../controlli/azioni.md)). Dalla 0.5 |
| passo | `controlla passo = N` | Passo del tutorial aperto (dal primo = 1); 0 = tutorial chiuso. Dalla 0.5 |

Una prova fallisce anche se il disegno, in qualche fotogramma, ha una zona cliccabile sotto 12x12
pixel (vedi [../mouse/](../mouse/)).

Un'azione vale dal campione in cui arriva: nel modo prova dal campione esatto del suo istante,
nella finestra dal blocco audio dopo (al massimo ~6 ms). I tasti del pad e le levette invece si
leggono a ogni fotogramma (ogni 1/50 s), come sulla PS1. Dalla 0.4 (prima: tutto dal fotogramma
dopo).

## Prove minime di ogni synth
In `prove/` nel repository, una per standard:
| File | Cosa prova |
|---|---|
| `prove/avvio.txt` | parte, suona, livello sopra la soglia, foto della schermata |
| `prove/midi.txt` | note, CC fissi, CC di pagina, soft takeover, program change, clock |
| `prove/mouse.txt` | pagine fisse e temporanee, trascina, rotella, doppio clic, pulsanti |
| `prove/tastiera.txt` | pagine, 4 manopole, azioni speciali |
| `prove/pad.txt` | grammatica del pad PS1 (con uno e due gamepad) |
| `prove/scene.txt` | salva, richiama, morph, file scritto bene |
| `prove/scene_riapri.txt` | le scene di `scene.txt` dopo chiudi e riapri |

`prove/esegui.py` le lancia tutte e dice OK / FALLITO per ognuna (solo libreria standard di
Python). Come prepara ogni prova (le scene dalla 0.5):
- ogni prova riceve un `--scene` suo, in una cartella temporanea, che all'inizio non c'e' (posti
  vuoti): nessuna prova dipende da un'altra o dalla cartella dei dati;
- se c'e' `prove/<nome>.scene`, la prova `<nome>.txt` parte da una sua copia;
- se c'e' `prove/<nome>.mappa`, la prova la riceve con `--mappa`;
- una prova `<nome>_riapri.txt` gira dopo `<nome>.txt` e riceve il file che quella ha lasciato
  (= chiudi il programma e riaprilo).
