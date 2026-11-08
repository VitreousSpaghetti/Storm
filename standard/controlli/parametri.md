# Tabella dei parametri

Ogni synth descrive i suoi parametri in **una sola tabella** nel codice. Tutto il resto (pannelli,
mouse, mappe MIDI, aiuto a schermo, salvataggio delle scene, documentazione) si ricava da li'.

## Campi
| Campo | Esempio (storm) | Note |
|---|---|---|
| `id` | `CUTOFF` | Maiuscolo, senza spazi, unico, **mai rinominato** dopo un rilascio: e' la chiave delle mappe MIDI e dei file delle scene |
| `nome` | `CUTOFF` | Testo a schermo (inglese), al massimo 7 caratteri |
| `lato` | `1` | 1 o 2: il controller o la meta' dell'interfaccia (storm: 1 = cosa suona, 2 = come si muove) |
| `pagina` | `SHAPE` | Nome della pagina (vedi [grammatica.md](grammatica.md)) |
| `posto` | `1` | 1-4: quale delle 4 manopole della pagina |
| `min`, `max` | `0`, `99` | Interi. Tutti i valori interni sono interi (come sulla PS1) |
| `iniziale` | `60` | Valore all'avvio |
| `tipo` | `continuo` | `continuo` (manopola), `scelta` (elenco di etichette), `pulsante` (azione, nessun valore) |
| `etichette` | `LP BP HP` | Solo per `scelta` |
| `bipolare` | no | Si' se lo zero sta al centro (es. FINE -50..+50, quantita' delle macro -9..+9) |
| `scena` | si' | Si salva nelle scene? Di solito no per cio' che non fa parte del suono (tempo del morph, BPM se segue il clock esterno). Il volume e' una scelta del synth: storm lo salva nelle scene come sulla PS1 |
| `anche` | `MACROS 1` | Facoltativo: altre pagine dove compare lo stesso parametro (`PAGINA posto`, separate da `, `). Es. storm: `M1RISE` sta su MIX posto 1 e anche su MACROS posto 1 |

Regole:
- Una pagina ha **al massimo 4 parametri**, in posti fissi: 1 = Su/Giu', 2 = Destra/Sinistra,
  3 = R1/L1, 4 = R2/L2 (stesso ordine per tutti i dispositivi).
- Ogni lato ha la pagina a riposo e **al massimo 10 altre pagine**, numerate 1-10 in un ordine fisso
  scelto dal synth (quello in cui le pagine compaiono nella tabella). Una pagina senza parametri
  nella tabella (un menu, es. EDIT di storm) ha comunque il suo numero: il synth la dichiara
  nell'elenco delle sue pagine.
- Lo stesso parametro su piu' pagine e' **una riga sola** (un solo `id`, un solo valore): le
  altre pagine si scrivono in `anche`.
- `pulsante`: `min` = `max` = 0, `scena` = no (non ha un valore da salvare).
- `bipolare` dice solo come si disegna: i valori restano interi tra `min` e `max` (FINE di storm:
  0-99, lo zero a schermo e' 50).
- Gli `id` dei parametri e i nomi delle azioni speciali (MUTE, HOLD...) stanno nello stesso
  elenco: un nome non puo' essere tutte e due le cose (le mappe MIDI e le prove li usano insieme).
- Un posto vuoto si lascia vuoto (es. NOTE di storm, posto 4): non si riempie con parametri di
  altre pagine.
- Il valore che **suona** (manopola + macro + modulazioni) e' diverso da quello impostato: lo
  schermo li mostra entrambi; mouse e MIDI cambiano sempre quello impostato.
- **Gamma che dipende da un altro parametro** (dalla 0.8, precisazione): `min` e `max` sono la
  gamma piu' larga. Il synth puo' stringere il valore secondo un altro parametro (es. un ritmo
  euclideo: i colpi non superano i passi) e lo scrive nelle note della sua tabella. `VALORE` si
  porta prima tra `min` e `max`, poi il synth stringe; il soft takeover del MIDI guarda il valore
  stretto. La parte della manopola oltre il limite di adesso non fa niente.
- **Precisione interna piu' fine** (dalla 0.8, precisazione): `min` e `max` sono la gamma della
  manopola, un passo = un intero. Un synth che tiene dentro una risoluzione piu' fine (es. il BPM in
  decimi) la cambia solo con cio' che non passa dalla tabella (tap, clock, levette del pad nativo)
  e puo' mostrarla a schermo. Anche il numero a schermo lo sceglie il synth (es. FINE di storm,
  o una manopola 0-112 mostrata 0-99): mouse, MIDI e prove usano sempre il valore della tabella.

## Documentazione generata
Il programma stampa la sua tabella in Markdown con `--parametri` (vedi [../prove/](../prove/)).
Le tabelle dei parametri nei documenti si rigenerano cosi', non si scrivono a mano.
