# Scene su file

Le scene del synth (storm: 8 posti, al posto della memory card della PS1) stanno in un file di
testo, `scene.txt`, nella cartella dei dati dell'utente (vedi
[../piattaforme/piattaforme.md](../piattaforme/piattaforme.md)). Il file si puo' leggere,
correggere a mano e copiare su un altro PC. Dalla 0.5.

## Formato
```
# scene di storm (versione 1)
[scena 2]
CUTOFF = 45
MODE = 0
macro 1 1 = CUTOFF 6
```

- `#` = commento, fino alla fine della riga. Le righe vuote non contano. La prima riga dice il
  synth e la versione del formato (oggi 1).
- `[scena N]` apre la sezione di un posto (N da 1 al numero di posti del synth). Una sezione per
  ogni posto pieno; un posto senza sezione e' vuoto.
- `ID = intero`: il valore impostato di un parametro con `scena = si'` nella tabella (vedi
  [parametri.md](parametri.md)). Per una `scelta` si scrive il **numero**, non l'etichetta: le
  etichette possono cambiare, gli `id` no.
- Dopo le righe `ID =` vengono le righe proprie del synth, descritte dal synth. Storm:
  `macro M P = ID quantita'` = posto P (1-4) della macro M (1-4): destinazione `ID` (un `id` della
  tabella) e quantita' da -9 a 9.
- Scrivendo, il synth mette tutti i parametri con `scena = si'` nell'ordine della tabella, poi le
  sue righe proprie.

## Lettura
| Caso | Cosa succede |
|---|---|
| Riga sbagliata, `id` sconosciuto, `id` con `scena = no`, sezione fuori dai posti | Avviso `nome_del_file:riga` su stderr e la riga (o la sezione) si salta; il programma va avanti |
| Valore fuori da `min`..`max` (o quantita' fuori da -9..9) | Portato dentro (al limite piu' vicino), senza avviso |
| `id` che manca nella sezione (es. file di una versione con meno parametri) | Valore `iniziale` della tabella |
| Stesso posto o stesso `id` due volte | Vale l'ultimo |
| File che non c'e' | Tutti i posti vuoti, nessun avviso |

Le scene non si caricano da sole all'avvio: il suono di partenza e' quello `iniziale`, come sulla
PS1. Si richiamano con l'azione `SCENA` (GUIDE, program change, zona comandi).

## Scrittura
- Il file si riscrive tutto a ogni `SALVA`, subito: non restano scene "non salvate", quindi
  Ctrl+Q esce senza chiedere (vedi [../tastiera/tastiera.md](../tastiera/tastiera.md)).
- Prima in un file temporaneo, poi rinominato (mai un file a meta'). Se la scrittura non riesce:
  avviso su stderr, messaggio a schermo, il vecchio file resta com'era.
- Il thread audio non scrive file: passa il contenuto al thread principale, che lo scrive.
- Nel modo prova il file e' solo quello di `--scene` (vedi [../prove/prove.md](../prove/prove.md)).
