/*
 * Controlli secondo lo standard: vedi controlli.h. Qui niente del synth.
 * Il gamepad segue la grammatica dei pad PS1 (standard/gamepad/): i suoi tasti diventano i bit del
 * pad PS1 del suo lato, e il synth li legge come sulla PS1 (tieni = pagina, scorrimento, doppio
 * tocco, combinazioni). La tastiera (standard/tastiera/) fa lo stesso per le 4 manopole (posto 1 =
 * Su/Giu', 2 = Destra/Sinistra, 3 = R1/L1, 4 = R2/L2, come dice la grammatica), cosi' scorrimento e
 * gesti di PLAY sono quelli del pad; pagine e azioni speciali diventano azioni comuni.
 * Il mouse (standard/mouse/) lavora sulle zone che il synth pubblica a ogni fotogramma: pannelli,
 * manopole, scelte, pulsanti; trascinare, rotella, doppio clic e menu del tasto destro sono qui.
 * Il MIDI (standard/midi/) arriva gia' nel thread principale: note, CC fissi o di pagina col soft
 * takeover, encoder, 14 bit, pitch bend, pressione, program change, clock; regole dal file di mappa,
 * MIDI learn scritto nella mappa dell'utente.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "controlli.h"

/* ---- Tabella dei parametri ---- */

static const Param *tab;
static int          n_tab;

void controlli_tabella(const Param *p, int n) { tab = p, n_tab = n; }

const Param *controlli_param(const char *id)
{
	for (int i = 0; i < n_tab; i++)
		if (strcmp(tab[i].id, id) == 0)
			return &tab[i];
	return NULL;
}

void controlli_stampa_parametri(void)
{
	static const char *const tipo[3] = { "continuo", "scelta", "pulsante" };
	printf("| id | nome | lato | pagina | posto | min | max | iniziale | tipo | etichette | bipolare | scena | anche |\n");
	printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
	for (int i = 0; i < n_tab; i++) {
		const Param *p = &tab[i];
		printf("| `%s` | %s | %d | %s | %d | %d | %d | %d | %s | %s | %s | %s | %s |\n", p->id, p->nome, p->lato, p->pagina,
		       p->posto, p->min, p->max, p->iniziale, tipo[p->tipo], p->etichette ? p->etichette : "",
		       p->bipolare ? "si'" : "no", p->scena ? "si'" : "no", p->anche ? p->anche : "");
	}
}

static const char *(*synth_pagina)(int lato);
static int (*synth_interruttore)(const char *nome);

void controlli_synth(const char *(*pagina)(int), int (*interruttore)(const char *))
{
	synth_pagina       = pagina;
	synth_interruttore = interruttore;
}
const char *controlli_pagina(int lato) { return synth_pagina ? synth_pagina(lato) : NULL; }
int controlli_interruttore(const char *nome) { return synth_interruttore ? synth_interruttore(nome) : -1; }

/* ---- Coda delle azioni: un produttore, un consumatore ---- */

#define N_CODA    256
#define N_RISERVA 4096
static Azione        coda[N_CODA];
static SDL_AtomicInt testa, fondo; // testa la muove solo chi scrive, fondo solo chi legge
static Azione        riserva[N_RISERVA]; // del produttore: azioni che non stavano nella coda, in ordine
static int           n_ris, persi;

static int in_coda(Azione a)
{
	unsigned t = (unsigned) SDL_GetAtomicInt(&testa);
	if (t - (unsigned) SDL_GetAtomicInt(&fondo) >= N_CODA)
		return 0;
	coda[t % N_CODA] = a;
	SDL_SetAtomicInt(&testa, (int) (t + 1)); // barriera piena: l'azione e' scritta prima
	return 1;
}

void controlli_riprova(void)
{
	int i = 0;
	while (i < n_ris && in_coda(riserva[i]))
		i++;
	memmove(riserva, riserva + i, (size_t) (n_ris - i) * sizeof *riserva);
	n_ris -= i;
}

void controlli_azione(Azione a)
{
	controlli_riprova();
	if (n_ris == 0 && in_coda(a))
		return;
	if (n_ris < N_RISERVA) // il synth non le prende (audio fermo?): aspettano qui
		riserva[n_ris++] = a;
	else if (!persi++)
		fprintf(stderr, "coda delle azioni piena: azioni perse\n");
}

int controlli_in_attesa(void) { return n_ris; }

int controlli_prendi(Azione *a)
{
	unsigned f = (unsigned) SDL_GetAtomicInt(&fondo);
	if (f == (unsigned) SDL_GetAtomicInt(&testa))
		return 0;
	*a = coda[f % N_CODA];
	SDL_SetAtomicInt(&fondo, (int) (f + 1));
	return 1;
}

/* ---- Pad PS1 di ogni lato ---- */

// Bit del pad PS1 (ordine del protocollo dei pad)
enum { B_SELECT, B_L3, B_R3, B_START, B_SU, B_DESTRA, B_GIU, B_SINISTRA, B_L2, B_R2, B_L1, B_R1,
       B_TRIANGOLO, B_CERCHIO, B_CROCE, B_QUADRATO };

static SDL_AtomicInt tasti[4][2]; // per sorgente (0 = tastiera, 1-2 = gamepad, 3 = mouse) e lato: bit tenuti
static SDL_AtomicInt premuti[2];  // bit premuti dall'ultima lettura: anche un tocco brevissimo si vede
static SDL_AtomicInt leve[2];     // 4 byte (sx X, Y, dx X, Y) XOR 0x80: 0 = levette ferme
static SDL_AtomicInt lato_vis;    // controlli_lato() + 1
static SDL_AtomicInt due;         // 1 = due gamepad: ognuno ha il suo lato, niente da mostrare

static void metti(SDL_AtomicInt *a, int bit, int giu)
{
	int v = SDL_GetAtomicInt(a);
	while (!SDL_CompareAndSwapAtomicInt(a, v, giu ? v | 1 << bit : v & ~(1 << bit)))
		v = SDL_GetAtomicInt(a);
}

// Bit del pad che alzano / abbassano le 4 manopole (grammatica: Su, Destra, R1, R2 alzano)
static const uint8_t piu[4] = { B_SU, B_DESTRA, B_R1, B_R2 }, meno[4] = { B_GIU, B_SINISTRA, B_L1, B_L2 };

static void premi(int sorgente, int lato, int bit, int giu)
{
	metti(&tasti[sorgente][lato], bit, giu);
	if (giu)
		metti(&premuti[lato], bit, 1);
}

uint16_t controlli_pad(int lato, uint8_t l[4])
{
	int t = SDL_SetAtomicInt(&premuti[lato], 0); // restituisce il valore di prima
	for (int s = 0; s < 4; s++)
		t |= SDL_GetAtomicInt(&tasti[s][lato]);
	uint32_t v = (uint32_t) SDL_GetAtomicInt(&leve[lato]) ^ 0x80808080u;
	for (int i = 0; i < 4; i++)
		l[i] = (uint8_t) (v >> 8 * i);
	return (uint16_t) t;
}

int controlli_lato(void) { return SDL_GetAtomicInt(&due) ? -1 : SDL_GetAtomicInt(&lato_vis) - 1; }

static SDL_AtomicInt disp; // D_*: chi ha premuto per ultimo

// Chi suona col MIDI usa il mouse per i menu: i testi restano quelli del MIDI
static void usato(int d)
{
	if (d != D_MOUSE || SDL_GetAtomicInt(&disp) != D_MIDI)
		SDL_SetAtomicInt(&disp, d);
}
int controlli_dispositivo(void) { return SDL_GetAtomicInt(&disp); }

/* ---- Gamepad: il primo collegato = controller 1, il secondo = controller 2 ---- */

static uint32_t slot[2];     // id dei gamepad (0 = posto libero)
static uint32_t attesa[4];   // gamepad collegati oltre i primi due: prendono il primo posto che si libera
static int      slot_lato[2]; // lato che guida (un gamepad solo: il tasto Guide lo cambia)
static int      grilletto[2][2];

static int trova(uint32_t id) { return id && slot[0] == id ? 0 : id && slot[1] == id ? 1 : -1; }

static void lascia_slot(int s)
{
	SDL_SetAtomicInt(&tasti[1 + s][slot_lato[s]], 0);
	SDL_SetAtomicInt(&leve[slot_lato[s]], 0);
	grilletto[s][0] = grilletto[s][1] = 0;
}

static void occupa(int s, uint32_t id)
{
	slot[s]      = id;
	slot_lato[s] = s;
	if (slot[s ^ 1] && slot_lato[s ^ 1] != (s ^ 1)) { // ora sono due: ognuno al suo lato
		lascia_slot(s ^ 1);
		slot_lato[s ^ 1] = s ^ 1;
	}
}

void controlli_pad_collegato(uint32_t id, int si)
{
	int s = trova(id);
	if (si && s < 0) {
		s = slot[0] ? 1 : 0;
		if (!slot[s])
			occupa(s, id);
		else // un terzo gamepad non guida niente finche' un posto non si libera
			for (int i = 0; i < 4; i++)
				if (!attesa[i] || attesa[i] == id) {
					attesa[i] = id;
					break;
				}
	} else if (!si && s >= 0) {
		lascia_slot(s);
		slot[s] = 0;
		if (attesa[0]) {
			occupa(s, attesa[0]);
			memmove(attesa, attesa + 1, sizeof attesa - sizeof attesa[0]);
			attesa[3] = 0;
		}
	} else if (!si)
		for (int i = 0; i < 4; i++)
			if (attesa[i] == id) {
				memmove(attesa + i, attesa + i + 1, (size_t) (3 - i) * sizeof attesa[0]);
				attesa[3] = 0;
			}
	SDL_SetAtomicInt(&due, slot[0] && slot[1]);
}

void controlli_evento_pad(uint32_t id, int tasto, int giu)
{
	static const int8_t bit[SDL_GAMEPAD_BUTTON_COUNT] = { // bit del pad PS1 + 1 (0 = nessuno)
		[SDL_GAMEPAD_BUTTON_SOUTH] = B_CROCE + 1,           [SDL_GAMEPAD_BUTTON_EAST] = B_CERCHIO + 1,
		[SDL_GAMEPAD_BUTTON_WEST] = B_QUADRATO + 1,         [SDL_GAMEPAD_BUTTON_NORTH] = B_TRIANGOLO + 1,
		[SDL_GAMEPAD_BUTTON_BACK] = B_SELECT + 1,           [SDL_GAMEPAD_BUTTON_START] = B_START + 1,
		[SDL_GAMEPAD_BUTTON_LEFT_STICK] = B_L3 + 1,         [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = B_R3 + 1,
		[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = B_L1 + 1,      [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = B_R1 + 1,
		[SDL_GAMEPAD_BUTTON_DPAD_UP] = B_SU + 1,            [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = B_GIU + 1,
		[SDL_GAMEPAD_BUTTON_DPAD_LEFT] = B_SINISTRA + 1,    [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = B_DESTRA + 1 };
	int s = trova(id);
	if (s < 0 || tasto < 0 || tasto >= SDL_GAMEPAD_BUTTON_COUNT)
		return;
	if (giu)
		usato(D_PAD);
	if (tasto == SDL_GAMEPAD_BUTTON_GUIDE) { // un gamepad solo: tocco = passa all'altro lato
		if (giu && !slot[s ^ 1]) {
			lascia_slot(s);
			slot_lato[s] ^= 1;
			SDL_SetAtomicInt(&lato_vis, slot_lato[s] + 1);
		}
		return;
	}
	if (bit[tasto])
		premi(1 + s, slot_lato[s], bit[tasto] - 1, giu);
}

void controlli_evento_asse(uint32_t id, int asse, int v)
{
	int s = trova(id);
	if (s < 0)
		return;
	if (asse == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || asse == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) { // L2 / R2: oltre meta'
		int r = asse == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, giu = v > 16384;
		if (giu)
			usato(D_PAD);
		if (giu != grilletto[s][r])
			premi(1 + s, slot_lato[s], r ? B_R2 : B_L2, grilletto[s][r] = giu);
		return;
	}
	if (asse < SDL_GAMEPAD_AXIS_LEFTX || asse > SDL_GAMEPAD_AXIS_RIGHTY)
		return;
	// Zona morta 15%, poi lineare fino a 88 passi come sulla PS1. Il synth toglie ancora la sua zona
	// morta (quella del pad PS1): qui si parte da li', cosi' appena fuori dal 15% si muove.
	int a = v < 0 ? -v : v, zm = 32767 * 15 / 100, d = 0;
	if (a > zm) { // 41..128 dal centro, cioe' 1..88 passi dopo la zona morta del synth
		usato(D_PAD);
		d = ZONA_MORTA_PS1 + ((a - zm) * 88 + (32767 - zm) - 1) / (32767 - zm);
	}
	int b = 128 + (v < 0 ? -d : d);
	b     = b < 0 ? 0 : b > 255 ? 255 : b;
	SDL_AtomicInt *l = &leve[slot_lato[s]];
	int o = SDL_GetAtomicInt(l), sh = 8 * asse;
	while (!SDL_CompareAndSwapAtomicInt(l, o, (int) (((uint32_t) o & ~(0xffu << sh)) | (uint32_t) (b ^ 0x80) << sh)))
		o = SDL_GetAtomicInt(l);
}

/* ---- MIDI ---- */

// Regola di una sorgente MIDI (CC, nota della zona comandi, pressione): n = parametro della tabella
// (R_PARAM) o scena 0-7 (R_SCENA)
enum { R_NIENTE, R_PARAM, R_SPECIALE, R_PAGINE, R_SCENA };
typedef struct { int8_t tipo, rel; int16_t n; const char *nome; } Regola;
#define N_PAR 256        // parametri al massimo seguiti dal soft takeover
#define MS100 100000000u // 100 ms in ns

static Regola        r_cc[128], r_zona[128], r_pres;
static int           piega = 2, note_si = 1, prog_si, zona_si, zona_da = 36, zona_a = 47, canale[2]; // canale 0 = omni
static float         cc_prima[128];   // posizione fisica di prima (0..1) di ogni CC
static uint8_t       cc_visto[128], cc_giu[128], msb[32];
static int           ult_cc = -1;     // CC del messaggio di prima (14 bit: CC n seguito da CC n + 32)
static SDL_AtomicInt preso[N_PAR];    // 0 = mai toccato; 1 = toccato, non preso; 2 + valore mandato - min = preso
static uint64_t      preso_ns[N_PAR]; // quando si e' mandato l'ultimo valore
static uint8_t       preso_cc[N_PAR]; // CC che ha mandato l'ultimo valore (un altro CC lo deve prendere da se')
static int           rel_v[N_PAR];    // encoder: ultimo valore mandato - min + 1 (0 = nessuno), per i messaggi fitti
static int           pag_ult[2] = { -1, -1 };
static uint64_t      pag_ns[2], tick_ns[25];
static int           n_tick;
static SDL_AtomicInt learn, midi_si;  // learn: indice del parametro + 1 (0 = nessuno)
static const char   *mappa_file;

void controlli_midi_attivo(int si) { SDL_SetAtomicInt(&midi_si, si); }
int  controlli_midi(void) { return SDL_GetAtomicInt(&midi_si); }
void controlli_mappa_utente(const char *percorso) { mappa_file = percorso; }

const Param *controlli_learn(void)
{
	int l = SDL_GetAtomicInt(&learn);
	return l ? &tab[l - 1] : NULL;
}

// ponytail: il valore lo scrive il thread del synth; un int allineato si legge intero su tutte le
// piattaforme previste. Se servisse di piu': copia dei valori a ogni fotogramma, come per le zone.
static int valore_ora(const Param *p) { return *(volatile const int *) p->valore - p->min; }

int controlli_midi_preso(const Param *p)
{
	int i = p ? (int) (p - tab) : N_PAR, s = i < N_PAR ? SDL_GetAtomicInt(&preso[i]) : 0;
	return s == 0 ? -1 : s == 1 ? 0 : s - 2 == valore_ora(p);
}

// Nome della pagina n del lato (0/1) nell'ordine della tabella (i parametri di una pagina stanno
// insieme), NULL se non c'e'
static const char *pagina_n(int lato, int n)
{
	const char *ult = NULL;
	for (int i = 0; i < n_tab; i++)
		if (tab[i].lato == lato + 1 && (!ult || strcmp(ult, tab[i].pagina))) {
			ult = tab[i].pagina;
			if (n-- == 0)
				return ult;
		}
	return NULL;
}

// Parametro al posto 1-4 della pagina pg del lato (0/1), anche se ci sta come "anche"
static const Param *al_posto(int lato, const char *pg, int posto)
{
	char a[40];
	snprintf(a, sizeof a, "%s %d", pg ? pg : "", posto);
	for (int i = 0; pg && i < n_tab; i++)
		if (tab[i].lato == lato + 1 && ((tab[i].posto == posto && !strcmp(tab[i].pagina, pg)) || (tab[i].anche && !strcmp(tab[i].anche, a))))
			return &tab[i];
	return NULL;
}

static void manda(int tipo, int modo, int n, int passi, const char *nome, float v)
{
	Azione a = { (int8_t) tipo, 0, (int8_t) modo, (int8_t) n, (int16_t) passi, nome, v };
	controlli_azione(a);
}

// Manopola assoluta in f (0..1) sul parametro p, col soft takeover: il valore segue la manopola solo
// dopo che lei ci e' passata sopra (tra il messaggio di prima e questo) o ci sta a +-1/127
static void assoluto(int cc, const Param *p, float f, uint64_t ns)
{
	int   i = (int) (p - tab), s = SDL_GetAtomicInt(&preso[i]), d = p->max - p->min, ora = valore_ora(p);
	float pf = (float) ora / (float) d, e = f - pf;
	// preso resta preso finche' il synth mostra il valore mandato (o lo mostrera': 100 ms); se un altro
	// ingresso l'ha cambiato, la manopola lo deve prendere di nuovo. Un secondo CC sullo stesso
	// parametro lo deve prendere da se' e, finche' non lo fa, non lo toglie al primo
	int tiene = s >= 2 && (s - 2 == ora || ns - preso_ns[i] < MS100);
	if (!(tiene && preso_cc[i] == cc) && !(e * e <= 1.0f / (127 * 127) || (cc_visto[cc] && (cc_prima[cc] - pf) * e <= 0))) {
		if (!tiene)
			SDL_SetAtomicInt(&preso[i], 1);
		return;
	}
	int v = (int) (f * (float) d + 0.5f);
	SDL_SetAtomicInt(&preso[i], 2 + v);
	preso_ns[i] = ns, preso_cc[i] = (uint8_t) cc;
	manda(A_VALORE, 0, 0, 0, p->id, (float) v / (float) d);
}

// Encoder senza fine (complemento a due: 1-63 = +, 65-127 = -)
static void relativo(const Param *p, int v, uint64_t ns)
{
	int i = (int) (p - tab), d = p->max - p->min;
	int x = (rel_v[i] && ns - preso_ns[i] < MS100 ? rel_v[i] - 1 : valore_ora(p)) + (v < 64 ? v : v - 128);
	x           = x < 0 ? 0 : x > d ? d : x;
	rel_v[i]    = x + 1;
	preso_ns[i] = ns;
	manda(A_VALORE, 0, 0, 0, p->id, (float) x / (float) d);
}

// CC 20-29: 20 / 25 = pagina del lato 1 / 2 (valore x pagine / 128), 21-24 / 26-29 = sue manopole
static void cc_pagina(int cc, int v, uint64_t ns)
{
	int lato = cc >= 25, j = cc - 20 - 5 * lato;
	if (j == 0) {
		int np = 0;
		while (pagina_n(lato, np))
			np++;
		int         p   = v * np / 128;
		const char *ora = controlli_pagina(lato);
		// FISSA sulla pagina gia' fissa la chiude: si manda solo per un'altra pagina, e non due volte
		// di fila mentre la prima e' ancora in viaggio
		if (ora && strcmp(ora, pagina_n(lato, p)) && (p != pag_ult[lato] || ns - pag_ns[lato] >= MS100)) {
			Azione a = { A_PAGINA, (int8_t) lato, FISSA, (int8_t) p, 0, NULL, 0 };
			controlli_azione(a);
			pag_ult[lato] = p, pag_ns[lato] = ns;
		}
		return;
	}
	const char  *pg = pag_ult[lato] >= 0 && ns - pag_ns[lato] < MS100 ? pagina_n(lato, pag_ult[lato]) : controlli_pagina(lato);
	const Param *p  = al_posto(lato, pg, j);
	if (p && p->tipo != P_PULSANTE && p - tab < N_PAR)
		assoluto(cc, p, (float) v / 127, ns);
}

// Comando di una regola: azione speciale (giu' = acceso, su = spento) o scena (alla pressione)
static void comando(const Regola *r, int giu)
{
	if (r->tipo == R_SPECIALE)
		manda(A_SPECIALE, giu ? ACCESO : SPENTO, 0, 0, r->nome, 0);
	else if (r->tipo == R_SCENA && giu)
		manda(A_SCENA, 0, r->n, 0, NULL, 0);
}

static void salva_mappa(const int *cc, int n);

static void evento_cc(int cc, int v, uint64_t ns)
{
	int l = SDL_GetAtomicInt(&learn), prima = ult_cc;
	ult_cc = cc;
	if (l) { // learn: da adesso questo CC muove il parametro scelto (il valore non salta)
		SDL_SetAtomicInt(&learn, 0);
		r_cc[cc]     = (Regola) { R_PARAM, 0, (int16_t) (l - 1), NULL };
		cc_prima[cc] = (float) v / 127, cc_visto[cc] = 1;
		salva_mappa(&cc, 1);
		return;
	}
	const Regola *r = &r_cc[cc];
	float         f = (float) v / 127;
	if (r->tipo == R_NIENTE && cc >= 32 && cc < 64 && prima == cc - 32 && r_cc[cc - 32].tipo == R_PARAM && !r_cc[cc - 32].rel) {
		cc -= 32; // 14 bit: CC n (la parte alta) e CC n + 32 (la bassa) sono un valore solo
		r = &r_cc[cc];
		f = (float) (msb[cc] * 128 + v) / 16383;
	} else if (cc < 32)
		msb[cc] = (uint8_t) v;
	if (r->tipo == R_PARAM && r->rel)
		relativo(&tab[r->n], v, ns);
	else if (r->tipo == R_PARAM)
		assoluto(cc, &tab[r->n], f, ns);
	else if (r->tipo == R_PAGINE)
		cc_pagina(cc, v, ns);
	else if ((v >= 64) != cc_giu[cc]) // pedale: si manda solo quando cambia (soglia 64)
		comando(r, cc_giu[cc] = v >= 64);
	cc_prima[cc] = f, cc_visto[cc] = 1;
}

void controlli_evento_midi(const uint8_t *m, int n, uint64_t ns)
{
	int st = n > 0 ? m[0] & 0xf0 : 0, ch = n > 0 ? (m[0] & 15) + 1 : 0;
	if (n == 1 && m[0] >= 0xf8) { // clock: 24 tick per battito, BPM mediato sull'ultimo battito
		if (m[0] == 0xf8) {
			if (n_tick && ns - tick_ns[n_tick - 1] > 20 * MS100)
				n_tick = 0; // pausa piu' lunga di 2 s: si ricomincia a contare
			if (n_tick == 25) {
				memmove(tick_ns, tick_ns + 1, 24 * sizeof *tick_ns);
				n_tick = 24;
			}
			tick_ns[n_tick++] = ns;
			uint64_t dt = tick_ns[n_tick - 1] - tick_ns[0];
			manda(A_CLOCK, C_TICK, 0, 0, NULL, dt ? (float) (60e9 / 24 * (n_tick - 1) / (double) dt) : 0);
		} else if (m[0] == 0xfa || m[0] == 0xfb || m[0] == 0xfc)
			manda(A_CLOCK, m[0] == 0xfa ? C_START : m[0] == 0xfb ? C_CONTINUA : C_STOP, 0, 0, NULL, 0);
		return;
	}
	if (st < 0x80 || st == 0xf0 || n < (st == 0xc0 || st == 0xd0 ? 2 : 3) ||
	    (canale[0] && canale[1] && ch != canale[0] && ch != canale[1]))
		return;
	if ((st == 0x90 && m[2]) || st == 0xb0 || st == 0xd0 || st == 0xe0) // premuto: non il clock, non le note lasciate
		usato(D_MIDI);
	if (st == 0x90 || st == 0x80) {
		int nota = m[1] & 127, giu = st == 0x90 && m[2] > 0; // premuta con velocita' 0 = lasciata
		if (zona_si && nota >= zona_da && nota <= zona_a)
			comando(&r_zona[nota], giu);
		else if (note_si)
			manda(A_NOTA, 1, nota, giu ? m[2] & 127 : 0, NULL, 0);
	} else if (st == 0xb0)
		evento_cc(m[1] & 127, m[2] & 127, ns);
	else if (st == 0xc0 && prog_si && m[1] < 8)
		manda(A_SCENA, 0, m[1], 0, NULL, 0);
	else if (st == 0xd0 && r_pres.tipo == R_PARAM)
		manda(A_VALORE, 0, 0, 0, tab[r_pres.n].id, (float) (m[1] & 127) / 127);
	else if (st == 0xd0 && r_pres.tipo == R_SPECIALE) // SINK: il gesto della macro 2, fin qui
		manda(A_SPECIALE, 0, 0, 0, r_pres.nome, (float) (m[1] & 127) / 127);
	else if (st == 0xe0) {
		int v = ((m[2] & 127) << 7 | (m[1] & 127)) - 8192;
		manda(A_PIEGA, 0, 0, piega, NULL, v < -8191 ? -1.0f : (float) v / 8191);
	}
}

/* ---- File di mappa ---- */

// Nome di un'azione speciale in un posto fisso (un'azione in coda lo usa anche dopo la riga)
static const char *nome_fisso(const char *s)
{
	static char nomi[16][16];
	static int  n;
	for (int i = 0; i < n; i++)
		if (!strcmp(nomi[i], s))
			return nomi[i];
	return n < 16 && strlen(s) < 16 ? strcpy(nomi[n++], s) : NULL;
}

// Destra dell'"=": ID [relativo] | AZIONE SPECIALE | SCENA n | pagine (solo CC 20-29) | spento
static int regola(const char *d1, const char *d2, int cc, Regola *r)
{
	const Param *p = controlli_param(d1);
	int          n = 0, k = 0;
	if (p && p->tipo != P_PULSANTE && p - tab < N_PAR && (!*d2 || !strcmp(d2, "relativo")))
		*r = (Regola) { R_PARAM, (int8_t) (*d2 != 0), (int16_t) (p - tab), NULL };
	else if (!strcmp(d1, "SCENA") && sscanf(d2, "%d%n", &n, &k) == 1 && !d2[k] && n >= 1 && n <= 8)
		*r = (Regola) { R_SCENA, 0, (int16_t) (n - 1), NULL };
	else if (*d2)
		return 0;
	else if (!strcmp(d1, "spento"))
		*r = (Regola) { 0 };
	else if (!strcmp(d1, "pagine") && cc >= 20 && cc <= 29)
		*r = (Regola) { R_PAGINE, 0, 0, NULL };
	else if ((controlli_interruttore(d1) >= 0 || !strcmp(d1, "SALVA") || !strcmp(d1, "AVANTI") || !strcmp(d1, "INDIETRO")) &&
	         (r->nome = nome_fisso(d1))) // interruttori del synth e azioni secche dello standard
		r->tipo = R_SPECIALE;
	else
		return 0;
	return 1;
}

// Una riga, gia' senza commento; 0 = sbagliata
static int riga_mappa(char *s)
{
	char  sx[16], d1[32] = "", d2[32] = "", x[2];
	int   a = 0, b = 0, n = 0, k = 0;
	char *u = strchr(s, '=');
	if (u) {
		*u++  = 0;
		int w = sscanf(u, "%31s %31s %1s", d1, d2, x);
		if (w != 1 && w != 2)
			return 0; // a destra una o due parole
	}
	if (sscanf(s, " %15s %n", sx, &n) != 1)
		return !u; // riga vuota
	const char *rs  = s + n;
	int         uno = sscanf(rs, "%d %n", &a, &k) == 1 && !rs[k];
	int         due = !uno && sscanf(rs, "%d-%d %n", &a, &b, &k) == 2 && !rs[k];
	if (!strcmp(sx, "cc") && u && (uno || due)) { // "cc N = ..." o "cc N-M = ..."
		b = uno ? a : b;
		if (a < 0 || a > b || b > 127)
			return 0;
		Regola r = { 0 };
		for (int c = a; c <= b; c++)
			if (!regola(d1, d2, c, &r))
				return 0;
		for (int c = a; c <= b; c++)
			regola(d1, d2, c, &r_cc[c]);
		return 1;
	}
	if (!strcmp(sx, "zona") && u) { // "zona N = AZIONE" o "= SCENA n"
		Regola r = { 0 };
		if (!uno || a < 0 || a > 127 || !regola(d1, d2, -1, &r) || r.tipo == R_PARAM)
			return 0;
		r_zona[a] = r;
		return 1;
	}
	if (!strcmp(sx, "zona")) { // "zona N-M accesa|spenta"
		if (sscanf(rs, "%d-%d %15s %n", &a, &b, d1, &k) != 3 || rs[k] || a < 0 || a > b || b > 127 ||
		    (strcmp(d1, "accesa") && strcmp(d1, "spenta")))
			return 0;
		zona_da = a, zona_a = b, zona_si = d1[0] == 'a';
		return 1;
	}
	if (!strcmp(sx, "canale")) { // "canale omni", "canale N" (tutti e due i lati), "canale L = N" / "= omni"
		if (u ? !uno || a < 1 || a > 2 || *d2 : sscanf(rs, "%31s %1s", d1, x) != 1)
			return 0;
		int ch = !strcmp(d1, "omni") ? 0 : sscanf(d1, "%d%n", &n, &k) == 1 && !d1[k] && n >= 1 && n <= 16 ? n : -1;
		if (ch < 0)
			return 0;
		if (u)
			canale[a - 1] = ch;
		else
			canale[0] = canale[1] = ch;
		return 1;
	}
	if (!u || *rs)
		return 0;
	if (!strcmp(sx, "pressione")) {
		Regola r = { 0 };
		if (!strcmp(d1, "SINK") && !*d2) // SINK non e' un interruttore: lo si riconosce qui
			r = (Regola) { R_SPECIALE, 0, 0, "SINK" };
		else if (!regola(d1, d2, -1, &r) || (r.tipo != R_PARAM && r.tipo != R_NIENTE) || r.rel)
			return 0;
		r_pres = r;
	} else if (*d2)
		return 0;
	else if (!strcmp(sx, "piega") && sscanf(d1, "%d%n", &a, &k) == 1 && !d1[k] && a >= 1 && a <= 12)
		piega = a;
	else if (!strcmp(sx, "nota") && (!strcmp(d1, "drone") || !strcmp(d1, "note") || !strcmp(d1, "spento")))
		note_si = d1[0] != 's';
	else if (!strcmp(sx, "programma") && (!strcmp(d1, "scene") || !strcmp(d1, "spento")))
		prog_si = d1[1] == 'c';
	else
		return 0;
	return 1;
}

int controlli_mappa(const char *testo, const char *nome)
{
	int saltate = 0, nr = 0;
	if (!strncmp(testo, "\xef\xbb\xbf", 3)) // BOM UTF-8 di alcuni editor: non fa parte della prima riga
		testo += 3;
	for (const char *t = testo; *t;) {
		char   r[256];
		size_t l = strcspn(t, "\n");
		snprintf(r, sizeof r, "%.*s", (int) (l < sizeof r ? l : sizeof r - 1), t);
		t += l + (t[l] == '\n');
		nr++;
		r[strcspn(r, "#\r")] = 0;
		if (l >= sizeof r || !riga_mappa(r)) {
			fprintf(stderr, "%s:%d: regola MIDI non valida, saltata\n", nome, nr);
			saltate++;
		}
	}
	return saltate;
}

// Riscrive la mappa dell'utente: le sue righe meno quelle dei CC cambiati, piu' le regole nuove.
// File temporaneo + rinomina: un'interruzione a meta' non lascia un file mezzo scritto.
static void salva_mappa(const int *cc, int n)
{
	if (!mappa_file)
		return; // modo prova senza --mappa: il learn resta in memoria
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s.tmp", mappa_file);
	size_t        len   = 0;
	char         *prima = SDL_LoadFile(mappa_file, &len);
	SDL_IOStream *f     = SDL_IOFromFile(tmp, "w");
	int           ok    = f != NULL;
	if (ok && !prima)
		ok = SDL_IOprintf(f, "# MIDI dell'utente (MIDI learn): vince sulla mappa predefinita\n") > 0;
	for (const char *t = prima; ok && t && *t;) {
		size_t l = strcspn(t, "\n");
		int    a, k = 0, via = 0;
		if (sscanf(t, " cc %d%n", &a, &k) == 1 && (size_t) k <= l && t[k] != '-')
			for (int i = 0; i < n; i++)
				via |= a == cc[i];
		if (!via)
			ok = SDL_WriteIO(f, t, l) == l && SDL_IOprintf(f, "\n") == 1;
		t += l + (t[l] == '\n');
	}
	for (int i = 0; ok && i < n; i++)
		ok = SDL_IOprintf(f, "cc %d = %s\n", cc[i], r_cc[cc[i]].tipo == R_PARAM ? tab[r_cc[cc[i]].n].id : "spento") > 0;
	SDL_free(prima);
	if (f && !SDL_CloseIO(f))
		ok = 0;
	if (!ok || !SDL_RenamePath(tmp, mappa_file)) { // su Windows MoveFileEx sostituisce il file che c'e'
		fprintf(stderr, "non riesco a scrivere la mappa MIDI %s: %s\n", mappa_file, SDL_GetError());
		SDL_RemovePath(tmp); // la mappa vecchia resta com'era
	}
}

// CLEAR: nessun CC muove piu' il parametro i
static void togli_cc(int i)
{
	int cc[128], n = 0;
	for (int c = 0; c < 128; c++)
		if (r_cc[c].tipo == R_PARAM && r_cc[c].n == i) {
			r_cc[c] = (Regola) { 0 };
			cc[n++] = c;
		}
	if (n)
		salva_mappa(cc, n);
}

/* ---- Tastiera del PC ---- */

static int      kb_lato;                   // lato delle manopole: quello dell'ultima pagina aperta
static int      shift, ctrl;               // tenuti
static int      pag_sc, pag_n, pag_l, pag_usato; // tasto della pagina tenuta (0 = nessuno)
static uint32_t pag_ms;

static void azione(int tipo, int lato, int modo, int n, int passi, const char *nome)
{
	Azione a = { (int8_t) tipo, (int8_t) lato, (int8_t) modo, (int8_t) n, (int16_t) passi, nome, 0 };
	controlli_azione(a);
}

void controlli_evento_tasto(int sc, int giu, uint32_t ms)
{
	static const struct { uint8_t sc, posto, piu; } man[] = { // le 4 manopole: alza / abbassa
		{ SDL_SCANCODE_UP, 0, 1 },    { SDL_SCANCODE_W, 0, 1 }, { SDL_SCANCODE_DOWN, 0, 0 }, { SDL_SCANCODE_S, 0, 0 },
		{ SDL_SCANCODE_RIGHT, 1, 1 }, { SDL_SCANCODE_D, 1, 1 }, { SDL_SCANCODE_LEFT, 1, 0 }, { SDL_SCANCODE_A, 1, 0 },
		{ SDL_SCANCODE_E, 2, 1 },     { SDL_SCANCODE_Q, 2, 0 }, { SDL_SCANCODE_R, 3, 1 },    { SDL_SCANCODE_F, 3, 0 } };
	static const struct { uint8_t sc; const char *nome; } spec[] = {
		{ SDL_SCANCODE_M, "MUTE" },   { SDL_SCANCODE_P, "PLAY" },  { SDL_SCANCODE_SPACE, "FREEZE" },
		{ SDL_SCANCODE_V, "VISUAL" }, { SDL_SCANCODE_F1, "GUIDE" }, { SDL_SCANCODE_TAB, "GUIDE" },
		{ SDL_SCANCODE_H, "HOLD" },   { SDL_SCANCODE_PAGEDOWN, "AVANTI" }, { SDL_SCANCODE_PAGEUP, "INDIETRO" } };

	if (sc == SDL_SCANCODE_UNKNOWN) { // fuoco perso: niente tasti rimasti giu'
		SDL_SetAtomicInt(&tasti[0][0], 0);
		SDL_SetAtomicInt(&tasti[0][1], 0);
		shift = ctrl = 0;
		if (pag_sc)
			azione(A_PAGINA, pag_l, LASCIA, pag_n, 0, NULL);
		pag_sc = 0;
		return;
	}
	if (giu)
		usato(D_TASTIERA);
	if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
		shift = giu;
		return;
	}
	if (sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL || sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI) {
		ctrl = giu; // Cmd sul Mac vale come Ctrl
		return;
	}
	if (giu && ctrl && (sc == SDL_SCANCODE_L || sc == SDL_SCANCODE_S || sc == SDL_SCANCODE_Q)) {
		if (sc != SDL_SCANCODE_Q) // Ctrl+Q = esci: lo fa la finestra
			azione(A_SPECIALE, 0, 0, 0, 0, sc == SDL_SCANCODE_L ? "LOCK" : "SALVA");
		return;
	}
	for (size_t i = 0; i < sizeof man / sizeof man[0]; i++) {
		if (man[i].sc != sc)
			continue;
		int b = man[i].piu ? piu[man[i].posto] : meno[man[i].posto];
		if (!giu)
			premi(0, kb_lato, b, 0);
		else if (shift) // passi da 10
			azione(A_MANOPOLA, kb_lato, 0, man[i].posto, man[i].piu ? 10 : -10, NULL);
		else if (ctrl) // un passo, senza scorrimento: solo un tocco del tasto del pad
			metti(&premuti[kb_lato], b, 1);
		else
			premi(0, kb_lato, b, 1);
		pag_usato |= giu;
		return;
	}
	if (sc == SDL_SCANCODE_RETURN) { // conferma nei menu (Croce del pad)
		premi(0, kb_lato, B_CROCE, giu);
		return;
	}
	// Pagine: 1-9, 0 = riposo, - = pagina 10; con Shift il lato 2. Tocco = fissa, tenuto = temporanea
	int n = sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0 ? (sc - SDL_SCANCODE_1 + 1) % 10 : sc == SDL_SCANCODE_MINUS ? 10 : -1;
	if (n >= 0) {
		if (giu) {
			if (shift != kb_lato) {
				SDL_SetAtomicInt(&tasti[0][kb_lato], 0);
				kb_lato = shift;
			}
			SDL_SetAtomicInt(&lato_vis, kb_lato + 1);
			if (n == 0) {
				azione(A_PAGINA, kb_lato, FISSA, 0, 0, NULL);
				return;
			}
			azione(A_PAGINA, kb_lato, TIENI, n, 0, NULL);
			pag_sc = sc, pag_n = n, pag_l = kb_lato, pag_ms = ms, pag_usato = 0;
		} else if (sc == pag_sc) { // un tocco breve senza manopole in mezzo = pagina fissa
			azione(A_PAGINA, pag_l, !pag_usato && ms - pag_ms < 300 ? FISSA : LASCIA, pag_n, 0, NULL);
			pag_sc = 0;
		}
		return;
	}
	if (!giu)
		return;
	if (sc == SDL_SCANCODE_ESCAPE) {
		SDL_SetAtomicInt(&learn, 0);
		azione(A_PAGINA, -1, FISSA, 0, 0, NULL);
		return;
	}
	for (size_t i = 0; i < sizeof spec / sizeof spec[0]; i++)
		if (spec[i].sc == sc)
			azione(A_SPECIALE, 0, 0, 0, 0, spec[i].nome);
}

/* ---- Mouse ---- */

#define N_ZONE 160
static Zona          zone[3][N_ZONE]; // triplo buffer: il synth scrive in una, gli ingressi leggono un'altra
static int           zone_n[3], zone_w[3], zone_h[3], zone_scr = 0, zone_leg = 2, piccole;
static SDL_AtomicInt zone_pronte = { 1 }; // indice dell'ultima lista finita, + 4 se non ancora presa

void controlli_zone(const Zona *z, int n, int w, int h)
{
	n = n < N_ZONE ? n : N_ZONE;
	for (int i = 0; i < n; i++)
		if ((z[i].w < ZONA_MIN || z[i].h < ZONA_MIN) && !piccole++)
			fprintf(stderr, "zona piu' piccola di %dx%d: %dx%d in (%d, %d)\n", ZONA_MIN, ZONA_MIN, z[i].w, z[i].h, z[i].x, z[i].y);
	memcpy(zone[zone_scr], z, (size_t) n * sizeof *z);
	zone_n[zone_scr] = n, zone_w[zone_scr] = w, zone_h[zone_scr] = h;
	zone_scr = SDL_SetAtomicInt(&zone_pronte, zone_scr | 4) & 3; // barriera piena: la lista e' scritta prima
}

int controlli_zone_piccole(void) { return piccole; }

const Zona *zona_sotto(const Zona *z, int n, int x, int y)
{
	for (int i = n - 1; i >= 0; i--)
		if (x >= z[i].x && x < z[i].x + z[i].w && y >= z[i].y && y < z[i].y + z[i].h)
			return &z[i];
	return NULL;
}

static SDL_AtomicInt m_stato; // 0 = mai visto; se no 1 | giu' << 1 | (x + 1) << 2 | (y + 1) << 12
static SDL_AtomicInt m_menu;  // 0 = chiuso; se no 1 | x << 1 | y << 11 | indice del parametro << 21
static Zona          prem;    // zona premuta col sinistro (copia: il synth intanto ne pubblica altre)
static int           premuto, px, py, pval, pacc, ptras, pusato; // dove; valore e pixel del trascinamento
static uint32_t      pms;
static Zona          ult;     // zona dell'ultimo clic, per il doppio clic
static uint32_t      ult_ms;
static const Param  *mio_p;   // ultimo valore mandato: vale finche' le zone mostrano ancora mio_z
static int           mio_v, mio_z;
static uint32_t      mio_ms;

int controlli_mouse(int *x, int *y)
{
	int s = SDL_GetAtomicInt(&m_stato);
	*x = (s >> 2 & 1023) - 1;
	*y = (s >> 12 & 1023) - 1;
	return s ? s >> 1 & 1 : -1;
}

int controlli_menu(int *x, int *y, const Param **p)
{
	int m = SDL_GetAtomicInt(&m_menu);
	*x = m >> 1 & 1023;
	*y = m >> 11 & 1023;
	*p = m ? &tab[m >> 21] : NULL;
	return m != 0;
}

int controlli_menu_voce(int x, int y)
{
	int mx, my;
	const Param *p;
	if (!controlli_menu(&mx, &my, &p) || x < mx || x >= mx + MENU_W || y < my)
		return -1;
	return (y - my) / MENU_RIGA < MENU_N ? (y - my) / MENU_RIGA : -1;
}

// Appena il synth mostra un valore diverso (il nostro o di un altro ingresso) vale quello: cosi'
// un cambio dalla tastiera, dal MIDI o da una scena non viene annullato. 250 ms = tetto di sicurezza.
static int valore_di(const Zona *z, uint32_t ms)
{
	return z->par == mio_p && z->valore == mio_z && ms - mio_ms < 250 ? mio_v : z->valore;
}

// vz = valore che le zone mostravano quando si e' mandato (INT_MIN = nessuno: fuori da int16_t)
static void manda_valore(const Param *p, int v, int vz, uint32_t ms)
{
	v     = v < p->min ? p->min : v > p->max ? p->max : v;
	mio_p = p, mio_v = v, mio_z = vz, mio_ms = ms;
	Azione a = { A_VALORE, 0, 0, 0, 0, p->id, p->max > p->min ? (float) (v - p->min) / (float) (p->max - p->min) : 0 };
	controlli_azione(a);
}

static int stessa(const Zona *a, const Zona *b) { return a && a->tipo == b->tipo && a->x == b->x && a->y == b->y; }

int controlli_evento_mouse(int ev, int x, int y, int n, uint32_t ms)
{
	if (SDL_GetAtomicInt(&zone_pronte) & 4)
		zone_leg = SDL_SetAtomicInt(&zone_pronte, zone_leg) & 3;
	const Zona *zz = zone[zone_leg], *z;
	int nz = zone_n[zone_leg], w = zone_w[zone_leg], h = zone_h[zone_leg], r = MOUSE_NIENTE;
	int dentro = x >= 0 && y >= 0 && x < w && y < h;
	if (ev == MOUSE_GIU || ev == MOUSE_DESTRO || (ev == MOUSE_ROTELLA && n)) // il solo movimento non conta
		usato(D_MOUSE);
	if (ev == MOUSE_MUOVI && premuto && ptras) { // trascina: in su = alza; con Shift 4 pixel = un passo
		pacc += py - y;
		py = y;
		int per = shift ? 4 : 1, passi = pacc / per;
		if (passi) {
			pacc -= passi * per;
			pusato = 1; // girato: non e' piu' un clic (niente doppio clic dopo)
			if (prem.tipo == Z_MANOPOLA) {
				pval = pval + passi < prem.par->min ? prem.par->min : pval + passi > prem.par->max ? prem.par->max : pval + passi;
				manda_valore(prem.par, pval, prem.valore, ms);
			} else
				azione(A_MANOPOLA, prem.lato, 0, prem.posto, passi, NULL);
		}
	} else if (ev == MOUSE_GIU && dentro && !premuto) {
		int mx, my, v = controlli_menu_voce(x, y);
		const Param *mp;
		SDL_SetAtomicInt(&learn, 0); // un clic annulla il learn in attesa
		if (controlli_menu(&mx, &my, &mp)) { // col menu aperto il clic lo chiude e non fa altro
			if (v == MENU_DEFAULT)
				manda_valore(mp, mp->iniziale, INT_MIN, ms);
			else if (v == MENU_LEARN && controlli_midi()) // il prossimo CC mosso sara' di questo parametro
				SDL_SetAtomicInt(&learn, (int) (mp - tab) + 1);
			else if (v == MENU_CLEAR && controlli_midi())
				togli_cc((int) (mp - tab));
			SDL_SetAtomicInt(&m_menu, 0);
		} else {
			z          = zona_sotto(zz, nz, x, y);
			int doppio = stessa(z, &ult) && ms - ult_ms < 400;
			ult        = z ? *z : (Zona) { .tipo = -1 };
			ult_ms     = doppio ? ms - 1000 : ms; // tre clic non fanno due doppi clic
			premuto = 1, pms = ms, px = x, py = y, pacc = ptras = pusato = 0;
			prem = ult;
			if (!z) // clic fuori: si torna a riposo (come Esc)
				azione(A_PAGINA, -1, FISSA, 0, 0, NULL);
			else if (z->tipo == Z_PANNELLO)
				azione(A_PAGINA, z->lato, TIENI, z->n, 0, NULL);
			else if (z->tipo == Z_MANOPOLA) {
				pval = valore_di(z, ms);
				if (doppio)
					manda_valore(z->par, pval = z->par->iniziale, z->valore, ms);
				ptras = 1;
			} else if (z->tipo == Z_SCELTA) {
				int s2 = valore_di(z, ms) + (z->passi ? z->passi : 1);
				manda_valore(z->par, !z->passi && s2 > z->par->max ? z->par->min : s2, z->valore, ms);
			} else if (z->tipo == Z_PASSO) {
				if (z->passi)
					azione(A_MANOPOLA, z->lato, 0, z->posto, z->passi, NULL);
				else
					ptras = 1;
			} else if (z->tipo == Z_PAD)
				premi(3, z->lato, z->n, 1);
			else if (z->tipo == Z_PULSANTE && !z->tieni)
				azione(A_SPECIALE, 0, CAMBIA, 0, 0, z->nome);
			else if (z->tipo == Z_NOTA) {
				Azione a = { A_NOTA, 0, 0, z->n, 100, NULL, 0 };
				controlli_azione(a);
				premi(3, z->lato, z->passi > 0 ? piu[z->posto & 3] : meno[z->posto & 3], 1);
			} else if (z->tipo == Z_TESTATA && doppio)
				r = MOUSE_TUTTO_SCHERMO;
			if (ptras)
				r = MOUSE_RELATIVO; // puntatore fermo e nascosto finche' si trascina
		}
	} else if (ev == MOUSE_SU && premuto) {
		premuto = 0;
		if (pusato || ms - pms >= 300) // non era un clic (standard: lasciato entro 300 ms senza girare)
			ult_ms = ms - 1000;
		if (prem.tipo == Z_PANNELLO) // tocco breve senza girare niente = fissa
			azione(A_PAGINA, prem.lato, !pusato && ms - pms < 300 ? FISSA : LASCIA, prem.n, 0, NULL);
		else if (prem.tipo == Z_PAD)
			premi(3, prem.lato, prem.n, 0);
		else if (prem.tipo == Z_PULSANTE && prem.tieni && ms - pms >= (uint32_t) prem.tieni && stessa(zona_sotto(zz, nz, x, y), &prem))
			azione(A_SPECIALE, 0, CAMBIA, 0, 0, prem.nome);
		else if (prem.tipo == Z_NOTA) {
			Azione a = { A_NOTA, 0, 0, prem.n, 0, NULL, 0 };
			controlli_azione(a);
			premi(3, prem.lato, prem.passi > 0 ? piu[prem.posto & 3] : meno[prem.posto & 3], 0);
		}
		if (ptras) {
			r = MOUSE_ASSOLUTO;
			x = px, y = py; // il puntatore torna dove si era premuto
		}
		ptras = 0;
	} else if (ev == MOUSE_DESTRO && dentro) {
		z = zona_sotto(zz, nz, x, y);
		int i  = z && z->par && (z->tipo == Z_MANOPOLA || z->tipo == Z_SCELTA) ? (int) (z->par - tab) : -1;
		int mx = x < w - MENU_W ? x : w - MENU_W, my = y < h - MENU_N * MENU_RIGA ? y : h - MENU_N * MENU_RIGA;
		SDL_SetAtomicInt(&m_menu, i < 0 ? 0 : 1 | mx << 1 | my << 11 | i << 21);
	} else if (ev == MOUSE_ROTELLA && dentro && n && (z = zona_sotto(zz, nz, x, y))) {
		pusato = premuto;
		if (z->tipo == Z_MANOPOLA) { // 1 scatto = 1 passo, con Shift 10
			manda_valore(z->par, valore_di(z, ms) + n * (shift ? 10 : 1), z->valore, ms);
			if (premuto && ptras && prem.par == z->par) // il trascinamento continua da qui
				pval = mio_v;
		} else if (z->tipo == Z_SCELTA)
			manda_valore(z->par, valore_di(z, ms) + n, z->valore, ms);
		else if (z->tipo == Z_PASSO)
			azione(A_MANOPOLA, z->lato, 0, z->posto, n * (shift ? 10 : 1), NULL);
	}
	if (premuto && ptras) // durante il trascinamento il puntatore e' fermo dove si e' premuto
		x = px, y = py;
	dentro = x >= 0 && y >= 0 && x < w && y < h;
	if (dentro || SDL_GetAtomicInt(&m_stato)) // visto solo dopo un passaggio sul disegno
		SDL_SetAtomicInt(&m_stato, 1 | premuto << 1 | (dentro ? (x + 1) << 2 | (y + 1) << 12 : 0));
	return r;
}
