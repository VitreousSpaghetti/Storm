# Tastiera del PC

I tasti si leggono **per posizione** (scancode), non per lettera: valgono uguali su tastiere
italiane, americane, francesi o tedesche. I nomi qui sotto sono quelli della tastiera americana.

## Pagine
| Tasto | Lato 1 | Tasto | Lato 2 |
|---|---|---|---|
| 1 | pagina 1 (storm: OSC) | Shift + 1 | pagina 1 (storm: MOD A) |
| 2 | pagina 2 (GRAIN) | Shift + 2 | pagina 2 (MOD B) |
| ... | nell'ordine della tabella | ... | nell'ordine della tabella |
| - | pagina 10 | Shift + - | pagina 10 (storm: EDIT) |
| 0 | torna a riposo | Shift + 0 | torna a riposo |
| Esc | tutti e due i lati a riposo | | |

Il tasto `-` e' quello a destra dello 0 (sulla tastiera italiana e' `'`).

- **Tocco** = pagina fissa (tocca di nuovo per chiudere). **Tenuto** = temporanea, come il pad.
- Le pagine a riposo non hanno un numero: ci si torna con 0 o Esc.

## Manopole
Il lato su cui agiscono le manopole e' quello dell'ultima pagina aperta (mostrato in testata).
| Posto | Alza | Abbassa |
|---|---|---|
| 1 | Su, W | Giu', S |
| 2 | Destra, D | Sinistra, A |
| 3 | E | Q |
| 4 | R | F |

| Modo | Cosa fa |
|---|---|
| tasto tenuto | il valore scorre come sul pad (un passo, poi dopo una pausa uno per fotogramma) |
| Shift + tasto | **un passo da 10** per ogni pressione, senza scorrimento |
| Ctrl (Cmd sul Mac) + tasto | **un passo solo**, senza scorrimento, anche se tieni |

- Shift ha due usi che non si scontrano: con un numero sceglie il lato 2, con una manopola fa
  passi da 10. Per girare di un passo alla volta una pagina del lato 2 **tenuta**: tieni il numero,
  lascia Shift, gira.
- I tasti riservati vincono: Ctrl+S resta salva e Ctrl+Q resta esci (non sono un passo di S e Q).
  Per un passo singolo su quei posti basta un tocco breve, o le frecce per i posti 1 e 2.
- La ripetizione automatica dei tasti del sistema non conta: lo scorrimento lo fa il synth.

## Azioni speciali
Vedi la tabella in [../controlli/azioni.md](../controlli/azioni.md). Tasti riservati per tutti i
synth: F1/Tab = GUIDE, F11 = tutto schermo, Esc = chiudi pagina o menu, Ctrl+S = salva scena,
Ctrl+Q / Cmd+Q = esci (chiede conferma se c'e' qualcosa di non salvato).

| Tasto | Nei menu (GUIDE e simili) | Fuori dai menu |
|---|---|---|
| Su / Giu' | sceglie la voce | manopola 1 |
| Invio | conferma (come Croce del pad) | come Croce del pad |
| Esc | indietro di un livello; dal primo livello chiude il menu | chiude le pagine fisse |
| PagGiu' / PagSu' | — | `AVANTI` / `INDIETRO`: passo dopo / prima del tutorial aperto (dalla 0.5) |

"Non salvato" vuol dire: scene o mappa MIDI cambiate e non ancora scritte su file. Finche' il synth
non scrive niente su file, Ctrl+Q esce subito. Quando la finestra perde il fuoco, tutti i tasti si
considerano lasciati.

## PLAY
Con PLAY acceso le stesse coppie diventano gesti (storm: Su = SWELL, Giu' = SINK, Sinistra/Destra
= nota, Q/E = ottava, F/R = accordo).
