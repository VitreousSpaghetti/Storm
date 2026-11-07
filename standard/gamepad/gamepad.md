# Gamepad

I gamepad seguono esattamente i comandi della versione PS1 (vedi il LEGGIMI del synth). Si
leggono con l'API gamepad di SDL, che li rende tutti uguali (PlayStation, Xbox, Switch, generici).

## Tasti per posizione
| PS1 | Posizione SDL | Xbox |
|---|---|---|
| Croce (X) | sud | A |
| Cerchio | est | B |
| Quadrato | ovest | X |
| Triangolo | nord | Y |
| Select | back | View |
| Start | start | Menu |
| L1 / R1 | spalla sinistra / destra | LB / RB |
| L2 / R2 | grilletti (premuti oltre meta') | LT / RT |
| levette | levette | levette |

## Synth nati sulla PS1: pad nativo
Per un synth che nasce sulla PS1 (come storm) il gamepad non diventa azioni comuni ma **stato del
pad PS1**: 16 tasti nell'ordine del protocollo dei pad e 4 byte delle levette come il DualShock
(128 = ferma). Il synth lo legge una volta per fotogramma come sulla PS1 e fa da se' i gesti (tieni
= pagina, doppio tocco, scorrimento, combinazioni con Start e Select, gesti di PLAY). Cosi' il
codice del synth resta quello della PS1 e la grammatica e' identica per costruzione.
- Anche le **manopole della tastiera del PC** (frecce, WASD, Q/E, R/F) arrivano come tasti di
  questo pad, sul lato scelto dalla tastiera; pagine e azioni speciali della tastiera restano
  azioni comuni.
- Un tocco piu' corto di un fotogramma conta lo stesso.
- Un synth che non nasce sulla PS1 riceve dal gamepad le azioni comuni `PAGINA` e `MANOPOLA`.

## Uno o due gamepad
- Due gamepad: il primo collegato = controller 1, il secondo = controller 2.
- Un gamepad solo: il tasto **Guide** (PS, Xbox, Home) tocco = passa dal lato 1 al lato 2 e
  viceversa (la testata mostra C1 o C2).
- Si collegano e scollegano a programma acceso; uno scollegato lascia tutti i suoi tasti. Un terzo
  gamepad non guida niente.
- Con due gamepad il tasto Guide non fa niente.
- Le levette: zona morta 15%, poi lineari. Nel byte del pad PS1 il bordo del 15% vale 128 +/- 41:
  i synth PS1 del progetto tolgono una loro zona morta di 40 e vedono -88..88, come sulla PS1.
- L2 / R2: premuti quando il grilletto passa meta' corsa.
