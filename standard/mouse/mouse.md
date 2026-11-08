# Mouse

Con il solo mouse (o trackpad) si deve poter fare tutto: aprire le pagine, girare ogni manopola,
usare le azioni speciali, la GUIDE, salvare e richiamare le scene, assegnare il MIDI.

## Schermo e coordinate
- Il synth disegna a bassa risoluzione (storm: 320x256, come la PS1). La finestra si ingrandisce
  di un fattore intero, con bande nere se avanza spazio; si puo' ridimensionare e mettere a tutto
  schermo (F11 / doppio clic sulla testata).
- Le coordinate del mouse si convertono nei pixel del disegno; tutte le zone cliccabili sono
  almeno **12x12 pixel del disegno** (con fattore 3 = 36 pixel veri). Le bande nere sono fuori dal
  disegno: un clic li' non fa niente.
- Finche' il mouse non passa sul disegno, lo schermo resta quello del pad (come la PS1). Dal primo
  movimento del mouse sul disegno compaiono i pulsanti della testata, i nomi delle pagine in basso
  e l'aiuto della cosa sotto il puntatore, e restano. Un evento con il puntatore fuori dal disegno
  (finestra che perde il fuoco, mouse che esce) non li fa comparire. Dalla 0.3.

## Gesti
| Gesto | Dove | Effetto |
|---|---|---|
| Clic | su un pannello o sul nome di una pagina | Apre la sua pagina **fissa** (cornice gialla); clic di nuovo = chiude. Sul pannello della pagina a riposo: lo porta davanti fisso, di nuovo = torna a posto |
| Clic tenuto | su un pannello o sul nome di una pagina | Pagina **temporanea** finche' tieni (come il pad). Clic = lasciato prima di 300 ms senza girare niente in mezzo, come il tocco della tastiera |
| Clic | fuori dai pannelli | Come Esc: chiude le pagine fisse e si torna a riposo; nei menu (GUIDE) indietro di un livello. Lo sfondo di un menu o dell'aiuto non conta come "fuori" |
| Trascina su/giu' | su una manopola | Alza/abbassa: tutta la gamma in circa 100 pixel del disegno (vedi sotto); con **Shift** = fine (4 volte piu' pixel per passo) |
| Rotella | su una manopola | 1 scatto = 1 passo; con Shift = 10 passi |
| Doppio clic | su una manopola | Torna al valore iniziale. Doppio clic = due clic sulla stessa zona entro 400 ms; un terzo clic non fa un altro doppio clic |
| Clic | su una `scelta` | Etichetta successiva; dopo l'ultima si ricomincia dalla prima. Un pulsante che va in una sola direzione (es. `OCT-` / `OCT+`) si ferma agli estremi |
| Rotella | su una `scelta` | Etichetta dopo / prima; si ferma alla prima e all'ultima (non ricomincia) |
| Tasto destro | su una manopola o una `scelta` | Menu: `LEARN`, `CLEAR`, `DEFAULT` |
| Clic | pulsanti della testata | Azioni speciali (MUTE, PLAY, FREEZE, VISUAL, GUIDE, HOLD, LOCK); LOCK va tenuto 1 s, anche per sbloccare. Un pulsante da tenere (es. LOCK) conta solo se lo lasci sopra il pulsante; lasciato fuori, o con la finestra che perde il fuoco, non fa niente (la barra che si riempie si svuota) |
| Passaggio | su qualsiasi cosa | L'aiuto in basso spiega quella cosa |

- Durante il trascinamento il puntatore resta fermo e nascosto (movimento relativo), cosi' non
  esce dalla finestra; al rilascio torna dove era.
- Pixel per passo nel trascinamento (dalla 0.8): **100 / (max - min)**, parte intera, minimo 1.
  Per una gamma 0-99 (tutti i `continuo` di storm) resta 1 pixel = 1 passo, con Shift 4 pixel,
  come prima; per una gamma piccola i passi si allargano (0-8: 12 pixel a passo, 0-2: 50), cosi'
  un valore si posa anche con la mano che trema. Un synth che lo usa non deve fare niente: il
  traduttore lo ricava da `min` e `max`.
- Il trascinamento di un parametro della tabella manda azioni `VALORE` (non `MANOPOLA`): niente
  scorrimento accelerato. Cio' che non e' nella tabella (selezione in un menu, quantita' di una
  macro) manda `MANOPOLA` del suo posto, un passo per pixel.
- Shift si legge dalla tastiera del PC (il mouse non ha uno Shift suo).

## Testata
- Ogni interruttore e ogni azione speciale del synth ha il suo pulsante in testata: acceso = col
  suo colore, spento = scuro. Il synth sceglie dove metterli; se non stanno su una riga si usano
  due righe (storm: VISUAL, GUIDE, LOCK in cima; PLAY, FREEZE, MUTE, HOLD nella riga della nota).
- `HOLD` e gli altri interruttori col pulsante mandano `cambia`.
- Nella testata del tutorial ci sono due pulsanti `<` e `>` (`INDIETRO` / `AVANTI`, almeno 12x12,
  senza coprire il testo del passo). Dalla 0.5.

## Pagine in basso
- Col mouse, al posto dell'aiuto del pad, in basso ci sono i nomi delle pagine dei due lati: si
  usano come i pannelli. Ogni pagina deve poter essere aperta con un clic: se non ha un suo
  pannello, ha almeno il nome in basso.
- Sotto i nomi, l'aiuto della cosa sotto il puntatore.

## Menu del tasto destro
- Si apre dove si e' cliccato, dentro lo schermo; un clic su una voce la esegue, un clic altrove
  lo chiude e basta.
- Una voce che oggi non puo' funzionare (LEARN e CLEAR senza il MIDI) resta **grigia** e non fa
  niente: non si nasconde, cosi' il menu e' sempre uguale.

## Modo PLAY col mouse
In PLAY, con nessuna pagina del lato 1 aperta, in basso compare una **striscia di un'ottava** (12
tasti) piu' due pulsanti `OCT-` / `OCT+` (al posto dei nomi delle pagine del lato 1): clic = nota,
con velocita' 100. Tenendo premuto il tasto sinistro nella meta' alta / bassa di un tasto SWELL /
SINK crescono finche' tieni (come Su/Giu' del pad).

## Cose da non fare
- Niente menu a tendina del sistema, niente finestre di dialogo: tutto dentro lo schermo del synth.
- Niente gesti che servono solo al mouse e non esistono su pad o tastiera (tranne il learn).
