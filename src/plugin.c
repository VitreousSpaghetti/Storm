/*
 * storm come plugin CLAP (e VST3, fatto da clap-wrapper sopra questo stesso file): lo stesso synth
 * di storm.c sopra la PS1 finta, suonato dalla DAW, con la finestra dentro la DAW (finestra.h).
 * Note e CC arrivano come messaggi MIDI e passano dal traduttore di controlli.c con la mappa
 * predefinita, come da una tastiera MIDI vera; il tempo della DAW diventa clock MIDI; i parametri
 * della tabella sono i parametri della DAW (automazione), e i cambi fatti col mouse o dal MIDI le
 * tornano indietro come eventi. Tutto gira nel thread audio della DAW, che qui e' l'unico produttore
 * della coda delle azioni: il mouse (thread principale) gli arriva con una coda propria, il progetto
 * da caricare con un intero atomico.
 * La SPU suona a 44100 Hz come sulla PS1; a un'altra frequenza della DAW converte SDL.
 * Lo stato di storm e' globale: per avere piu' istanze, dalla seconda in poi si carica una copia di
 * questo stesso file (cartella dei dati, "istanze"), che ha le sue globali. Due livelli: "dentro"
 * (fabbrica, istanza, estensioni; storm_interno la da' alle copie) e "fuori" (clap_entry: sceglie
 * modulo o copia e la scarica dopo destroy).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <clap/clap.h>
#include "psx.h"
#include "controlli.h"
#include "finestra.h"
#include "mappa_storm.h" // mappe/storm.txt, generato da CMakeLists.txt

#define RATE    44100       // frequenza della SPU
#define PASSO   (RATE / 50) // 882 campioni per fotogramma
#define BLOCCO  64          // campioni a 44100 Hz per giro della conversione
#define N_PAR   256         // limite della tabella dei parametri qui dentro
#define N_STATO 1160        // storm_stato, versione 1
#define N_TICK  512         // tick del clock della DAW in un blocco
#define N_TOPO  256         // eventi del mouse in attesa del thread audio
#define G_PASSO (((1 << 20) + RATE / 20 - 1) / (RATE / 20)) // guadagno da 0 a pieno in 50 ms

// storm.c
void storm_avvia(void);
void storm_fotogramma(void);
void storm_azioni(void);
void storm_scene_leggi(const char *testo, const char *nome);
int  storm_stato(uint8_t *b, int n);
void storm_stato_carica(const uint8_t *b, int n);
void storm_plugin(void);
int  storm_scene_testo(char *t, int n);
void storm_scene_errore(void);
int  storm_scene_chiesta(void);
void storm_scene_importa(const char *t, const char *nome);

// ---- Motore ----------------------------------------------------------------------------------
static int              avviato, occupato; // storm avviato una volta; un'istanza che suona
static int              al_fot;            // campioni che mancano al prossimo fotogramma di storm
static int32_t          guad;              // 0..1<<20: si sale in 50 ms all'inizio, niente click
static SDL_AudioStream *conv;              // 44100 -> frequenza della DAW (NULL = uguale)
static double           rate_daw;
static uint64_t         t_daw;             // campioni della DAW prima del blocco: tempo dei messaggi MIDI

static int          n_par;                 // parametri dati alla DAW (i pulsanti no)
static const Param *par[N_PAR];
static int          mandato[N_PAR];        // ultimo valore che la DAW conosce
static uint8_t      gesto[N_PAR];          // la DAW sa che il mouse lo sta muovendo

// Fotografia dello stato, fatta dal thread audio a ogni fotogramma e letta da save (thread
// principale): stato_seq dispari = si sta scrivendo
static uint8_t       stato_foto[N_STATO];
static SDL_AtomicInt stato_seq;

// Progetto da caricare: lo scrive il thread principale, lo applica il thread audio.
// carica_stato: 0 = libero, 1 = pronto, 2 = il thread audio lo sta leggendo, 3 = in scrittura
static int           carica_tipo; // 1 = valori dei parametri (formato di prima), 2 = stato completo,
                                  // 3 = scene da scene.txt (IMPORT)
static int           carica[N_PAR];
static uint8_t       carica_b[N_STATO];
static char          carica_mappa[128 * 48]; // tipo 2: le regole MIDI del learn salvate nel progetto
static char          carica_txt[32768];      // tipo 3: il file delle scene
static int           carica_txt_si;          // tipo 3: 0 = il file non c'e'
static SDL_AtomicInt carica_stato;

// File dell'utente (cartella dei dati, come il programma) e CC cambiati col learn in questo progetto
static char    mappa_utente[1024], scene_utente[1024];
static uint8_t cc_progetto[128];
static int     imp_attesa; // IMPORT chiesto, il posto per passarlo al thread audio era occupato

// Mouse della finestra: thread principale -> thread audio
typedef struct { int ev, x, y, n; uint32_t ms; } Topo;
static Topo          topo[N_TOPO];
static SDL_AtomicInt topo_t, topo_f;
static int           topo_giu; // tasto sinistro giu' (thread audio): i cambi sono un gesto

// Tempo della DAW
static int usa_tempo;          // la DAW da' il tempo: il clock MIDI della traccia non conta
static int daw_suona, daw_aspetta; // in play; aspetta il prossimo battito per lo Start

static void fotografa(void)
{
	SDL_AddAtomicInt(&stato_seq, 1);
	storm_stato(stato_foto, N_STATO);
	SDL_AddAtomicInt(&stato_seq, 1);
}

static void avvia(void)
{
	if (avviato++)
		return;
	storm_avvia();
	controlli_mappa(mappa_storm, "mappe/storm.txt");
	// la mappa e le scene dell'utente, come nella finestra; il learn non scrive (niente MIDI attivo)
	char *d = SDL_GetPrefPath("", "storm");
	if (d) {
		char *t;
		snprintf(mappa_utente, sizeof mappa_utente, "%smappa.txt", d);
		if ((t = SDL_LoadFile(mappa_utente, NULL)))
			controlli_mappa(t, mappa_utente);
		SDL_free(t);
		snprintf(scene_utente, sizeof scene_utente, "%sscene.txt", d);
		if ((t = SDL_LoadFile(scene_utente, NULL)))
			storm_scene_leggi(t, scene_utente);
		SDL_free(t);
	}
	SDL_free(d);
	// MIDI learn e CLEAR dal menu del tasto destro: la mappa la scrive il thread principale (lavori)
	controlli_mappa_utente(mappa_utente[0] ? mappa_utente : NULL);
	controlli_mappa_rimanda();
	controlli_midi_attivo(1);
	storm_plugin();
	int          n;
	const Param *p = controlli_parametri(&n);
	for (int i = 0; i < n && n_par < N_PAR; i++)
		if (p[i].tipo != P_PULSANTE)
			par[n_par++] = &p[i];
	fotografa(); // il suono non e' ancora partito
}

// n campioni stereo a 44100 Hz, come audio_calcola della finestra
static void calcola(int16_t *buf, int n)
{
	while (n > 0) {
		if (al_fot == 0) {
			storm_fotogramma();
			fotografa();
			al_fot = PASSO;
		}
		storm_azioni();
		int k = n < al_fot ? n : al_fot;
		psx_suona(buf, k);
		for (int i = 0; i < 2 * k && guad < 1 << 20; i += 2) { // stessa rampa della finestra: 50 ms
			guad = guad + G_PASSO < 1 << 20 ? guad + G_PASSO : 1 << 20;
			buf[i]     = (int16_t) ((int64_t) buf[i] * guad >> 20);
			buf[i + 1] = (int16_t) ((int64_t) buf[i + 1] * guad >> 20);
		}
		buf += 2 * k;
		n -= k;
		al_fot -= k;
	}
}

// n campioni della DAW in sx e dx (float)
static void suona(float *sx, float *dx, int n)
{
	static int16_t b[2 * BLOCCO];
	static float   f[2 * BLOCCO];
	while (n > 0) {
		int k = n < BLOCCO ? n : BLOCCO;
		if (!conv) {
			calcola(b, k);
			for (int i = 0; i < k; i++)
				sx[i] = b[2 * i] / 32768.0f, dx[i] = b[2 * i + 1] / 32768.0f;
		} else {
			while (SDL_GetAudioStreamAvailable(conv) < k * 8) {
				calcola(b, BLOCCO);
				SDL_PutAudioStreamData(conv, b, sizeof b);
			}
			SDL_GetAudioStreamData(conv, f, k * 8);
			for (int i = 0; i < k; i++)
				sx[i] = f[2 * i], dx[i] = f[2 * i + 1];
		}
		sx += k, dx += k, n -= k;
	}
}

static int intero(const Param *p, double v)
{
	int i = (int) (v + (v < 0 ? -0.5 : 0.5));
	return i < p->min ? p->min : i > p->max ? p->max : i;
}

static void valore(int i, double v)
{
	const Param *p = par[i];
	Azione       a = { A_VALORE, 0, 0, 0, 0, p->id, p->max > p->min ? (float) ((v - p->min) / (p->max - p->min)) : 0 };
	controlli_azione(a);
}

static void midi(const uint8_t *m, int n, uint32_t t)
{
	uint64_t ns = rate_daw > 0 ? (uint64_t) ((double) (t_daw + t) * 1e9 / rate_daw) : 0;
	controlli_evento_midi(m, n, ns);
}

// Un evento della DAW; dal thread audio (o da flush a plugin fermo: nessun altro produttore)
static void evento(const clap_event_header_t *h)
{
	if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
		return;
	if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
		const clap_event_note_t *e = (const clap_event_note_t *) h;
		int vel = h->type == CLAP_EVENT_NOTE_OFF ? 0 : (int) (e->velocity * 127 + 0.5);
		uint8_t m[3] = { (uint8_t) (0x90 | (e->channel & 15)), (uint8_t) (e->key & 127),
		                 (uint8_t) (vel < 1 && h->type == CLAP_EVENT_NOTE_ON ? 1 : vel > 127 ? 127 : vel) };
		if (e->key >= 0)
			midi(m, 3, h->time);
	} else if (h->type == CLAP_EVENT_MIDI) {
		const clap_event_midi_t *e = (const clap_event_midi_t *) h;
		int st = e->data[0];
		// lunghezza dal tipo del messaggio; SysEx e messaggi di sistema a 2-3 byte non servono
		int n = st >= 0xf8 ? 1 : (st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0 ? 2 : st >= 0x80 && st < 0xf0 ? 3 : 0;
		if (usa_tempo && st >= 0xf8 && st <= 0xfc)
			n = 0; // clock della traccia: vince il tempo della DAW
		if (n)
			midi(e->data, n, h->time);
	} else if (h->type == CLAP_EVENT_PARAM_VALUE) {
		const clap_event_param_value_t *e = (const clap_event_param_value_t *) h;
		if (e->param_id < (clap_id) n_par) {
			valore((int) e->param_id, e->value);
			mandato[e->param_id] = intero(par[e->param_id], e->value); // la DAW lo sa gia': niente eco
		}
	}
}

static void eventi(const clap_input_events_t *in)
{
	uint32_t n = in ? in->size(in) : 0;
	for (uint32_t i = 0; i < n; i++)
		evento(in->get(in, i));
}

// Tempo della DAW -> clock MIDI: 24 tick per battito sulla linea dei battiti della DAW. Start (o
// Continue fuori dall'inizio) al primo battito intero dopo il play, cosi' storm parte a tempo.
typedef struct { uint32_t t; uint8_t b; } Tick;
static int tempo_daw(const clap_event_transport_t *tr, uint32_t n, Tick *tk)
{
	usa_tempo = tr && (tr->flags & CLAP_TRANSPORT_HAS_TEMPO) && (tr->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) && tr->tempo > 0;
	if (!usa_tempo)
		return 0;
	int m = 0;
	if (!(tr->flags & CLAP_TRANSPORT_IS_PLAYING)) {
		if (daw_suona)
			tk[m++] = (Tick) { 0, 0xfc };
		daw_suona = 0;
		return m;
	}
	if (!daw_suona)
		daw_suona = daw_aspetta = 1;
	double b0  = (double) tr->song_pos_beats / CLAP_BEATTIME_FACTOR;
	double spb = rate_daw * 60.0 / tr->tempo; // campioni della DAW per battito
	for (int64_t q = (int64_t) SDL_ceil(b0 * 24 - 1e-9); m < N_TICK - 1; q++) {
		double t = (q / 24.0 - b0) * spb;
		if (t >= n)
			break;
		uint32_t ti = t > 0 ? (uint32_t) t : 0;
		if (daw_aspetta) {
			if (q % 24)
				continue;
			tk[m++]     = (Tick) { ti, q == 0 ? 0xfa : 0xfb };
			daw_aspetta = 0;
		}
		tk[m++] = (Tick) { ti, 0xf8 };
	}
	return m;
}

// I valori cambiati da storm (mouse, MIDI, scene) tornano alla DAW; col mouse giu' come un gesto
static void manda_cambi(const clap_output_events_t *out)
{
	if (!out)
		return;
	for (int i = 0; i < n_par; i++) {
		int v = *(volatile const int *) par[i]->valore;
		if (v == mandato[i])
			continue;
		if (topo_giu && !gesto[i]) {
			clap_event_param_gesture_t g = { { sizeof g, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_BEGIN, 0 }, (clap_id) i };
			out->try_push(out, &g.header);
			gesto[i] = 1;
		}
		clap_event_param_value_t e = { { sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 }, (clap_id) i, NULL, -1, -1, -1, -1, v };
		if (out->try_push(out, &e.header))
			mandato[i] = v;
	}
	for (int i = 0; i < n_par && !topo_giu; i++)
		if (gesto[i]) {
			clap_event_param_gesture_t g = { { sizeof g, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_END, 0 }, (clap_id) i };
			out->try_push(out, &g.header);
			gesto[i] = 0;
		}
}

static void topo_metti(Topo e) // thread principale
{
	int t = SDL_GetAtomicInt(&topo_t);
	if (t - SDL_GetAtomicInt(&topo_f) >= N_TOPO)
		return; // la DAW non suona: il mouse aspetta, l'eccesso si perde
	topo[t % N_TOPO] = e;
	SDL_SetAtomicInt(&topo_t, t + 1);
}

static void topo_prendi(void) // thread audio
{
	for (int f = SDL_GetAtomicInt(&topo_f); f != SDL_GetAtomicInt(&topo_t); SDL_SetAtomicInt(&topo_f, ++f)) {
		Topo e = topo[f % N_TOPO];
		controlli_evento_mouse(e.ev, e.x, e.y, e.n, e.ms);
		if (e.ev == MOUSE_GIU)
			topo_giu = 1;
		else if (e.ev == MOUSE_SU)
			topo_giu = 0;
	}
}

// ---- Lavori del thread principale (timer della finestra, salvataggio del progetto) ------------
// File temporaneo + rinomina (come il programma): mai un file a meta'
static int scrivi_sicuro(const char *percorso, const char *t, size_t n)
{
	char tmp[1040];
	snprintf(tmp, sizeof tmp, "%s.tmp", percorso);
	SDL_IOStream *f  = SDL_IOFromFile(tmp, "w");
	int           ok = f && SDL_WriteIO(f, t, n) == n;
	if (f && !SDL_CloseIO(f))
		ok = 0;
	if (ok && SDL_RenamePath(tmp, percorso))
		return 1;
	SDL_RemovePath(tmp);
	return 0;
}

static void lavori(void)
{
	uint8_t c[128] = { 0 };
	if (controlli_mappa_scrivi(c)) // learn o CLEAR: mappa.txt, e la regola va anche nel progetto
		for (int i = 0; i < 128; i++)
			cc_progetto[i] |= c[i];
	static char t[32768];
	int         n = storm_scene_testo(t, sizeof t); // EXPORT
	if (n < 0 || (n > 0 && (!scene_utente[0] || !scrivi_sicuro(scene_utente, t, (size_t) n)))) {
		fprintf(stderr, "storm: non riesco a scrivere le scene in %s\n", scene_utente);
		storm_scene_errore();
	}
	if (storm_scene_chiesta()) // IMPORT
		imp_attesa = 1;
	if (imp_attesa && SDL_CompareAndSwapAtomicInt(&carica_stato, 0, 3)) {
		size_t l = 0;
		char  *f = scene_utente[0] ? SDL_LoadFile(scene_utente, &l) : NULL;
		carica_txt_si = f != NULL;
		snprintf(carica_txt, sizeof carica_txt, "%.*s", (int) (l < sizeof carica_txt ? l : sizeof carica_txt - 1), f ? f : "");
		SDL_free(f);
		carica_tipo = 3, imp_attesa = 0;
		SDL_SetAtomicInt(&carica_stato, 1);
	}
}

// ---- Plugin ----------------------------------------------------------------------------------
#define W 320
#define H 256
typedef struct {
	clap_plugin_t      plugin;
	int                mio;   // 1 = e' l'istanza che suona
	const clap_host_t *host;
	clap_id            timer;
	int                z;     // ingrandimento della finestra
} Istanza;

static Finestra *fin; // una per modulo: suona una sola istanza per modulo

static const char *const caratteri[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                         CLAP_PLUGIN_FEATURE_STEREO, NULL };
static const clap_plugin_descriptor_t descrittore = {
	.clap_version = CLAP_VERSION_INIT,
	.id           = "storm.synth",
	.name         = "storm",
	.vendor       = "storm",
	.url          = "",
	.manual_url   = "",
	.support_url  = "",
	.version      = "0.2",
	.description  = "Drone synth from the PlayStation",
	.features     = caratteri,
};

static Istanza *ist(const clap_plugin_t *p) { return p->plugin_data; }
static int      mio(const clap_plugin_t *p) { return ist(p)->mio; }

static bool p_init(const clap_plugin_t *p)
{
	Istanza *s = ist(p);
	avvia();
	if (!occupato)
		occupato = s->mio = 1;
	else
		fprintf(stderr, "storm: questa istanza resta muta (copia del plugin non riuscita)\n");
	return true;
}

static void p_destroy(const clap_plugin_t *p)
{
	Istanza *s = ist(p);
	if (s->mio)
		occupato = 0;
	free(s);
}

static bool p_activate(const clap_plugin_t *p, double rate, uint32_t min, uint32_t max)
{
	(void) min, (void) max;
	if (!mio(p))
		return true;
	rate_daw = rate;
	for (int i = 0; i < n_par; i++) // il thread audio non gira ancora
		mandato[i] = *par[i]->valore;
	if ((int) rate != RATE) {
		SDL_AudioSpec da = { SDL_AUDIO_S16, 2, RATE }, a = { SDL_AUDIO_F32, 2, (int) rate };
		if (!(conv = SDL_CreateAudioStream(&da, &a))) {
			fprintf(stderr, "storm: conversione a %g Hz: %s\n", rate, SDL_GetError());
			return false;
		}
	}
	return true;
}

static void p_deactivate(const clap_plugin_t *p)
{
	if (mio(p) && conv) {
		SDL_DestroyAudioStream(conv);
		conv = NULL;
	}
}

static bool p_start(const clap_plugin_t *p) { (void) p; return true; }
static void p_stop(const clap_plugin_t *p) { (void) p; }
static void p_reset(const clap_plugin_t *p)
{
	if (mio(p) && conv)
		SDL_ClearAudioStream(conv);
}

// il suono da fatto fino a t
static void fino(float *sx, float *dx, uint32_t *fatto, uint32_t t)
{
	if (t > *fatto) {
		suona(sx + *fatto, dx + *fatto, (int) (t - *fatto));
		*fatto = t;
	}
}

static clap_process_status p_process(const clap_plugin_t *p, const clap_process_t *pr)
{
	if (pr->audio_outputs_count < 1 || pr->audio_outputs[0].channel_count < 2)
		return CLAP_PROCESS_ERROR;
	float   *sx = pr->audio_outputs[0].data32[0], *dx = pr->audio_outputs[0].data32[1];
	uint32_t n  = pr->frames_count;
	if (!mio(p)) {
		memset(sx, 0, n * sizeof *sx);
		memset(dx, 0, n * sizeof *dx);
		return CLAP_PROCESS_SLEEP;
	}
	if (SDL_CompareAndSwapAtomicInt(&carica_stato, 1, 2)) { // un progetto appena aperto
		if (carica_tipo == 2) {
			storm_stato_carica(carica_b, N_STATO);
			if (carica_mappa[0])
				controlli_mappa(carica_mappa, "progetto");
		} else if (carica_tipo == 3)
			storm_scene_importa(carica_txt_si ? carica_txt : NULL, scene_utente);
		else
			for (int i = 0; i < n_par; i++)
				valore(i, carica[i]);
		SDL_SetAtomicInt(&carica_stato, 0);
	}
	controlli_riprova();
	topo_prendi();
	// a pezzi tra un evento e l'altro (eventi della DAW e tick del suo tempo, in ordine): ogni
	// evento vale dal suo campione (a BLOCCO campioni, convertendo)
	static Tick tk[N_TICK];
	int         nt = tempo_daw(pr->transport, n, tk), it = 0;
	const clap_input_events_t *in = pr->in_events;
	uint32_t ne = in->size(in), ie = 0, fatto = 0;
	while (ie < ne || it < nt) {
		const clap_event_header_t *h = ie < ne ? in->get(in, ie) : NULL;
		uint32_t th = h ? (h->time < n ? h->time : n) : UINT32_MAX;
		if (it < nt && tk[it].t <= th) {
			fino(sx, dx, &fatto, tk[it].t);
			midi(&tk[it].b, 1, tk[it].t);
			it++;
		} else {
			fino(sx, dx, &fatto, th);
			evento(h);
			ie++;
		}
	}
	fino(sx, dx, &fatto, n);
	t_daw += n;
	manda_cambi(pr->out_events);
	return CLAP_PROCESS_CONTINUE;
}

// ---- Parametri -------------------------------------------------------------------------------
static uint32_t pa_count(const clap_plugin_t *p) { (void) p; return (uint32_t) n_par; }

static bool pa_info(const clap_plugin_t *p, uint32_t i, clap_param_info_t *info)
{
	(void) p;
	if (i >= (uint32_t) n_par)
		return false;
	const Param *q = par[i];
	memset(info, 0, sizeof *info);
	info->id    = i;
	info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | (q->tipo == P_SCELTA ? CLAP_PARAM_IS_ENUM : 0);
	snprintf(info->name, sizeof info->name, "%s", q->nome);
	snprintf(info->module, sizeof info->module, "%s", q->pagina);
	info->min_value     = q->min;
	info->max_value     = q->max;
	info->default_value = q->iniziale;
	return true;
}

static bool pa_value(const clap_plugin_t *p, clap_id id, double *v)
{
	(void) p;
	if (id >= (clap_id) n_par)
		return false;
	*v = *(volatile const int *) par[id]->valore;
	return true;
}

static bool pa_testo(const clap_plugin_t *p, clap_id id, double v, char *t, uint32_t n)
{
	(void) p;
	if (id >= (clap_id) n_par || n == 0)
		return false;
	const Param *q = par[id];
	int          k = intero(q, v);
	const char  *e = q->etichette;
	for (int i = 0; e && i < k - q->min && (e = strstr(e, ", ")); i++)
		e += 2;
	if (q->tipo == P_SCELTA && e) {
		size_t l = strcspn(e, ",");
		snprintf(t, n, "%.*s", (int) l, e);
	} else
		snprintf(t, n, "%d", k);
	return true;
}

static bool pa_valore(const clap_plugin_t *p, clap_id id, const char *t, double *v)
{
	(void) p;
	char *f;
	if (id >= (clap_id) n_par)
		return false;
	*v = strtod(t, &f);
	return f != t;
}

static void pa_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out)
{
	if (!mio(p))
		return;
	eventi(in);
	manda_cambi(out);
}

static const clap_plugin_params_t ext_par = { pa_count, pa_info, pa_value, pa_testo, pa_valore, pa_flush };

// ---- Stato del progetto ----------------------------------------------------------------------
// "storm 2" e lo stato completo in esadecimale; si legge anche il formato di prima ("ID valore")
static bool st_salva(const clap_plugin_t *p, const clap_ostream_t *o)
{
	if (mio(p))
		lavori(); // un learn appena fatto entra nel progetto
	uint8_t b[N_STATO];
	if (SDL_GetAtomicInt(&carica_stato) == 1 && carica_tipo == 2)
		memcpy(b, carica_b, N_STATO); // caricato ma non ancora suonato: e' questo lo stato
	else
		for (;;) { // l'ultima fotografia intera
			int s = SDL_GetAtomicInt(&stato_seq);
			if (s & 1)
				continue;
			memcpy(b, stato_foto, N_STATO);
			SDL_MemoryBarrierAcquire();
			if (SDL_GetAtomicInt(&stato_seq) == s)
				break;
		}
	// dopo lo stato, una riga per CC cambiato col learn: "cc 3 = CUTOFF" (come nella mappa)
	char t[8 + 2 * N_STATO + 1 + 128 * 48];
	int  l = snprintf(t, sizeof t, "storm 2\n");
	for (int i = 0; i < N_STATO; i++)
		l += snprintf(t + l, sizeof t - (size_t) l, "%02x", b[i]);
	l += snprintf(t + l, sizeof t - (size_t) l, "\n");
	for (int i = 0; i < 128; i++)
		if (cc_progetto[i])
			l += controlli_regola_cc(i, t + l, (int) sizeof t - l);
	for (int f = 0; f < l;) {
		int64_t s = o->write(o, t + f, (uint64_t) (l - f));
		if (s <= 0)
			return false;
		f += (int) s;
	}
	return true;
}

static bool st_carica(const clap_plugin_t *p, const clap_istream_t *is)
{
	if (!mio(p))
		return true;
	static char t[2 * N_STATO + N_PAR * 24 + 128 * 48 + 64];
	int         l = 0;
	int64_t     s;
	while (l < (int) sizeof t - 1 && (s = is->read(is, t + l, sizeof t - 1 - (size_t) l)) > 0)
		l += (int) s;
	t[l] = 0;
	// aspetta che il thread audio abbia finito con il progetto di prima (se lo sta leggendo)
	while (!SDL_CompareAndSwapAtomicInt(&carica_stato, 0, 3) && !SDL_CompareAndSwapAtomicInt(&carica_stato, 1, 3))
		SDL_Delay(1);
	int ok = 1;
	if (strncmp(t, "storm 2\n", 8) == 0) {
		carica_tipo = 2;
		for (int i = 0; i < N_STATO && ok; i++) {
			unsigned v;
			ok = sscanf(t + 8 + 2 * i, "%2x", &v) == 1;
			carica_b[i] = (uint8_t) v;
		}
		const char *r = ok ? t + 8 + 2 * N_STATO : ""; // le regole del learn, se ci sono
		r += *r == '\n';
		snprintf(carica_mappa, sizeof carica_mappa, "%s", r);
		memset(cc_progetto, 0, sizeof cc_progetto);
		for (; *r; r += strcspn(r, "\n"), r += *r == '\n') {
			int c;
			if (sscanf(r, "cc %d", &c) == 1 && c >= 0 && c < 128)
				cc_progetto[c] = 1;
		}
	} else {
		carica_tipo = 1;
		for (int i = 0; i < n_par; i++)
			carica[i] = *(volatile const int *) par[i]->valore;
		for (char *r = strtok(t, "\n"); r; r = strtok(NULL, "\n")) {
			char id[64];
			int  v;
			if (sscanf(r, "%63s %d", id, &v) != 2)
				continue;
			for (int i = 0; i < n_par; i++)
				if (strcmp(par[i]->id, id) == 0)
					carica[i] = intero(par[i], v);
		}
	}
	SDL_SetAtomicInt(&carica_stato, ok ? 1 : 0);
	return ok;
}

static const clap_plugin_state_t ext_stato = { st_salva, st_carica };

// ---- Finestra --------------------------------------------------------------------------------
static bool g_api(const clap_plugin_t *p, const char *api, bool flottante)
{
	(void) p;
	return !flottante && strcmp(api, finestra_api()) == 0;
}

static bool g_preferita(const clap_plugin_t *p, const char **api, bool *flottante)
{
	(void) p;
	*api = finestra_api(), *flottante = false;
	return true;
}

static void sul_timer(const clap_plugin_t *p, clap_id id);

static bool g_crea(const clap_plugin_t *p, const char *api, bool flottante)
{
	Istanza *s = ist(p);
	if (!s->mio || fin || !g_api(p, api, flottante))
		return false;
	// disegno e mouse a 50 Hz col timer della DAW (thread principale)
	const clap_host_timer_support_t *ts = s->host->get_extension(s->host, CLAP_EXT_TIMER_SUPPORT);
	if (!ts || !ts->register_timer(s->host, 20, &s->timer))
		return false;
	if (!s->z)
		s->z = 3;
	return true;
}

static void g_distruggi(const clap_plugin_t *p)
{
	Istanza *s = ist(p);
	const clap_host_timer_support_t *ts = s->host->get_extension(s->host, CLAP_EXT_TIMER_SUPPORT);
	if (ts)
		ts->unregister_timer(s->host, s->timer);
	finestra_chiudi(fin);
	fin = NULL;
	topo_metti((Topo) { MOUSE_SU, -1, -1, 0, 0 }); // un trascinamento a meta' finisce
}

static bool g_scala(const clap_plugin_t *p, double s) { (void) p, (void) s; return false; } // la misura la sceglie chi ridimensiona

static bool g_misura(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
	*w = (uint32_t) (W * ist(p)->z), *h = (uint32_t) (H * ist(p)->z);
	return true;
}

static bool g_ridim(const clap_plugin_t *p) { (void) p; return true; }

static bool g_consigli(const clap_plugin_t *p, clap_gui_resize_hints_t *c)
{
	(void) p;
	*c = (clap_gui_resize_hints_t) { true, true, true, W, H };
	return true;
}

static bool g_aggiusta(const clap_plugin_t *p, uint32_t *w, uint32_t *h) // multipli interi del disegno
{
	(void) p;
	uint32_t z = *w / W < *h / H ? *w / W : *h / H;
	z      = z < 1 ? 1 : z;
	*w = W * z, *h = H * z;
	return true;
}

static bool g_metti_misura(const clap_plugin_t *p, uint32_t w, uint32_t h)
{
	Istanza *s = ist(p);
	s->z       = (int) (w / W < h / H ? w / W : h / H);
	s->z       = s->z < 1 ? 1 : s->z;
	if (fin)
		finestra_misura(fin, W * s->z, H * s->z);
	return true;
}

static bool g_genitore(const clap_plugin_t *p, const clap_window_t *w)
{
	Istanza  *s = ist(p);
	uintptr_t g = strcmp(w->api, CLAP_WINDOW_API_X11) == 0 ? (uintptr_t) w->x11 : (uintptr_t) w->ptr;
	if (fin)
		return false;
	fin = finestra_apri(g, W * s->z, H * s->z);
	return fin != NULL;
}

static bool g_transitoria(const clap_plugin_t *p, const clap_window_t *w) { (void) p, (void) w; return false; }
static void g_titolo(const clap_plugin_t *p, const char *t) { (void) p, (void) t; }
static bool g_mostra(const clap_plugin_t *p) { (void) p; return true; }
static bool g_nascondi(const clap_plugin_t *p) { (void) p; return true; }

static const clap_plugin_gui_t ext_gui = { g_api,      g_preferita,   g_crea,     g_distruggi, g_scala,   g_misura,
                                           g_ridim,    g_consigli,    g_aggiusta, g_metti_misura, g_genitore,
                                           g_transitoria, g_titolo,   g_mostra,   g_nascondi };

static void sul_timer(const clap_plugin_t *p, clap_id id)
{
	(void) id;
	if (!mio(p) || !fin)
		return;
	lavori();
	if (psx_disegna()) // l'ultimo fotogramma finito dal thread audio
		finestra_disegna(fin, psx_schermo);
	Topo e;
	while (finestra_mouse(fin, &e.ev, &e.x, &e.y, &e.n, &e.ms))
		topo_metti(e);
}

static const clap_plugin_timer_support_t ext_timer = { sul_timer };

// ---- Porte -----------------------------------------------------------------------------------
static uint32_t au_count(const clap_plugin_t *p, bool in) { (void) p; return in ? 0 : 1; }
static bool     au_get(const clap_plugin_t *p, uint32_t i, bool in, clap_audio_port_info_t *info)
{
	(void) p;
	if (in || i)
		return false;
	memset(info, 0, sizeof *info);
	info->id            = 0;
	info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 2;
	info->port_type     = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	snprintf(info->name, sizeof info->name, "out");
	return true;
}
static const clap_plugin_audio_ports_t ext_audio = { au_count, au_get };

static uint32_t no_count(const clap_plugin_t *p, bool in) { (void) p; return in ? 1 : 0; }
static bool     no_get(const clap_plugin_t *p, uint32_t i, bool in, clap_note_port_info_t *info)
{
	(void) p;
	if (!in || i)
		return false;
	memset(info, 0, sizeof *info);
	info->id                 = 0;
	info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
	info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI;
	snprintf(info->name, sizeof info->name, "midi");
	return true;
}
static const clap_plugin_note_ports_t ext_note = { no_count, no_get };

static const void *p_ext(const clap_plugin_t *p, const char *id)
{
	(void) p;
	if (!strcmp(id, CLAP_EXT_PARAMS))
		return &ext_par;
	if (!strcmp(id, CLAP_EXT_STATE))
		return &ext_stato;
	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
		return &ext_audio;
	if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
		return &ext_note;
	if (!strcmp(id, CLAP_EXT_GUI))
		return &ext_gui;
	if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT))
		return &ext_timer;
	return NULL;
}

static void p_main(const clap_plugin_t *p) { (void) p; }

// ---- Fabbrica dentro (anche quella delle copie) ----------------------------------------------
static uint32_t f_count(const clap_plugin_factory_t *f) { (void) f; return 1; }
static const clap_plugin_descriptor_t *f_desc(const clap_plugin_factory_t *f, uint32_t i)
{
	(void) f;
	return i == 0 ? &descrittore : NULL;
}

static const clap_plugin_t *f_crea(const clap_plugin_factory_t *f, const clap_host_t *host, const char *id)
{
	(void) f;
	if (strcmp(id, descrittore.id))
		return NULL;
	Istanza *s = calloc(1, sizeof *s);
	if (!s)
		return NULL;
	s->host   = host;
	s->plugin = (clap_plugin_t) { &descrittore, s, p_init, p_destroy, p_activate, p_deactivate, p_start,
	                              p_stop, p_reset, p_process, p_ext, p_main };
	return &s->plugin;
}

static const clap_plugin_factory_t fabbrica = { f_count, f_desc, f_crea };

CLAP_EXPORT const clap_plugin_factory_t *storm_interno(void) { return &fabbrica; }

// ---- Fuori: un modulo per istanza ------------------------------------------------------------
// La DAW parla con una copia della clap_plugin_t di dentro (stesso plugin_data: il codice di dentro
// usa solo quello), con destroy che poi scarica e cancella la copia del modulo.
typedef struct {
	clap_plugin_t       p;
	const clap_plugin_t *dentro;
	SDL_SharedObject   *so;   // NULL = questo modulo
	char                file[1100];
} Copia;

static char modulo[1024]; // il binario di questo plugin
static char cartella[1024]; // dove vanno le copie ("" = niente copie)
static int  originale;    // un'istanza usa gia' questo modulo

static void c_destroy(const clap_plugin_t *p)
{
	Copia *c = (Copia *) p;
	c->dentro->destroy(p);
	if (c->so) {
		SDL_UnloadObject(c->so);
		SDL_RemovePath(c->file);
	} else
		originale = 0;
	free(c);
}

static const clap_plugin_t *copia_crea(const clap_host_t *host, const char *id, Copia *c)
{
	static unsigned n;
	if (!cartella[0] || !modulo[0])
		return NULL;
	// con l'estensione: senza, Windows cercherebbe "storm-...dll"
	snprintf(c->file, sizeof c->file, "%sstorm-%llu-%u.bin", cartella, (unsigned long long) SDL_GetTicksNS(), ++n);
	if (!SDL_CopyFile(modulo, c->file) || !(c->so = SDL_LoadObject(c->file))) {
		fprintf(stderr, "storm: copia del plugin per un'altra istanza: %s\n", SDL_GetError());
		SDL_RemovePath(c->file);
		return NULL;
	}
	const clap_plugin_factory_t *(*interno)(void) = (const clap_plugin_factory_t *(*) (void)) SDL_LoadFunction(c->so, "storm_interno");
	const clap_plugin_factory_t *fab = interno ? interno() : NULL;
	const clap_plugin_t         *d   = fab ? fab->create_plugin(fab, host, id) : NULL;
	if (!d) {
		SDL_UnloadObject(c->so);
		SDL_RemovePath(c->file);
		c->so = NULL;
	}
	return d;
}

static const clap_plugin_t *fuori_crea(const clap_plugin_factory_t *f, const clap_host_t *host, const char *id)
{
	(void) f;
	Copia *c = calloc(1, sizeof *c);
	if (!c)
		return NULL;
	if (originale)
		c->dentro = copia_crea(host, id, c);
	if (!c->dentro) { // la prima istanza, o la copia non si e' potuta fare (muta)
		c->dentro  = f_crea(&fabbrica, host, id);
		originale |= c->dentro != NULL;
	}
	if (!c->dentro) {
		free(c);
		return NULL;
	}
	c->p         = *c->dentro;
	c->p.destroy = c_destroy;
	return &c->p;
}

static const clap_plugin_factory_t fabbrica_fuori = { f_count, f_desc, fuori_crea };

static bool e_init(const char *path)
{
	// il binario: su macOS il plugin e' una cartella (Contents/MacOS/storm)
	SDL_PathInfo pi;
	if (path && SDL_GetPathInfo(path, &pi))
		snprintf(modulo, sizeof modulo, pi.type == SDL_PATHTYPE_DIRECTORY ? "%s/Contents/MacOS/storm" : "%s", path);
	char *d = SDL_GetPrefPath("", "storm");
	if (d) {
		snprintf(cartella, sizeof cartella, "%sistanze/", d);
		SDL_free(d);
		SDL_CreateDirectory(cartella);
		// copie rimaste da una DAW chiusa male (quelle in uso su Windows non si cancellano: va bene)
		int    n;
		char **v = SDL_GlobDirectory(cartella, "storm-*", 0, &n);
		for (int i = 0; v && i < n; i++) {
			char f[1100];
			snprintf(f, sizeof f, "%s%s", cartella, v[i]);
			SDL_RemovePath(f);
		}
		SDL_free(v);
	}
	return true;
}
static void        e_deinit(void) {}
static const void *e_factory(const char *id) { return strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? NULL : &fabbrica_fuori; }

CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, e_init, e_deinit, e_factory };
