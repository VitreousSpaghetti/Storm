# Grammatica: pagine e 4 manopole

La grammatica nasce sui pad PS1 (**tieni un tasto = pagina, frecce e dorsali = 4 manopole**) e
resta la stessa su tutti i dispositivi. Chi sa suonare il synth con un ingresso lo sa suonare con
tutti gli altri.

## Pagine
- Ogni lato ha una **pagina a riposo** (storm: SHAPE sul lato 1, MIX sul lato 2): e' quella che
  le manopole muovono quando nessuna pagina e' aperta.
- Una pagina si apre in due modi:
  - **temporanea**: finche' tieni il comando (tasto del pad, tasto del PC, tasto del mouse);
    quando lasci si torna alla pagina a riposo;
  - **fissa**: con un tocco doppio (pad), un tocco singolo (tastiera PC), un clic (mouse); resta
    aperta con la cornice gialla finche' non la chiudi o ne apri un'altra.
  - sulla tastiera del PC e col mouse **tocco** (clic) = tasto lasciato prima di 300 ms senza aver
    girato manopole in mezzo; altrimenti e' tenuto.
- Anche la pagina a riposo si puo' portare davanti (il suo pannello in grande): finche' tieni, o
  fissa con la cornice gialla. Nelle azioni: `PAGINA lato 0 tieni`, poi `lascia` (torna a posto)
  o `fissa` (resta davanti; la volta dopo torna a posto). `PAGINA lato 0 fissa` da sola (tasto 0)
  chiude come prima la pagina fissa del lato e torna a riposo. Dalla 0.3.
- I due lati sono indipendenti: si puo' avere una pagina aperta sul lato 1 e una sul lato 2.
- Lo schermo mostra sempre in basso l'aiuto della pagina aperta (o di quella sotto il mouse).

## Manopole
| Posto | Pad | Tastiera PC | Mouse | MIDI |
|---|---|---|---|---|
| 1 | Su / Giu' | Su / Giu', W / S | trascina o rotella sulla manopola 1 | CC di pagina 1 o CC fisso |
| 2 | Destra / Sinistra | Destra / Sinistra, D / A | idem manopola 2 | CC di pagina 2 o CC fisso |
| 3 | R1 / L1 | E / Q | idem manopola 3 | CC di pagina 3 o CC fisso |
| 4 | R2 / L2 | R / F | idem manopola 4 | CC di pagina 4 o CC fisso |

- **Su, Destra, R1, R2 alzano; Giu', Sinistra, L1, L2 abbassano.** Tenendo premuto il valore
  scorre (prima lento, poi veloce), come sulla PS1.
- Per i parametri `scelta` un passo = un'etichetta.

## Modo PLAY
Un interruttore per lato 1: quando e' acceso, i comandi delle manopole diventano **gesti** (storm:
Su = SWELL, Giu' = SINK, Sinistra/Destra = nota, L1/R1 = ottava, L2/R2 = accordo). La tastiera MIDI
suona sempre, anche con PLAY spento: PLAY serve a chi non ha la tastiera MIDI.

## Cose da non cambiare tra synth
- l'ordine dei 4 posti;
- "alza" sempre sopra/destra, "abbassa" sempre sotto/sinistra;
- giallo = pagina fissa, aiuto sempre in basso;
- le azioni comuni di [azioni.md](azioni.md) con i loro tasti.
