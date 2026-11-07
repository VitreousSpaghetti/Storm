/*
 * storm per PC: finestra 320x256 ingrandita a fattore intero, audio e modo prova senza finestra
 * (standard/prove/prove.md). Il synth e' il programma della PS1 (storm.c) sopra la piccola PS1 finta
 * (psx.h): la SPU suona a 44100 Hz come sulla PS1, SDL converte alla frequenza del dispositivo.
 * Il tempo lo da' l'audio: audio_calcola() fa girare la SPU e ogni 882 campioni (1/50 s) un
 * fotogramma di storm (pad, manopole, MOTION, registri della SPU, ordering table). Nella finestra
 * questo gira nel thread audio, che non alloca, non apre file e non aspetta lock: il thread
 * principale gli passa solo interi atomici e la coda delle azioni (controlli.c, prese a ogni blocco
 * audio) e riceve la lista dei comandi grafici del fotogramma, che rasterizza e presenta
 * (psx_disegna). I messaggi MIDI (midi.c) arrivano al thread principale come eventi SDL e passano
 * dal traduttore di controlli.c come tastiera, mouse e gamepad. Il modo prova chiama le stesse
 * funzioni nel thread principale, senza SDL_Init: nessuna finestra, nessun dispositivo audio; ogni
 * fotogramma si rasterizza subito, cosi' stesso seme = stesse foto.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "psx.h"
#include "controlli.h"
#include "mappa_storm.h" // mappe/storm.txt, generato da CMakeLists.txt

#define W      320
#define H      256
#define FPS    50
#define RATE   44100        // frequenza della SPU
#define PASSO  (RATE / FPS) // 882 campioni per fotogramma
#define BLOCCO 256          // campioni per chiamata di audio_calcola nel thread audio

// storm.c
void storm_avvia(void);
void storm_fotogramma(void);
void storm_azioni(void); // le azioni in coda (storm_fotogramma le prende anche lui)
void storm_seme(uint32_t s);
void storm_scene_leggi(const char *testo, const char *nome); // dopo storm_avvia, prima del suono
int  storm_scene_testo(char *t, int n); // testo del file dopo un SAVE: 0 = niente, -1 = t troppo piccolo
void storm_scene_errore(void);          // il file non si e' potuto scrivere: messaggio a schermo
void storm_scene_ultimo(void);          // a suono fermo: anche il SAVE rimandato va scritto
int  storm_passo(void);                 // passo del tutorial aperto, 0 = chiuso
// midi.c (solo nella finestra)
Uint32 midi_avvia(void); // tipo dell'evento SDL dei messaggi MIDI
int    midi_porte(void); // rilegge l'elenco delle porte (aperte e chiuse per nome); quante sono aperte
int    midi_messaggio(const SDL_Event *e, uint8_t m[3]); // byte del messaggio, quanti sono
void   midi_chiudi(void);

// ---- Audio -----------------------------------------------------------------------------------
#define G_MAX   (1 << 20)            // guadagno pieno
#define G_PASSO ((G_MAX + RATE / 20 - 1) / (RATE / 20)) // da 0 a pieno (o da pieno a 0) in 50 ms: niente click

static SDL_AtomicInt suona;   // 1 = suona, 0 = sfuma fino al silenzio (all'avvio e alla chiusura)
static int32_t       guad;    // guadagno attuale 0..G_MAX, si muove a rampa verso quello voluto
static int           al_fot;  // campioni che mancano al prossimo fotogramma di storm

// n campioni stereo (2n valori) a 44100 Hz
void audio_calcola(int16_t *buf, int n)
{
	int32_t meta = SDL_GetAtomicInt(&suona) ? G_MAX : 0;
	while (n > 0) {
		if (al_fot == 0) {
			storm_fotogramma();
			al_fot = PASSO;
		}
		storm_azioni(); // a ogni blocco: un comando vale da qui, non dal prossimo fotogramma
		int k = n < al_fot ? n : al_fot;
		psx_suona(buf, k);
		for (int i = 0; i < 2 * k; i += 2) {
			if (guad < meta)
				guad = guad + G_PASSO < meta ? guad + G_PASSO : meta;
			else if (guad > meta)
				guad = guad - G_PASSO > meta ? guad - G_PASSO : meta;
			if (guad != G_MAX) {
				buf[i]     = (int16_t) ((int64_t) buf[i] * guad >> 20);
				buf[i + 1] = (int16_t) ((int64_t) buf[i + 1] * guad >> 20);
			}
		}
		buf += 2 * k;
		n -= k;
		al_fot -= k;
	}
}

static void SDLCALL richiama_audio(void *u, SDL_AudioStream *st, int serve, int totale)
{
	static int16_t blocco[2 * BLOCCO];
	(void) u;
	(void) totale;
	for (int n = serve / 4; n > 0; n -= BLOCCO) {
		int k = n < BLOCCO ? n : BLOCCO;
		audio_calcola(blocco, k);
		SDL_PutAudioStreamData(st, blocco, k * 4);
	}
}

// ---- Modo prova ------------------------------------------------------------------------------
enum { E_NIENTE, E_FOTO, E_LIVELLO, E_PARAM, E_TASTO, E_PAD, E_ASSE, E_LEVA, E_PAGINA, E_INTER, E_MOUSE, E_MIDI, E_BPM,
       E_DISP, E_PASSO, E_FINE };
typedef struct {
	int64_t      campione; // quando, in campioni a 44100 Hz
	int          riga, tipo;
	char         op;       // '>' o '<' per il livello
	double       soglia, y; // soglia del livello; E_LEVA: X e Y (-1..1); E_BPM: BPM in soglia
	int          p, v, a;  // E_PARAM, E_INTER: valore atteso in v; E_PAGINA: lato 0/1 in p; E_TASTO: scancode, giu';
	                       // E_PAD / E_ASSE / E_LEVA: gamepad (1-2), giu', tasto o asse (sinistra 0, destra 2);
	                       // E_DISP, E_PASSO: valore atteso in v
	const Param *par;      // E_PARAM
	int          scatti;   // E_MOUSE: scatti della rotella (x, y in p, v; -1 = dove era; evento MOUSE_* in a)
	uint8_t      m[3];     // E_MIDI: il messaggio come arriva da una porta, di nm byte
	int          nm;
	char         nome[64]; // nome della foto, del parametro, della pagina o dell'interruttore
} Evento;

static Evento *ev;
static int     n_ev;
static int16_t *uscita; // tutto il suono calcolato (stereo)
static int64_t  n_usc, max_usc;

static void calcola_fino(int64_t a)
{
	if (a > max_usc) {
		max_usc = a * 2 + RATE;
		uscita  = realloc(uscita, (size_t) max_usc * 4);
		if (!uscita) {
			fprintf(stderr, "memoria finita\n");
			exit(2);
		}
	}
	while (n_usc < a) { // a blocchi come nel thread audio
		int k = a - n_usc < BLOCCO ? (int) (a - n_usc) : BLOCCO;
		audio_calcola(uscita + 2 * n_usc, k);
		n_usc += k;
		psx_disegna(); // un blocco e' piu' corto di un fotogramma: si disegnano tutti
	}
}

// RMS dell'ultimo secondo, 0..1
static double livello(void)
{
	int64_t da = n_usc > RATE ? n_usc - RATE : 0;
	double  s  = 0;
	for (int64_t i = 2 * da; i < 2 * n_usc; i++)
		s += (double) uscita[i] * uscita[i];
	return n_usc > da ? sqrt(s / (double) (2 * (n_usc - da))) / 32768.0 : 0;
}

static int nome_tasto(const char *t) // nomi per posizione della tastiera americana -> scancode (0 = nessuno)
{
	static const char *const it[][2] = { { "su", "Up" },         { "giu", "Down" },       { "giu'", "Down" },
	                                     { "sinistra", "Left" }, { "destra", "Right" },   { "esc", "Escape" },
	                                     { "spazio", "Space" },  { "invio", "Return" },   { "shift", "Left Shift" },
	                                     { "ctrl", "Left Ctrl" }, { "alt", "Left Alt" },  { "cmd", "Left GUI" },
                                     { "pagsu", "PageUp" },  { "paggiu", "PageDown" } };
	for (size_t i = 0; i < sizeof it / sizeof it[0]; i++)
		if (SDL_strcasecmp(t, it[i][0]) == 0)
			t = it[i][1];
	return SDL_GetScancodeFromName(t);
}

// Nome del tasto del pad PS1 (piu' guide) -> tasto del gamepad SDL; L2 e R2 sono assi: -1 - asse.
// -100 = sconosciuto
static int nome_pad(const char *t)
{
	static const struct { const char *n; int b; } tasti[] = {
		{ "croce", SDL_GAMEPAD_BUTTON_SOUTH },        { "cerchio", SDL_GAMEPAD_BUTTON_EAST },
		{ "quadrato", SDL_GAMEPAD_BUTTON_WEST },      { "triangolo", SDL_GAMEPAD_BUTTON_NORTH },
		{ "su", SDL_GAMEPAD_BUTTON_DPAD_UP },         { "giu", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
		{ "sinistra", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "destra", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
		{ "l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },   { "r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
		{ "l2", -1 - SDL_GAMEPAD_AXIS_LEFT_TRIGGER }, { "r2", -1 - SDL_GAMEPAD_AXIS_RIGHT_TRIGGER },
		{ "l3", SDL_GAMEPAD_BUTTON_LEFT_STICK },      { "r3", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
		{ "select", SDL_GAMEPAD_BUTTON_BACK },        { "start", SDL_GAMEPAD_BUTTON_START },
		{ "guide", SDL_GAMEPAD_BUTTON_GUIDE } };
	for (size_t i = 0; i < sizeof tasti / sizeof tasti[0]; i++)
		if (strcmp(t, tasti[i].n) == 0)
			return tasti[i].b;
	return -100;
}

// Una riga di eventi; 0 = errore. Ogni forma deve occupare tutta la riga (%n alla fine).
#define FORMA(k, fmt, ...) (n = -1, sscanf(r, fmt " %n", __VA_ARGS__) == (k) && n >= 0 && r[n] == 0)
static int leggi_evento(const char *r, const char *disp, Evento *e)
{
	int    n, a, b, c;
	double x;
	char   s1[64], s2[16];
	e->tipo = E_NIENTE;
	if (strcmp(disp, "midi") == 0) { // i byte del messaggio, come da una porta vera; "canale C" in fondo, di base 1
		char t[128], *c;
		int  ch = 1, ok;
		snprintf(t, sizeof t, "%s", r);
		if ((c = strstr(t, "canale"))) {
			n = -1;
			if (sscanf(c, "canale %d %n", &ch, &n) != 1 || n < 0 || c[n] || ch < 1 || ch > 16)
				return 0;
			*c = 0;
		}
		{
			const char *r = t; // FORMA legge r: qui la riga senza "canale"
			e->tipo = E_MIDI, e->nm = 3;
			if (FORMA(2, "cc %d %d", &a, &b, &n) || FORMA(2, "nota %d %d", &a, &b, &n))
				e->m[0] = r[0] == 'c' ? 0xb0 : 0x90, ok = a >= 0 && a < 128 && b >= 0 && b < 128;
			else if (FORMA(1, "programma %d", &a, &n) || FORMA(1, "pressione %d", &a, &n))
				e->m[0] = strncmp(r, "pro", 3) ? 0xd0 : 0xc0, e->nm = 2, b = 0, ok = a >= 0 && a < 128;
			else if (FORMA(1, "piega %d", &a, &n)) // -8192..8191 in 14 bit: prima la parte bassa
				e->m[0] = 0xe0, ok = a >= -8192 && a < 8192, b = (a + 8192) >> 7, a = (a + 8192) & 127;
			else
				return 0;
		}
		e->m[0] |= (uint8_t) (ch - 1), e->m[1] = (uint8_t) a, e->m[2] = (uint8_t) b;
		return ok;
	}
	if (strcmp(disp, "mouse") == 0) { // coordinate nel disegno; "destro X Y" = clic del tasto destro
		e->tipo = E_MOUSE;
		e->p = e->v = -1;
		if (FORMA(0, "su", &n)) {
			e->a = MOUSE_SU;
			return 1;
		}
		c    = 0;
		e->a = FORMA(2, "giu %d %d", &a, &b, &n) ? MOUSE_GIU : FORMA(2, "muovi %d %d", &a, &b, &n) ? MOUSE_MUOVI
		     : FORMA(2, "su %d %d", &a, &b, &n) ? MOUSE_SU : FORMA(2, "destro %d %d", &a, &b, &n) ? MOUSE_DESTRO
		     : FORMA(3, "rotella %d %d %d", &a, &b, &c, &n) ? MOUSE_ROTELLA : -1;
		if (e->a < 0)
			return 0;
		e->p = a, e->v = b, e->scatti = c;
		return a >= 0 && a < W && b >= 0 && b < H;
	}
	if (strcmp(disp, "tasto") == 0) {
		if (!FORMA(2, "%63s %15s", s1, s2, &n) || !(e->p = nome_tasto(s1)) || (strcmp(s2, "giu") && strcmp(s2, "su")))
			return 0;
		e->tipo = E_TASTO;
		e->v    = !strcmp(s2, "giu");
		return 1;
	}
	if (strcmp(disp, "pad1") == 0 || strcmp(disp, "pad2") == 0) {
		e->p = disp[3] - '0';
		if (FORMA(3, "leva %15s %lf %lf", s2, &e->soglia, &e->y, &n)) { // leva sx|dx X Y (-1..1, Y: -1 = su)
			e->tipo = E_LEVA;
			e->a    = !strcmp(s2, "dx") ? 2 : 0;
			return (!strcmp(s2, "sx") || !strcmp(s2, "dx")) && fabs(e->soglia) <= 1 && fabs(e->y) <= 1;
		}
		if (!FORMA(2, "%63s %15s", s1, s2, &n) || (e->a = nome_pad(s1)) == -100 || (strcmp(s2, "giu") && strcmp(s2, "su")))
			return 0;
		e->tipo = e->a < 0 ? E_ASSE : E_PAD;
		e->a    = e->a < 0 ? -1 - e->a : e->a;
		e->v    = !strcmp(s2, "giu");
		return 1;
	}
	if (strcmp(disp, "clock") == 0) { // messaggi di clock; "bpm X" fa partire i tick da soli (modo_prova)
		e->tipo = E_MIDI, e->nm = 1;
		e->m[0] = FORMA(0, "start", &n) ? 0xfa : FORMA(0, "stop", &n) ? 0xfc : FORMA(0, "continua", &n) ? 0xfb
		        : FORMA(0, "tick", &n) ? 0xf8 : 0;
		if (!e->m[0] && FORMA(1, "bpm %lf", &x, &n) && x >= 1 && x <= 999)
			e->tipo = E_BPM, e->soglia = x;
		return e->m[0] || e->tipo == E_BPM;
	}
	if (strcmp(disp, "foto") == 0) {
		if (!FORMA(1, "%63s", e->nome, &n))
			return 0;
		for (const char *p = e->nome; *p; p++)
			if (!isalnum((unsigned char) *p) && *p != '_' && *p != '-')
				return 0;
		e->tipo = E_FOTO;
		return 1;
	}
	if (strcmp(disp, "controlla") == 0) {
		if (FORMA(2, "livello %c %lf", &e->op, &e->soglia, &n) && (e->op == '>' || e->op == '<')) {
			e->tipo = E_LIVELLO;
			return 1;
		}
		if (FORMA(2, "pagina %d = %63[A-Z0-9 ]", &e->p, e->nome, &n) && (e->p == 1 || e->p == 2)) {
			for (char *f = e->nome + strlen(e->nome); f > e->nome && f[-1] == ' ';) // spazi in fondo
				*--f = 0;
			e->tipo = E_PAGINA;
			e->p--;
			return 1;
		}
		if (FORMA(1, "dispositivo = %63[A-Z]", e->nome, &n)) { // nell'ordine di D_PAD.. (controlli.h)
			static const char *const d[4] = { "PAD", "TASTIERA", "MOUSE", "MIDI" };
			for (e->v = 0; e->v < 4 && strcmp(e->nome, d[e->v]); e->v++) {}
			e->tipo = E_DISP;
			return e->v < 4;
		}
		if (FORMA(1, "passo = %d", &e->v, &n)) {
			e->tipo = E_PASSO;
			return 1;
		}
		if (FORMA(2, "%63[A-Za-z0-9_] = %d", e->nome, &e->v, &n)) {
			if ((e->par = controlli_param(e->nome))) {
				e->tipo = E_PARAM;
				return 1;
			}
			if (controlli_interruttore(e->nome) >= 0 && (e->v == 0 || e->v == 1)) {
				e->tipo = E_INTER;
				return 1;
			}
			fprintf(stderr, "parametro o interruttore sconosciuto: %s (vedi --parametri)\n", e->nome);
		}
		return 0;
	}
	if (strcmp(disp, "fine") == 0) {
		e->tipo = E_FINE;
		return *r == 0;
	}
	return 0;
}

static int leggi_file(const char *nome)
{
	FILE *f = fopen(nome, "r");
	if (!f) {
		fprintf(stderr, "non riesco ad aprire %s\n", nome);
		return 0;
	}
	char    riga[512], disp[32], vuota[2];
	int     nr = 0, max_ev = 0, ok = 1;
	int64_t prima = 0;
	while (fgets(riga, sizeof riga, f)) {
		nr++;
		char *c = strchr(riga, '#');
		if (c)
			*c = 0;
		if (sscanf(riga, " %1s", vuota) != 1)
			continue;
		if (n_ev == max_ev) {
			max_ev = max_ev * 2 + 64;
			ev     = realloc(ev, (size_t) max_ev * sizeof *ev);
			if (!ev) {
				fprintf(stderr, "memoria finita\n");
				exit(2);
			}
		}
		Evento *e = &ev[n_ev];
		memset(e, 0, sizeof *e);
		double sec;
		int    u = 0;
		if (sscanf(riga, " %lf %31s %n", &sec, disp, &u) != 2 || u == 0 || !(sec >= 0) || sec > 86400) {
			fprintf(stderr, "%s:%d: serve \"secondi dispositivo evento\"\n", nome, nr);
			ok = 0;
			continue;
		}
		e->riga     = nr;
		e->campione = llround(sec * RATE);
		if (e->campione < prima) {
			fprintf(stderr, "%s:%d: il tempo torna indietro\n", nome, nr);
			ok = 0;
			continue;
		}
		if (!leggi_evento(riga + u, disp, e)) {
			fprintf(stderr, "%s:%d: evento non valido: %s", nome, nr, riga);
			if (riga[strlen(riga) - 1] != '\n')
				fputc('\n', stderr);
			ok = 0;
			continue;
		}
		prima = e->campione;
		n_ev++;
	}
	fclose(f);
	return ok;
}

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t) v, p[1] = (uint8_t) (v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v), le16(p + 2, v >> 16); }

static int scrivi_wav(const char *nome)
{
	FILE *f = fopen(nome, "wb");
	if (!f)
		return 0;
	uint32_t dati = (uint32_t) (n_usc * 4);
	uint8_t  h[44];
	memcpy(h, "RIFF", 4);
	le32(h + 4, 36 + dati);
	memcpy(h + 8, "WAVEfmt ", 8);
	le32(h + 16, 16);
	le16(h + 20, 1); // PCM
	le16(h + 22, 2); // stereo
	le32(h + 24, RATE);
	le32(h + 28, RATE * 4);
	le16(h + 32, 4);
	le16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	le32(h + 40, dati);
	// campioni scritti come stanno in memoria: tutte le piattaforme previste sono little-endian
	int ok = fwrite(h, 1, 44, f) == 44 && fwrite(uscita, 4, (size_t) n_usc, f) == (size_t) n_usc;
	return fclose(f) == 0 && ok;
}

// ---- Scene ----------------------------------------------------------------------------------
static const char *scene_file; // NULL = le scene restano in memoria (modo prova senza --scene)

// File temporaneo + rinomina (come la mappa MIDI): mai un file a meta', e se va male resta il vecchio
static int scrivi_sicuro(const char *percorso, const char *t, size_t n)
{
	char tmp[1040];
	snprintf(tmp, sizeof tmp, "%s.tmp", percorso);
	SDL_IOStream *f  = SDL_IOFromFile(tmp, "w");
	int           ok = f && SDL_WriteIO(f, t, n) == n;
	if (f && !SDL_CloseIO(f))
		ok = 0;
	if (ok && SDL_RenamePath(tmp, percorso)) // su Windows MoveFileEx sostituisce il file che c'e'
		return 1;
	if (f) // se no l'errore da dire e' quello dell'apertura
		SDL_RemovePath(tmp);
	return 0;
}

// Dal thread principale, spesso: se storm ha fatto un SAVE, riscrive tutto il file delle scene
static void scrivi_scene(void)
{
	static char t[32768]; // 8 scene da ~1,7 KB
	int         n = storm_scene_testo(t, sizeof t);
	if (n == 0 || !scene_file)
		return;
	if (n < 0 || !scrivi_sicuro(scene_file, t, (size_t) n)) {
		fprintf(stderr, "non riesco a scrivere le scene in %s: %s\n", scene_file, n < 0 ? "testo troppo lungo" : SDL_GetError());
		storm_scene_errore();
	}
}

static int salva_foto(const char *cartella, const char *nome)
{
	char percorso[1024];
	size_t l = strlen(cartella);
	int    sep = l > 0 && cartella[l - 1] != '/' && cartella[l - 1] != '\\';
	snprintf(percorso, sizeof percorso, "%s%s%s.png", cartella, sep ? "/" : "", nome);
	SDL_Surface *s = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_RGB24, psx_schermo, W * 3);
	int          ok = s && SDL_SavePNG(s, percorso);
	if (!ok)
		fprintf(stderr, "non riesco a salvare %s: %s\n", percorso, SDL_GetError());
	SDL_DestroySurface(s);
	return ok;
}

static int modo_prova(const char *file, const char *wav, const char *foto)
{
	if (!leggi_file(file))
		return 2;
	int    fallite = 0, errore = 0, pad2 = 0, mx = -1, my = -1;
	double tick = 0, t_tick = 0; // "clock bpm": campioni tra due tick (0 = fermo) e istante del prossimo
	SDL_SetAtomicInt(&suona, 1);
	controlli_midi_attivo(1);      // LEARN e CLEAR si usano come con una porta aperta
	controlli_pad_collegato(1, 1); // pad1 = il primo gamepad; pad2 si collega al suo primo evento
	for (int e = 0; e < n_ev; e++) {
		Evento *x = &ev[e];
		for (; tick && llround(t_tick) <= x->campione; t_tick += tick) { // a campioni esatti, mai dall'orologio
			static const uint8_t f8 = 0xf8;
			int64_t              c  = llround(t_tick);
			calcola_fino(c);
			controlli_evento_midi(&f8, 1, (uint64_t) c * 1000000000u / RATE);
		}
		calcola_fino(x->campione); // un'azione vale da questo campione; pad e levette dal prossimo fotogramma
		scrivi_scene();
		if (x->tipo >= E_PAD && x->tipo <= E_LEVA && x->p == 2 && !pad2)
			controlli_pad_collegato(2, pad2 = 1);
		if (x->tipo == E_FINE)
			break;
		else if (x->tipo == E_TASTO) // stessi traduttori della tastiera e dei gamepad veri
			controlli_evento_tasto(x->p, x->v, (uint32_t) (x->campione * 1000 / RATE));
		else if (x->tipo == E_PAD)
			controlli_evento_pad((uint32_t) x->p, x->a, x->v);
		else if (x->tipo == E_MOUSE) { // stesso traduttore del mouse vero; "su X Y" e la rotella prima si spostano
			uint32_t ms = (uint32_t) (x->campione * 1000 / RATE);
			if (x->p >= 0)
				mx = x->p, my = x->v;
			if (x->a == MOUSE_SU || x->a == MOUSE_ROTELLA || x->a == MOUSE_DESTRO)
				controlli_evento_mouse(MOUSE_MUOVI, mx, my, 0, ms);
			controlli_evento_mouse(x->a, mx, my, x->scatti, ms);
		}
		else if (x->tipo == E_MIDI) { // stesso traduttore dei messaggi delle porte vere
			controlli_evento_midi(x->m, x->nm, (uint64_t) x->campione * 1000000000u / RATE);
			if (x->m[0] == 0xfc)
				tick = 0; // clock stop: anche il generatore si ferma
		} else if (x->tipo == E_BPM) // 24 tick per battito, da adesso
			tick = RATE * 60.0 / 24 / x->soglia, t_tick = (double) x->campione;
		else if (x->tipo == E_ASSE)
			controlli_evento_asse((uint32_t) x->p, x->a, x->v ? 32767 : 0);
		else if (x->tipo == E_LEVA) {
			controlli_evento_asse((uint32_t) x->p, x->a, (int) lround(x->soglia * 32767));
			controlli_evento_asse((uint32_t) x->p, x->a + 1, (int) lround(x->y * 32767));
		} else if (x->tipo == E_FOTO) {
			if (!foto)
				fprintf(stderr, "riga %d: foto %s saltata (manca --foto)\n", x->riga, x->nome);
			else if (!salva_foto(foto, x->nome))
				errore = 1;
		} else if (x->tipo == E_LIVELLO) {
			double l  = livello();
			int    ok = x->op == '>' ? l > x->soglia : l < x->soglia;
			printf("riga %d: livello %.4f %c %g: %s\n", x->riga, l, x->op, x->soglia, ok ? "ok" : "FALLITO");
			fallite += !ok;
		} else if (x->tipo == E_PARAM || x->tipo == E_INTER) {
			int v  = x->tipo == E_PARAM ? *x->par->valore : controlli_interruttore(x->nome);
			int ok = v == x->v;
			printf("riga %d: %s = %d (atteso %d): %s\n", x->riga, x->nome, v, x->v, ok ? "ok" : "FALLITO");
			fallite += !ok;
		} else if (x->tipo == E_DISP || x->tipo == E_PASSO) {
			int v  = x->tipo == E_DISP ? controlli_dispositivo() : storm_passo();
			int ok = v == x->v;
			printf("riga %d: %s = %d (atteso %d): %s\n", x->riga, x->tipo == E_DISP ? "dispositivo" : "passo", v, x->v, ok ? "ok" : "FALLITO");
			fallite += !ok;
		} else if (x->tipo == E_PAGINA) {
			const char *pg = controlli_pagina(x->p);
			int         ok = pg && strcmp(pg, x->nome) == 0;
			printf("riga %d: pagina %d = %s (attesa %s): %s\n", x->riga, x->p + 1, pg ? pg : "?", x->nome, ok ? "ok" : "FALLITO");
			fallite += !ok;
		}
		if (controlli_in_attesa()) { // il synth prende le azioni a ogni blocco: non si aspetta
			fprintf(stderr, "riga %d: troppe azioni nello stesso istante (coda delle azioni piena)\n", x->riga);
			return 2;
		}
	}
	if (controlli_zone_piccole()) { // standard/mouse: niente zone sotto 12x12 pixel del disegno
		printf("zone del mouse piu' piccole di 12x12: FALLITO\n");
		fallite++;
	}
	SDL_SetAtomicInt(&suona, 0); // sfuma prima di chiudere il WAV: niente click alla fine
	calcola_fino(n_usc + RATE / 20 + 1);
	scrivi_scene(); // un SAVE dell'ultimo istante
	storm_scene_ultimo();
	scrivi_scene();
	if (wav && !scrivi_wav(wav)) {
		fprintf(stderr, "non riesco a scrivere %s\n", wav);
		errore = 1;
	}
	return errore ? 2 : fallite ? 1 : 0;
}

// ---- Finestra --------------------------------------------------------------------------------
static int modo_finestra(void)
{
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256"); // buffer piccolo: poca latenza
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 2;
	}
	SDL_Window   *fin;
	SDL_Renderer *ren;
	if (!SDL_CreateWindowAndRenderer("storm", W * 3, H * 3, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &fin, &ren)) {
		fprintf(stderr, "finestra: %s\n", SDL_GetError());
		SDL_Quit();
		return 2;
	}
	SDL_SetRenderLogicalPresentation(ren, W, H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
	SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

	SDL_SetAtomicInt(&suona, 1);
	SDL_AudioSpec    spec = { SDL_AUDIO_S16, 2, RATE };
	SDL_AudioStream *st   = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, richiama_audio, NULL);
	if (st)
		SDL_ResumeAudioStreamDevice(st);
	else
		fprintf(stderr, "audio: %s\n", SDL_GetError());

	Uint32 ev_midi  = midi_avvia();
	Uint64 prossimo = SDL_GetTicksNS(), porte = 0;
	int    vai      = 1;
	int    rel      = 0; // trascinamento col mouse: puntatore fermo e nascosto (movimento relativo)
	float  rx = 0, ry = 0, wx = 0, wy = 0, rot = 0; // punto virtuale nel disegno; dove tornare; resto della rotella
	while (vai) {
		scrivi_scene();
		Uint64 ora = SDL_GetTicksNS();
		if (ora >= porte) { // porte MIDI collegate o staccate a programma avviato
			controlli_midi_attivo(midi_porte() > 0);
			porte = ora + 2 * SDL_NS_PER_SECOND;
		}
		if (ora < prossimo) // fino al prossimo fotogramma; un evento (anche un messaggio MIDI) sveglia subito
			SDL_WaitEventTimeout(NULL, (Sint32) ((prossimo - ora + 999999) / 1000000));
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			int giu = e.type == SDL_EVENT_KEY_DOWN;
			if (e.type == SDL_EVENT_QUIT)
				vai = 0;
			else if ((giu || e.type == SDL_EVENT_KEY_UP) && !e.key.repeat) { // il tasto tenuto lo fa scorrere storm
				if (giu && e.key.scancode == SDL_SCANCODE_F11)
					SDL_SetWindowFullscreen(fin, !(SDL_GetWindowFlags(fin) & SDL_WINDOW_FULLSCREEN));
				else if (giu && e.key.scancode == SDL_SCANCODE_Q && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)))
					vai = 0; // Ctrl+Q / Cmd+Q: le scene sono gia' scritte a ogni SAVE, si esce subito
				else
					controlli_evento_tasto(e.key.scancode, giu, (uint32_t) (e.key.timestamp / 1000000));
			} else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
				controlli_evento_tasto(SDL_SCANCODE_UNKNOWN, 0, 0);
				controlli_evento_mouse(MOUSE_SU, -1, -1, 0, (uint32_t) SDL_GetTicks());
				if (rel)
					SDL_SetWindowRelativeMouseMode(fin, false);
				rel = 0;
			} else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && !rel && !(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
				controlli_evento_mouse(MOUSE_MUOVI, -1, -1, 0, (uint32_t) SDL_GetTicks()); // col tasto giu' si trascina ancora
			else if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
			         e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_WHEEL) {
				// mouse: coordinate (e spostamenti) convertite nei pixel del disegno; bande nere = fuori
				float fx = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
				float fy = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
				SDL_ConvertEventToRenderCoordinates(ren, &e);
				uint32_t ms = (uint32_t) (e.common.timestamp / 1000000);
				int r = MOUSE_NIENTE;
				if (e.type == SDL_EVENT_MOUSE_MOTION) {
					if (rel)
						rx += e.motion.xrel, ry += e.motion.yrel;
					else
						rx = e.motion.x, ry = e.motion.y;
					r = controlli_evento_mouse(MOUSE_MUOVI, (int) SDL_floorf(rx), (int) SDL_floorf(ry), 0, ms);
				} else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
					rot += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
					int n = (int) rot; // scatti interi (un trackpad ne manda pezzi)
					rot -= (float) n;
					if (n)
						r = controlli_evento_mouse(MOUSE_ROTELLA, (int) SDL_floorf(e.wheel.mouse_x), (int) SDL_floorf(e.wheel.mouse_y), n, ms);
				} else if (e.button.button == SDL_BUTTON_LEFT || (e.button.button == SDL_BUTTON_RIGHT && e.button.down)) {
					if (!rel)
						rx = e.button.x, ry = e.button.y, wx = fx, wy = fy;
					int ev = e.button.button == SDL_BUTTON_RIGHT ? MOUSE_DESTRO : e.button.down ? MOUSE_GIU : MOUSE_SU;
					r = controlli_evento_mouse(ev, (int) SDL_floorf(rx), (int) SDL_floorf(ry), 0, ms);
				}
				if (r == MOUSE_RELATIVO && !rel)
					rel = SDL_SetWindowRelativeMouseMode(fin, true);
				else if (r == MOUSE_ASSOLUTO && rel) { // il puntatore ricompare dove si era premuto
					SDL_SetWindowRelativeMouseMode(fin, false);
					SDL_WarpMouseInWindow(fin, wx, wy);
					rel = 0;
				} else if (r == MOUSE_TUTTO_SCHERMO)
					SDL_SetWindowFullscreen(fin, !(SDL_GetWindowFlags(fin) & SDL_WINDOW_FULLSCREEN));
			}
			else if (e.type == SDL_EVENT_GAMEPAD_ADDED) { // arriva anche per quelli gia' collegati all'avvio
				if (SDL_OpenGamepad(e.gdevice.which))
					controlli_pad_collegato(e.gdevice.which, 1);
			} else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
				controlli_pad_collegato(e.gdevice.which, 0);
				SDL_CloseGamepad(SDL_GetGamepadFromID(e.gdevice.which));
			} else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
				controlli_evento_pad(e.gbutton.which, e.gbutton.button, e.gbutton.down);
			else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
				controlli_evento_asse(e.gaxis.which, e.gaxis.axis, e.gaxis.value);
			else if (e.type == ev_midi) {
				uint8_t m[3];
				int     n = midi_messaggio(&e, m);
				controlli_evento_midi(m, n, e.common.timestamp);
			}
		}
		controlli_riprova(); // azioni rimaste fuori da una coda piena
		if (SDL_GetTicksNS() < prossimo)
			continue; // svegliato da un evento: il disegno aspetta il suo fotogramma
		if (psx_disegna()) // l'ultimo fotogramma finito dal thread audio
			SDL_UpdateTexture(tex, NULL, psx_schermo, W * 3);
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		SDL_RenderTexture(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		// ritmo di 50 Hz; dopo una pausa lunga (finestra trascinata) si riparte senza rincorrere
		prossimo += SDL_NS_PER_SECOND / FPS;
		ora = SDL_GetTicksNS();
		if (ora > prossimo && ora - prossimo > SDL_NS_PER_SECOND / 10)
			prossimo = ora;
	}
	midi_chiudi();
	SDL_SetAtomicInt(&suona, 0); // sfuma (50 ms) prima di chiudere l'audio
	SDL_Delay(80);
	SDL_DestroyAudioStream(st);
	scrivi_scene(); // un SAVE appena prima di Ctrl+Q non si perde, neanche quello rimandato
	storm_scene_ultimo();
	scrivi_scene();
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(fin);
	SDL_Quit();
	return 0;
}

// Mappa MIDI: la predefinita, poi quella dell'utente (cartella dei dati) o --mappa, che vince. Il
// learn scrive in --mappa o in quella dell'utente; il modo prova non tocca la cartella dei dati.
static void leggi_mappe(const char *mappa, int finestra)
{
	static char utente[1024];
	controlli_mappa(mappa_storm, "mappe/storm.txt");
	if (!mappa && finestra) { // %APPDATA%\storm\, ~/Library/Application Support/storm/, ~/.local/share/storm/
		char *d = SDL_GetPrefPath("", "storm");
		if (d)
			snprintf(utente, sizeof utente, "%smappa.txt", d);
		SDL_free(d);
		mappa = utente[0] ? utente : NULL;
	}
	char *t = mappa ? SDL_LoadFile(mappa, NULL) : NULL; // se non c'e' ancora va bene: la scrive il learn
	if (t)
		controlli_mappa(t, mappa);
	SDL_free(t);
	controlli_mappa_utente(mappa);
}

// Scene: --scene, o nella finestra quelle dell'utente (cartella dei dati); il modo prova senza --scene
// le tiene in memoria. Un file che non c'e' = posti vuoti.
static void leggi_scene(const char *scene, int finestra)
{
	static char utente[1024];
	if (!scene && finestra) {
		char *d = SDL_GetPrefPath("", "storm");
		if (d)
			snprintf(utente, sizeof utente, "%sscene.txt", d);
		SDL_free(d);
		scene = utente[0] ? utente : NULL;
	}
	scene_file = scene;
	char *t    = scene ? SDL_LoadFile(scene, NULL) : NULL;
	if (t)
		storm_scene_leggi(t, scene);
	SDL_free(t);
}

static int uso(void)
{
	fprintf(stderr, "uso: storm [--prova FILE] [--wav FILE] [--foto CARTELLA] [--seme N] [--parametri] "
	                "[--mappa FILE] [--scene FILE]\n");
	return 2;
}

int main(int argc, char **argv)
{
	const char *prova = NULL, *wav = NULL, *foto = NULL, *mappa = NULL, *scene = NULL;
	int         seme = 0;
	uint32_t    sv   = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (strcmp(a, "--parametri") == 0) {
			storm_avvia();
			controlli_stampa_parametri();
			return 0;
		}
		if (!v)
			return uso();
		i++;
		if (strcmp(a, "--prova") == 0)
			prova = v;
		else if (strcmp(a, "--wav") == 0)
			wav = v;
		else if (strcmp(a, "--foto") == 0)
			foto = v;
		else if (strcmp(a, "--mappa") == 0)
			mappa = v; // al posto di quella dell'utente
		else if (strcmp(a, "--scene") == 0)
			scene = v; // anche questo
		else if (strcmp(a, "--seme") == 0) {
			char *f;
			unsigned long s = strtoul(v, &f, 10);
			if (*f || f == v || *v == '-')
				return uso();
			seme = 1;
			sv   = (uint32_t) s * 2654435761u ^ 0x9e3779b9u;
		} else
			return uso();
	}
	if (seme) // senza --seme il caso parte come sulla PS1
		storm_seme(sv);
	storm_avvia(); // come la PS1 all'accensione: SPU, onde, voci, riverbero
	leggi_mappe(mappa, !prova);
	leggi_scene(scene, !prova);
	return prova ? modo_prova(prova, wav, foto) : modo_finestra();
}
