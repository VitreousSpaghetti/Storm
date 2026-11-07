/*
 * Controlli secondo lo standard (standard/controlli/): tabella dei parametri, azioni comuni e
 * traduttori di tastiera del PC, gamepad, mouse e MIDI. Non conosce il synth: riceve la sua tabella e le
 * sue zone cliccabili come dati.
 * Chi legge gli ingressi (thread principale o modo prova) chiama controlli_evento_*; il synth (nel
 * thread dell'audio) prende le azioni con controlli_prendi e lo stato del pad con controlli_pad.
 * Tra i due solo interi atomici e una coda fissa: nessun lock, nessuna allocazione.
 */
#ifndef CONTROLLI_H
#define CONTROLLI_H
#include <stdint.h>

// Zona morta che i synth nati sulla PS1 tolgono da soli alle levette (byte del pad, 128 +/- 40)
#define ZONA_MORTA_PS1 40

/* ---- Tabella dei parametri (standard/controlli/parametri.md) ---- */
enum { P_CONTINUO, P_SCELTA, P_PULSANTE };
typedef struct {
	const char *id, *nome, *pagina;
	const char *etichette; // solo per P_SCELTA, separate da ", "
	const char *anche;     // altre pagine con lo stesso parametro ("MACROS 1"), NULL se nessuna
	int         lato, posto; // lato 1-2, posto 1-4
	int         min, max, iniziale, tipo, bipolare, scena;
	int        *valore;    // valore impostato; si scrive solo nel thread del synth
} Param;

void         controlli_tabella(const Param *p, int n); // la tabella resta del synth
const Param *controlli_param(const char *id);          // NULL se l'id non c'e'
void         controlli_stampa_parametri(void);          // in Markdown (--parametri)
// Per il modo prova (controlla pagina, controlla degli interruttori), dal thread del synth: nome
// della pagina aperta sul lato 0/1; stato di un'azione speciale con stato (0/1, -1 = non c'e')
void         controlli_synth(const char *(*pagina)(int lato), int (*interruttore)(const char *nome));
const char  *controlli_pagina(int lato);
int          controlli_interruttore(const char *nome);

/* ---- Azioni comuni (standard/controlli/azioni.md) ---- */
enum { A_PAGINA, A_MANOPOLA, A_SPECIALE, A_VALORE, A_NOTA, A_PIEGA, A_CLOCK, A_SCENA }; // le ultime 3 dal MIDI
enum { TIENI, LASCIA, FISSA };              // modo di A_PAGINA
enum { CAMBIA, ACCESO, SPENTO };            // modo di A_SPECIALE (un'azione secca: ACCESO = CAMBIA)
enum { C_TICK, C_START, C_STOP, C_CONTINUA }; // modo di A_CLOCK
typedef struct {
	int8_t      tipo;
	int8_t      lato;  // 0 = lato 1, 1 = lato 2; -1 = tutti e due (Esc: chiude pagine fisse e menu)
	int8_t      modo;  // A_PAGINA: TIENI, LASCIA, FISSA (FISSA sulla pagina gia' fissa = la chiude);
	                   // A_SPECIALE: CAMBIA, ACCESO, SPENTO; A_CLOCK: C_*; A_NOTA: 1 = dal MIDI (la
	                   // velocita' muove SWELL, note insieme = accordo), 0 = striscia del mouse
	int8_t      n;     // A_PAGINA: pagina nell'ordine della tabella (0 = riposo); A_MANOPOLA: posto 0-3;
	                   // A_NOTA: nota MIDI; A_SCENA: scena 0-7
	int16_t     passi; // A_MANOPOLA: + alza, - abbassa; A_NOTA: velocita' (0 = lasciata);
	                   // A_PIEGA: ampiezza in semitoni (1-12)
	const char *nome;  // A_SPECIALE: nome dell'azione speciale (vedi la tabella del synth); A_VALORE: id
	                   // del parametro; stringhe costanti
	float       v;     // A_VALORE: 0.0-1.0, il synth lo porta tra min e max; A_PIEGA: -1..1;
	                   // A_CLOCK tick: BPM mediato (0 = non ancora); A_SPECIALE "SINK": pressione 0-1
} Azione;

// Un solo produttore: il thread principale (o il modo prova). Se la coda e' piena l'azione aspetta
// in una riserva e riparte alla prossima chiamata o a controlli_riprova(); niente si perde finche'
// la riserva non e' piena. Un altro thread (la callback di RtMidi gira nel suo) NON deve chiamare
// controlli_azione: deve passare dal thread principale, se no i produttori diventano due.
void controlli_azione(Azione a);
void controlli_riprova(void);    // dal produttore, a ogni giro del ciclo principale
int  controlli_in_attesa(void);  // azioni nella riserva (il modo prova la vuole a 0: errore)
int  controlli_prendi(Azione *a); // dal thread del synth; 0 se la coda e' vuota

/* ---- Ingressi ---- */
// Tastiera del PC per posizione (SDL_Scancode); ms = tempo dell'evento. Scancode 0 lasciato =
// lascia tutti i tasti (finestra che perde il fuoco).
void controlli_evento_tasto(int scancode, int giu, uint32_t ms);
// Gamepad SDL: id = SDL_JoystickID; tasto = SDL_GamepadButton; asse = SDL_GamepadAxis
void controlli_pad_collegato(uint32_t id, int si);
void controlli_evento_pad(uint32_t id, int tasto, int giu);
void controlli_evento_asse(uint32_t id, int asse, int valore);

/* ---- Mouse (standard/mouse/mouse.md) ---- */
// Il synth descrive a ogni fotogramma cosa c'e' sotto ogni punto del suo disegno (coordinate in
// pixel del disegno, almeno 12x12). Le zone aggiunte dopo stanno sopra a quelle di prima.
enum { Z_NIENTE,   // c'e' qualcosa ma il clic non fa niente (non chiude le pagine)
       Z_PANNELLO, // pagina n del lato: clic = fissa (di nuovo = chiude), tenuto = finche' tieni
       Z_MANOPOLA, // par: trascina su/giu', rotella, doppio clic = iniziale, tasto destro = menu
       Z_SCELTA,   // par: clic = passi (0 = etichetta dopo, in giro), rotella, tasto destro = menu
       Z_PASSO,    // manopola posto del lato: clic = passi; passi 0 = si trascina; rotella
       Z_PAD,      // tasto del pad PS1 del lato finche' tieni, n = bit (come PAD_CROSS): conferma e
                   // indietro nei menu
       Z_PULSANTE, // azione speciale nome; tieni > 0 = va tenuto tanti ms (LOCK)
       Z_NOTA,     // nota n; tenuto = manopola posto del lato verso passi (+1 alza, -1 abbassa)
       Z_TESTATA }; // doppio clic = tutto schermo
typedef struct {
	int16_t      x, y, w, h;
	int8_t       tipo, lato, n, posto;
	int16_t      passi, tieni;
	int16_t      valore; // Z_MANOPOLA, Z_SCELTA: valore impostato quando la zona e' stata fatta
	const Param *par;
	const char  *nome;   // Z_PULSANTE: azione speciale
	const char  *aiuto;  // testo dell'aiuto per chi lo disegna (puo' essere NULL)
} Zona;
#define ZONA_MIN 12

// Dal thread del synth, a ogni fotogramma (una copia passa al thread degli ingressi senza lock);
// w, h: grandezza del disegno, per il menu del tasto destro
void controlli_zone(const Zona *z, int n, int w, int h);
int  controlli_zone_piccole(void); // zone pubblicate sotto 12x12 (il modo prova le vuole a 0)
// La zona che sta sopra in (x, y), NULL se nessuna
const Zona *zona_sotto(const Zona *z, int n, int x, int y);

// Eventi del mouse dal thread principale (o dal modo prova): x, y in pixel del disegno (fuori =
// negativi o oltre il disegno), n = scatti della rotella (+ = in su), ms = tempo dell'evento.
// Shift si legge dalla tastiera (controlli_evento_tasto). Il valore restituito dice alla finestra
// cosa fare col puntatore.
enum { MOUSE_MUOVI, MOUSE_GIU, MOUSE_SU, MOUSE_DESTRO, MOUSE_ROTELLA };
enum { MOUSE_NIENTE, MOUSE_RELATIVO, MOUSE_ASSOLUTO, MOUSE_TUTTO_SCHERMO };
int controlli_evento_mouse(int ev, int x, int y, int n, uint32_t ms);
// Per il disegno, dal thread del synth: -1 se il mouse non e' mai passato sul disegno, se no 1 se
// il tasto sinistro e' giu'; x = y = -1 se il puntatore e' fuori
int controlli_mouse(int *x, int *y);
// Menu del tasto destro (LEARN, CLEAR, DEFAULT): 1 se aperto, con angolo e parametro. LEARN e CLEAR
// restano grigi finche' non c'e' il MIDI (controlli_midi).
enum { MENU_LEARN, MENU_CLEAR, MENU_DEFAULT, MENU_N };
#define MENU_W    64
#define MENU_RIGA 13
int controlli_menu(int *x, int *y, const Param **p);
int controlli_menu_voce(int x, int y); // voce del menu aperto in (x, y), -1 se nessuna

/* ---- MIDI (standard/midi/midi.md) ---- */
// Un messaggio MIDI intero (1-3 byte, niente SysEx) dal thread principale o dal modo prova, mai
// dalla callback di RtMidi; ns = tempo del messaggio (per il BPM del clock e il soft takeover)
void controlli_evento_midi(const uint8_t *m, int n, uint64_t ns);
// Regole di una mappa (tutto il testo; nome per gli avvisi): vincono su quelle lette prima. Una riga
// sbagliata si salta con un avviso "nome:riga"; restituisce quante righe sono state saltate.
int  controlli_mappa(const char *testo, const char *nome);
// File della mappa dell'utente, dove learn e CLEAR scrivono (NULL = le assegnazioni restano in memoria)
void controlli_mappa_utente(const char *percorso);
// 1 = c'e' il MIDI (almeno una porta aperta, o il modo prova): LEARN e CLEAR si possono usare
void controlli_midi_attivo(int si);
int  controlli_midi(void);
// Dal thread del synth: soft takeover del parametro (-1 = mai toccato dal MIDI, 0 = la manopola
// fisica non l'ha ancora preso, 1 = preso) e parametro in attesa del learn (NULL = nessuno)
int          controlli_midi_preso(const Param *p);
const Param *controlli_learn(void);

// Pad PS1 del lato 0/1 (tasti del gamepad + manopole della tastiera + tasti tenuti col mouse): bit nell'ordine del pad
// PS1 (1 = premuto; un tocco piu' corto di un fotogramma vale lo stesso) e levette come i byte
// del DualShock (sinistra X, Y, destra X, Y; 128 = ferma). Dal thread del synth, una volta per
// fotogramma.
uint16_t controlli_pad(int lato, uint8_t leve[4]);
// Lato da mostrare in testata (0/1), -1 finche' tastiera o tasto Guide non l'hanno scelto
int controlli_lato(void);
// Dispositivo usato per ultimo, di cui parlano i testi a schermo (standard/controlli/azioni.md):
// l'ultimo che ha premuto qualcosa; il mouse non toglie il MIDI. All'avvio D_PAD.
enum { D_PAD, D_TASTIERA, D_MOUSE, D_MIDI };
int controlli_dispositivo(void);
#endif
