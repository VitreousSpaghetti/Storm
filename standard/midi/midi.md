# MIDI

Ingresso MIDI da qualsiasi porta (USB, DIN con interfaccia, porte virtuali), su tutte le
piattaforme. Tutte le porte d'ingresso sono aperte insieme; una porta collegata a programma avviato
viene trovata da sola (si ricontrolla l'elenco ogni 2 s).

## Note (tastiera MIDI)
Un synth puo' essere **a voce fissa** (drone, come storm), **a note** (polifonico) o **a
sequenza** (un sequencer che suona da solo e le note lo spostano: dalla 0.8). Lo standard:

| Evento | Synth a drone (storm) | Synth a note | Synth a sequenza |
|---|---|---|---|
| Nota premuta | Diventa la nota di casa (ROOT + OCTAVE) con GLIDE; la velocita' muove la macro 1 (SWELL) per la durata del tasto | Suona una voce | Diventa la tonica della sequenza (senza l'ottava, se il registro lo decidono altri parametri); la sequenza continua e si sposta. La velocita' la usa il synth se vuole |
| Nota lasciata | Il drone **continua** sull'ultima nota; finisce lo SWELL | Rilascio della voce | Niente: la sequenza continua |
| Piu' note insieme | Premute entro 50 ms l'una dall'altra: la piu' bassa = ROOT, gli intervalli delle altre scelgono l'accordo degli strati (CHORD) se ce n'e' uno uguale, se no CHORD non cambia. Una nota nuova premuta mentre se ne tiene un'altra da prima = nuova nota (legato), non accordo | Polifonia | Nessun accordo: conta la nota piu' bassa |
| Pitch bend | Intonazione propria del synth, sopra FINE: non cambia il parametro FINE (storm: ±2 semitoni, FINE resta ±50 cent). Ampiezza nel file di mappa | Pitch bend | Intonazione propria, su tutta la sequenza |
| Aftertouch di canale | Macro 2 (SINK) | Configurabile | Configurabile |
| Mod wheel (CC 1) | Macro 3 | Configurabile | Configurabile |
| Sustain (CC 64) | `HOLD`: pedale giu' = acceso, su = spento | Sustain | L'interruttore che tiene del synth (es. il giro si ripete): giu' = acceso, su = spento |

Il pedale sustain va sempre all'interruttore "che tiene" del synth, se ne ha uno (storm: `HOLD`),
scritto nella sua mappa come `cc 64 = NOME` (vedi [../controlli/azioni.md](../controlli/azioni.md)).
Dalla 0.8, precisazione.

**Zona comandi** (facoltativa, spenta di base): le note di un'ottava scelta (es. 36-47) non
suonano ma lanciano azioni (scene 1-8, PLAY, FREEZE, MUTE, VISUAL). Serve con le tastiere
piccole senza pulsanti. Si accende nel file di mappa. Oltre a scene e interruttori, una nota della
zona accetta le azioni secche (`SALVA`, `AVANTI`, `INDIETRO`: dalla 0.5); `AVANTI` e `INDIETRO` non
hanno una nota predefinita. Dalla 0.8 la zona accetta **ogni** azione speciale del synth
(anche interruttori e azioni secche che lo standard non nomina) e i parametri di tipo `pulsante`
(es. `TAP`: un colpo quando la nota si preme).

Le note suonano sempre, anche con PLAY spento (PLAY riguarda solo i gesti del pad e del mouse) e,
in un synth a sequenza, anche con `AVVIA` spento.

## CC: due livelli
1. **CC fissi**: ogni parametro puo' avere un suo CC, sempre lo stesso, qualunque pagina sia
   aperta. Si usano i numeri consigliati dal MIDI dove esistono: 74 = cutoff/brillantezza,
   71 = risonanza, 91 = riverbero, 7 = volume, 1 = mod wheel, 64 = sustain.
2. **CC di pagina**: CC 20 sceglie la pagina del lato 1 (valore 0-127 diviso per il numero di
   pagine), CC 21-24 = le 4 manopole della pagina aperta sul lato 1; CC 25 = pagina del lato 2,
   CC 26-29 = sue 4 manopole. Cosi' un controller con 8 manopole suona tutto il synth senza
   mappa.

## Manopole assolute e relative
- **Assolute** (0-127, la maggior parte): **soft takeover**. Se la manopola fisica non e' dove sta
  il valore, il valore non salta: aspetta che la manopola lo "prenda" passandoci sopra. Lo schermo
  mostra un segno sulla manopola finche' non l'ha preso.
- **Relative** (encoder senza fine): si indicano nel file di mappa (`relativo`), formato
  "complemento a due" (1-63 = +, 65-127 = -).
- Risoluzione: 128 passi bastano per parametri 0-99; per gli altri si accettano coppie a 14 bit
  (CC n + CC n+32) se il controller le manda.

## MIDI learn
- Mouse: tasto destro su una manopola > `LEARN`, poi muovi un controllo MIDI: e' assegnato.
  `CLEAR` lo toglie. Esc annulla.
- L'assegnazione si salva subito nel file di mappa dell'utente (vedi sotto).

## Clock
- **Clock in** (24 tick per battito): se arriva, il CLOCK del synth lo segue (BPM mediato su 1
  battito, mostrato con una `E` vicino al BPM); senza clock per 2 s torna al suo tempo.
- I passi dei sequencer **cadono sui tick**: ogni 6 tick un sedicesimo; se il sequencer e' fuori
  di piu' di mezzo sedicesimo si rimette a posto (storm fa cosi'). Il BPM mediato serve allo
  schermo e ai tempi dentro un passo, non a contare i passi: cosi' la sequenza non scivola rispetto
  a chi manda il clock. Dalla 0.8, precisazione.
- Start / Continue = il prossimo tick e' il battito 1. Stop = i sequencer si fermano, **il suono
  no** (un drone non si ferma mai).
- Un synth che ha `AVVIA` (dalla 0.8): Start / Continue = `AVVIA` acceso, Stop = `AVVIA` spento.
  Un synth senza `AVVIA` (un drone) fa come sopra: con Stop si fermano solo i sequencer.
- Clock out: non in v0.1.

## Program change
Program change 0-7 = scene 1-8 (morph col tempo della scena). Gli altri si ignorano. Un synth
senza scene li ignora tutti.

## Canali
Di base si ascoltano tutti i canali (omni). Nel file di mappa si puo' fissare un canale per
lato (`canale 1 = 1`).

## File di mappa
Testo semplice, una regola per riga, `#` = commento. Il predefinito sta nel repository
(`mappe/<synth>.txt`); quello dell'utente (con le assegnazioni del learn) sta nella cartella dei
dati del programma (vedi [../piattaforme/](../piattaforme/)) e vince sul predefinito.

```
# mappe/storm.txt
canale      omni
cc 74       = CUTOFF
cc 71       = RES
cc 91       = REVERB
cc 7        = VOLUME
cc 1        = M3UNREST
cc 64       = HOLD          # azione speciale
cc 20-29    = pagine        # CC di pagina come sopra
cc 16       = FOLD          # uso generale: assoluto
cc 2        = spento        # toglie un CC della mappa predefinita
piega       = 2             # ampiezza in semitoni (1-12)
pressione   = SINK          # gesto della macro 2 (oppure un id, es. M2DEEP)
nota        = drone         # drone | note | sequenza | spento
zona        36-47 spenta    # zona comandi: accesa / spenta
zona 36     = SCENA 1
zona 44     = PLAY
programma   = scene
```
Regole del formato:
- a sinistra dell'`=` la sorgente (`cc N`, `cc N-M`, `piega`, `pressione`, `nota`, `zona N`,
  `programma`, `canale`); a destra un `id` della tabella dei parametri o un'azione speciale;
- un `id` di tipo `pulsante` (es. `TAP`) a destra di `cc N =` o `zona N =` (dalla 0.8): un colpo
  quando il CC passa da sotto 64 a 64 o piu', o quando la nota della zona si preme. Sui CC di
  pagina (21-24, 26-29) un `pulsante` non fa niente, come prima;
- un'azione secca del synth a destra di `cc N =` o `zona N =` (dalla 0.8): parte quando il CC
  passa a 64 o piu' o quando la nota si preme, come `SALVA`;
- `piega = N`: ampiezza del pitch bend in semitoni (1-12), sommata all'intonazione e non a un
  parametro;
- `pressione = ID | SINK`: con un `id` la pressione muove quella manopola; con `SINK` fa il gesto
  della macro 2, che si somma alla manopola e ricade quando si lascia (la manopola non si muove);
- `pressione = ID somma` (dalla 0.8): la pressione fa un gesto su quel parametro, come `SINK`: si
  somma al valore impostato (fino a `max`) e ricade quando si lascia; il valore impostato e la
  manopola non si muovono. Serve quando si premono tasti di continuo e la pressione non deve
  cancellare una regolazione fatta col mouse o con un CC;
- `nota = sequenza` (dalla 0.8): per un synth a sequenza; il traduttore manda le note come con
  `note`, e' il synth che le usa come tonica;
- `cc N = spento`: toglie il CC `N` della mappa predefinita (e' la riga che scrive `CLEAR` nel
  file dell'utente);
- encoder senza fine: `cc N = ID relativo` su un CC che il controller manda come encoder (i CC
  16-19 sono quasi sempre manopole assolute);
- una riga sbagliata si salta e si scrive un avviso (numero di riga) senza fermare il programma.
