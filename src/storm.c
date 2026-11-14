/*
 * storm - synth per drone PS1 PAL (progetto in DESIGN.md). Tappa c: tutto il suono.
 * Una nota tenuta, niente sequencer. La CPU calcola a 22050 Hz (motore DSP di nucleo, senza coda
 * di note): OSC A con 3 forme insieme (sinusoide, quadra a larghezza variabile, sega) modulato in
 * fase da OSC B (FM), poi il fold (piega a triangolo), piu' il SUB (sinusoide un'ottava sotto,
 * non piegata); poi SHAPE: DRIVE (saturazione morbida) e filtro state variable LP/BP/HP, con un
 * anello di FEEDBACK (uscita del filtro di nuovo nel DRIVE, ritardata di un periodo della nota,
 * attraverso un VCA mosso dal caso); poi il volume. I campioni diventano blocchi ADPCM in un
 * anello in SPU RAM che la voce 0 suona in loop: il ritardo (~50-80 ms) in un drone non conta.
 * MOTION: 3 generatori (SIN, TRI, BREATH, RANDOM) a velocita' non commensurabili e 2 derive
 * lentissime (cutoff e rapporto FM), calcolati a ogni blocco del motore (787,5 volte al secondo).
 * Sulla SPU, aggiornati a ogni fotogramma: 4 voci LAYERS (sega a 4 colori, un'ottava sopra la
 * nota, stonate, ognuna modulata in pitch dalla voce prima = BELLS, la FM hardware), il rumore,
 * il pan con la fase invertita oltre WIDE 50, il riverbero (ROOM, TAIL = vWALL, DAMP = vIIR).
 * 4 MACRO: ognuna somma 4 manopole a scelta con attenuatori -9..+9.
 * Controller 1: a riposo SHAPE; tieni X = OSC, Quadrato = GRAIN, Triangolo = NOTE, Cerchio = LAYERS;
 *   Select = MODE del filtro, Start = MUTE (tenuto 1 s = RESET).
 * Controller 2: a riposo MIX; tieni X/Quadrato/Triangolo = MOD A/B/C, Cerchio = SPACE,
 *   Select = MACROS (+ X/Quadrato/Triangolo/Cerchio = EDIT della macro 1-4); Start = FREEZE.
 * Frecce e dorsali = 4 manopole della pagina; levette DualShock = manopole in piu'.
 *
 * SPU RAM: 0x00000-0x00FFF riservati, 0x1000 = campione muto dell'SDK,
 *          0x01010-0x0164F suoni.h (muto, 4 seghe, 8 seni: onde da 128 byte, loop a +64),
 *          0x02000-0x023FF anello del motore DSP (64 blocchi ADPCM),
 *          0x70000-0x7FFFF area del riverbero (azzerata solo all'avvio).
 * Voci: 0 = anello DSP; 1, 3, 5, 7 = modulatori BELLS (volume 0); 2, 4, 6, 8 = LAYERS.
 * Schermata (tappa d): 6 pannelli 3D (angoli proiettati con la GTE) con figure animate dai valori
 * suonati; il pannello della pagina tenuta viene avanti con i valori delle sue 4 manopole.
 * Patch (tappa g): un file sulla memory card della porta 1 con 8 posti; si legge all'avvio (prima
 * del suono) e si scrive da SAVE SCENE della GUIDE, dopo aver riempito l'anello del motore DSP.
 * VRAM: (0,0) e (0,256) framebuffer 320x256; (320,0) font 64x112 a 4 bit; (320,112) CLUT 16x1.
 * Il font si carica solo all'avvio, prima dell'interrupt del timer.
 *
 * Versione PC: lo stesso programma sopra la piccola PS1 finta di psx.h. Cambia solo dove su PC non
 * ha senso: il motore DSP scrive ogni blocco ADPCM nell'anello proprio quando la voce 0 lo legge
 * (niente ritardo), le scene in un file di testo al posto della memory card (e niente pausa del
 * suono), e il main loop e' diviso in storm_avvia() e storm_fotogramma(), che chi lo usa chiama 50
 * volte al secondo di audio.
 * I pad arrivano da controlli.c (gamepad e manopole della tastiera come pad PS1); pagine e azioni
 * speciali della tastiera come azioni comuni (storm_azione, in fondo). Per il mouse storm pubblica a
 * ogni fotogramma le zone cliccabili, prese dalle stesse posizioni con cui disegna (zona, celle,
 * guida_disegna, testata e aiuto in basso); appena il mouse passa sul disegno compaiono i pulsanti
 * in testata, le pagine in basso al posto dell'aiuto del pad e l'aiuto della cosa sotto il puntatore.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psx.h"
#include "controlli.h"
#include "suoni.h"
#include "font.h"
#include "curva.h"
#include <SDL3/SDL_atomic.h>
#include "testi.h"

#define W 320
#ifndef NTSC
#define NTSC 0    // 1 = versione NTSC (320x240 a 60 Hz): build.py <gioco> ntsc
#endif
#define H   256   // si disegna sempre a 320x256 (VRAM: 2 x 256 righe)
#define FPS (NTSC ? 60 : 50)
// NTSC: al TV vanno 240 righe a partire dalla riga 6 del disegno; il TV ne mostra ~224 (6-8 righe
// tagliate sopra e sotto), cioe' 14-238: proprio dove stanno testata e aiuto della versione PAL
#define VIS_Y (NTSC ? 6 : 0)
#define VIS_H (NTSC ? 240 : 256)
#define BUF_LEN 40960 // a riposo ~250 caratteri (POLY_FT4 da 40 byte) + ~600 segmenti e quad (24 byte)
#define FONT_X 320
#define FONT_Y 0
#define CLUT_X 320
#define CLUT_Y 112
#define RIGA   12     // altezza di una riga di testo in pixel
#define SUONI  0x1010 // indirizzo del blob in SPU RAM

// Livelli dell'OT (piu' alto = disegnato prima): i pannelli dietro, poi quello venuto avanti
// (fondo e figura), il testo sopra a tutto
enum { Z_TESTO, Z_FIGZ, Z_PANZ, Z_FIG, Z_PAN, OT_LEN };
#define Z_FONDO Z_PAN

typedef struct {
	DISPENV  disp;
	DRAWENV  draw;
	uint32_t ot[OT_LEN];
	uint8_t  buf[BUF_LEN];
} Buffer;

static Buffer   fb[2];
static int      cur;
static uint8_t *nextp; // prossima primitiva libera in fb[cur].buf

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t seme = 0x2545F491; // xorshift32: tutto il caso (RANDOM e derive)
static uint32_t caso(void) {
	seme ^= seme << 13;
	seme ^= seme >> 17;
	seme ^= seme << 5;
	return seme;
}

/* Controller: porta 1 e porta 2, tasti e levette */

static uint8_t  padbuf[2][34];
static uint16_t held[2], hit[2];
static uint8_t  tenuto[2][16]; // fotogrammi da cui il tasto e' giu' (per lo scorrimento)
static int      leve[2][4];    // per porta: sinistra X, Y, destra X, Y (-88..88, 0 = ferma o pad digitale)
static int      blocco;        // LOCK: dei pad passano solo C1 Start (MUTE) e Start + X (lo sblocco)

// Levetta -> deviazione oltre la zona morta (+-40, come nucleo): una levetta lasciata non cambia niente
static int leva(int v) {
	int d = v - 128;
	return d > 40 ? d - 40 : d < -40 ? d + 40 : 0;
}

static void read_pad(void) {
	for (int i = 0; i < 2; i++) {
		PADTYPE *p = (PADTYPE *) padbuf[i];
		// assente = nessun tasto; type 0 = buffer non ancora scritto dal BIOS (tutto a zero)
		int ok = p->stat == 0 && p->type;
		uint16_t now = ok ? (uint16_t) ~p->btn : 0;
		if (blocco) now &= i ? 0 : PAD_START | (now & PAD_START ? PAD_CROSS : 0);
		ok = ok && !blocco; // niente levette
		hit[i]  = now & ~held[i];
		held[i] = now;
		for (int b = 0; b < 16; b++)
			tenuto[i][b] = now >> b & 1 ? (tenuto[i][b] < 255 ? tenuto[i][b] + 1 : 255) : 0;
		int ana = ok && p->type == PAD_ID_ANALOG; // DualShock con LED rosso
		leve[i][0] = ana ? leva(p->ls_x) : 0;
		leve[i][1] = ana ? leva(p->ls_y) : 0;
		leve[i][2] = ana ? leva(p->rs_x) : 0;
		leve[i][3] = ana ? leva(p->rs_y) : 0;
	}
}

// Un passo del tasto: 1 alla pressione; se scorre, 1 a ogni fotogramma dopo 12 fotogrammi tenuto
// (0-99 in ~2 s)
static int passo(int c, uint16_t bit, int scorre) {
	return (hit[c] & bit) || (scorre && tenuto[c][__builtin_ctz(bit)] > 12);
}

// Tasto toccato da solo: vale al rilascio, e solo se mentre era giu' non si e' premuto altro
// (cosi' Start + Select = GUIDE non fa anche MUTE, MODE o FREEZE)
static int solo(int c, uint16_t bit, int *giu, int *altro) {
	if (held[c] & bit) {
		if ((!*giu && (held[c] & ~bit)) || (hit[c] & ~bit)) *altro = 1;
		*giu = 1;
		return 0;
	}
	int r = *giu && !*altro;
	*giu = *altro = 0;
	return r;
}

// Pagina del controller c: tasto tenuto oppure pagina fissa (doppio tocco del tasto pagina la
// tiene aperta, un altro tocco la chiude). Come in nucleo; n = quante pagine.
static int fissa[2], tocco_k[2];
static uint32_t tocco_t[2];
static int pagina(int c, uint16_t h, uint16_t t, int n, uint32_t ora) {
	static const uint16_t tasto[4] = { PAD_CROSS, PAD_SQUARE, PAD_TRIANGLE, PAD_CIRCLE };
	for (int i = 0; i < n; i++) {
		if (!(t & tasto[i])) continue;
		if (fissa[c] == i + 1)
			fissa[c] = 0;
		else if (tocco_k[c] == i + 1 && ora - tocco_t[c] < 15) // due tocchi in 0,3 s
			fissa[c] = i + 1;
		tocco_k[c] = i + 1;
		tocco_t[c] = ora;
	}
	for (int i = 0; i < n; i++)
		if (h & tasto[i]) return i + 1;
	return fissa[c];
}

/* Manopole. Continue 0-99 (scorrono tenendo il tasto) o liste (un passo a pressione). */

enum { K_SINE, K_SQUARE, K_SAW, K_SUB, K_FOLD, K_FM, K_RATIO, K_WIDTH, K_ROOT, K_OCT, K_VOL, K_GLIDE,
       K_CUT, K_RES, K_MODE, K_DRIVE, K_FEED, K_AM, K_PWM,
       K_DETUNE, K_BELLS, K_INTERVAL, K_COLOR,
       K_M1, K_M2, K_M3, K_M4, K_REVERB, K_LAYERS, K_NOISE,
       K_WIDE, K_TAIL, K_ROOM, K_DAMP,
       K_STEPS, K_MUTATE, K_SCALE, K_EVERY,      // SEQ
       K_HITS1, K_STEPS1, K_HITS2, K_STEPS2,     // PULSE
       K_BPM, K_RANGE, K_TAP,                    // CLOCK (BPM = 40 + manopola; TAP: solo il tasto)
       K_MOD,                                 // MOD A, B, C: 4 manopole ciascuno
       K_FINE = K_MOD + 12, K_PEDAL, K_THUMP, K_WIND, // TEXTURE (C1 Select + X)
       K_MORPH, K_NUDGE,                      // SCENE: tempo della scivolata; CLOCK: solo i tasti L1/R1
       N_K };
#define N_K1 (K_MOD + 12) // manopole delle patch salvate in versione 1
enum { M_RATE, M_DEPTH, M_WAVE, M_DEST };    // K_MOD + 4 * generatore + questo
#define MK(g, m) (K_MOD + 4 * (g) + (m))

static int                k[N_K];
static const int16_t      k_init[N_K] = { 70, 20, 0, 50, 25, 15, 3, 30, 0, 1, 80, 2,   // A1, rapporto 2
                                          45, 30, 0, 20, 0, 0, 0,                     // LP, niente feedback, AM, PWM
                                          30, 10, 1, 1,                               // 5TH, SOFT
                                          0, 0, 0, 0, 55, 45, 10,                     // macro a 0
                                          40, 60, 2, 50,                              // HALL
                                          0, 30, 0, 2,  3, 8, 5, 12,  40, 0, 0,       // SEQ spento, E(3,8) e E(5,12), 80 BPM
                                          20, 35, 0, 0,  25, 25, 3, 2,  15, 30, 2, 11,   // SIN su CUTOFF, RANDOM su FOLD, BREATH su LAYERS
                                          50, 0, 0, 0,  2, 0 };                       // FINE al centro, PEDAL OFF, MORPH 10 S
static const int16_t      k_max[N_K]  = { 99, 99, 99, 99, 99, 99, 5, 99, 11, 3, 99, 8,
                                          99, 99, 2, 99, 99, 99, 99,
                                          99, 99, 8, 5,
                                          99, 99, 99, 99, 99, 99, 99,
                                          99, 99, 4, 99,
                                          7, 99, 8, 4,  16, 16, 16, 16,  160, 1, 0,
                                          99, 99, 6, 22,  99, 99, 6, 22,  99, 99, 6, 22,
                                          99, 3, 99, 99,  6, 0 };
static const char * const k_nome[N_K] = { "SINE", "SQUARE", "SAW", "SUB", "FOLD", "FM", "RATIO", "WIDTH",
                                          "ROOT", "OCTAVE", "VOLUME", "GLIDE", "CUTOFF", "RES", "MODE", "DRIVE",
                                          "FEEDBACK", "AM", "PWM", "DETUNE", "BELLS", "CHORD", "COLOR",
                                          "M1 RISE", "M2 DEEP", "M3 UNREST", "M4 HAZE", "REVERB", "LAYERS", "NOISE",
                                          "WIDE", "TAIL", "ROOM", "DAMP",
                                          "STEPS", "MUTATE", "SCALE", "EVERY", "HITS 1", "STEPS 1", "HITS 2", "STEPS 2",
                                          "BPM", "RANGE", "TAP",
                                          "RATE", "DEPTH", "WAVE", "DEST", "RATE", "DEPTH", "WAVE", "DEST",
                                          "RATE", "DEPTH", "WAVE", "DEST",
                                          "FINE", "PEDAL", "THUMP", "WIND", "MORPH", "NUDGE" };
#define LISTA(i) (k_max[i] < 99)

// Rapporto FM = frequenza di OSC B / OSC A
static const uint8_t rap[6][2] = { { 1, 2 }, { 1, 1 }, { 3, 2 }, { 2, 1 }, { 3, 1 }, { 7, 2 } };
static const char * const rap_nome[6]   = { "1/2", "1", "3/2", "2", "3", "7/2" };
static const char * const nota_nome[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char * const modo_nome[3]  = { "LP", "BP", "HP" };
static const char * const wave_nome[7]  = { "SIN", "TRI", "BREATH", "RANDOM", "NOISE", "PULSE1", "PULSE2" };
static const char * const wave_corto[7] = { "SIN", "TRI", "BRTH", "RND", "NOIS", "PLS1", "PLS2" };
static const char * const int_nome[9]   = { "UNISON", "5TH", "OCTAVE", "2 OCT", "MINOR", "MAJOR", "SUS4", "MIN7", "SCALE" };
static const char * const int_corto[9]  = { "UNI", "5TH", "OCT", "2OC", "MIN", "MAJ", "SUS4", "MIN7", "SCL" };
static const char * const scala_nome[9] = { "MINOR", "DORIAN", "PENTA", "FIFTHS", "OCTAVES", "PHRYGIAN", "LYDIAN",
                                             "MIXOLYD", "MAJOR" };
static const char * const pedal_nome[4] = { "OFF", "SUB", "LAYERS", "BOTH" };
static const uint8_t      morph_s[7]    = { 2, 5, 10, 20, 40, 80, 120 }; // MORPH: secondi della scivolata
static const uint8_t      ogni[5]       = { 1, 2, 4, 8, 16 }; // EVERY: battiti per passo del SEQ
static const char * const col_nome[6]   = { "DARK", "SOFT", "WARM", "BRIGHT", "THIN", "AIR" };
static const char * const room_nome[5]  = { "SMALL", "STUDIO", "HALL", "ECHO", "SPACE" };
static const char * const room_corto[5] = { "SML", "STU", "HALL", "ECHO", "SPC" };
// Destinazioni dei MOD: le prime N_DK il motore DSP le somma alla manopola; PITCH al pitch; LAYERS
// e PAN li usa la SPU; DEPTH A-C = la profondita' di un altro MOD (modulazione attraverso un VCA
// mosso da un'altra sorgente); MACRO 1-4 = la macro (patch che evolve da solo)
enum { D_PITCH = 10, D_LAYERS, D_PAN, D_DEPTH, D_MACRO = D_DEPTH + 3, D_RATE = D_MACRO + 4 }; // RATE A-C in fondo: le patch salvate restano giuste
#define N_DK 10
static const char * const dest_nome[23]  = { "CUTOFF", "RES", "FOLD", "FM", "AM", "PWM", "WIDTH", "NOISE", "FEEDBACK",
                                             "VOLUME", "PITCH", "LAYERS", "PAN", "DEPTH A", "DEPTH B", "DEPTH C",
                                             "M1 RISE", "M2 DEEP", "M3 UNREST", "M4 HAZE", "RATE A", "RATE B", "RATE C" };
static const char * const dest_corto[23] = { "CUT", "RES", "FOLD", "FM", "AM", "PWM", "WID", "NOIS", "FB", "VOL",
                                             "PIT", "LAY", "PAN", "DEPA", "DEPB", "DEPC", "MAC1", "MAC2", "MAC3", "MAC4",
                                             "RATA", "RATB", "RATC" };
static const int8_t dest_k[N_DK] = { K_CUT, K_RES, K_FOLD, K_FM, K_AM, K_PWM, K_WIDTH, K_NOISE, K_FEED, K_VOL };
// Manopole che il motore usa lisciate (le altre: liste e RATE, lette cosi' come sono)
static const int8_t lisce[] = { K_SINE, K_SQUARE, K_SAW, K_SUB, K_FOLD, K_FM, K_WIDTH, K_VOL, K_CUT, K_RES,
                                K_DRIVE, K_FEED, K_AM, K_PWM, K_NOISE, K_MOD + 1, K_MOD + 5, K_MOD + 9 };

// MACRO: 4 macro x 4 posti; ogni posto = destinazione (indice in macro_k) e attenuatore -9..+9
static const int8_t macro_k[] = { K_CUT, K_RES, K_DRIVE, K_FEED, K_SINE, K_SQUARE, K_SAW, K_SUB, K_FOLD, K_FM,
                                  K_AM, K_PWM, K_WIDTH, K_VOL, K_DETUNE, K_BELLS, K_LAYERS, K_NOISE, K_REVERB,
                                  K_TAIL, K_DAMP, K_WIDE, MK(0, 0), MK(0, 1), MK(1, 0), MK(1, 1), MK(2, 0), MK(2, 1) };
static const char * const macro_nome[] = { "CUTOFF", "RES", "DRIVE", "FEEDBACK", "SINE", "SQUARE", "SAW", "SUB", "FOLD",
                                           "FM", "AM", "PWM", "WIDTH", "VOLUME", "DETUNE", "BELLS", "LAYERS", "NOISE",
                                           "REVERB", "TAIL", "DAMP", "WIDE", "RATE A", "DEPTH A", "RATE B", "DEPTH B",
                                           "RATE C", "DEPTH C" };
#define N_MDEST ((int) sizeof(macro_k))
static int8_t mdest[4][4], mamt[4][4];
static const int8_t mdest_init[4][4] = { { 0, 8, 9, 18 }, { 7, 0, 19, 16 }, { 23, 25, 24, 15 }, { 21, 14, 20, 17 } };
static const int8_t mamt_init[4][4]  = { { 6, 4, 3, -5 }, { 5, -4, 5, 3 }, { 5, 6, 4, 4 }, { 6, 5, -4, 3 } };
static const char * const macro_titolo[4] = { "M1 RISE", "M2 DEEP", "M3 UNREST", "M4 HAZE" };
static int madd[N_K]; // quanto le macro aggiungono a ogni manopola (x 256), rifatto a ogni fotogramma
static int mval[4];   // valore suonato delle macro: manopola + MOD che le muovono, 0-99

// Pagine e loro manopole: SU/GIU, SX/DX, L1/R1, L2/R2. C2 con Select tenuto: 5 = MACROS, + X/Quadrato/
// Triangolo/Cerchio = SEQ, PULSE, CLOCK, EDIT
#define N_PAG 5
#define P_MACROS 5
#define P_SEQ    6
#define P_PULSE  7
#define P_CLOCK  8
#define P_EDIT   9
#define P_SCENE  10 // C2 Start tenuto
#define P_TEXTURE 7 // C1 Select + X
static const int8_t pag_k[2][9][4] = { // C1 5 = OSC B (Select tenuto), 7 = TEXTURE (Select + X)
	{ { K_CUT, K_RES, K_MODE, K_DRIVE },            // C1 a riposo = SHAPE
	  { K_SINE, K_SQUARE, K_SAW, K_SUB },           // X = OSC
	  { K_FOLD, K_NOISE, K_WIDTH, K_FEED },         // Quadrato = GRAIN
	  { K_ROOT, K_OCT, K_GLIDE, -1 },               // Triangolo = NOTE
	  { K_DETUNE, K_BELLS, K_INTERVAL, K_COLOR },   // Cerchio = LAYERS
	  { K_FM, K_AM, K_RATIO, K_PWM },               // Select = OSC B: modulazione a frequenza audio
	  { -1, -1, -1, -1 },
	  { K_THUMP, K_WIND, K_PEDAL, K_FINE },         // Select + X = TEXTURE
	  { -1, -1, -1, -1 } },
	{ { K_M1, K_REVERB, K_LAYERS, K_VOL },          // C2 a riposo = MIX
	  { MK(0, 0), MK(0, 1), MK(0, 2), MK(0, 3) },   // X = MOD A
	  { MK(1, 0), MK(1, 1), MK(1, 2), MK(1, 3) },   // Quadrato = MOD B
	  { MK(2, 0), MK(2, 1), MK(2, 2), MK(2, 3) },   // Triangolo = MOD C
	  { K_WIDE, K_TAIL, K_ROOM, K_DAMP },           // Cerchio = SPACE
	  { K_M1, K_M2, K_M3, K_M4 },                   // Select = MACROS
	  { K_STEPS, K_MUTATE, K_SCALE, K_EVERY },      // Select + X = SEQ
	  { K_HITS1, K_STEPS1, K_HITS2, K_STEPS2 },     // Select + Quadrato = PULSE
	  { K_BPM, K_RANGE, K_NUDGE, K_TAP } },         // Select + Triangolo = CLOCK
};
static const char * const pag_nome[2][9] = { { "SHAPE", "OSC", "GRAIN", "NOTE", "LAYERS", "OSC B", "", "TEXTURE", "" },
                                             { "MIX", "MOD A", "MOD B", "MOD C", "SPACE", "MACROS", "SEQ", "PULSE", "CLOCK" } };

/* PLAY: scale, SEQ (grado che suona, dal motore) e gesti del d-pad (C1, modo PLAY) */
static const int8_t scala_s[9][8] = { { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 }, { 0, 3, 5, 7, 10, -1 },
                                      { 0, 7, -1 }, { 0, -1 }, { 0, 1, 3, 5, 7, 8, 10, -1 }, { 0, 2, 4, 6, 7, 9, 11, -1 },
                                      { 0, 2, 4, 5, 7, 9, 10, -1 }, { 0, 2, 4, 5, 7, 9, 11, -1 } };
static int suona, gest[2], gest_deg, gest_oct; // PLAY acceso; SWELL e SINK (x 256); nota e ottava dei gesti
static volatile int seq_deg;                   // grado del SEQ che suona

static int scala_len(void) {
	int n = 0;
	while (n < 8 && scala_s[k[K_SCALE]][n] >= 0) n++;
	return n;
}

// Grado della scala (anche negativo o oltre l'ottava) -> semitoni dalla ROOT
static int gradi(int g) {
	int n = scala_len(), o = g >= 0 ? g / n : -((-g + n - 1) / n);
	return 12 * o + scala_s[k[K_SCALE]][g - o * n];
}

static int grado_tot(void) { return (k[K_STEPS] ? seq_deg : 0) + gest_deg; }

// MIDI della nota che suona: ROOT 0 = LA, + ottave, + SEQ e gesti sulla scala (LA0-LA4)
static int nota(void) { return clamp(21 + 12 * (k[K_OCT] + gest_oct) + k[K_ROOT] + gradi(grado_tot()), 21, 69); }
// Nota di casa (PEDAL e THUMP): ROOT + OCTAVE, ferma
static int nota_casa(void) { return 21 + 12 * k[K_OCT] + k[K_ROOT]; }
// FINE: +-50 cent, in 1/4096 (1 cent = 2,37/4096). PC: piu' il pitch bend MIDI (piega, in 1/4096 anche
// lui), moltiplicato e non sommato: a +-2 semitoni resta intonato. FINE resta il suo parametro.
static int piega;
static int fine_c(void) {
	int f = (k[K_FINE] - 50) * 2427 >> 10;
	return f + piega + f * piega / 4096;
}
static int freeze;
// PC, clock MIDI esterno: incremento di d.clk (BPM mediato x 1420, 0 = BPM proprio) e BPM da mostrare;
// Stop = SEQ e PULSE fermi; fotogrammi dall'ultimo messaggio; tick dal battito 1 (-1 = da trovare)
// e sedicesimo del battito 1
static int clk_est, clk_bpm, clk_fermo, clk_t, clk_n = -1, clk_base;

/* Motore DSP: timer 2 a 1200 Hz per sapere dove legge la SPU (stesso quarzo: 18,375 campioni a
   22050 Hz per tick). I blocchi dell'anello li chiede la voce 0 mentre suona (dsp_scrivi). */

#define TICK_HZ    1200
#define DSP_VOCE   0
#define ANELLO     0x2000 // SPU RAM
#define ANELLO_BLK 64     // blocchi da 28 campioni: 1792 campioni = 81 ms
#define V99        (99 * 256)
static int spad[84];
#define SPAD       spad // scratchpad: buffer di lavoro del blocco (84 int)

static volatile uint32_t ticks;
static void tick(void) { ticks++; }

// Generatore di movimento: fase a 32 bit; per RANDOM il valore di partenza e quello di arrivo
typedef struct { uint32_t ph; int da, a; } Gen;

static struct {
	uint32_t fa, fb, fs;  // fasi: OSC A, OSC B, SUB
	int      inc;         // incremento di OSC A (insegue la nota col glide)
	int      incp;        // PEDAL: incremento della nota di casa (ROOT + OCTAVE, senza SEQ e gesti)
	int      v[N_K];      // manopole continue x 256, lisciate (niente scatti nel suono)
	int      eff[N_K];    // valore suonato x 256: manopola + macro + movimenti
	Gen      g[3], dr[2]; // MOD A, B, C e derive (cutoff, rapporto FM)
	Gen      gfb;         // il caso che muove il VCA del feedback
	int      out[3];      // ultimo valore dei generatori, -32767..32767
	int      drv;         // deriva del cutoff, -32767..32767 (per la schermata)
	int      dep[3];      // profondita' suonata dei generatori x 256 (DEPTH + macro + altri MOD)
	uint32_t clk;         // CLOCK: frazione di sedicesimo (1 << 24 = un sedicesimo)
	int      s16;         // sedicesimi contati
	int      spos;        // passo del SEQ
	uint8_t  sreg[8];     // registro del SEQ (valori 0-255 -> gradi della scala)
	int      penv[3];     // inviluppo PULSE di ogni MOD, 0-32767
	int      lampo_p[2];  // per la schermata: sedicesimo dell'ultimo impulso di PULSE 1 e 2
	uint32_t rum;         // rumore (xorshift) del NOISE nel filtro
	int      ic1, ic2;    // stato del filtro
	int16_t  fbuf[1024];  // ritardo del feedback (un periodo della nota, fino a 1023 campioni)
	uint32_t fw;          // dove si scrive nel ritardo
	int      s1, s2;      // ultimi 2 campioni decodificati: lo stato ADPCM della SPU
	int      gain;        // volume a fine blocco
	int      picco;       // picco dell'uscita del motore (ricade piano), per il meter del MIX
} d;

// Glide 1-8: costante di tempo 1-8 s, coefficiente su 65536 per blocco (787,5 blocchi al secondo)
static const uint8_t glide_coef[9] = { 0, 83, 42, 28, 21, 17, 14, 12, 10 };
// Velocita' dei generatori moltiplicate per numeri irrazionali (in 1/4096): 1, pi/4, 1/phi.
// Con la stessa manopola RATE non si riallineano mai.
static const uint16_t gen_molt[3] = { 4096, 3217, 2531 };
// Derive: un valore a caso ogni ~4 e ~7 minuti, raggiunto scivolando; VCA del feedback ~0,3 Hz
static const uint32_t dr_inc[2] = { 22722, 12984 };
#define FB_INC 1636000

// Avanza il generatore e restituisce il valore (-32767..32767) della forma f
static int gen(Gen *g, uint32_t inc, int f) {
	uint32_t prima = g->ph;
	g->ph += inc;
	if (g->ph < prima) { // fine del giro: RANDOM sceglie il prossimo valore
		g->da = g->a;
		g->a  = (int) (caso() >> 16) - 32768;
	}
	int t = g->ph >> 16, u = t >> 1; // 0-65535, 0-32767
	switch (f) {
	case 4:  return (int) (caso() >> 16) - 32768; // NOISE: un valore nuovo a ogni blocco (787 Hz)
	case 0:  return seno[g->ph >> 24];
	case 1:  return t < 32768 ? t * 2 - 32767 : 32767 - (t - 32768) * 2;
	case 2:  // BREATH: sale per 2/3 del giro partendo piano, scende nell'ultimo terzo
		if (t < 43690) { int r = t * 3 >> 2; return (r * r >> 15) - 32767; }           // r 0-32767
		else           { int q = 32767 - ((t - 43690) * 3 >> 1); return (q * q >> 15) - 32767; }
	default: { // RANDOM: dal valore di prima a quello nuovo con una curva morbida (smoothstep)
		int s = (u * u >> 15) * (3 * 32768 - 2 * u) >> 16; // 0-32767
		return g->da + ((g->a - g->da) * s >> 15);
	}
	}
}

static void macro_somma(void) {
	for (int i = 0; i < N_K; i++)
		madd[i] = 0;
	for (int m = 0; m < 4; m++) {
		int v = (k[K_M1 + m] << 8) + (m < 2 ? gest[m] : 0); // PLAY: SWELL spinge MACRO 1, SINK MACRO 2
		for (int j = 0; j < 3; j++)
			if (k[MK(j, M_DEST)] == D_MACRO + m) v += d.out[j] * (d.dep[j] >> 8) >> 7;
		mval[m] = clamp(v, 0, V99) >> 8;
		for (int s = 0; s < 4; s++)
			madd[macro_k[mdest[m][s]]] += mval[m] * mamt[m][s] * 256 / 9;
	}
}

// Valore suonato di una manopola letto dal main loop (per la SPU e la schermata): manopola +
// macro + movimenti, 0..V99
static int pan_mod; // somma dei generatori su PAN, -V99..V99
static int suonato(int i) {
	int e = (k[i] << 8) + madd[i];
	for (int j = 0; j < 3; j++) {
		int de = k[MK(j, M_DEST)];
		if (de == D_LAYERS && i == K_LAYERS)
			e += d.out[j] * (d.dep[j] >> 8) >> 7;
	}
	return clamp(e, 0, V99);
}

// Blocco ADPCM (di nucleo): tra i filtri 1-3 della SPU si sceglie quello col residuo massimo piu'
// piccolo, poi la codifica segue la decodifica della SPU (closed loop): l'errore non si somma.
// La scelta del filtro (3 passate) si fa solo se scegli = 1, cioe' sul primo blocco di ogni gruppo
// di 4: un drone cambia piano, e gli altri 3 blocchi riusano lo stesso filtro con una passata sola
// (~40 cicli per campione in meno).
static const int8_t adpcm_pos[4] = { 0, 60, 115, 98 }, adpcm_neg[4] = { 0, 0, -52, -55 }; // in 1/64

static void adpcm(const int *x, uint8_t *out, int flag, int scegli) {
	static int fb = 2;
	int f0 = scegli ? 1 : fb, f1 = scegli ? 4 : fb + 1, mb = 0x7fffffff;
	for (int f = f0; f < f1; f++) {
		int mx = 0, a = d.s1, b = d.s2;
		for (int i = 0; i < 28; i++) {
			int r = x[i] - ((a * adpcm_pos[f] + b * adpcm_neg[f]) >> 6);
			if (r < 0) r = -r;
			if (r > mx) mx = r;
			b = a;
			a = x[i];
		}
		if (mx < mb) { mb = mx; fb = f; }
	}
	int e = 0;
	while (e < 12 && (mb >> e) > 7)
		e++;
	out[0] = (12 - e) | (fb << 4); // shift, filtro
	out[1] = flag;
	int fp = adpcm_pos[fb], fn = adpcm_neg[fb];
	for (int i = 0; i < 28; i++) {
		int pred = (d.s1 * fp + d.s2 * fn + 32) >> 6;
		int nib  = clamp(e ? (x[i] - pred + (1 << (e - 1))) >> e : x[i] - pred, -8, 7);
		d.s2 = d.s1;
		d.s1 = clamp((nib << e) + pred, -32768, 32767);
		if (i & 1) out[2 + i / 2] |= (nib & 15) << 4;
		else       out[2 + i / 2]  = nib & 15;
	}
}

// 28 campioni. Le manopole (+ macro) inseguono il valore (1/8 della strada per blocco, ~10 ms);
// i generatori sommano +-DEPTH alla loro destinazione; poi:
// OSC A = fase + FM (seno di OSC B x indice), 3 forme sulla stessa fase, mix normalizzato
// (somma dei livelli oltre 99 = si divide), fold a triangolo, + SUB, + FEEDBACK, DRIVE, filtro,
// volume.
static void dsp_blocco(uint8_t *out, int flag, int scegli) {
	int *v = d.v, *e = d.eff;
	for (int j = 0; j < (int) sizeof(lisce); j++) {
		int i = lisce[j], dv = (k[i] << 8) + madd[i] - v[i];
		v[i] += dv / 8 + (dv > 0) - (dv < 0);
		e[i] = v[i];
	}
	// MOTION: valori dei generatori sommati alle destinazioni (+-DEPTH su 0-99)
	// CLOCK (sedicesimi a BPM): PULSE 1 e 2 = ritmi euclidei E(HITS, STEPS); SEQ = un passo ogni
	// EVERY battiti, che con probabilita' MUTATE cambia a caso (registro che muta, come nucleo)
	int trig = 0;
	if (!freeze && !clk_fermo) { // PC: clock esterno (MIDI) al posto del BPM proprio; Stop lo ferma
		d.clk += clk_est ? clk_est : (k[K_BPM] + 40) * 1420; // BPM x 4 / 60 / 787,5 blocchi al secondo, in 1/2^24
		if (d.clk >= 1u << 24) {
			d.clk -= 1u << 24;
			int sd = d.s16++;
			for (int pp = 0; pp < 2; pp++) {
				int n = k[K_STEPS1 + 2 * pp], hk = k[K_HITS1 + 2 * pp];
				if (n && (sd % n) * hk % n < hk) { trig |= 1 << pp; d.lampo_p[pp] = sd; }
			}
			int len = k[K_STEPS] ? k[K_STEPS] + 1 : 0;
			if (len && sd % (4 * ogni[k[K_EVERY]]) == 0) {
				d.spos = (d.spos + 1) % len;
				if ((int) (caso() % 100) < k[K_MUTATE]) d.sreg[d.spos] = caso() >> 24;
				seq_deg = d.sreg[d.spos] * scala_len() * (k[K_RANGE] + 1) >> 8;
			}
		}
	}

	int pitch = 0, *dep = d.dep;
	for (int j = 0; j < 3; j++) {
		int rate = k[MK(j, M_RATE)] + (madd[MK(j, M_RATE)] >> 8), w = k[MK(j, M_WAVE)];
		for (int x = 0; x < 3; x++) // un altro MOD su RATE (uscita e profondita' del blocco prima)
			if (k[MK(x, M_DEST)] == D_RATE + j) rate += d.out[x] * (d.dep[x] >> 8) >> 15;
		rate = clamp(rate, 0, 99);
		uint32_t inc = freeze ? 0 : (inc_mod[rate] >> 12) * gen_molt[j];
		if (w >= 5) { // PULSE1/2: un colpo a ogni impulso, poi ricade (RATE = quanto dura)
			if (trig & 1 << (w - 5)) d.penv[j] = 32767;
			else                     d.penv[j] -= d.penv[j] * dec_pulse[rate] >> 12;
			d.out[j] = d.penv[j];
		} else
			d.out[j] = freeze && w == 4 ? d.out[j] : gen(&d.g[j], inc, w);
		dep[j]   = v[MK(j, M_DEPTH)];
	}
	for (int j = 0; j < 3; j++) { // un MOD su DEPTH di un altro: la modulazione passa da un VCA mosso
		int de = k[MK(j, M_DEST)];
		if (de >= D_DEPTH && de < D_MACRO) dep[de - D_DEPTH] += d.out[j] * (v[MK(j, M_DEPTH)] >> 8) >> 7;
	}
	for (int j = 0; j < 3; j++) {
		dep[j] = clamp(dep[j], 0, V99);
		int m  = d.out[j] * (dep[j] >> 8) >> 7; // +-DEPTH x 256 (32767 x 99 >> 7 = 25343)
		int de = k[MK(j, M_DEST)];
		if (de < N_DK) e[dest_k[de]] += m;
		else if (de == D_PITCH) pitch += m;
	}
	int dr0 = gen(&d.dr[0], freeze ? 0 : dr_inc[0], 3), dr1 = gen(&d.dr[1], freeze ? 0 : dr_inc[1], 3);
	e[K_CUT] += dr0 >> 4; // deriva del cutoff: +-8 su 99
	d.drv = dr0;
	for (int j = 0; j < (int) sizeof(lisce); j++)
		e[lisce[j]] = clamp(e[lisce[j]], 0, V99);

	// Pitch: glide verso la nota, poi PITCH dai generatori (+-2 semitoni a DEPTH 99)
	int dest = inc_nota[nota() - NOTA0];
	if (k[K_GLIDE]) d.inc += ((dest - d.inc) >> 8) * glide_coef[k[K_GLIDE]] >> 8;
	if (!k[K_GLIDE] || (dest - d.inc) >> 8 == 0) d.inc = dest;
	int destp = inc_nota[nota_casa() - NOTA0];
	if (k[K_GLIDE]) d.incp += ((destp - d.incp) >> 8) * glide_coef[k[K_GLIDE]] >> 8;
	if (!k[K_GLIDE] || (destp - d.incp) >> 8 == 0) d.incp = destp;
	int fc = fine_c();
	int inca = d.inc + (d.inc >> 12) * (pitch * 5 >> 8) + (d.inc >> 12) * fc;
	int incb = inca / rap[k[K_RATIO]][1] * rap[k[K_RATIO]][0];
	incb += (incb >> 12) * (dr1 * 41 >> 15); // deriva del rapporto FM: +-1%
	int incs = (k[K_PEDAL] & 1 ? d.incp + (d.incp >> 12) * fc : inca) / 2; // PEDAL SUB: il SUB resta sulla nota di casa

	// Divisioni per costanti fatte con moltiplicazione e shift (x / V99 = x * 0,0000395)
	int div = e[K_SINE] + e[K_SQUARE] + e[K_SAW];
	int rd  = (1 << 24) / (div < V99 ? V99 : div);
	int cs = e[K_SINE] * rd >> 12, cq = e[K_SQUARE] * rd >> 12, cw = e[K_SAW] * rd >> 12; // in 1/4096
	int csub = e[K_SUB] * 41 >> 8;                              // 0-4060
	int g    = 4096 + (e[K_FOLD] * 207 >> 8);                   // guadagno del fold: 1x-6x (in 1/4096)
	uint32_t fmk = (uint32_t) (e[K_FM] * e[K_FM]) >> 12;         // fino a ~1,2 giri di fase (7,5 rad)
	uint32_t thr = 0x80000000u - (uint32_t) e[K_WIDTH] * 79652u; // larghezza: 50% -> 3%

	// FEEDBACK: ritardo = un periodo di OSC A (anello accordato sulla nota); guadagno fino a 0,95 x
	// 2 (il filtro lavora a meta' livello) x VCA mosso dal caso (0,5-1)
	int del = clamp(0xffffffffu / (uint32_t) inca, 28, 1023);
	int vca = 24576 + (gen(&d.gfb, freeze ? 0 : FB_INC, 3) >> 2);         // 0,5-1 in 1/32768
	int fbg = (e[K_FEED] * 157 >> 9) * vca >> 15;                          // 0-7772 in 1/4096

	// DRIVE: guadagno 0,5x-10x prima di una saturazione morbida (cubica)
	int gd = 2048 + (e[K_DRIVE] * e[K_DRIVE] >> 14);
	// Filtro state variable "a ritardo zero" di nucleo: g da tabella (interpolata), smorzamento
	// k = 1/Q da 1,41 (RES 0) a 0,05 (RES 99)
	int gq = e[K_CUT] * 41 >> 8, gi = gq >> 4;               // indice in 1/16, 0-4058
	int gf = svf_g[gi] + ((svf_g[gi < 255 ? gi + 1 : 255] - svf_g[gi]) * (gq & 15) >> 4);
	int kq = 5793 - (e[K_RES] * 56 >> 8);
	int a1 = (4096 << 12) / (4096 + (gf * (gf + kq) >> 12));
	int a2 = gf * a1 >> 12, a3 = gf * a2 >> 12, md = k[K_MODE];

	// Tutti i guadagni lineari dopo la saturazione in uno solo, a fine catena (il filtro e'
	// lineare): volume x 1,5 (in DuckStation il picco era basso) x 2 (il filtro riceve meta'
	// livello: con la risonanza alta lo stato non trabocca) x guadagno reso dal DRIVE sotto 1x
	// (a DRIVE basso il livello resta uguale) x compensazione della risonanza (il picco del filtro
	// vale ~1/k: con RES alta si abbassa, come all'ingresso di un filtro analogico).
	// In 1/4 per non traboccare nella moltiplicazione per campione.
	int mk   = (4096 << 12) / (gd < 4096 ? gd : 4096);         // 1x-2x in 1/4096
	int comp = (kq + 1024) * 4096 / (5793 + 1024);              // 1x-0,18x in 1/4096
	int fine = (e[K_VOL] * 331 >> 8) * 3 * (mk >> 4) / 256 * (comp >> 4) / 256 >> 2;
	int dg   = (fine - d.gain) / 28;
	// OSC B a frequenza audio: AM sul volume finale (99 = da pieno a zero a ogni giro di OSC B),
	// PWM sulla larghezza (+-40%), NOISE prima del DRIVE e del filtro
	int cam = e[K_AM] * 331 >> 8, cpw = e[K_PWM] * 263 >> 8, cno = e[K_NOISE] * 41 >> 8; // 0-32767, 0-26000, 0-4060
	int sq = cq != 0, sw = cw != 0, sf = fbg != 0, sa = cam != 0, sp = cpw != 0 && sq, sn = cno != 0;
	int duty = thr >> 16; // larghezza in 1/65536 di giro

	// Il blocco si calcola in passate corte invece che in un ciclo solo: con tutto in un ciclo i
	// valori vivi erano ~35, piu' dei registri della CPU, e il compilatore li teneva in RAM (la PS1
	// non ha cache dei dati); in piu' il ciclo non stava nella cache istruzioni da 4 KB. Misurato:
	// 685 cicli per campione invece di ~480. Ogni passata ha pochi valori vivi e i buffer da 28
	// campioni stanno nello scratchpad (1 KB di RAM veloce dentro la CPU, 0x1F800000).
	uint32_t *ph = (uint32_t *) SPAD; // fase di OSC A dopo la FM
	int *mbv = SPAD + 28, *y = SPAD + 56;

	// 1: fasi, OSC B, FM e SUB
	uint32_t fa = d.fa, fbp = d.fb, fs = d.fs;
	for (int i = 0; i < 28; i++) {
		fa += inca;
		fbp += incb;
		fs += incs;
		int mb = seno[fbp >> 24];                                // OSC B
		mbv[i] = mb;
		ph[i]  = fa + (uint32_t) mb * fmk;                       // FM: modulo 2^32, la fase gira da sola
		y[i]   = (seno[fs >> 24] >> 1) * csub >> 12;             // SUB
	}
	d.fa = fa; d.fb = fbp; d.fs = fs;

	// 2: forme sulla stessa fase (PWM: OSC B muove la larghezza), fold
	for (int i = 0; i < 28; i++) {
		uint32_t p = ph[i];
		int a = (seno[p >> 24] >> 1) * cs;
		if (sp) a += ((int) (p >> 16) < clamp(duty + (mbv[i] * cpw >> 15), 2000, 63535) ? 16383 : -16384) * cq;
		else if (sq) a += (p < thr ? 16383 : -16384) * cq;
		if (sw) a += ((int) (p >> 17) - 16384) * cw;
		int f = (a >> 12) * g >> 12;                             // fold: piega dentro +-16384
		int r = (f + 16384) & 0xffff;
		y[i] += r < 32768 ? r - 16384 : 49152 - r;
	}

	// 3: NOISE (rumore bianco nel DRIVE e nel filtro) e FEEDBACK (il ritardo e' di almeno 28
	// campioni: si leggono solo campioni dei blocchi prima)
	if (sn) {
		uint32_t rum = d.rum;
		for (int i = 0; i < 28; i++) {
			rum ^= rum << 13;
			rum ^= rum >> 17;
			rum ^= rum << 5;
			y[i] += ((int) (rum >> 16) - 32768) * cno >> 13;
		}
		d.rum = rum;
	}
	if (sf)
		for (int i = 0; i < 28; i++)
			y[i] += d.fbuf[(d.fw + i - del) & 1023] * fbg >> 12;

	// 4: DRIVE (3x - x^3) / 2 a meta' livello, filtro, AM (OSC B sul volume), volume
	int ic1 = d.ic1, ic2 = d.ic2;
	for (int i = 0; i < 28; i++) {
		int s = clamp(y[i] * gd >> 12, -32767, 32767);
		s = (3 * s - ((s * s >> 15) * s >> 15)) >> 2;
		int v3 = s - ic2;
		int v1 = (a1 * ic1 + a2 * v3) >> 12;
		int v2 = ic2 + ((a2 * ic1 + a3 * v3) >> 12);
		ic1 = 2 * v1 - ic1;
		ic2 = 2 * v2 - ic2;
		y[i] = clamp(md == 0 ? v2 : md == 1 ? v1 : s - (kq * v1 >> 12) - v2, -32767, 32767);
	}
	d.ic1 = ic1; d.ic2 = ic2;
	if (sa)
		for (int i = 0; i < 28; i++)
			y[i] = y[i] * (32768 - (cam * (32767 - mbv[i]) >> 16)) >> 15;
	if (sf) { // il ritardo tiene il suono di quando il feedback era acceso l'ultima volta
		for (int i = 0; i < 28; i++)
			d.fbuf[(d.fw + i) & 1023] = y[i];
		d.fw += 28;
	}
	int gain = d.gain, pk = 0;
	for (int i = 0; i < 28; i++) {
		gain += dg;
		int o = clamp(y[i] * gain >> 13, -32768, 32767);
		y[i] = o;
		if (o > pk) pk = o; else if (-o > pk) pk = -o;
	}
	d.picco = pk > d.picco ? pk : d.picco - (d.picco >> 6);
	d.gain = fine;
	adpcm(y, out, flag, scegli);
}

// PC: la voce 0 sta per leggere il blocco ind dell'anello (unita' da 8 byte): lo si calcola adesso,
// con gli stessi flag e la stessa scelta del filtro a gruppi di 4 della PS1. Niente ritardo, niente buchi.
static void dsp_scrivi(int voce, uint32_t ind) {
	static uint8_t buf[16] __attribute__((aligned(4)));
	int b = ((int) ind * 8 - ANELLO) / 16;
	if (voce != DSP_VOCE || b < 0 || b >= ANELLO_BLK) return;
	dsp_blocco(buf, b == 0 ? 0x04 : b == ANELLO_BLK - 1 ? 0x03 : 0, (b & 3) == 0);
	SpuSetTransferStartAddr(ANELLO + b * 16);
	SpuWrite((const uint32_t *) buf, 16);
}

static void dsp_accendi(void) {
	static uint8_t vuoto[ANELLO_BLK * 16] __attribute__((aligned(4)));
	for (int b = 0; b < ANELLO_BLK; b++) // silenzio, con i flag del loop
		vuoto[b * 16 + 1] = b == 0 ? 0x04 : b == ANELLO_BLK - 1 ? 0x03 : 0;
	SpuSetTransferStartAddr(ANELLO);
	SpuWrite((const uint32_t *) vuoto, sizeof(vuoto));
	SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
	SPU_CH_ADDR(DSP_VOCE)  = ANELLO / 8;
	SPU_CH_FREQ(DSP_VOCE)  = 0x800; // 22050 Hz
	SPU_CH_VOL_L(DSP_VOCE) = 0x3fff;
	SPU_CH_VOL_R(DSP_VOCE) = 0x3fff;
	for (int i = 0; i < N_K; i++)
		d.v[i] = k[i] << 8;
	for (int j = 0; j < 3; j++)
		d.g[j].a = (int) (caso() >> 16) - 32768;
	for (int i = 0; i < 8; i++)
		d.sreg[i] = caso() >> 24;
	d.inc = inc_nota[nota() - NOTA0];
	d.incp = inc_nota[nota_casa() - NOTA0];
	d.rum = 1;
	spu_prima_del_blocco = dsp_scrivi;
}

/* Strati, rumore, stereo e riverbero sulla SPU: aggiornati una volta per fotogramma */

#define ONDA(off, i) ((SUONI + (off) + (i) * ONDA_LEN) / 8)
static Gen lgen[4]; // passeggiata a caso lenta del pitch di ogni voce LAYERS (pochi cent)

static void spu_voci(void) {
	for (int v = 1; v <= 8; v++) {
		SPU_CH_ADDR(v)  = v & 1 ? ONDA(OFF_CAMPANE, 0) : ONDA(OFF_STRATI, 1);
		SPU_CH_FREQ(v)  = 0x1000;
		SPU_CH_VOL_L(v) = 0;
		SPU_CH_VOL_R(v) = 0;
	}
	for (int v = 0; v <= 10; v++) {
		SPU_CH_ADSR1(v) = 0x000f;                  // attacco istantaneo, sustain al massimo
		SPU_CH_ADSR2(v) = (1 << 14) | (0x7f << 6); // sustain fermo
	}
	// THUMP (voci 9 e 11, a turno): il seno piu' forte, attacco istantaneo, poi cala fino a zero:
	// -8 dB in ~45 ms (la botta), -40 dB a ~0,4 s, zero a ~0,41 s. Sulla PS1 il colpo dura ~40 ms
	// (il seno suona due cicli e si ferma); qui suona tutto il ciclo e si allunga.
	for (int v = 9; v <= 11; v += 2) {
		SPU_CH_ADDR(v)  = ONDA(OFF_CAMPANE, 7);
		SPU_CH_ADSR1(v) = 0x00a5;               // decadimento esponenziale (shift 10) fino a 3/8
		SPU_CH_ADSR2(v) = 0xc000 | (0x31 << 6)  // poi esponenziale piu' lento fino a zero
		                | 0x20 | 6;             // rilascio esponenziale veloce (~10 ms): il colpo dopo lo chiude
	}
	// WIND (voce 10): il generatore di rumore della SPU (il campione serve solo a tenere la voce accesa)
	SPU_CH_ADDR(10) = ONDA(OFF_STRATI, 1);
	SPU_CH_FREQ(10) = 0x1000;
	SPU_CH_VOL_L(9) = SPU_CH_VOL_R(9) = SPU_CH_VOL_L(11) = SPU_CH_VOL_R(11) = SPU_CH_VOL_L(10) = SPU_CH_VOL_R(10) = 0;
	SPU_NOISE_MODE1 = 1 << 10;
	SPU_FM_MODE1    = 0x0154;         // voci 2, 4, 6, 8 modulate dalla voce prima
	SPU_REVERB_ON1  = 0x0555;         // DSP, strati e vento nel riverbero (i modulatori e il THUMP no)
	SpuSetKey(1, 0x5ff);              // tutte insieme
}

// Riverbero: preset di psx-spx (registri 0x1F801DC0-0x1F801DFF, indirizzi in unita' da 8 byte),
// come nucleo. TAIL = vWALL (registro 7, il riflesso che rientra), DAMP = vIIR (registro 2, il
// filtro passa-basso della coda: piu' basso = coda piu' scura).
static const uint16_t rev[5][32] = {
	{ // Room
	0x007D,0x005B,0x6D80,0x54B8,0xBED0,0x0000,0x0000,0xBA80,0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
	0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C,0x8000,0x8000 },
	{ // Studio Medium
	0x00B1,0x007F,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0xB4C0,0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
	0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7,0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080,0x8000,0x8000 },
	{ // Hall
	0x01A5,0x0139,0x6000,0x5000,0x4C00,0xB800,0xBC00,0xC000,0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
	0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD,0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A,0x8000,0x8000 },
	{ // Half Echo
	0x0017,0x0013,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0x8500,0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
	0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059,0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014,0x8000,0x8000 },
	{ // Space Echo
	0x033D,0x0231,0x7E00,0x5000,0xB400,0xB000,0x4C00,0xB000,0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC3,0x16B2,
	0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D,0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232,0x8000,0x8000 },
};
static const uint32_t rev_size[5] = { 0x26C0, 0x4840, 0xADE0, 0x3C00, 0xF6C0 };
#define REV_FONDO 0x70000
#define REV_REG   (&SPU_R(0x1dc0))

// Cambio di ROOM: niente azzeramento dell'area (la DMA di 64 KB farebbe un buco nel motore DSP):
// la coda vecchia letta coi tempi nuovi e' solo suono passato che sfuma.
static void set_rev(int r) {
	SPU_CTRL &= ~0x0080; // riverbero fermo mentre cambio i registri
	SPU_REVERB_ADDR = (0x80000 - rev_size[r]) / 8;
	for (int i = 0; i < 32; i++)
		REV_REG[i] = rev[r][i];
	SPU_CTRL |= 0x0080;
}

static int lay_x[4], lay_c[4], lay_liv, lay_amp; // per la schermata: pan (-8192..8192), cent, livello, BELLS

static int16_t vol15(int v) { return clamp(v, -0x4000, 0x3fff) & 0x7fff; } // volume fisso a 15 bit con segno

static void spu_aggiorna(void) {
	static int room_prima = -1;
	if (k[K_ROOM] != room_prima) set_rev(room_prima = k[K_ROOM]);
	int rv = suonato(K_REVERB) * 93 >> 8; // 0-0x2400 (0x3000 con space echo saturava, bordone)
	SPU_REVERB_VOL_L = rv;
	SPU_REVERB_VOL_R = rv;
	// TAIL 99 = HOLD: niente piu' suono in ingresso, riflessi al massimo e niente filtro: la coda resta
	int hold = k[K_TAIL] == 99;
	REV_REG[7]  = hold ? 0x7fff : (int16_t) rev[k[K_ROOM]][7] * (suonato(K_TAIL) >> 8) / 99;
	REV_REG[2]  = hold ? 0x7fff : (int16_t) rev[k[K_ROOM]][2] * (4096 - (suonato(K_DAMP) * 31 >> 8)) >> 12; // fino a 1/4
	REV_REG[30] = hold ? 0 : rev[k[K_ROOM]][30];
	REV_REG[31] = hold ? 0 : rev[k[K_ROOM]][31];

	// THUMP: un colpo basso (la nota di casa portata tra FA1 e MI2, 44-82 Hz) a ogni colpo di PULSE 1. Semplificazione: si
	// guarda una volta per fotogramma, il colpo arriva fino a 20 ms dopo l'impulso; se serve a tempo
	// stretto, key-on dal motore DSP.
	static int thump_prima;
	if (d.lampo_p[0] != thump_prima) {
		thump_prima = d.lampo_p[0];
		if (k[K_THUMP]) {
			int n = nota_casa();
			while (n > 40) n -= 12;
			while (n < 29) n += 12;
			int t = (inc_nota[n - NOTA0] >> 6) * 224 >> 16; // la PS1 usa 229: ~37 cent crescente
			// due voci a turno: il key-on riparte da zero, quindi il colpo prima non si taglia ma
			// sfuma col rilascio mentre il nuovo parte sull'altra voce (niente scatti coi colpi fitti)
			static int tv = 9;
			SpuSetKey(0, 1 << tv);
			tv ^= 9 ^ 11;
			SPU_CH_FREQ(tv)  = t * (4096 + fine_c()) >> 12;
			SPU_CH_VOL_L(tv) = SPU_CH_VOL_R(tv) = k[K_THUMP] * 0x3fff / 99;
			SPU_CH_LOOP_ADDR(tv) = ONDA(OFF_CAMPANE, 7) + 8; // il ciclo si ripete (+64 byte, come le campane)
			SpuSetKey(1, 1 << tv);
		}
	}
	// WIND: rumore con raffiche (passeggiata a caso del livello) e fischio (clock del rumore che sale
	// con la raffica); un po' piu' a sinistra o a destra secondo un'altra passeggiata
	static Gen raffica, lato;
	int ra = gen(&raffica, freeze ? 0 : 1800000, 3), la = gen(&lato, freeze ? 0 : 700000, 3);
	int wv = k[K_WIND] * (20000 + (ra >> 1)) / 99 >> 1;       // 0-~18000
	SPU_CH_VOL_L(10) = vol15(wv * (32768 + (la >> 1)) >> 16);
	SPU_CH_VOL_R(10) = vol15(wv * (32768 - (la >> 1)) >> 16);
	SPU_CTRL = (SPU_CTRL & ~0x3f00) | clamp(0x24 + (ra >> 12), 0x18, 0x3f) << 8;

	// LAYERS: un'ottava sopra la nota di OSC A (glide compreso), 2 voci sulla nota e 2
	// sull'intervallo; stonate di +-DETUNE (fino a ~30 cent) e di una passeggiata a caso ciascuna.
	// Pitch SPU: 0x1000 = 393,75 Hz (ciclo da 112 campioni); oltre 0x3FFF si scende di ottava.
	// BELLS: il modulatore (voce prima) suona a x1,414 col seno dell'ampiezza scelta.
	// CHORD: semitoni delle 4 voci; SCALE = triade della scala sul grado che suona (segue SEQ e gesti)
	static const int8_t accordo[8][4] = { { 0, 0, 0, 0 }, { 0, 0, 7, 7 }, { 0, 0, 12, 12 }, { 0, 0, 24, 24 },
	                                      { 0, 3, 7, 12 }, { 0, 4, 7, 12 }, { 0, 5, 7, 12 }, { 0, 3, 7, 10 } };
	static const int16_t semi[25] = { 4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732, 8192,
	                                  8679, 9195, 9742, 10321, 10935, 11585, 12274, 13004, 13777, 14596, 15464, 16384 };
	static const int16_t segno[4] = { -10, 6, -4, 10 };
	int ch[4];
	if (k[K_INTERVAL] == 8) {
		int g = k[K_PEDAL] & 2 ? 0 : grado_tot(), b = gradi(g);
		ch[0] = 0; ch[1] = gradi(g + 2) - b; ch[2] = gradi(g + 4) - b; ch[3] = 12;
	} else
		for (int j = 0; j < 4; j++) ch[j] = accordo[k[K_INTERVAL]][j];
	static const int16_t pos[4] = { -4096, 4096, -1365, 1365 };
	// pitch della nota + un'ottava (PEDAL: nota di casa). Pitch = inc x 22050 / 2^32 / 393,75 x 2 x 4096,
	// cioe' (inc >> 6) x 224 >> 15. La PS1 usa 229 (strati ~37 cent crescenti): qui e' corretto.
	int base = ((k[K_PEDAL] & 2 ? d.incp : d.inc) >> 6) * 224 >> 15;
	base = base * (4096 + fine_c()) >> 12;
	int det = suonato(K_DETUNE) * 30 / V99;               // 0-30 cent (prima x3: a DETUNE 30 erano 90 cent)
	int amp = suonato(K_BELLS) * 7 / V99;                 // seno 0-7
	int liv = suonato(K_LAYERS) * 0x1000 / V99;           // per voce: 4 voci = 0x4000
	lay_liv = liv;
	lay_amp = amp;
	int w = suonato(K_WIDE), spread = w > 128 * 99 ? 4096 : w * 4096 / (128 * 99), inv = w > 128 * 99 ? (w - 128 * 99) * 4096 / (128 * 99) : 0;
	int pm = 0;
	for (int j = 0; j < 3; j++)
		if (k[MK(j, M_DEST)] == D_PAN)
			pm += d.out[j] * (d.dep[j] >> 8) >> 7;
	pan_mod = clamp(pm, -V99, V99);
	for (int j = 0; j < 4; j++) {
		// cent: DETUNE + passeggiata a caso di +-4 cent, un valore nuovo ogni ~20-35 s per voce
		int c = det * segno[j] / 10 + (gen(&lgen[j], freeze ? 0 : 2400000 + j * 377000, 3) * 4 >> 15);
		int p = (base * semi[clamp(ch[j], 0, 24)] >> 12) * (4096 + (c * 2427 >> 10)) >> 12;
		while (p > 0x3fff) p >>= 1;
		int q = p * 5793 >> 12;
		while (q > 0x3fff) q >>= 1;
		int vc = 2 + 2 * j, vm = vc - 1;
		SPU_CH_FREQ(vc) = p;
		SPU_CH_FREQ(vm) = q;
		SPU_CH_LOOP_ADDR(vc) = ONDA(OFF_STRATI, k[K_COLOR]) + 8; // +64 byte: il 2deg ciclo
		SPU_CH_LOOP_ADDR(vm) = ONDA(OFF_CAMPANE, amp) + 8;
		// pan: posizione x apertura + PAN dai generatori (in verso opposto a voci alterne);
		// oltre WIDE 50 il lato lontano va in fase invertita (stereo oltre le casse)
		int x = clamp((pos[j] * spread >> 12) + ((j & 1 ? pan_mod : -pan_mod) * 4096 / V99), -4096, 4096);
		int xa = x * (4096 + inv) >> 12;
		lay_x[j] = xa;
		lay_c[j] = c;
		SPU_CH_VOL_L(vc) = vol15(liv * (4096 - xa) >> 13);
		SPU_CH_VOL_R(vc) = vol15(liv * (4096 + xa) >> 13);
	}

}

static void reset(void) {
	for (int i = 0; i < N_K; i++)
		k[i] = k_init[i];
	for (int m = 0; m < 4; m++)
		for (int s = 0; s < 4; s++) {
			mdest[m][s] = mdest_init[m][s];
			mamt[m][s]  = mamt_init[m][s];
		}
	freeze = fissa[0] = fissa[1] = 0;
}

/* Schermata (fino alla tappa d: solo testo) */

// Simboli dei tasti nel font (codici 0x80-0x85, vedi script/prepara.py)
#define SU_GIU "\x80"
#define SX_DX  "\x81"
#define CROCE  "\x82"
#define QUADR  "\x83"
#define TRIAN  "\x84"
#define CERCH  "\x85"

// Prossima primitiva libera, appesa al livello z dell'OT; 0 se il buffer e' pieno (si salta il
// disegno: in Release assert e' spento)
static void *prim(int z, int size) {
	uint8_t *p = nextp;
	if (p + size > &fb[cur].buf[BUF_LEN]) return 0;
	addPrim(&fb[cur].ot[z], p);
	nextp += size;
	return p;
}

static int indice(char ch) { // carattere -> cella del font
	int c = (uint8_t) ch;
	return c >= 32 && c <= 126 ? c - 32 : c >= 0x80 && c <= 0x85 ? c - 0x80 + 96 : '?' - 32;
}

static int largo(const char *s) { // larghezza in pixel
	int w = 0;
	for (; *s; s++) w += font_w[indice(*s)];
	return w;
}

// Un carattere: POLY_FT4 col font (bianco = 128). 15: u + 16 andrebbe oltre 255 (colonna 15 vuota).
static POLY_FT4 *glifo(int c, int r, int g, int b) {
	POLY_FT4 *p = prim(Z_TESTO, sizeof(POLY_FT4));
	if (!p) return 0;
	int u = (c % 16) * 16, v = (c / 16) * 16;
	setPolyFT4(p);
	setUVWH(p, u, v, 15, 16);
	setRGB0(p, r, g, b);
	p->tpage = getTPage(0, 0, FONT_X, FONT_Y); // 0 = 4 bit
	p->clut  = getClut(CLUT_X, CLUT_Y);
	return p;
}

// Testo 2D; restituisce la x dopo l'ultimo carattere
static int testo(int x, int y, const char *s, int r, int g, int b) {
	for (; *s; s++) {
		int c = indice(*s);
		POLY_FT4 *p;
		if (c && (p = glifo(c, r, g, b))) setXYWH(p, x, y, 15, 16);
		x += font_w[c];
	}
	return x;
}

// Riga dell'aiuto: parole dei tasti in giallo (simboli, L1R1/L2R2, SELECT, START), il resto chiaro
static void riga_aiuto(int x, int y, const char *s) {
	int giallo = 0;
	for (const char *c = s; *c; c++) {
		if (c == s || c[-1] == ' ')
			giallo = (uint8_t) *c >= 0x80 || ((c[0] == 'L' || c[0] == 'R') && (c[1] == '1' || c[1] == '2')) ||
			         (c[0] == 'S' && c[1] == 'E' && c[2] == 'L') || (c[0] == 'S' && c[1] == 'T' && c[2] == 'A');
		int i = indice(*c);
		POLY_FT4 *p;
		if (i && (p = giallo ? glifo(i, 128, 104, 32) : glifo(i, 120, 120, 120)))
			setXYWH(p, x, y, 15, 16);
		x += font_w[i];
	}
}

// Rettangolo pieno 2D (sotto il testo, o al livello z)
static void tile_z(int z, int x, int y, int w, int h, int r, int g, int b) {
	TILE *t = prim(z, sizeof(TILE));
	if (!t) return;
	setTile(t);
	setXY0(t, x, y);
	setWH(t, w, h);
	setRGB0(t, r, g, b);
}
static void tile(int x, int y, int w, int h, int r, int g, int b) { tile_z(Z_FONDO, x, y, w, h, r, g, b); }

/* PC, mouse: zone cliccabili del fotogramma (si pubblicano per controlli.c alla fine del disegno) */
#define N_ZZ 160
static Zona zz[N_ZZ];
static int  nz, mm, mx = -1, my = -1, mgiu; // mm: il mouse e' passato sul disegno (pulsanti, pagine, aiuto)
static const Param *k_par[N_K];              // parametro della tabella di ogni manopola

static Zona *zona(int tipo, int x, int y, int w, int h) {
	static Zona scarto;
	Zona *z = nz < N_ZZ ? &zz[nz++] : &scarto;
	*z = (Zona) { .x = x, .y = y, .w = w, .h = h, .tipo = tipo };
	return z;
}

// Zona della manopola i (lato c, posto j): continua = si trascina, lista = scelta; TAP = un clic
static Zona *zona_k(int x, int y, int w, int h, int c, int j, int i) {
	Zona *z = zona(i == K_TAP ? Z_PASSO : LISTA(i) ? Z_SCELTA : Z_MANOPOLA, x, y, w, h);
	z->lato = c, z->posto = j;
	if (i == K_TAP) { z->passi = 1; z->aiuto = "TAP: CLICK IN TIME,\nTWO TIMES OR MORE"; }
	else            { z->par = k_par[i]; z->valore = k[i]; }
	return z;
}

// Valore di una manopola come testo (corto = per le righe compatte della schermata)
static const char *valore(int i, int corto) {
	static char s[12];
	int m = i >= K_MOD && i < K_FINE ? (i - K_MOD) % 4 : -1;
	if (i < 0)           return "-";
	if (i == K_RATIO)    return rap_nome[k[i]];
	if (i == K_ROOT)     return nota_nome[(k[i] + 9) % 12];
	if (i == K_MODE)     return modo_nome[k[i]];
	if (i == K_INTERVAL) return (corto ? int_corto : int_nome)[k[i]];
	if (i == K_COLOR)    return corto ? (const char *[]) { "DRK", "SFT", "WRM", "BRT", "THN", "AIR" }[k[i]] : col_nome[k[i]];
	if (i == K_ROOM)     return (corto ? room_corto : room_nome)[k[i]];
	if (i == K_SCALE)    return corto ? (const char *[]) { "MIN", "DOR", "PENT", "5THS", "OCT", "PHR", "LYD", "MIX", "MAJ" }[k[i]] : scala_nome[k[i]];
	if (i == K_PEDAL)    return pedal_nome[k[i]];
	if (i == K_MORPH)    { sprintf(s, "%d S", morph_s[k[i]]); return s; }
	if (i == K_NUDGE)    return "L1R1";
	if (i == K_FINE)     { sprintf(s, "%+d", k[i] - 50); return s; }
	if (i == K_TAIL && k[i] == 99) return "HOLD";
	if (i == K_TAP)      return "L2R2";
	if (i == K_RANGE)    return k[i] ? "2 OCT" : "1 OCT";
	if ((i == K_STEPS || i == K_STEPS1 || i == K_STEPS2) && !k[i]) return "OFF";
	if (i == K_STEPS)    { sprintf(s, "%d", k[i] + 1); return s; }
	if (i == K_EVERY)    { sprintf(s, "%d", ogni[k[i]]); return s; }
	if (i == K_BPM)      { sprintf(s, clk_est ? "%dE" : "%d", clk_est ? clk_bpm : k[i] + 40); return s; } // E = clock MIDI
	if (m == M_WAVE)     return (corto ? wave_corto : wave_nome)[k[i]];
	if (m == M_DEST)     return (corto ? dest_corto : dest_nome)[k[i]];
	sprintf(s, "%d", k[i]);
	return s;
}

/* Patch (tappa g). File "BESLES-99999STORM" sulla memory card della porta 1, 1 blocco: 2 settori
   di intestazione (titolo, palette, icona: card.h) + 8 settori da 128 byte, uno per patch:
   [0] 'P' se pieno, [1] versione, poi le N_K manopole (un byte ciascuna), le destinazioni e gli
   attenuatori delle 4 macro. Funzioni del BIOS (open/read/write), che bloccano il main loop: prima
   di scrivere si riempie l'anello del motore DSP fino a ~76 ms avanti. */

#define CARD_FILE "bu00:BESLES-99999STORM"
#define N_POSTI   8
static uint8_t card_buf[256 + N_POSTI * 128] __attribute__((aligned(4)));
static int     carica, carica_n;           // LOAD / GO / HOME / NUDGE: fotogrammi rimasti e totali della scivolata
static int16_t k_da[N_K], k_a[N_K], k_sc[N_K]; // da, a, ultimo valore scritto dalla scivolata
static int     gposto;                     // SAVE / LOAD / SCENE: posto scelto (0-7)
static uint8_t casa[128];                  // HOME: la scena di partenza (all'avvio il suono iniziale)
_Static_assert(2 + N_K + 32 <= 128, "una patch deve stare in un settore");

static uint8_t *posto(int i) { return card_buf + 256 + i * 128; }

// PC: al posto della memory card il file delle scene (standard/controlli/scene.md), letto all'avvio
// (storm_scene_leggi) e scritto dal thread principale: qui, nel thread del suono, SAVE copia solo gli
// 8 posti in scr_buf e lo dice con scr_pronto; se il file di prima non e' ancora scritto, riprova al
// fotogramma dopo (scr_dopo). Sulla PS1 la scrittura fermava il main loop per ~2 s.
static uint8_t       scr_buf[N_POSTI * 128];
static SDL_AtomicInt scr_pronto, scr_errore; // scr_buf da scrivere; il thread principale non ci e' riuscito
static int           scr_dopo;

static void card_scrivi(void) {
	scr_dopo = SDL_GetAtomicInt(&scr_pronto);
	if (scr_dopo) return;
	memcpy(scr_buf, posto(0), sizeof scr_buf);
	SDL_SetAtomicInt(&scr_pronto, 1);
}

static int scena_ora = -1, auto_on, auto_t; // scena caricata per ultima; AUTO: scene in fila da sole
static const char *smsg;                       // messaggio della pagina SCENE
static int smsg_t;

// PC, plugin: SAVE resta nei posti (che stanno nel progetto della DAW); il file delle scene si
// scrive solo con EXPORT e si legge con IMPORT, due pulsanti in piu' nella pagina SCENE
static int           plugin_si;
static SDL_AtomicInt imp_chiesta;
void storm_plugin(void) { plugin_si = 1; }
static int pulsante(int x, int y, const char *nome, int on, int r, int g, int b, int tieni, const char *aiuto);

static void patch_salva(uint8_t *p) {
	p[0] = 'P';
	p[1] = 2;
	for (int j = 0; j < N_K; j++) p[2 + j] = k[j];
	for (int m = 0; m < 16; m++) { p[2 + N_K + m] = mdest[m >> 2][m & 3]; p[2 + N_K + 16 + m] = mamt[m >> 2][m & 3]; }
}

// Scivolata: da adesso verso k_a in n fotogrammi
static void scivola_da_qui(int n) {
	for (int j = 0; j < N_K; j++) k_da[j] = k_sc[j] = k[j];
	carica = carica_n = n;
}

// LOAD / GO: liste e macro subito, manopole continue che scivolano nel tempo di MORPH. Versione 1:
// 57 manopole (quelle nuove restano come sono). MORPH non si carica: e' di chi suona.
static void patch_carica(const uint8_t *p) {
	int nk = p[1] == 1 ? N_K1 : N_K;
	for (int m = 0; m < 16; m++) { mdest[m >> 2][m & 3] = p[2 + nk + m] % N_MDEST; mamt[m >> 2][m & 3] = clamp((int8_t) p[2 + nk + 16 + m], -9, 9); }
	for (int j = 0; j < N_K; j++) {
		int v = j < nk && j != K_MORPH ? clamp(p[2 + j], 0, k_max[j]) : k[j];
		if (LISTA(j)) k[j] = v;
		k_a[j] = v;
	}
	scivola_da_qui(morph_s[k[K_MORPH]] * FPS);
}

// Una manopola toccata durante la scivolata (tasti o levette) esce dalla scivolata e resta li'
static void patch_scivola(void) {
	if (!carica) return;
	carica--;
	for (int j = 0; j < N_K; j++) {
		if (LISTA(j)) continue;
		if (k[j] != k_sc[j]) k_da[j] = k_a[j] = k[j];
		k[j] = k_sc[j] = k_a[j] + (k_da[j] - k_a[j]) * carica / carica_n;
	}
}

// GO: la scena i (scivolando in MORPH), che diventa anche la casa di HOME. 0 se il posto e' vuoto.
static int scena_vai(int i) {
	if (posto(i)[0] != 'P') return 0;
	patch_carica(posto(i));
	for (int b = 0; b < 128; b++) casa[b] = posto(i)[b];
	scena_ora = i;
	return 1;
}

// AUTO: la scena piena dopo quella di adesso, ogni 2 x MORPH; nessuna scena piena = AUTO spento
static void scena_auto(void) {
	if (!auto_on || ++auto_t < morph_s[k[K_MORPH]] * 2 * FPS) return;
	auto_t = 0;
	for (int n = 1; n <= N_POSTI; n++)
		if (scena_vai((scena_ora + n + N_POSTI) % N_POSTI)) return;
	auto_on = 0;
	smsg = "NO SAVED SCENES";
	smsg_t = 100;
}

// HOME: verso la scena di partenza, con le macro a 0
static void casa_torna(void) {
	patch_carica(casa);
	for (int m = 0; m < 4; m++) k_a[K_M1 + m] = 0;
}

// NUDGE: 6 manopole del suono a caso, spostate di 1-5 in 3 s (durante una scivolata: il punto d'arrivo)
static const int8_t spinta_k[] = { K_CUT, K_RES, K_FOLD, K_FM, K_DRIVE, K_FEED, K_WIDTH, K_NOISE, K_DETUNE,
                                   K_BELLS, K_DAMP, K_AM, K_PWM, K_SQUARE, K_SAW, K_SUB, K_WIDE, K_LAYERS };
static void spinta(void) {
	if (!carica) {
		for (int j = 0; j < N_K; j++) k_a[j] = k[j];
		scivola_da_qui(150);
	}
	for (int n = 0; n < 6; n++) {
		int i = spinta_k[caso() % sizeof(spinta_k)], dv = 1 + caso() % 5;
		k_a[i] = clamp(k_a[i] + (caso() & 1 ? dv : -dv), 0, 99);
	}
}

/* Interfaccia 3D (tappa d). Ogni pannello e' un rettangolo nello spazio: la GTE ne proietta i 4
   angoli (inclinato all'indietro come su un tavolo; dritto e a scala 1 quando viene avanti) e
   tutto quello che c'e' dentro si disegna con coordinate del pannello (u, v da 0 a 4096) portate
   sullo schermo tra i 4 angoli: le figure seguono il pannello mentre gira e si avvicina. */

#define FUOCO 512 // distanza dello schermo: a z = FUOCO un'unita' GTE = un pixel
typedef struct { int x[4], y[4]; } Quad; // angoli a schermo: 0 alto-sx, 1 alto-dx, 2 basso-sx, 3 basso-dx

enum { PN_SRC, PN_SHP, PN_LAY, PN_SPC, PN_MOT, PN_MIX, N_PN };
static const int16_t pn_riposo[N_PN][4] = { // centro x, y, mezza larghezza, mezza altezza (pixel)
	{ 49, 81, 32, 36 }, { 123, 81, 32, 36 }, { 197, 81, 32, 36 }, { 271, 81, 32, 36 },
	{ 128, 141, 112, 18 }, { 275, 141, 29, 18 } };
static const char * const pn_nome[N_PN] = { "SOURCES", "SHAPE", "LAYERS", "SPACE", "MOTION", "MIX" };
static const uint8_t pn_lato[N_PN] = { 0, 0, 0, 1, 1, 1 }; // colore della cornice: 0 = C1 verde, 1 = C2 azzurro
// Pannello di ogni pagina (C1: SHAPE, OSC, GRAIN, NOTE, LAYERS, OSC B, -, TEXTURE; C2: MIX, MOD A-C, SPACE,
// MACROS, SEQ, PULSE, CLOCK, EDIT, SCENE)
static const int8_t pag_pn[2][11] = { { PN_SHP, PN_SRC, PN_SRC, PN_SRC, PN_LAY, PN_SRC, PN_SRC, PN_SRC },
                                      { PN_MIX, PN_MOT, PN_MOT, PN_MOT, PN_SPC, PN_MIX, PN_MOT, PN_MOT, PN_MOT, PN_MIX, PN_MIX } };

static void pannello_quad(int cx, int cy, int hw, int hh, int tilt, Quad *q) {
	SVECTOR rot = { tilt, 0, 0 };
	MATRIX  m;
	RotMatrix(&rot, &m);
	m.t[0] = cx - W / 2;
	m.t[1] = cy - H / 2;
	m.t[2] = FUOCO;
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);
	SVECTOR v[4] = { { -hw, -hh, 0 }, { hw, -hh, 0 }, { -hw, hh, 0 }, { hw, hh, 0 } };
	for (int i = 0; i < 4; i++) {
		int32_t xy;
		gte_ldv0(&v[i]);
		gte_rtps();
		gte_stsxy(&xy);
		q->x[i] = (int16_t) xy;
		q->y[i] = xy >> 16;
	}
}

// Punto (u, v) del pannello -> schermo (interpolazione tra i 4 angoli proiettati)
static void mappa(const Quad *q, int u, int v, int *x, int *y) {
	int tx = q->x[0] + ((q->x[1] - q->x[0]) * u >> 12), ty = q->y[0] + ((q->y[1] - q->y[0]) * u >> 12);
	int bx = q->x[2] + ((q->x[3] - q->x[2]) * u >> 12), by = q->y[2] + ((q->y[3] - q->y[2]) * u >> 12);
	*x = tx + ((bx - tx) * v >> 12);
	*y = ty + ((by - ty) * v >> 12);
}

// Il rettangolo dritto che contiene il rettangolo (u0,v0)-(u1,v1) del pannello
static void rett_uv(const Quad *q, int u0, int v0, int u1, int v1, int16_t r[4]) {
	int x[4], y[4], x0 = 999, y0 = 999, x1 = -999, y1 = -999;
	mappa(q, u0, v0, &x[0], &y[0]);
	mappa(q, u1, v0, &x[1], &y[1]);
	mappa(q, u0, v1, &x[2], &y[2]);
	mappa(q, u1, v1, &x[3], &y[3]);
	for (int i = 0; i < 4; i++) {
		x0 = x[i] < x0 ? x[i] : x0, x1 = x[i] > x1 ? x[i] : x1;
		y0 = y[i] < y0 ? y[i] : y0, y1 = y[i] > y1 ? y[i] : y1;
	}
	r[0] = x0, r[1] = y0, r[2] = x1 - x0, r[3] = y1 - y0;
}

static Zona *zona_uv(int tipo, const Quad *q, int u0, int v0, int u1, int v1) {
	int16_t r[4];
	rett_uv(q, u0, v0, u1, v1, r);
	return zona(tipo, r[0], r[1], r[2], r[3]);
}

// Segmento di 2 pixel (le linee di 1 pixel sulla TV a tubo spariscono)
static void seg(int z, int x0, int y0, int x1, int y1, int r, int g, int b) {
	POLY_F4 *p = prim(z, sizeof(POLY_F4));
	if (!p) return;
	int dx = x1 - x0, dy = y1 - y0, ox = (dx < 0 ? -dx : dx) < (dy < 0 ? -dy : dy) ? 2 : 0, oy = 2 - ox;
	setPolyF4(p);
	setRGB0(p, r, g, b);
	setXY4(p, x0, y0, x1, y1, x0 + ox, y0 + oy, x1 + ox, y1 + oy);
}

static void seg_uv(int z, const Quad *q, int u0, int v0, int u1, int v1, int r, int g, int b) {
	int x0, y0, x1, y1;
	mappa(q, u0, v0, &x0, &y0);
	mappa(q, u1, v1, &x1, &y1);
	seg(z, x0, y0, x1, y1, r, g, b);
}

// Rettangolo pieno (u0,v0)-(u1,v1) del pannello
static void pieno(int z, const Quad *q, int u0, int v0, int u1, int v1, int r, int g, int b) {
	POLY_F4 *p = prim(z, sizeof(POLY_F4));
	if (!p) return;
	int x[4], y[4];
	mappa(q, u0, v0, &x[0], &y[0]);
	mappa(q, u1, v0, &x[1], &y[1]);
	mappa(q, u0, v1, &x[2], &y[2]);
	mappa(q, u1, v1, &x[3], &y[3]);
	setPolyF4(p);
	setRGB0(p, r, g, b);
	setXY4(p, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
}

static void contorno(int z, const Quad *q, int u0, int v0, int u1, int v1, int r, int g, int b) {
	seg_uv(z, q, u0, v0, u1, v0, r, g, b);
	seg_uv(z, q, u0, v1, u1, v1, r, g, b);
	seg_uv(z, q, u0, v0, u0, v1, r, g, b);
	seg_uv(z, q, u1, v0, u1, v1, r, g, b);
}

static void testo_uv(const Quad *q, int u, int v, const char *t, int r, int g, int b) {
	int x, y;
	mappa(q, u, v, &x, &y);
	testo(x, y, t, r, g, b);
}

// Manopola 2D: disco a 8 lati di raggio 5 e tacca chiara da -135 a +135 gradi
static void manopola(int x, int y, int val, int max) {
	for (int i = 0; i < 8; i++) {
		POLY_F3 *p = prim(Z_FIGZ, sizeof(POLY_F3));
		if (!p) return;
		int a0 = i * 512, a1 = a0 + 512;
		setPolyF3(p);
		setRGB0(p, 70, 70, 84);
		setXY3(p, x, y, x + (icos(a0) * 5 >> 12), y + (isin(a0) * 5 >> 12), x + (icos(a1) * 5 >> 12), y + (isin(a1) * 5 >> 12));
	}
	int a = -1536 + val * 3072 / (max ? max : 1);
	seg(Z_TESTO, x, y, x + (isin(a) * 6 >> 12), y - (icos(a) * 6 >> 12), 230, 225, 200);
}

/* Figure: (u0,v0)-(u1,v1) = zona del pannello; l = luce 0-128 (piu' scuro quando e' dietro) */

#define C(x) ((x) * l >> 7)
static int16_t lis_u[3][48], lis_v[3][48]; // Lissajous dei 3 fotogrammi (scie stile feedback video)
static int     lis_i;

// SOURCES: Lissajous, x = OSC B, y = OSC A dopo FM e fold, con le manopole suonate
static void lissajous_calcola(void) {
	int *e = d.eff, den = rap[k[K_RATIO]][1], num = rap[k[K_RATIO]][0];
	int div = e[K_SINE] + e[K_SQUARE] + e[K_SAW], rd = (1 << 24) / (div < V99 ? V99 : div);
	int cs = e[K_SINE] * rd >> 12, cq = e[K_SQUARE] * rd >> 12, cw = e[K_SAW] * rd >> 12;
	int g = 4096 + (e[K_FOLD] * 207 >> 8);
	uint32_t fmk = (uint32_t) (e[K_FM] * e[K_FM]) >> 12, thr = 0x80000000u - (uint32_t) e[K_WIDTH] * 79652u;
	lis_i = (lis_i + 1) % 3;
	for (int i = 0; i < 48; i++) { // den giri di OSC A = num giri di OSC B: la curva si chiude
		uint32_t pa = (uint32_t) i * (den * 89478485u), pb = (uint32_t) i * (num * 89478485u);
		int mb = seno[pb >> 24];
		uint32_t ph = pa + (uint32_t) mb * fmk;
		int a = (seno[ph >> 24] >> 1) * cs + (ph < thr ? 16383 : -16384) * cq + ((int) (ph >> 17) - 16384) * cw;
		int f = (a >> 12) * g >> 12, rr = (f + 16384) & 0xffff;
		f = rr < 32768 ? rr - 16384 : 49152 - rr;
		lis_u[lis_i][i] = 2048 + (mb >> 4);
		lis_v[lis_i][i] = 2048 - (f >> 3);
	}
}

static void fig_sorgenti(int z, const Quad *q, int u0, int v0, int u1, int v1, int l) {
	int du = u1 - u0, dv = v1 - v0;
	for (int h = 2; h <= 3; h++) { // una scia (scura) e il fotogramma nuovo
		int f = (lis_i + h) % 3, lh = l * (h - 1) / 2;
		int px = 0, py = 0, x0 = 0, y0 = 0;
		for (int i = 0; i <= 48; i++) {
			int x, y, j = i % 48;
			mappa(q, u0 + (lis_u[f][j] * du >> 12), v0 + (lis_v[f][j] * dv >> 12), &x, &y);
			if (i) seg(z, px, py, x, y, 120 * lh >> 7, 220 * lh >> 7, 140 * lh >> 7);
			else   { x0 = x; y0 = y; }
			px = x; py = y;
		}
		(void) x0; (void) y0;
	}
}

// SHAPE: curva del filtro (LP/BP/HP, picco della risonanza), colore dal viola (chiuso) al ciano (aperto)
static void fig_forma(int z, const Quad *q, int u0, int v0, int u1, int v1, int l) {
	int gi = (d.eff[K_CUT] * 41 >> 8) >> 4, md = k[K_MODE];
	int rq = d.eff[K_RES] * 3 / 99, r0 = rq >> 8, rf = rq & 255, r1 = r0 < 3 ? r0 + 1 : 3;
	int c = d.eff[K_CUT] * 255 / V99;
	int cr = C(90 - c * 10 / 255), cg = C(50 + c * 160 / 255), cb = C(140 + c * 90 / 255);
	int px = 0, py = 0;
	for (int j = 0; j < 24; j++) {
		int fi = j * 253 / 23, idx = clamp((fi - gi + 128) >> 3, 0, 32);
		int h = curva[md][r0][idx] + ((curva[md][r1][idx] - curva[md][r0][idx]) * rf >> 8);
		int x, y;
		mappa(q, u0 + j * (u1 - u0) / 23, v1 - h * (v1 - v0) / 64, &x, &y);
		if (j) seg(z, px, py, x, y, cr, cg, cb);
		px = x; py = y;
	}
	// dove sta il cutoff: tacca in basso
	int uc = u0 + gi * (u1 - u0) / 253;
	seg_uv(z, q, uc, v1 - (v1 - v0) / 8, uc, v1, C(200), C(200), C(210));
}

// LAYERS: 4 colonne (livello), spostate dalla stonatura; anelli = BELLS; colore = COLOR
static void fig_strati(int z, const Quad *q, int u0, int v0, int u1, int v1, int l) {
	static const uint8_t col[6][3] = { { 60, 90, 80 }, { 80, 130, 100 }, { 110, 170, 120 }, { 150, 220, 160 },
	                                   { 190, 210, 240 }, { 210, 225, 250 } };
	const uint8_t *cc = col[k[K_COLOR]];
	int du = (u1 - u0) / 4, h = lay_liv * (v1 - v0) / 0x1000;
	for (int j = 0; j < 4; j++) {
		int uc = u0 + du / 2 + j * du + lay_c[j] * du / 80, w = du / 4;
		pieno(z, q, uc - w, v1 - h, uc + w, v1, C(cc[0]), C(cc[1]), C(cc[2]));
		for (int a = 1; a <= lay_amp; a += 2) { // BELLS: un trattino ogni 2 livelli sopra la colonna
			int va = v1 - h - a * (v1 - v0) / 16;
			if (va > v0) seg_uv(z, q, uc - w, va, uc + w, va, C(200), C(190), C(140));
		}
	}
}

// SPACE: campo stereo (centro = motore DSP, puntini = LAYERS secondo il pan; oltre le casse =
// fase invertita) e cerchi che si allargano e sbiadiscono (TAIL; DAMP = piu' grigi)
static void fig_spazio(int z, const Quad *q, int u0, int v0, int u1, int v1, int l, int fot) {
	int um = (u0 + u1) / 2, vm = (v0 + v1) / 2, du = (u1 - u0) / 2, dv = (v1 - v0) / 2;
	seg_uv(z, q, u0, vm, u1, vm, C(60), C(70), C(90));
	pieno(z, q, um - du / 12, vm - dv / 8, um + du / 12, vm + dv / 8, C(200), C(200), C(210));
	for (int j = 0; j < 4; j++) {
		int uj = um + clamp(lay_x[j], -8192, 8192) * du / 8192, s = 1 + lay_liv * 3 / 0x1000;
		int out = lay_x[j] > 4096 || lay_x[j] < -4096;
		pieno(z, q, uj - s * du / 40, vm - s * dv / 12, uj + s * du / 40, vm + s * dv / 12,
		      C(out ? 220 : 90), C(out ? 190 : 170), C(out ? 120 : 220));
	}
	int per = 40 + (suonato(K_TAIL) >> 8) * 2, grigio = suonato(K_DAMP) >> 8;
	for (int j = 0; j < 3; j++) {
		int t = (fot + j * per / 3) % per, rr = t * 4096 / per, lu = l * (per - t) / per;
		int cr = (180 - grigio) * lu >> 7, cg = (200 - grigio / 2) * lu >> 7, cb = 230 * lu >> 7;
		contorno(z, q, um - (rr * du >> 12), vm - (rr * dv >> 12), um + (rr * du >> 12), vm + (rr * dv >> 12), cr, cg, cb);
	}
}

// MOTION: tracce dei 3 generatori e della deriva (ultimi 64 valori, uno ogni 3 fotogrammi)
static int16_t storia[4][64];
static int     storia_i;
static void fig_moto(int z, const Quad *q, int u0, int v0, int u1, int v1, int l, int sel) {
	static const uint8_t col[4][3] = { { 200, 140, 220 }, { 90, 200, 220 }, { 140, 210, 120 }, { 110, 110, 120 } };
	int dv = (v1 - v0) / 2, vm = v0 + dv;
	for (int j = 3; j >= 0; j--) {
		int lj = sel < 0 || sel == j ? l : l / 3;
		int px = 0, py = 0;
		for (int i = 0; i < 32; i++) {
			int x, y, val = storia[j][(storia_i + 2 * i + 1) & 63];
			mappa(q, u0 + i * (u1 - u0) / 31, vm - val * dv / 32768, &x, &y);
			if (i) seg(z, px, py, x, y, col[j][0] * lj >> 7, col[j][1] * lj >> 7, col[j][2] * lj >> 7);
			px = x; py = y;
		}
	}
	// SEQ: un quadratino per passo (altezza = grado), quello che suona chiaro; PULSE 1 e 2: un
	// puntino per passo, i colpi piu' chiari, quello in corso bianco, il colpo appena suonato grande
	int ub = u0 < 1600 ? 1600 : u0, len = k[K_STEPS] ? k[K_STEPS] + 1 : 0, du = (u1 - ub) / 16;
	for (int i = 0; i < len; i++) {
		int on = i == d.spos, h = 80 + d.sreg[i] * 2;
		pieno(z, q, ub + i * du * 2, v0 - 150 - h, ub + i * du * 2 + du, v0 - 150,
		      C(on ? 230 : 90), C(on ? 210 : 140), C(on ? 140 : 90));
	}
	for (int pp = 0; pp < 2; pp++) {
		int n = k[K_STEPS1 + 2 * pp], hk = k[K_HITS1 + 2 * pp], vv = pp ? v1 + 120 : v0 - 60;
		if (!n || (pp == 0 && len)) continue; // con il SEQ acceso la riga in alto e' sua
		for (int i = 0; i < n; i++) {
			int colpo = i * hk % n < hk, ora = i == d.s16 % n, gr = ora && d.lampo_p[pp] == d.s16 - 1 ? 2 : 1;
			int c0 = ora ? 230 : colpo ? 120 : 50;
			pieno(z, q, ub + i * du, vv - 60 * gr, ub + i * du + 30 * gr + 40, vv + 60 * gr, C(c0), C(c0), C(ora ? 240 : colpo ? 160 : 70));
		}
	}
}

// MIX: 4 barre verticali (MACRO 1, REVERB, LAYERS, VOLUME) o le 4 macro suonate
static void fig_mix(int z, const Quad *q, int u0, int v0, int u1, int v1, int l, int macro) {
	int du = (u1 - u0) / 5;
	// OUT: meter dell'uscita del nucleo (verde, giallo chiaro vicino al massimo: niente rosso)
	int hm = d.picco * (v1 - v0) / 32768, um = u0 + du / 2 + 4 * du, alto = d.picco > 29000;
	pieno(z, q, um - du / 4, v0, um + du / 4, v1, C(30), C(36), C(50));
	pieno(z, q, um - du / 4, v1 - hm, um + du / 4, v1, C(alto ? 230 : 90), C(alto ? 220 : 200), C(alto ? 120 : 110));
	for (int j = 0; j < 4; j++) {
		int val = macro ? mval[j] * 256 : j == 0 ? k[K_M1] * 256 : suonato((const int8_t[]) { 0, K_REVERB, K_LAYERS, K_VOL }[j]);
		int h = val * (v1 - v0) / V99, uc = u0 + du / 2 + j * du;
		pieno(z, q, uc - du / 4, v0, uc + du / 4, v1, C(30), C(36), C(50));
		pieno(z, q, uc - du / 4, v1 - h, uc + du / 4, v1, C(60), C(130), C(200));
	}
}

// SCENE: 8 caselle (piena = colorata, scelta = cornice gialla, quella che suona = punto chiaro) e
// sotto la barra della scivolata in corso
static void fig_scene(int z, const Quad *q, int u0, int v0, int u1, int v1, int l) {
	int du = (u1 - u0) / 4, dv = (v1 - v0 - 500) / 2;
	for (int i = 0; i < N_POSTI; i++) {
		int a = u0 + (i & 3) * du + 40, b = v0 + (i >> 2) * dv + 40, pieno_p = posto(i)[0] == 'P';
		// nello stesso livello dell'OT l'ultima primitiva aggiunta si disegna per prima: prima il davanti
		if (i == scena_ora) pieno(z, q, a + du / 2 - 120, b + dv / 2 - 120, a + du / 2 + 40, b + dv / 2 + 40, C(240), C(240), C(240));
		contorno(z, q, a, b, a + du - 80, b + dv - 80, C(i == gposto ? 230 : 70), C(i == gposto ? 200 : 80), C(i == gposto ? 50 : 100));
		if (pieno_p) pieno(z, q, a, b, a + du - 80, b + dv - 80, C(40 + i * 20), C(90), C(200 - i * 15));
	}
	int f = carica ? (carica_n - carica) * (u1 - u0) / carica_n : u1 - u0;
	pieno(z, q, u0, v1 - 300, u0 + f, v1, C(carica ? 230 : 60), C(carica ? 200 : 100), C(carica ? 50 : 140));
	pieno(z, q, u0, v1 - 300, u1, v1, C(30), C(36), C(50));
}

static int V(int i);

// Le 4 righe-manopola della pagina tenuta, nella meta' destra del pannello venuto avanti
static uint8_t lampo[N_K];
static void celle(const Quad *q, int c, int pp, int ed_posto, int ux) {
	int un = ux + (4000 - ux) / 6, uk = ux + (4000 - ux) * 3 / 4; // nome, manopola
	static const char *tasto[4] = { SU_GIU, SX_DX, "L1", "L2" };
	char t[8];
	if (pp == P_EDIT) { // EDIT: i 4 posti della macro del posto scelto (1-16, 4 per macro)
		int m = ed_posto >> 2;
		testo_uv(q, ux, 200, macro_titolo[m], 128, 104, 32);
		// mouse: titolo = macro dopo; riga = sceglie il posto; quella scelta: nome = destinazione, barra = quantita'
		Zona *z = zona_uv(Z_PASSO, q, ux, 0, 4000, 1000);
		z->lato = c, z->passi = m < 3 ? -4 : 12, z->aiuto = "CLICK: NEXT MACRO";
		for (int s = 0; s < 4; s++) {
			int v = 1150 + s * 800, x, y;
			if (s != (ed_posto & 3)) {
				z = zona_uv(Z_PASSO, q, ux, v - 150, 4000, v + 650);
				z->lato = c, z->passi = ed_posto - (m * 4 + s), z->aiuto = "CLICK: CHOOSE THIS SLOT";
			} else {
				z = zona_uv(Z_PASSO, q, ux, v - 150, uk - 300, v + 650);
				z->lato = c, z->posto = 2, z->passi = 1, z->aiuto = "DESTINATION: CLICK = NEXT,\nWHEEL = UP/DOWN";
				z = zona_uv(Z_PASSO, q, uk - 300, v - 150, 4000, v + 650);
				z->lato = c, z->posto = 1, z->aiuto = "AMOUNT: DRAG UP/DOWN OR WHEEL";
			}
			mappa(q, ux, v, &x, &y);
			if (s == (ed_posto & 3)) testo(x - 10, y, ">", 128, 104, 32);
			testo(x, y, macro_nome[mdest[m][s]], 110, 110, 110);
			mappa(q, uk - 250, v + 300, &x, &y);
			int w = mamt[m][s] * 4;
			tile(w < 0 ? x + w : x, y, w < 0 ? -w : w, 3, 60, 130, 200);
			sprintf(t, "%+d", mamt[m][s]);
			testo_uv(q, 3720, v, t, 128, 128, 128);
		}
		return;
	}
	if (pp == P_SCENE) { // SCENE: 4 righe di tasti, e il messaggio dell'ultimo comando
		char r0[32], r1[24];
		sprintf(r0, SU_GIU " SCENE %d %s", gposto + 1, posto(gposto)[0] == 'P' ? "SAVED" : "EMPTY");
		sprintf(r1, SX_DX " MORPH %d S", morph_s[k[K_MORPH]]);
		const char *rr[4] = { r0, r1, "L1 HOME  R1 GO", "L2 AUTO  R2 NUDGE" };
		if (plugin_si && !smsg_t) { // al posto del titolo, a destra (a sinistra c'e' il nome del pannello);
			                        // il messaggio dell'ultimo comando li copre
			int x, y, yd, w = largo("EXPORT") + largo("IMPORT") + 13;
			mappa(q, 4000, 200, &x, &yd);
			mappa(q, ux, 200, &yd, &y);
			x += pulsante(x - w, y - 2, "EXPORT", 0, 60, 70, 100, 0, "EXPORT: WRITE THE 8 SCENES\nTO THE SCENE FILE") + 1 - w;
			pulsante(x, y - 2, "IMPORT", 0, 60, 70, 100, 0, "IMPORT: READ THE 8 SCENES\nFROM THE SCENE FILE");
		} else
			testo_uv(q, ux, 200, smsg_t ? smsg : "SCENES", 128, 104, 32);
		for (int j = 0; j < 4; j++) {
			int x, y, v = 1150 + j * 800;
			mappa(q, ux, v, &x, &y);
			riga_aiuto(x, y, rr[j]);
			// mouse: scena dopo, MORPH, HOME | GO, AUTO | NUDGE (meta' sinistra = L1/L2, destra = R1/R2)
			int16_t r[4];
			rett_uv(q, ux, v - 150, 4000, v + 650, r);
			if (j == 1) { zona_k(r[0], r[1], r[2], r[3], c, 1, K_MORPH); continue; }
			int xm = j ? x + largo(j == 2 ? "L1 HOME  " : "L2 AUTO  ") - 3 : r[0] + r[2];
			Zona *z = zona(Z_PASSO, r[0], r[1], xm - r[0], r[3]);
			z->lato = c, z->posto = j, z->passi = -1;
			z->aiuto = j == 0 ? "SCENE: CLICK = NEXT SLOT. TO SAVE:\nGUIDE > SAVE SCENE"
			         : j == 2 ? "HOME: BACK TO THE START SCENE" : "AUTO: PLAY THE SAVED SCENES\nONE AFTER THE OTHER";
			if (!j) continue;
			z = zona(Z_PASSO, xm, r[1], r[0] + r[2] - xm, r[3]);
			z->lato = c, z->posto = j, z->passi = 1;
			z->aiuto = j == 2 ? "GO: MORPH TO THE CHOSEN SCENE" : "NUDGE: A SMALL RANDOM PUSH";
		}
		return;
	}
	testo_uv(q, ux, 200, pag_nome[c][pp], 128, 104, 32);
	if (pp == 8 && c) { // CLOCK: luce grande del battito, per il TAP e il NUDGE
		int on = (d.s16 & 3) == 0;
		pieno(Z_FIGZ, q, ux + 1100, 150, ux + 1400, 500, on ? 240 : 40, on ? 230 : 40, on ? 120 : 50);
	}
	for (int j = 0; j < 4; j++) {
		int i = pag_k[c][pp][j], v = 1150 + j * 800, x, y;
		if (i < 0)
			zona_uv(Z_NIENTE, q, ux, v - 150, 4000, v + 650);
		else if (i == K_NUDGE) { // NUDGE: meta' sinistra = indietro (L1), destra = avanti (R1)
			Zona *z = zona_uv(Z_PASSO, q, ux, v - 150, (ux + 4000) / 2, v + 650);
			z->lato = c, z->posto = j, z->passi = -1, z->aiuto = "NUDGE: CLICK = BEAT A BIT EARLIER";
			z = zona_uv(Z_PASSO, q, (ux + 4000) / 2, v - 150, 4000, v + 650);
			z->lato = c, z->posto = j, z->passi = 1, z->aiuto = "NUDGE: CLICK = BEAT A BIT LATER";
		} else {
			int16_t r[4];
			rett_uv(q, ux, v - 150, 4000, v + 650, r);
			zona_k(r[0], r[1], r[2], r[3], c, j, i);
		}
		testo_uv(q, ux, v, tasto[j], 128, 104, 32);
		testo_uv(q, un, v, i < 0 ? "-" : pp == P_MACROS && c ? macro_titolo[j] : k_nome[i], 100, 100, 100);
		if (i < 0) continue;
		if (!LISTA(i)) {
			mappa(q, uk, v + 330, &x, &y);
			manopola(x, y, k[i], 99);
			if (controlli_midi_preso(k_par[i]) == 0) // MIDI: la manopola fisica non l'ha ancora presa
				tile_z(Z_TESTO, x + 7, y - 8, 3, 3, 150, 150, 165);
			if (i < K_FINE && i != K_BPM) { // valore suonato (con MOD e macro): barretta sottile sotto il nome
				pieno(Z_FIGZ, q, un, v + 520, un + V(i) * 570 / 99, v + 590, 60, 170, 200);
			}
		}
		const char *val = valore(i, 0);
		mappa(q, 4000, v, &x, &y);
		testo(x - largo(val), y, val, 128, lampo[i] ? 104 : 128, lampo[i] ? 32 : 128);
	}
}

/* Menu GUIDE (tappa e): elenco a sinistra, a destra testo (da script/testi.txt) e figura viva
   del blocco. Il suono continua; mentre e' aperto i pad guidano solo il menu. */

static int guida, gsel, gconf; // gconf: pagina TUTORIAL, RESET o SAVE in attesa di conferma
static const char *gmsg;       // messaggio dopo SAVE / LOAD
static int gmsg_t;
static int gsalvato = -1;      // PC: posto e fotogramma dell'ultimo SAVE
static uint32_t gsalvato_fot;

/* TUTORIAL (tappa f): costruisce un drone da una sinusoide sola. A ogni passo la testata dice cosa
   fare (testi in script/testi.txt), il pannello giusto lampeggia in giallo e si aspetta il valore
   vero; poi per 3 s dice cosa si sente e passa al passo dopo. Start + R1 / L1 = avanti / indietro;
   la voce TUTORIAL della GUIDE lo chiude (il suono resta com'e'). */
typedef struct { int8_t k; char op; int8_t v; int8_t pn; } Passo; // k -1 = solo Start + R1; -2 = posto 1 della macro 1 tra -1 e -3; -3 = SWELL in modo PLAY;
// -4 = MOD C su VOLUME con DEPTH sopra 30 (PC: sulla PS1 solo DEST, ma con DEPTH 0 il respiro non si sente)
// PC: MACRO 1 basta a 40 (PS1: 70) e CUTOFF a 60 (PS1: 40), cosi' con il posto 1 sotto zero il tutorial
// finisce col filtro aperto
static const Passo passi[N_PASSI] = {
	{ -1, 0, 0, PN_SRC },            { K_SQUARE, '>', 30, PN_SRC },   { K_SUB, '>', 40, PN_SRC },
	{ K_FOLD, '>', 40, PN_SRC },     { K_FM, '>', 30, PN_SRC },       { K_CUT, '<', 60, PN_SHP },
	{ K_RES, '>', 50, PN_SHP },      { MK(0, 1), '>', 40, PN_MOT },   { MK(1, 1), '>', 30, PN_MOT },
	{ -4, 0, 0, PN_MOT },            { K_LAYERS, '>', 50, PN_LAY },   { K_DETUNE, '>', 40, PN_LAY },
	{ K_BELLS, '>', 30, PN_LAY },    { K_REVERB, '>', 60, PN_SPC },   { K_WIDE, '>', 60, PN_SPC },
	{ K_DAMP, '>', 60, PN_SPC },     { K_M1, '>', 40, PN_MIX },       { K_M2, '>', 50, PN_MIX },
	{ -2, 0, 0, PN_MIX },            { K_STEPS, '>', 3, PN_MOT },     { MK(0, 2), '=', 5, PN_MOT },
	{ -3, 0, 0, PN_SRC },            { -1, 0, 0, PN_MIX },
};
static int tut, tut_passo, tut_t; // tut_t > 0: fatto, si mostra cosa si sente (fotogrammi rimasti)

static int fatto(const Passo *pa) {
	if (pa->k == -1) return 0;
	if (pa->k == -2) return mamt[0][0] < 0 && mamt[0][0] >= -3; // PC: da -1 a -3, se no a fine tutorial il filtro resta chiuso
	if (pa->k == -3) return suona && gest[0] > V99 / 2;
	if (pa->k == -4) return k[MK(2, 3)] == 9 && k[MK(2, 1)] > 30;
	int v = k[pa->k];
	return pa->op == '>' ? v >= pa->v : pa->op == '<' ? v <= pa->v : v == pa->v;
}

// Stato zero: solo la sinusoide, filtro aperto, niente movimenti, strati, rumore, riverbero basso
static void tut_inizia(void) {
	reset();
	static const int8_t zero[] = { K_SQUARE, K_SAW, K_SUB, K_FOLD, K_FM, K_AM, K_PWM, K_NOISE, K_RES, K_FEED,
	                               K_DRIVE, K_LAYERS, K_DETUNE, K_BELLS, K_WIDE, K_DAMP, MK(0, 1), MK(1, 1), MK(2, 1) };
	for (int i = 0; i < (int) sizeof(zero); i++)
		k[zero[i]] = 0;
	k[K_SINE]   = 80;
	k[K_CUT]    = 99;
	k[K_REVERB] = 15;
	suona = gest_deg = gest_oct = 0;
	tut = 1;
	tut_passo = tut_t = 0;
}

static void tut_avanza(int nav) {
	if (nav) {
		tut_passo += nav;
		tut_t = 0;
	} else if (!tut_t) {
		if (fatto(&passi[tut_passo])) tut_t = 150; // 3 s di "cosa si sente"
		return;
	} else if (--tut_t)
		return;
	else
		tut_passo++;
	if (tut_passo < 0) tut_passo = 0;
	if (tut_passo >= N_PASSI) tut = 0;
}

// Testata del tutorial: numero del passo e 2 righe (cosa fare in bianco, cosa si sente in azzurro)
static void tut_testata(void) {
	char riga[48];
	sprintf(riga, "%d/%d", tut_passo + 1, N_PASSI);
	int x = testo(16, 16, riga, 128, 104, 32) + 8;
	const char *t = tut_t ? passo_senti[tut_passo] : passo_fai[controlli_dispositivo()][tut_passo];
	for (int y = 16; *t; y += 13) {
		int n = 0;
		while (t[n] && t[n] != '\n' && n < 47) { riga[n] = t[n]; n++; }
		riga[n] = 0;
		if (tut_t) testo(x, y, riga, 90, 200, 220);
		else       riga_aiuto(x, y, riga);
		t += n + (t[n] == '\n');
	}
}

// Righe separate da '\n'; quelle che iniziano con "> " sono tasti (parole dei tasti in giallo)
static void paragrafo(int x, int y, const char *t) {
	char riga[48];
	while (*t) {
		int n = 0;
		while (t[n] && t[n] != '\n' && n < 47) { riga[n] = t[n]; n++; }
		riga[n] = 0;
		if (riga[0] == '>') riga_aiuto(x, y, riga + 2);
		else                testo(x, y, riga, 128, 128, 128);
		t += n + (t[n] == '\n');
		y += 11;
	}
}

// SIGNAL PATH: la catena dei 4 blocchi con le frecce, un impulso che la percorre, MOTION e MACRO sotto
static void fig_percorso(const Quad *q, int fot) {
	static const char *nome[4] = { "SRC", "SHP", "LAY", "SPC" };
	for (int i = 0; i < 4; i++) {
		int u = 100 + i * 1024, c2 = i == 3;
		contorno(Z_FIG, q, u, 300, u + 760, 1900, c2 ? 40 : 60, c2 ? 100 : 130, c2 ? 170 : 72);
		testo_uv(q, u + 120, 700, nome[i], 128, 128, 128);
		if (i < 3) seg_uv(Z_FIG, q, u + 760, 1100, u + 1024, 1100, 150, 150, 170);
		seg_uv(Z_FIG, q, u + 380, 2600, u + 380, 1900, 90, 90, 110);
	}
	int t = fot % 100;
	pieno(Z_FIG, q, 100 + t * 3700 / 100, 1000, 300 + t * 3700 / 100, 1250, 230, 230, 240);
	contorno(Z_FIG, q, 100, 2600, 2700, 3900, 40, 100, 170);
	testo_uv(q, 250, 2900, "MOTION", 60, 130, 200);
	contorno(Z_FIG, q, 2900, 2600, 3996, 3900, 40, 100, 170);
	testo_uv(q, 3000, 2900, "MACRO", 60, 130, 200);
}

static void guida_disegna(int fot) {
	if (mm) zona(Z_NIENTE, 16, 42, W - 32, 198); // un clic nel menu fuori dalle voci non lo chiude
	for (int i = 0; i < N_GUIDA; i++) {
		int y = mm ? 46 + i * 12 : 47 + i * 11, on = i == gsel; // mouse: righe da 12 pixel
		if (on) tile(18, y - 2, 94, mm ? 12 : 14, 40, 50, 70);
		testo(on ? 24 : 22, y, guida_voce[i], on ? 128 : 90, on ? 104 : 110, on ? 32 : 120);
		if (!mm) continue;
		Zona *z = zona(on ? Z_PAD : Z_PASSO, 18, y - 2, 94, 12); // clic = sceglie; di nuovo = OK (Croce)
		z->n = __builtin_ctz(PAD_CROSS), z->passi = gsel - i;
		z->aiuto = on ? "CLICK AGAIN OR OK: DO IT" : "CLICK: CHOOSE";
	}
	int dv = controlli_dispositivo(); // PC: i testi parlano del dispositivo usato per ultimo
	paragrafo(116, 50, gsel == G_TUTORIAL && tut ? tut_attivo[dv] : gsel == G_TUTORIAL && gconf ? tut_conferma[dv] :
	                   gsel == G_RESET_SOUND && gconf ? tut_resetconf[dv] : gsel == G_SAVE_SCENE && gconf ? tut_overwrite[dv] :
	                   guida_testo[dv][gsel]);
	if (gsel == G_SAVE_SCENE || gsel == G_LOAD_SCENE) { // gli 8 posti: pieni o vuoti, quello scelto in evidenza
		char t[24];
		for (int i = 0; i < N_POSTI; i++) {
			int y = 100 + i * 12, on = i == gposto, pieno = posto(i)[0] == 'P';
			sprintf(t, "SCENE %d   %s", i + 1, pieno ? "SAVED" : "EMPTY");
			if (on) tile(114, y - 2, 130, 12, 40, 50, 70);
			testo(on ? 124 : 120, y, t, on ? 128 : 90, on ? 104 : pieno ? 128 : 90, on ? 32 : pieno ? 128 : 100);
			if (!mm) continue;
			Zona *z = zona(on ? Z_PAD : Z_PASSO, 114, y - 2, 130, 12); // clic = sceglie; di nuovo = OK
			z->n = __builtin_ctz(PAD_CROSS), z->posto = 1, z->passi = i - gposto;
			z->aiuto = !on ? "CLICK: CHOOSE THIS SLOT" : gsel == G_SAVE_SCENE ? "CLICK AGAIN OR OK: SAVE HERE" : "CLICK AGAIN OR OK: LOAD IT";
		}
	}
	if (gmsg_t) {
		gmsg_t--;
		testo(116, 200, gmsg, 128, 104, 32);
	}
	Quad q = { { 116, 302, 116, 302 }, { 152, 152, 222, 222 } };
	if (gsel < G_TUTORIAL && gsel != G_KEYS && gsel != G_VISUAL) contorno(Z_FIG, &q, 0, 0, 4096, 4096, 50, 60, 90);
	switch (gsel) {
	case G_SIGNAL_PATH: fig_percorso(&q, fot); break;
	case G_SOURCES:     fig_sorgenti(Z_FIG, &q, 300, 300, 3796, 3796, 128); break;
	case G_SHAPE:       fig_forma(Z_FIG, &q, 300, 500, 3796, 3700, 128); break;
	case G_LAYERS:      fig_strati(Z_FIG, &q, 300, 500, 3796, 3700, 128); break;
	case G_MOTION:      fig_moto(Z_FIG, &q, 200, 900, 3896, 3500, 128, -1); break;
	case G_SEQ:         fig_moto(Z_FIG, &q, 200, 900, 3896, 3500, 128, 3); break;
	case G_PLAY:        fig_mix(Z_FIG, &q, 600, 500, 3496, 3700, 128, 1); break;
	case G_SPACE:       fig_spazio(Z_FIG, &q, 300, 300, 3796, 3796, 128, fot); break;
	case G_MACROS:      fig_mix(Z_FIG, &q, 600, 500, 3496, 3700, 128, 1); break;
	}
	if (mm) { // mouse: sotto il menu OK (Croce) e BACK (Triangolo); a destra l'aiuto (storm_fotogramma)
		int x = 16;
		for (int j = 0; j < 2; j++) {
			const char *t = j ? "BACK" : "OK";
			int w = largo(t) + 12;
			testo(x + 6, 244, t, 128, 128, 128);
			tile(x, 242, w, 13, 50, 60, 90);
			Zona *z = zona(Z_PAD, x, 242, w, 13);
			z->n     = __builtin_ctz(j ? PAD_TRIANGLE : PAD_CROSS);
			z->aiuto = j ? "BACK: CANCEL OR CLOSE THE GUIDE" : "OK: DO WHAT IS CHOSEN";
			x += w + 4;
		}
	} else if (dv == D_TASTIERA)
		riga_aiuto(20, 228, SU_GIU " CHOOSE  ENTER OK  ESC CLOSE  F1 GUIDE");
	else
		riga_aiuto(20, 228, SU_GIU " CHOOSE  " CROCE " OK  " TRIAN " CLOSE  START+SELECT 1S");
	tile(16, 42, W - 32, 2, 110, 110, 130); // riga chiara sopra (aggiunte per ultime = disegnate sotto)
	tile(16, 44, W - 32, 196, 14, 16, 26);
}

/* VISUAL (Start + Select toccati): una tempesta in mare aperto vista dalla prua di un peschereccio. Ogni
   parametro suonato muove qualcosa della scena (tabella nel DESIGN). Mare = griglia 3D (GTE) alzata
   da 4 onde; la barca galleggia sull'onda vera nel suo punto (altezza e pendenze -> vista che
   sale, beccheggia, rolla). Cielo, nuvole, sole/luna, stelle, pioggia e fulmini in coordinate
   "dell'orizzonte" (u lungo l'orizzonte, v in su), cosi' girano con il rollio. Niente texture. */

static int visual, vis_uscito; // vis_uscito: i pannelli ripartono da fermi

// Valore suonato 0-99 di una manopola: dal motore (con MOD e macro) se la usa lisciata, se no
// manopola + macro
static int V(int i) {
	for (int j = 0; j < (int) sizeof(lisce); j++)
		if (lisce[j] == i) return clamp(d.eff[i] >> 8, 0, 99);
	return clamp(k[i] + (madd[i] >> 8), 0, 99);
}

#define RIGHE_M 12 // righe del mare (da vicino a lontano)
#define COL_M   16
typedef struct { int cx, cz, a; uint32_t ph; } Onda; // direzione/lunghezza (indice di seno per unita' x 4096), ampiezza, fase
static Onda onde[4];
static int  z_riga[RIGHE_M + 1];

static int altezza(int x, int z) {
	int h = 0;
	for (int i = 0; i < 4; i++)
		h += onde[i].a * seno[(((x * onde[i].cx + z * onde[i].cz) >> 12) + (onde[i].ph >> 16)) & 255] >> 15;
	return h;
}

static struct {
	int roll, pitch, eye, h_prima;   // vista (4096 = un giro), altezza degli occhi
	int16_t rx[150], ry[150];        // pioggia (pixel x 16)
	uint8_t rz[150];                 // profondita' della goccia 1-3
	int nu[12], nv[12];              // nuvole: posizione lungo l'orizzonte e altezza
	int spin;                        // giro delle nuvole (uragano)
	int saetta[14][2], n_saetta, vita, ramo; // fulmine: punti (u, v), vita in fotogrammi
	int spos_prima, pulse_prima[2], bagliore;
	int t;
	int aiuto;                       // fotogrammi rimasti dell'aiuto all'ingresso
} vs;

static void vs_init(void) {
	for (int i = 0; i < 150; i++) { vs.rx[i] = caso() % (W * 16); vs.ry[i] = caso() % (H * 16); vs.rz[i] = 1 + caso() % 3; }
	for (int i = 0; i < 12; i++) { vs.nu[i] = (int) (caso() % 700) - 350; vs.nv[i] = 30 + caso() % 110; }
	for (int i = 0; i <= RIGHE_M; i++) z_riga[i] = 110 * (1 << (i / 2)) * (i & 1 ? 3 : 2) / 2; // 110 ... ~6000, piu' fitte vicino
	vs.eye = -80;
}

// Punto (u, v) del riferimento dell'orizzonte -> schermo. (hx, hy) = centro dell'orizzonte,
// (dx, dy) = direzione dell'orizzonte in 1/4096 (rollio)
static int hx, hy, dxr, dyr;
static void oriz(int u, int v, int *x, int *y) {
	*x = hx + ((dxr * u + dyr * v) >> 12);
	*y = hy + ((dyr * u - dxr * v) >> 12);
}

static void quad_g(int z, const int *x, const int *y, const uint8_t (*c)[3], int semi) {
	POLY_G4 *p = prim(z, sizeof(POLY_G4));
	if (!p) return;
	setPolyG4(p);
	setXY4(p, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
	setRGB0(p, c[0][0], c[0][1], c[0][2]);
	setRGB1(p, c[1][0], c[1][1], c[1][2]);
	setRGB2(p, c[2][0], c[2][1], c[2][2]);
	setRGB3(p, c[3][0], c[3][1], c[3][2]);
	if (semi) setSemiTrans(p, 1);
}

static uint8_t sat8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void tempesta(int fot, int sfuma) {
	int cut = V(K_CUT), vol = V(K_VOL), tod = k[K_MODE], fz = freeze;
	int hur = (mval[0] + mval[1] + mval[2] + mval[3]) * 4096 / 396;          // uragano 0-4096 (somma delle macro)
	int vento = 0;                                                         // raffiche: MOD A-C
	for (int j = 0; j < 3; j++) vento += d.out[j] * (d.dep[j] >> 8) >> 12; // +-~2400
	vento = vento / 12 + V(K_FEED) / 3 + hur / 200;                         // +-~250
	// luce 0-256: CUTOFF (tempesta buia / schiarita) x VOLUME x MUTE
	int luce = (70 + cut * 186 / 99) * (30 + vol * 70 / 99) / 100 * sfuma / 150;
	if (!fz) vs.t++;

	// --- onde: lunga (SUB + SINE, girata da SAW), media (SINE), corta (SQUARE), increspatura (DETUNE)
	int rabbia = 4096 + V(K_DRIVE) * 41 + hur;                              // x1 .. x3
	int swell = gest[0] * 4096 / V99, sink = gest[1] * 4096 / V99;
	static const int lung[4] = { 1400, 520, 190, 75 };
	int amp[4] = { (V(K_SUB) * 12 / 10 + V(K_SINE) / 2) * (4096 + 2 * swell) >> 12, V(K_SINE) * 6 / 10, V(K_SQUARE) / 2, V(K_DETUNE) * 15 / 100 };
	int dir = (V(K_SAW) - 50) * 6;                                          // gradi x 11 circa
	int vel = (k[K_BPM] + 40) * 30;
	for (int i = 0; i < 4; i++) {
		int a = dir * (i + 1) / 2 + i * 300, kk = 256 * 4096 / lung[i];
		onde[i].cx = isin(a) * kk >> 12;
		onde[i].cz = icos(a) * kk >> 12;
		onde[i].a  = amp[i] * rabbia >> 12;
		if (!fz) onde[i].ph -= (uint32_t) (vel * 4096 / (lung[i] < 300 ? 300 : lung[i]) * 8 + vento * 400 * (i + 1));
	}

	// --- la barca sull'onda: altezza e pendenze nel suo punto -> vista
	int h0 = altezza(0, 160), px = altezza(110, 160) - altezza(-110, 160), pz = altezza(0, 280) - altezza(0, 60);
	int roll_t  = clamp(px * 3, -420, 420) + (hur * isin(vs.t * 23) >> 12) / 10;
	int pitch_t = clamp(-pz * 3, -300, 300) + (swell * 220 >> 12) - (sink * 260 >> 12);
	int g = 2 + k[K_GLIDE];
	vs.roll  += (roll_t - vs.roll) / g;
	vs.pitch += (pitch_t - vs.pitch) / g;
	vs.eye   += (-(h0 + 75) + (sink * 90 >> 12) - vs.eye) / 2;
	MATRIX m;
	SVECTOR rot = { vs.pitch, 0, vs.roll };
	RotMatrix(&rot, &m);
	m.t[0] = -(m.m[0][1] * vs.eye >> 12);
	m.t[1] = -(m.m[1][1] * vs.eye >> 12);
	m.t[2] = -(m.m[2][1] * vs.eye >> 12);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);
	gte_SetGeomScreen(300 - V(K_WIDE) * 14 / 10); // WIDE: grandangolo

	// orizzonte: due punti lontanissimi sul livello del mare
	int32_t xy;
	SVECTOR o1 = { -12000, 0, 6000 }, o2 = { 12000, 0, 6000 };
	gte_ldv0(&o1); gte_rtps(); gte_stsxy(&xy);
	int ax = (int16_t) xy, ay = xy >> 16;
	gte_ldv0(&o2); gte_rtps(); gte_stsxy(&xy);
	int bx = (int16_t) xy, by = xy >> 16;
	hx = (ax + bx) / 2; hy = (ay + by) / 2;
	dxr = icos(vs.roll); dyr = isin(vs.roll);

	// --- colori: cielo per l'ora (MODE), nebbia (REVERB, DAMP), mare (COLOR)
	static const uint8_t cielo[3][2][3] = { { { 4, 6, 18 }, { 22, 26, 48 } }, { { 36, 26, 60 }, { 150, 92, 70 } },
	                                        { { 50, 80, 130 }, { 150, 170, 190 } } };
	static const uint8_t mare[6][3] = { { 8, 10, 14 }, { 10, 32, 28 }, { 10, 26, 52 }, { 12, 52, 62 }, { 32, 36, 42 }, { 62, 68, 76 } };
	int lampo = vs.vita > 0 && ((vs.vita + fot) & (V(K_AM) > 50 ? 1 : 3)) ? V(K_RES) * vs.vita * 3 : 0; // 0-~2400
	int lf = luce + lampo / 12;
	uint8_t ct[3], ch[3], nb[3];
	int damp = V(K_DAMP), nebbia = V(K_REVERB);
	for (int c = 0; c < 3; c++) {
		ct[c] = sat8(cielo[tod][0][c] * lf >> 8);
		ch[c] = sat8(cielo[tod][1][c] * lf >> 8);
		nb[c] = sat8((ch[c] * (120 - damp) + 30 * damp) / 100);
	}

	// --- cielo e mare di fondo (Z_PAN, il primo disegnato): sopra e sotto l'orizzonte
	{
		int x[4], y[4];
		uint8_t c[4][3];
		oriz(-420, 330, &x[0], &y[0]); oriz(420, 330, &x[1], &y[1]); oriz(-420, 0, &x[2], &y[2]); oriz(420, 0, &x[3], &y[3]);
		for (int i = 0; i < 3; i++) { c[0][i] = c[1][i] = ct[i]; c[2][i] = c[3][i] = nb[i]; }
		// sole (MODE giorno/alba) o luna (notte): altezza = nota che suona; prima del cielo = sopra
		int el = 18 + (nota() - 21) * 3, sx, sy;
		oriz(110, el, &sx, &sy);
		static const uint8_t astro[3][3] = { { 200, 210, 228 }, { 240, 170, 90 }, { 250, 230, 160 } };
		int la = (90 + lf) > 256 ? 256 : 90 + lf;
		for (int i = 0; i < 10; i++) {
			POLY_F3 *t = prim(Z_PAN, sizeof(POLY_F3));
			if (!t) break;
			int a0 = i * 410, a1 = a0 + 410;
			setPolyF3(t);
			setRGB0(t, astro[tod][0] * la >> 8, astro[tod][1] * la >> 8, astro[tod][2] * la >> 8);
			setXY3(t, sx, sy, sx + (icos(a0) * 11 >> 12), sy + (isin(a0) * 11 >> 12), sx + (icos(a1) * 11 >> 12), sy + (isin(a1) * 11 >> 12));
		}
		// stelle (BELLS), solo dove il cielo e' scuro
		int ns = V(K_BELLS) * 40 / 99;
		uint32_t sm = 12345;
		for (int i = 0; i < ns && lf < 200; i++) {
			sm = sm * 1103515245 + 12345;
			int u = (int) (sm >> 16) % 640 - 320, v = 40 + (int) (sm >> 8) % 200, x0, y0;
			oriz(u, v, &x0, &y0);
			int b = (caso() & 3) ? 140 : 230;
			TILE *st = prim(Z_PAN, sizeof(TILE));
			if (!st) break;
			setTile(st); setXY0(st, x0, y0); setWH(st, 2, 2); setRGB0(st, b, b, b);
		}
		quad_g(Z_PAN, x, y, c, 0);
		oriz(-420, 0, &x[0], &y[0]); oriz(420, 0, &x[1], &y[1]); oriz(-420, -400, &x[2], &y[2]); oriz(420, -400, &x[3], &y[3]);
		const uint8_t *mc = mare[k[K_COLOR]];
		for (int i = 0; i < 3; i++) { c[0][i] = c[1][i] = c[2][i] = c[3][i] = sat8(mc[i] * lf >> 8); }
		quad_g(Z_PAN, x, y, c, 0);
	}

	// --- nuvole (Z_FIG): copertura = LAYERS, strati = CHORD, vento e FEEDBACK le spingono,
	// l'uragano le fa girare attorno a un occhio; PULSE 2 le illumina da dentro
	if (!fz) vs.spin += hur / 40 + V(K_FEED) / 8;
	int nn = 3 + V(K_LAYERS) * 9 / 99, strati = 1 + (k[K_INTERVAL] % 4);
	if (d.lampo_p[1] != vs.pulse_prima[1]) { vs.pulse_prima[1] = d.lampo_p[1]; vs.bagliore = 6; }
	if (vs.bagliore) vs.bagliore--;
	for (int i = 0; i < nn; i++) {
		if (!fz) { vs.nu[i] += vento / 30 + 1; if (vs.nu[i] > 380) vs.nu[i] -= 760; if (vs.nu[i] < -380) vs.nu[i] += 760; }
		int u = vs.nu[i], v = 40 + (vs.nv[i] % (30 * strati)) + (i % strati) * 25;
		if (hur > 400) { // spirale attorno all'occhio (u 0, v 110)
			int a = vs.spin + i * 341, rr = 40 + i * 22;
			u = (icos(a) * rr >> 12) * (4096 + hur) >> 12;
			v = 110 + ((isin(a) * rr >> 12) * hur >> 13);
		}
		int w = 70 + i * 9, hh = 16 + (i & 3) * 5, x[4], y[4];
		oriz(u - w, v + hh, &x[0], &y[0]); oriz(u + w, v + hh, &x[1], &y[1]); oriz(u - w, v - hh, &x[2], &y[2]); oriz(u + w, v - hh, &x[3], &y[3]);
		int gc = 34 + (vs.bagliore ? vs.bagliore * 18 : 0), lc = lf + 40;
		uint8_t c[4][3];
		for (int j = 0; j < 4; j++) for (int q = 0; q < 3; q++) c[j][q] = sat8((gc + (j < 2 ? 0 : 14) + q * 2) * lc >> 8);
		quad_g(Z_FIG, x, y, c, 1);
	}

	// --- mare 3D (Z_PANZ): griglia proiettata dalla GTE; aggiunta da vicino a lontano (nello stesso
	// livello dell'OT l'ultima aggiunta e' disegnata per prima: il lontano finisce dietro)
	// ogni vertice: proiezione e colore una volta sola (luce della cresta, schiuma = FOLD, nebbia
	// verso il lontano = REVERB); poi i quad prendono i 4 vertici gia' pronti
	static int16_t sx[RIGHE_M + 1][COL_M + 1], sy[RIGHE_M + 1][COL_M + 1];
	static uint8_t cv[RIGHE_M + 1][COL_M + 1][3];
	const uint8_t *mc = mare[k[K_COLOR]];
	int schiuma = 130 - V(K_FOLD) * 12 / 10;                                // sopra questa altezza: bianco
	int mcl[3] = { mc[0] * lf >> 8, mc[1] * lf >> 8, mc[2] * lf >> 8 };     // colore del mare alla luce di adesso
	for (int rr = 0; rr <= RIGHE_M; rr++) {
		int z = z_riga[rr], f = rr * nebbia * 256 / (12 * 100), nf[3]; // nebbia 0-256
		for (int q = 0; q < 3; q++) nf[q] = nb[q] * f;
		for (int c = 0; c <= COL_M; c++) {
			int x = (c - COL_M / 2) * z / 5, h = altezza(x, z);
			SVECTOR v = { x, -h, z };
			gte_ldv0(&v); gte_rtps(); gte_stsxy(&xy);
			sx[rr][c] = (int16_t) xy; sy[rr][c] = xy >> 16;
			int sh8 = 150 + h / 2, fo = h > schiuma ? (h - schiuma) * 3 * lf >> 8 : 0;
			for (int q = 0; q < 3; q++)
				cv[rr][c][q] = sat8((((mcl[q] * sh8 >> 7) + fo) * (256 - f) + nf[q]) >> 8);
		}
	}
	for (int rr = 0; rr < RIGHE_M; rr++) {
		for (int c = 0; c < COL_M; c++) {
			if (sx[rr + 1][c] < -600 || sx[rr + 1][c] > 900 || sy[rr][c] < -400) continue;
			POLY_G4 *pq = prim(Z_PANZ, sizeof(POLY_G4));
			if (!pq) break;
			const uint8_t *c0 = cv[rr + 1][c], *c1 = cv[rr + 1][c + 1], *c2 = cv[rr][c], *c3 = cv[rr][c + 1];
			setPolyG4(pq);
			setXY4(pq, sx[rr + 1][c], sy[rr + 1][c], sx[rr + 1][c + 1], sy[rr + 1][c + 1], sx[rr][c], sy[rr][c], sx[rr][c + 1], sy[rr][c + 1]);
			setRGB0(pq, c0[0], c0[1], c0[2]);
			setRGB1(pq, c1[0], c1[1], c1[2]);
			setRGB2(pq, c2[0], c2[1], c2[2]);
			setRGB3(pq, c3[0], c3[1], c3[2]);
		}
	}

	// --- fulmini: FM = quanto spesso, ogni passo del SEQ e ogni colpo di PULSE 1 ne fanno cadere
	// uno; RATIO = vicino/lontano; PWM = rami; RES = forza del lampo; AM = sfarfallio
	int nuovo = !fz && (int) (caso() % 1000) < V(K_FM) * V(K_FM) / 60;
	if (d.spos != vs.spos_prima) { vs.spos_prima = d.spos; if (k[K_STEPS]) nuovo = 1; }
	if (d.lampo_p[0] != vs.pulse_prima[0]) { vs.pulse_prima[0] = d.lampo_p[0]; nuovo = 1; }
	if (nuovo && vs.vita < 3) {
		int vicino = k[K_RATIO], u = (int) (caso() % 360) - 180, v = 150;
		vs.n_saetta = 10;
		for (int i = 0; i < vs.n_saetta; i++) {
			vs.saetta[i][0] = u;
			vs.saetta[i][1] = v;
			u += (int) (caso() % (12 + vicino * 6)) - (6 + vicino * 3);
			v -= (150 + vicino * 12) / vs.n_saetta;
		}
		vs.ramo = V(K_PWM) / 25;
		vs.vita = 8;
	}
	if (vs.vita) {
		if (!fz) vs.vita--;
		if (lampo || vs.vita > 5) {
			for (int i = 0; i + 1 < vs.n_saetta; i++) {
				int x0, y0, x1, y1;
				oriz(vs.saetta[i][0], vs.saetta[i][1], &x0, &y0);
				oriz(vs.saetta[i + 1][0], vs.saetta[i + 1][1], &x1, &y1);
				seg(Z_FIGZ, x0, y0, x1, y1, 230, 235, 255);
				if (i < vs.ramo * 3 && (i % 3) == 1) seg(Z_FIGZ, x0, y0, x0 + (i & 2 ? 14 : -14), y0 + 12, 170, 180, 220);
			}
		}
	}

	// --- pioggia (Z_FIGZ): quantita' = NOISE, lunghezza = WIDTH, inclinata dal vento e dal rollio
	int ng = V(K_NOISE) * 150 / 99, lg = 4 + V(K_WIDTH) / 6;
	int incl = clamp(vento / 4, -40, 40);                                  // pixel x 16 per pixel di caduta
	int ca = dxr, sa = dyr;                                                 // rollio
	for (int i = 0; i < ng; i++) {
		if (!fz) {
			vs.ry[i] += 90 * vs.rz[i];
			vs.rx[i] += incl * vs.rz[i];
			if (vs.ry[i] > H * 16) { vs.ry[i] -= H * 16 + 200; vs.rx[i] = caso() % (W * 16); }
			if (vs.rx[i] < 0) vs.rx[i] += W * 16;
			if (vs.rx[i] >= W * 16) vs.rx[i] -= W * 16;
		}
		int x0 = vs.rx[i] >> 4, y0 = vs.ry[i] >> 4, l2 = lg * vs.rz[i] / 2;
		int ddx = incl * l2 / 16, ddy = l2;
		int ex = (ddx * ca - ddy * sa) >> 12, ey = (ddx * sa + ddy * ca) >> 12;
		int b = (60 + vs.rz[i] * 30) * lf >> 8;
		seg(Z_FIGZ, x0, y0, x0 - ex, y0 - ey, sat8(b), sat8(b + 8), sat8(b + 20));
	}

	// --- la prua del peschereccio (Z_FIGZ, aggiunta prima della pioggia = disegnata sopra): ponte di
	// legno che si stringe fino alla punta, murate bianche dentro con il bordo rosso, il verricello,
	// l'albero di prua con la lanterna e la bandierina che sventola sottovento; sobbalza con l'onda.
	// Nello stesso livello dell'OT l'ultima primitiva aggiunta si disegna per prima: prima il davanti.
	{
		int bob = clamp((h0 - vs.h_prima) * 2, -6, 6);
		vs.h_prima = h0;
		int ty = 168 + bob, lz = lf + 30; // punta della prua
#define LZ(c) sat8((c) * lz >> 8)
		// bandierina in cima all'albero
		int fl = clamp(vento / 3, -40, 40), sv = isin(vs.t * 180) * (4 + (vento < 0 ? -vento : vento) / 30) >> 12;
		POLY_F3 *b = prim(Z_FIGZ, sizeof(POLY_F3));
		if (b) {
			setPolyF3(b);
			setRGB0(b, LZ(200), LZ(60), LZ(40));
			setXY3(b, 162, 58 + bob, 162, 72 + bob, 162 + 18 + fl, 63 + sv + bob);
		}
		// lanterna (si vede di piu' quando e' buio)
		int ll = 255 - lf / 2;
		TILE *lt = prim(Z_FIGZ, sizeof(TILE));
		if (lt) { setTile(lt); setXY0(lt, 157, 88 + bob); setWH(lt, 7, 7); setRGB0(lt, sat8(ll), sat8(ll * 85 / 100), sat8(ll / 2)); }
		// albero di prua, crocetta e strallo verso la punta
		POLY_F4 *m = prim(Z_FIGZ, sizeof(POLY_F4));
		if (m) {
			setPolyF4(m);
			setRGB0(m, LZ(150), LZ(150), LZ(140));
			setXY4(m, 158, 56 + bob, 163, 56 + bob, 158, 222 + bob, 163, 222 + bob);
		}
		seg(Z_FIGZ, 146, 100 + bob, 175, 100 + bob, LZ(150), LZ(150), LZ(140));
		seg(Z_FIGZ, 160, 60 + bob, 160, ty - 8, LZ(90), LZ(90), LZ(90));
		// verricello: cassa scura e tamburo con la cima
		POLY_G4 *v = prim(Z_FIGZ, sizeof(POLY_G4));
		if (v) {
			setPolyG4(v);
			setXY4(v, 132, 226 + bob, 188, 226 + bob, 132, 244 + bob, 188, 244 + bob);
			setRGB0(v, LZ(110), LZ(100), LZ(60)); setRGB1(v, LZ(110), LZ(100), LZ(60));
			setRGB2(v, LZ(50), LZ(45), LZ(30)); setRGB3(v, LZ(50), LZ(45), LZ(30));
		}
		POLY_F4 *vc = prim(Z_FIGZ, sizeof(POLY_F4));
		if (vc) { setPolyF4(vc); setRGB0(vc, LZ(40), LZ(50), LZ(55)); setXY4(vc, 124, 236 + bob, 196, 236 + bob, 120, 262, 200, 262); }
		// testa del dritto di prua
		POLY_F3 *st = prim(Z_FIGZ, sizeof(POLY_F3));
		if (st) { setPolyF3(st); setRGB0(st, LZ(170), LZ(40), LZ(30)); setXY3(st, 155, ty - 6, 165, ty - 6, 160, ty - 15); }
		for (int lato = -1; lato <= 1; lato += 2) {
			// bordo rosso in cima alla murata, poi la murata bianca vista da dentro
			seg(Z_FIGZ, 160 + lato * 3, ty - 7, 160 + lato * 205, 186 + bob, LZ(180), LZ(45), LZ(35));
			POLY_G4 *mu = prim(Z_FIGZ, sizeof(POLY_G4));
			if (!mu) break;
			setPolyG4(mu);
			setXY4(mu, 160 + lato * 2, ty - 6, 160 + lato * 2, ty, 160 + lato * 205, 188 + bob, 160 + lato * 190, 262);
			setRGB0(mu, LZ(150), LZ(150), LZ(140)); setRGB1(mu, LZ(110), LZ(110), LZ(105));
			setRGB2(mu, LZ(200), LZ(200), LZ(190)); setRGB3(mu, LZ(120), LZ(120), LZ(115));
		}
		// ponte: assi di legno che si stringono verso la punta
		for (int i = 0; i < 6; i++) {
			int xa = 160 - 3 + i, xb = 160 - 190 + i * 64;
			POLY_G4 *t = prim(Z_FIGZ, sizeof(POLY_G4));
			if (!t) break;
			setPolyG4(t);
			setXY4(t, xa, ty, xa + 1, ty, xb + 2, 262, xb + 63, 262);
			int br = i & 1 ? 96 : 80;
			setRGB0(t, LZ(br * 6 / 10), LZ(br * 5 / 10), LZ(br * 3 / 10)); setRGB1(t, LZ(br * 6 / 10), LZ(br * 5 / 10), LZ(br * 3 / 10));
			setRGB2(t, LZ(br), LZ(br * 8 / 10), LZ(br / 2)); setRGB3(t, LZ(br), LZ(br * 8 / 10), LZ(br / 2));
		}
#undef LZ
	}

	// --- lampo su tutto lo schermo (somma additiva, Z_TESTO = per ultimo)
	if (lampo) {
		TILE *t = prim(Z_TESTO, sizeof(TILE));
		if (t) {
			int iv = clamp(lampo / 12, 0, 160);
			setTile(t); setXY0(t, 0, 0); setWH(t, W, H); setRGB0(t, iv, iv, iv + iv / 8); setSemiTrans(t, 1);
		}
		DR_TPAGE *tp = prim(Z_TESTO, sizeof(DR_TPAGE)); // aggiunto dopo = eseguito prima: somma (modo 1)
		if (tp) setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
	}
	// aiuto all'ingresso (3 s): cosa muove la scena e come si esce (sopra a tutto, Z_TESTO)
	if (vs.aiuto) {
		vs.aiuto--;
		const char *a1 = "NOISE RAIN  CUT LIGHT  FM BOLTS", *a2 = "SUB SWELL  MACROS STORM", *a3 = mm ? "CLICK: BACK" : "START+SELECT: BACK";
		testo(160 - largo(a1) / 2, 196, a1, 128, 128, 128);
		testo(160 - largo(a2) / 2, 208, a2, 128, 128, 128);
		testo(160 - largo(a3) / 2, 220, a3, 128, 104, 32);
		TILE *f = prim(Z_TESTO, sizeof(TILE)); // aggiunto dopo il testo = disegnato prima
		if (f) { setTile(f); setXY0(f, 30, 193); setWH(f, 260, 42); setRGB0(f, 12, 14, 22); }
	}
	gte_SetGeomScreen(FUOCO); // i pannelli usano la distanza normale
}

static void storm_tabella(void);

void storm_avvia(void) {
	SetVideoMode(NTSC ? MODE_NTSC : MODE_PAL);
	ResetGraph(0);
	InitGeom();
	gte_SetGeomOffset(W / 2, H / 2);
	gte_SetGeomScreen(FUOCO);
	RECT rf = { FONT_X, FONT_Y, 64, 112 }, rc = { CLUT_X, CLUT_Y, 16, 1 };
	LoadImage(&rf, (const uint32_t *) font_tex);
	LoadImage(&rc, (const uint32_t *) font_clut);
	DrawSync(0);
	SpuInit();
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);

	for (int i = 0; i < 2; i++) {
		SetDefDrawEnv(&fb[i].draw, 0, i * H, W, H);
		SetDefDispEnv(&fb[i].disp, 0, i * H + VIS_Y, W, VIS_H);
		fb[i].disp.screen.h = VIS_H; // PAL: senza, solo 240 righe
		setRGB0(&fb[i].draw, 10, 10, 18); // blu notte
		fb[i].draw.isbg = 1;
	}

	// Onde dopo 0x1000 (0x1000-0x100F = campione muto caricato da SpuInit); area del riverbero a zero
	static uint32_t zeri[1024];
	SpuSetTransferStartAddr(SUONI);
	SpuWrite((const uint32_t *) suoni, SUONI_LEN);
	SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
	for (uint32_t a = REV_FONDO; a < 0x80000; a += sizeof(zeri)) {
		SpuSetTransferStartAddr(a);
		SpuWrite(zeri, sizeof(zeri));
		SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
	}

	reset();
	macro_somma();
	vs_init();

	InitPAD(padbuf[0], 34, padbuf[1], 34);
	InitCARD(1); // memory card insieme ai pad (1 = i pad restano attivi)
	StartPAD();
	StartCARD();
	_bu_init();
	ChangeClearPAD(0);
	patch_salva(casa); // HOME all'inizio = il suono di partenza

	TIMER_CTRL(2)   = 0x0258; // CLK/8, azzera al target, IRQ al target ripetuto
	TIMER_RELOAD(2) = 4233600 / TICK_HZ;
	EnterCriticalSection();
	InterruptCallback(IRQ_TIMER2, &tick);
	ExitCriticalSection();
	dsp_accendi();
	spu_voci();
	spu_aggiorna();
	SetDispMask(1);

	nextp = fb[0].buf;
	ClearOTagR(fb[0].ot, OT_LEN);
	storm_tabella();
}

// Il corpo del main loop della PS1: un fotogramma (1/50 s). Le variabili del loop restano tra una
// chiamata e l'altra.
static uint32_t fot = 0;
static int      muto = 0, sfuma = 150;    // MUTE: dissolvenza di 3 s (150 fotogrammi) del volume generale
static int      ed_posto = 0;
static int      lacc[8] = { 0 };          // levette: accumulatori (16 = un passo)

/* PC: azioni comuni (standard/controlli/azioni.md), per ora dalla tastiera. Le manopole della
   tastiera arrivano come tasti del pad PS1 (controlli.c); qui pagine, passi da 10 e azioni speciali. */
static int tenuta[2];      // pagina tenuta dalla tastiera (0 = nessuna)
static int est[2][4];      // passi in piu' per le 4 manopole (Shift = 10), usati nel fotogramma
static int hold_tail = -1; // HOLD acceso: TAIL di prima (vale finche' TAIL resta a 99)
static int pag_ora[2];     // pagina aperta nell'ultimo fotogramma (per il modo prova)
static int davanti[2];     // pannello della pagina a riposo davanti: 1 = tenuto, 2 = fisso, 3 = fisso e ripremuto
// Pagine nell'ordine della tabella dei parametri (tasti 1-9, -): C2 9 = SCENE, 10 = EDIT
static const int8_t pag_ordine[2][11] = { { 0, 1, 2, 3, 4, 5, P_TEXTURE, -1, -1, -1, -1 },
                                          { 0, 1, 2, 3, 4, P_MACROS, P_SEQ, P_PULSE, P_CLOCK, P_SCENE, P_EDIT } };

// Nuovo stato di un interruttore: CAMBIA, ACCESO, SPENTO
static int vuoi(int ora, int modo) { return modo == CAMBIA ? !ora : modo == ACCESO; }

// PC, note MIDI tenute (128 bit), quando sono state premute (ticks, 1200 al secondo) e velocita'
// dell'ultima: finche' se ne tiene una, SWELL sale fino a li'
static uint32_t nt_giu[4], nt_t[128];
static int      nt_vel, nt_pres; // velocita' dell'ultima nota; pressione (0..V99) = tetto di SINK

// Note MIDI premute entro 50 ms con la nota n: la piu' bassa e' la nota, gli intervalli dalla piu'
// bassa scelgono CHORD se c'e' un accordo uguale (ottave sopra ripiegate). Sola = legato: n e basta.
static int accordo_midi(int n) {
	static const uint16_t forma[8] = { 0, 1 << 7, 0, 0, 1 << 3 | 1 << 7, 1 << 4 | 1 << 7, 1 << 5 | 1 << 7,
	                                   1 << 3 | 1 << 7 | 1 << 10 }; // intervalli (bit 1-11) di 5TH, MINOR, MAJOR, SUS4, MIN7
	int basso = n, dentro = 0, ott = 0;
	for (int i = 0; i < 128; i++)
		if (nt_giu[i >> 5] >> (i & 31) & 1 && ticks - nt_t[i] <= TICK_HZ / 20 && i < basso) basso = i;
	for (int i = basso + 1; i < 128; i++)
		if (nt_giu[i >> 5] >> (i & 31) & 1 && ticks - nt_t[i] <= TICK_HZ / 20) {
			if ((i - basso) % 12) dentro |= 1 << (i - basso) % 12;
			else                  ott = i - basso >= 24 ? 2 : ott ? ott : 1;
		}
	int c = !dentro && ott ? 1 + ott : -1; // solo ottave: OCTAVE o 2 OCT
	for (int j = 1; dentro && j < 8; j++)
		if (forma[j] == dentro) c = j;
	if (c >= 0) k[K_INTERVAL] = c;
	return basso;
}

static void storm_azione(const Azione *a) {
	const char *n = a->nome;
	int m = a->modo, c = a->lato;
	// LOCK: passano solo MUTE e LOCK (e le note lasciate e il clock, che non sono comandi)
	if (blocco && !(a->tipo == A_SPECIALE && (!strcmp(n, "MUTE") || !strcmp(n, "LOCK"))) &&
	    !(a->tipo == A_NOTA && !a->passi) && a->tipo != A_CLOCK) return;
	if (a->tipo == A_PAGINA) {
		if (c < 0) { // Esc: chiude le pagine fisse; nella GUIDE come Triangolo
			fissa[0] = fissa[1] = tenuta[0] = tenuta[1] = davanti[0] = davanti[1] = 0;
			if (gconf) gconf = 0;
			else       guida = 0;
			return;
		}
		int p = a->n >= 0 && a->n < 11 ? pag_ordine[c][a->n] : -1;
		if (p < 0) return;
		// Pagina a riposo davanti (mouse: solo lui manda TIENI della pagina 0): tenuta, poi un clic la
		// fissa e il clic dopo la chiude. FISSA 0 da sola (tasto 0) torna a riposo come prima.
		if (p == 0 && m != FISSA) {
			tenuta[c] = 0;
			if (m == TIENI)            davanti[c] = davanti[c] ? 3 : 1;
			else if (davanti[c] != 2)   davanti[c] = davanti[c] == 3 ? 2 : 0;
			return;
		}
		if (p == 0 && (davanti[c] & 1)) {
			fissa[c] = tenuta[c] = 0;
			davanti[c] = davanti[c] == 1 ? 2 : 0;
			return;
		}
		if (m != TIENI) davanti[c] = 0;
		if (m == TIENI) tenuta[c] = p;
		else {
			tenuta[c] = 0;
			if (m == FISSA) fissa[c] = fissa[c] == p ? 0 : p; // la gialla si chiude ritoccandola
		}
	} else if (a->tipo == A_MANOPOLA) est[c][a->n] += a->passi;
	else if (a->tipo == A_VALORE) { // mouse e MIDI: il valore impostato, tra min e max
		const Param *p = controlli_param(n);
		if (!p) return;
		int v = p->min + (int) (a->v * (float) (p->max - p->min) + 0.5f);
		*p->valore = clamp(v, p->min, p->max);
	} else if (a->tipo == A_NOTA) { // nota di casa = ROOT + OCTAVE (scivola col GLIDE); i gesti tornano a 0
		int v = a->n & 127;
		if (m) { // MIDI: con PLAY acceso e spento; lasciate tutte, il drone resta sull'ultima
			if (!a->passi) { // lasciata l'ultima nota, niente SINK (non tutte le tastiere mandano lo 0)
				nt_giu[v >> 5] &= ~(1u << (v & 31));
				if (!(nt_giu[0] | nt_giu[1] | nt_giu[2] | nt_giu[3])) nt_pres = 0;
				return;
			}
			nt_giu[v >> 5] |= 1u << (v & 31);
			nt_t[v] = ticks;
			nt_vel  = a->passi;
			v = accordo_midi(v);
		} else if (!a->passi) return;
		while (v > 21 + 12 * k_max[K_OCT] + 11) v -= 12; // fuori da LA0-SOL#4: la stessa nota in un'ottava che c'e'
		while (v < 21) v += 12;
		v -= 21;
		k[K_OCT] = v / 12, k[K_ROOT] = v % 12, gest_deg = gest_oct = 0;
	} else if (a->tipo == A_PIEGA) // pitch bend: +-passi semitoni, in 1/4096 come FINE
		piega = (int) lroundf((exp2f(a->v * a->passi / 12.0f) - 1) * 4096);
	else if (a->tipo == A_SCENA) scena_vai(a->n & 7); // posto vuoto: niente
	else if (a->tipo == A_CLOCK) { // clock MIDI, 24 tick per battito
		clk_t = 0;
		if (m == C_STOP) { clk_fermo = 1; return; }
		if (m != C_TICK) { clk_fermo = 0; clk_n = -1; return; } // Start, Continue: il prossimo tick e' il battito 1
		if (a->v > 0) { clk_est = (int) (a->v * 1420 + 0.5f); clk_bpm = (int) (a->v + 0.5f); }
		if (clk_fermo) return;
		if (clk_n < 0) { // il battito cade adesso, come col TAP
			d.clk = (1u << 24) - 1;
			d.s16 = clk_base = (d.s16 + 3) & ~3;
			clk_n = 0;
			return;
		}
		if (++clk_n % 6) return;
		// un sedicesimo ogni 6 tick: se il motore e' fuori di piu' di mezzo sedicesimo, si rimette a posto
		int e = clk_base + clk_n / 6;                                  // il sedicesimo che cade adesso
		int64_t err = ((int64_t) d.s16 - e - 1) * (1 << 24) + d.clk;  // 0 = appena suonato
		if (err < -(1 << 23))     { d.clk = (1u << 24) - 1; d.s16 = e; } // in ritardo: suona al prossimo blocco
		else if (err > (1 << 23)) { d.clk = 0; d.s16 = e + 1; }          // in anticipo: aspetta
	}
	else if (!strcmp(n, "EXPORT")) { if (plugin_si && m != SPENTO) { card_scrivi(); smsg = "EXPORTED"; smsg_t = 100; } }
	else if (!strcmp(n, "IMPORT")) { if (plugin_si && m != SPENTO) SDL_SetAtomicInt(&imp_chiesta, 1); }
	else if (!strcmp(n, "SINK"))   nt_pres = (int) (a->v * V99 + 0.5f); // pressione MIDI
	else if (!strcmp(n, "MUTE"))   { if (!guida) muto = vuoi(muto, m); }
	else if (!strcmp(n, "PLAY"))   { if (!guida) suona = vuoi(suona, m); }
	else if (!strcmp(n, "FREEZE")) { if (!guida) freeze = vuoi(freeze, m); }
	else if (!strcmp(n, "VISUAL")) {
		if (visual == vuoi(visual, m)) return;
		visual ^= 1;
		if (visual) vs.aiuto = 150;
		else        vis_uscito = 1;
	}
	else if (!strcmp(n, "GUIDE")) guida = vuoi(guida, m);
	else if (!strcmp(n, "LOCK"))  blocco = vuoi(blocco, m);
	else if (!strcmp(n, "SALVA")) { if (m != SPENTO) { guida = 1; gsel = G_SAVE_SCENE; gconf = 0; } } // GUIDE > SAVE SCENE
	else if (!strcmp(n, "AVANTI") || !strcmp(n, "INDIETRO")) { if (m != SPENTO && tut && !guida) tut_avanza(n[0] == 'A' ? 1 : -1); } // come Start + R1 / L1
	else if (!strcmp(n, "HOLD")) { // acceso = TAIL 99; spento = TAIL di prima
		int on = k[K_TAIL] == 99;
		if (vuoi(on, m) == on) return;
		if (!on) { hold_tail = k[K_TAIL]; k[K_TAIL] = 99; }
		else     { k[K_TAIL] = hold_tail >= 0 ? hold_tail : k_init[K_TAIL]; hold_tail = -1; }
	}
}

// PC: le azioni in coda. Si prendono anche a ogni blocco audio (main.c), non solo a ogni fotogramma
// come sulla PS1: una nota o una manopola MIDI vale dal blocco dopo (pochi ms), non dal fotogramma
void storm_azioni(void) { for (Azione a; controlli_prendi(&a);) storm_azione(&a); }

// Per il modo prova: pagina aperta del lato c e stato degli interruttori
static const char *nome_pag(int c, int p) { return p == P_SCENE ? "SCENE" : p == P_EDIT ? "EDIT" : pag_nome[c][p]; }
static const char *storm_pagina(int c) { return nome_pag(c, pag_ora[c]); }

static int storm_interruttore(const char *n) {
	static const char *const nome[7] = { "MUTE", "PLAY", "FREEZE", "VISUAL", "GUIDE", "LOCK", "HOLD" };
	int stato[7] = { muto, suona, freeze, visual, guida, blocco, k[K_TAIL] == 99 };
	for (int i = 0; i < 7; i++)
		if (!strcmp(n, nome[i])) return stato[i];
	return -1;
}

/* PC, mouse: testata, menu del tasto destro, pagine e striscia di PLAY in basso, aiuto della cosa
   sotto il puntatore. Si disegnano solo dopo che il mouse e' passato sul disegno (mm). */

static int lock_t; // fotogrammi da cui il tasto sinistro tiene giu' LOCK (il pulsante si riempie in 1 s)

// Pulsante di un'azione speciale: acceso = col suo colore, spento = scuro; restituisce la larghezza
static int pulsante(int x, int y, const char *nome, int on, int r, int g, int b, int tieni, const char *aiuto) {
	int w = largo(nome) + 6;
	testo(x + 3, y + 2, nome, on ? 128 : 96, on ? 128 : 96, on ? 128 : 110);
	if (tieni && lock_t) tile(x, y, w * (lock_t < 50 ? lock_t : 50) / 50, 13, 200, 170, 60); // aggiunto prima = sopra
	tile(x, y, w, 13, on ? r : 30, on ? g : 32, on ? b : 46);
	Zona *z = zona(Z_PULSANTE, x, y, w, 13);
	z->nome = nome, z->tieni = (int16_t) tieni, z->aiuto = aiuto;
	return w;
}

// Tutorial col mouse: < e > (passo prima e dopo, come Start + L1 / R1) sotto il numero del passo, e
// PLAY in cima tra AUTO e VISUAL (la riga della nota e' del testo del passo; serve al passo SWELL)
static void tut_frecce(void) {
	for (int j = 0; j < 2; j++) {
		int x = 16 + j * 17;
		testo(x + 4, 30, j ? ">" : "<", 128, 128, 128);
		tile(x, 28, 14, 13, 50, 60, 90);
		Zona *z = zona(Z_PULSANTE, x, 28, 14, 13);
		z->nome = j ? "AVANTI" : "INDIETRO", z->aiuto = j ? "NEXT STEP" : "BACK ONE STEP";
	}
	pulsante(128, 1, "PLAY", suona, 130, 100, 20, 0, "PLAY: PLAY NOTES ON THE KEY\nSTRIP BELOW (NO PAGE OPEN)");
}

// In cima: STORM, battito del CLOCK, AUTO e i pulsanti VISUAL GUIDE LOCK; nella riga della nota (riga1)
// PLAY FREEZE MUTE HOLD, nello spazio dei riquadri degli stati accesi
static void testata_mouse(int riga1) {
	int xb = testo(16, 3, "STORM", 128, 128, 128) + 6, battito = (d.s16 & 3) == 0, xa = xb + (clk_est ? 8 : 0);
	tile(xb, 4, 9, 9, battito ? 230 : 50, battito ? 230 : 50, battito ? 230 : 60);
	if (clk_est) testo(xb + 11, 3, "E", 128, 128, 128); // il CLOCK segue il clock MIDI
	if (auto_on) {
		testo(xa + 18, 3, "AUTO", 128, 128, 128);
		tile(xa + 15, 1, largo("AUTO") + 6, 13, 40, 110, 60);
	}
	zona(Z_TESTATA, 14, 1, 140, 13)->aiuto = "STORM. THE DOT IS THE CLOCK BEAT\nDOUBLE-CLICK: FULL SCREEN";
	static const char *const nome[7] = { "VISUAL", "GUIDE", "LOCK", "PLAY", "FREEZE", "MUTE", "HOLD" };
	static const uint8_t col[7][3] = { { 110, 70, 40 }, { 60, 70, 140 }, { 140, 40, 40 }, { 130, 100, 20 },
	                                   { 20, 100, 130 }, { 90, 90, 100 }, { 100, 60, 140 } };
	const char *aiuto[7] = { "VISUAL: A STORM AT SEA\nCLICK ANYWHERE TO COME BACK", "GUIDE: HELP, SAVE AND LOAD",
	                         blocco ? "LOCKED: ONLY MUTE WORKS\nHOLD 1 S TO UNLOCK" : "LOCK: HOLD 1 S, THEN ONLY\nMUTE WORKS (NO ACCIDENTS)",
	                         "PLAY: PLAY NOTES ON THE KEY\nSTRIP BELOW (NO PAGE OPEN)", "FREEZE: STOP ALL MOTION",
	                         "MUTE: FADE THE SOUND OUT / IN", "HOLD: ENDLESS REVERB TAIL" };
	int on[7] = { visual, guida, blocco, suona, freeze, muto, k[K_TAIL] == 99 };
	for (int j = riga1 ? 6 : 2, x = 304; j >= 0; j--) { // da destra
		if (j == 2) x = 304;
		x -= largo(nome[j]) + 6;
		pulsante(x, j < 3 ? 1 : 14, nome[j], on[j], col[j][0], col[j][1], col[j][2], j == 2 ? 1000 : 0, aiuto[j]);
		x -= 3;
	}
}

// Menu del tasto destro, dentro lo schermo: si aggiunge per primo, cosi' resta sopra a tutto il testo
static void menu_disegna(void) {
	int x, y;
	const Param *p;
	if (!controlli_menu(&x, &y, &p)) return;
	static const char *const voce[MENU_N] = { "LEARN", "CLEAR", "DEFAULT" };
	int v = controlli_menu_voce(mx, my);
	for (int j = 0; j < MENU_N; j++) { // LEARN e CLEAR servono al MIDI: grigi finche' non c'e'
		int on = j == MENU_DEFAULT || controlli_midi();
		testo(x + 5, y + j * MENU_RIGA + 2, voce[j], on ? 128 : 60, on ? 128 : 60, on ? 128 : 70);
	}
	if (v >= 0) tile_z(Z_TESTO, x, y + v * MENU_RIGA, MENU_W, MENU_RIGA, 50, 60, 90);
	tile_z(Z_TESTO, x, y, MENU_W, MENU_N * MENU_RIGA, 20, 22, 34);
	tile_z(Z_TESTO, x - 1, y - 1, MENU_W + 2, MENU_N * MENU_RIGA + 2, 110, 110, 130);
}

// Riga r dell'aiuto in basso: le pagine del lato c (righe 1-2 lato 1, 3-4 lato 2); ora = pagina aperta
static void schede(int r, int c, int ora) {
	static const int8_t da[4] = { 0, 4, 0, 5 }, a[4] = { 4, 7, 5, 11 };
	for (int o = da[r], x = 34, y = 170 + r * 14; o < a[r]; o++) {
		int p = pag_ordine[c][o], on = p == ora, w = largo(nome_pag(c, p)) + 6;
		testo(x + 3, y, nome_pag(c, p), on ? 128 : 96, on ? 104 : 96, on ? 32 : 110);
		if (on) tile(x, y - 2, w, 13, 40, 50, 70);
		Zona *z = zona(Z_PANNELLO, x, y - 2, w, 13);
		z->lato = (int8_t) c, z->n = (int8_t) o;
		x += w;
	}
}

// PLAY (lato 1 a riposo): OCT-, un'ottava da LA a SOL# dell'OCTAVE di adesso, OCT+. Clic su un tasto =
// quella nota; tenuto nella meta' alta = SWELL, bassa = SINK (come Su/Giu' del pad)
static void striscia(void) {
	for (int j = 0; j < 2; j++) {
		int x = j ? 264 : 34, w = j ? 34 : 32;
		const char *t = j ? "OCT+" : "OCT-";
		testo(x + (w - largo(t)) / 2, 176, t, 128, 128, 128);
		tile(x, 168, w, 27, 50, 60, 90);
		Zona *z = zona(Z_SCELTA, x, 168, w, 27);
		z->par = k_par[K_OCT], z->valore = (int16_t) k[K_OCT], z->passi = j ? 1 : -1;
		z->aiuto = j ? "ONE OCTAVE UP" : "ONE OCTAVE DOWN";
	}
	for (int i = 0; i < 12; i++) {
		int x = 68 + i * 16, nero = 0xa52 >> i & 1, on = i == k[K_ROOT]; // diesis: LA# DO# RE# FA# SOL#
		int r = on ? 200 : nero ? 34 : 110, g = on ? 170 : nero ? 36 : 110, b = on ? 60 : nero ? 48 : 122;
		if (!nero) {
			char t[2] = { nota_nome[(i + 9) % 12][0], 0 };
			testo(x + (15 - largo(t)) / 2, 170, t, 20, 20, 30);
		}
		tile(x, 168, 15, 13, r, g, b);
		tile(x, 182, 15, 13, r, g, b);
		for (int h = 0; h < 2; h++) {
			Zona *z = zona(Z_NOTA, x, h ? 181 : 168, 16, h ? 14 : 13);
			z->n = (int8_t) (21 + 12 * k[K_OCT] + i), z->passi = h ? -1 : 1;
		}
	}
}

// Aiuto della cosa sotto il puntatore: righe di testo in (x, y) (una sola nella GUIDE)
static void aiuto_mouse(int x, int y, int righe) {
	char a[2][48] = { "", "" };
	const Zona *z = mx >= 0 ? zona_sotto(zz, nz, mx, my) : NULL;
	int v = controlli_menu_voce(mx, my);
	const char *t = NULL;
	if (controlli_learn())
		t = "LEARN: MOVE A MIDI KNOB (ESC: CANCEL)";
	else if (v >= 0 && !controlli_midi() && v != MENU_DEFAULT)
		t = v == MENU_LEARN ? "LEARN: LINK A MIDI KNOB\n(NEEDS A MIDI PORT)" : "CLEAR: REMOVE THE MIDI LINK\n(NEEDS A MIDI PORT)";
	else if (v >= 0)
		t = v == MENU_DEFAULT ? "DEFAULT: BACK TO THE START VALUE" : v == MENU_LEARN ? "LEARN: MOVE A MIDI KNOB NOW"
		                                                            : "CLEAR: REMOVE THE MIDI LINK";
	else if (!z)
		t = "CLICK A PANEL OR A PAGE NAME\nCLICK OUTSIDE: CLOSE THE PAGES";
	else if (z->aiuto)
		t = z->aiuto;
	else if (z->tipo == Z_PANNELLO) {
		sprintf(a[0], "%s: CLICK = OPEN, AGAIN = CLOSE", nome_pag(z->lato, pag_ordine[z->lato][z->n]));
		strcpy(a[1], "HOLD = OPEN ONLY WHILE HELD");
	} else if (z->tipo == Z_MANOPOLA) {
		sprintf(a[0], "%s: DRAG UP/DOWN OR WHEEL", z->par->nome);
		strcpy(a[1], "SHIFT FINE, 2-CLICK RESET, RIGHT MENU");
	} else if (z->tipo == Z_SCELTA) {
		sprintf(a[0], "%s: CLICK = NEXT, OR WHEEL", z->par->nome);
		strcpy(a[1], "RIGHT-CLICK: MENU");
	} else if (z->tipo == Z_NOTA) {
		sprintf(a[0], "%s%d: CLICK = PLAY THIS NOTE", nota_nome[z->n % 12], z->n / 12 - 1);
		strcpy(a[1], "HOLD TOP = SWELL, BOTTOM = SINK");
	}
	for (int r = 0; t && r < 2; r++) { // due righe separate da '\n'
		int n = 0;
		while (t[n] && t[n] != '\n' && n < 47) { a[r][n] = t[n]; n++; }
		a[r][n] = 0;
		t = t[n] ? t + n + 1 : NULL;
	}
	testo(x, y, a[0], 128, 128, 128);
	if (righe > 1) testo(x, y + 13, a[1], 100, 100, 110);
}

void storm_fotogramma(void) {
	{
		psx_pad_leggi();
		read_pad();
		storm_azioni();
		fot++;

		/* Tutti e due i controller: tieni un tasto = pagina, frecce e dorsali = 4 manopole.
		   C2 con Select tenuto: MACROS, e con un tasto pagina EDIT di quella macro. */
		int pag[2];
		static int tocco[2]; // fotogrammi in cui il pannello della pagina di riposo resta avanti
		static const uint16_t piu[4] = { PAD_UP, PAD_RIGHT, PAD_R1, PAD_R2 }, meno[4] = { PAD_DOWN, PAD_LEFT, PAD_L1, PAD_L2 };
		static const uint16_t facce[4] = { PAD_CROSS, PAD_SQUARE, PAD_TRIANGLE, PAD_CIRCLE };
		// LOCK: C1 Start + X tenuti 2 s. Bloccato: solo MUTE (C1 Start) e lo sblocco
		static int blk_t;
		if ((held[0] & (PAD_START | PAD_CROSS | PAD_SELECT)) == (PAD_START | PAD_CROSS)) {
			if (++blk_t == 100) blocco ^= 1;
		} else
			blk_t = 0;
		static int st2_t; // C2 Start tenuto: fotogrammi (corto = FREEZE, oltre 8 = pagina SCENE)
		// Start + Select insieme su uno dei due controller: toccati (lasciati prima di 1 s) = VISUAL,
		// tenuti 1 s = GUIDE
		static int combo_t[2], combo_usato[2];
		for (int c = 0; c < 2; c++) {
			if ((held[c] & (PAD_START | PAD_SELECT)) == (PAD_START | PAD_SELECT)) {
				if (++combo_t[c] == 50) { guida ^= 1; combo_usato[c] = 1; }
			} else {
				if (combo_t[c] && !combo_usato[c]) {
					visual ^= 1;
					if (visual) vs.aiuto = 150; // 3 s di aiuto in basso
					else        vis_uscito = 1;
				}
				combo_t[c] = combo_usato[c] = 0;
			}
		}
		if (guida) { // il menu: SU/GIU' scelgono, X conferma (CLOSE), Triangolo chiude
			pag[0] = pag[1] = 0;
			for (int c = 0; c < 2; c++) {
				int ds = !!(hit[c] & PAD_DOWN) - !!(hit[c] & PAD_UP) - est[c][0]; // PC: anche la manopola 1 (mouse)
				if (ds) { gsel = ((gsel + ds) % N_GUIDA + N_GUIDA) % N_GUIDA; gconf = 0; }
				if (gsel == G_TUTORIAL && (hit[c] & PAD_CROSS)) { // TUTORIAL: chiedi conferma / parti / chiudi
					if (tut)        { tut = 0; guida = 0; }
					else if (gconf) { tut_inizia(); guida = gconf = 0; }
					else            gconf = 1;
				} else if ((gsel == G_SAVE_SCENE || gsel == G_LOAD_SCENE) && ((hit[c] & (PAD_LEFT | PAD_RIGHT)) || est[c][1])) {
					gposto = ((gposto + !!(hit[c] & PAD_RIGHT) - !!(hit[c] & PAD_LEFT) + est[c][1]) % N_POSTI + N_POSTI) % N_POSTI;
					gconf  = 0;
				} else if (gsel == G_SAVE_SCENE && (hit[c] & PAD_CROSS) && !(gsalvato == gposto && fot - gsalvato_fot < 100)) {
					// SAVE: posto pieno = conferma. PC: un secondo OK sullo stesso posto entro 2 s
					// non fa niente (il doppio clic sul posto gia' scelto mostrava insieme SAVED e OVERWRITE IT?)
					if (posto(gposto)[0] == 'P' && !gconf) gconf = 1;
					else {
						patch_salva(posto(gposto));
						if (!plugin_si) card_scrivi();
						gmsg     = "SAVED";
						gmsg_t   = 100;
						gsalvato = gposto;
						gsalvato_fot = fot;
						gconf  = 0;
					}
				} else if (gsel == G_LOAD_SCENE && (hit[c] & PAD_CROSS)) { // LOAD: scivola nel suono salvato
					if (posto(gposto)[0] == 'P') {
						scena_vai(gposto);
						gmsg = "LOADED";
						guida = 0;
					}
					else gmsg = "EMPTY SLOT";
					gmsg_t = 100;
				} else if (gsel == G_RESET_SOUND && (hit[c] & PAD_CROSS)) { // RESET: solo con conferma
					if (gconf) { reset(); guida = gconf = 0; }
					else       gconf = 1;
				} else if (hit[c] & PAD_TRIANGLE) {
					if (gconf) gconf = 0;
					else       guida = 0;
				} else if ((hit[c] & PAD_CROSS) && gsel == N_GUIDA - 1)
					guida = 0;
			}
		} else for (int c = 0; c < 2; c++) {
			if (tenuta[c])
				pag[c] = tenuta[c];
			else if (c == 0 && (held[0] & PAD_SELECT))
				pag[0] = held[0] & PAD_CROSS ? P_TEXTURE : 5; // OSC B (toccato da solo, al rilascio: PLAY)
			else if (c == 1 && (held[1] & PAD_SELECT)) {
				pag[1] = P_MACROS;
				for (int f = 0; f < 4; f++)
					if (held[1] & facce[f]) pag[1] = P_SEQ + f;
			} else if (c == 1 && (held[1] & PAD_START) && st2_t >= 8)
				pag[1] = P_SCENE;
			else
				pag[c] = pagina(c, held[c], hit[c], N_PAG - 1, fot);
			if (pag[c] == P_EDIT) { // EDIT: SU/GIU = posto 1-16 (4 per macro), SX/DX = attenuatore, L1/R1 = destinazione
				ed_posto = clamp(ed_posto + passo(c, PAD_DOWN, 0) - passo(c, PAD_UP, 0) - est[c][0], 0, 15); // PC: + mouse
				int m = ed_posto >> 2, sp = ed_posto & 3;
				mamt[m][sp]  = clamp(mamt[m][sp] + passo(c, PAD_RIGHT, 0) - passo(c, PAD_LEFT, 0) + est[c][1], -9, 9);
				mdest[m][sp] = ((mdest[m][sp] + passo(c, PAD_R1, 0) - passo(c, PAD_L1, 0) + est[c][2]) % N_MDEST + N_MDEST) % N_MDEST;
				continue;
			}
			if (pag[c] == P_SCENE) { // SCENE: SU/GIU scena, SX/DX MORPH, R1 GO, L1 HOME, R2 NUDGE, L2 AUTO
				gposto = ((gposto + !!(hit[1] & PAD_DOWN) - !!(hit[1] & PAD_UP) - est[1][0]) % N_POSTI + N_POSTI) % N_POSTI; // PC: + mouse
				k[K_MORPH] = clamp(k[K_MORPH] + !!(hit[1] & PAD_RIGHT) - !!(hit[1] & PAD_LEFT), 0, k_max[K_MORPH]);
				const char *m = 0;
				if ((hit[1] & PAD_R1) || est[1][2] > 0) m = scena_vai(gposto) ? "GO" : "EMPTY SLOT";
				if ((hit[1] & PAD_L1) || est[1][2] < 0) { casa_torna(); m = "HOME"; }
				if ((hit[1] & PAD_R2) || est[1][3] > 0) { spinta(); m = "NUDGE"; }
				if ((hit[1] & PAD_L2) || est[1][3] < 0) { auto_on ^= 1; auto_t = 1 << 20; m = auto_on ? "AUTO ON" : "AUTO OFF"; }
				if (m) { smsg = m; smsg_t = 100; }
				continue;
			}
			if (c == 0 && (held[0] & PAD_START)) continue; // Start + frecce/dorsali: comandi del tutorial
			if (c == 0 && !pag[0] && suona) { // PLAY: il d-pad si suona (SWELL e SINK sotto, a ogni fotogramma)
				gest_deg = clamp(gest_deg + !!(hit[0] & PAD_RIGHT) - !!(hit[0] & PAD_LEFT), -14, 14);
				gest_oct = clamp(gest_oct + !!(hit[0] & PAD_R1) - !!(hit[0] & PAD_L1), -2, 2);
				k[K_INTERVAL] = (k[K_INTERVAL] + !!(hit[0] & PAD_R2) - !!(hit[0] & PAD_L2) + 9) % 9;
				continue;
			}
			for (int j = 0; j < 4; j++) {
				int i = pag_k[c][pag[c]][j];
				if (i < 0) continue;
				if (i == K_NUDGE) { // CLOCK: R1 / L1 = il battito un sedicesimo/4 avanti o indietro
					if ((hit[c] & PAD_R1) || est[c][j] > 0) d.clk += 1 << 22;
					if ((hit[c] & PAD_L1) || est[c][j] < 0) d.clk = d.clk > 1u << 22 ? d.clk - (1 << 22) : 0;
					continue;
				}
				if (i == K_TAP) { // CLOCK: L2/R2 a tempo = tap tempo (media degli ultimi 4 intervalli, in tick)
					static uint32_t taps[5];
					static int ntap;
					if (!(hit[c] & (PAD_L2 | PAD_R2)) && !est[c][j]) continue; // PC: o un clic
					uint32_t now = ticks;
					if (ntap && now - taps[ntap - 1] > 3 * TICK_HZ) ntap = 0; // pausa > 3 s = nuova serie
					if (ntap == 5) { for (int t = 0; t < 4; t++) taps[t] = taps[t + 1]; ntap = 4; }
					taps[ntap++] = now;
					if (ntap >= 2) k[K_BPM] = clamp(60 * TICK_HZ * (ntap - 1) / (int) (taps[ntap - 1] - taps[0]) - 40, 0, 160);
					d.clk = (1u << 24) - 1;         // il battito cade adesso: il prossimo sedicesimo e' il primo di un quarto
					d.s16 = (d.s16 + 3) & ~3;
					continue;
				}
				int dv = passo(c, piu[j], !LISTA(i)) - passo(c, meno[j], !LISTA(i)) +
				         (LISTA(i) ? (est[c][j] > 0) - (est[c][j] < 0) : est[c][j]); // PC: Shift della tastiera
				if (dv && !pag[c]) tocco[c] = 75;
				if (i == K_ROOT) k[i] = (k[i] + dv + 12) % 12;
				else             k[i] = clamp(k[i] + dv, 0, k_max[i]);
			}
		}
		memset(est, 0, sizeof est);
		pag_ora[0] = pag[0], pag_ora[1] = pag[1];
		if (k[K_TAIL] != 99) hold_tail = -1; // TAIL mosso: HOLD spento, TAIL resta li'
		k[K_HITS1] = clamp(k[K_HITS1], 0, k[K_STEPS1]); // i colpi non superano i passi
		k[K_HITS2] = clamp(k[K_HITS2], 0, k[K_STEPS2]);
		// PLAY: tieni SU = SWELL (MACRO 1 sale in ~1 s), tieni GIU' = SINK (MACRO 2); lasciati ricadono in ~2 s.
		// PC: una nota MIDI tenuta porta SWELL fino alla sua velocita', la pressione SINK fin dove preme, anche con PLAY spento
		for (int m = 0; m < 2; m++) {
			int giu = suona && !guida && !pag[0] && !(held[0] & PAD_START) && (held[0] & (m ? PAD_DOWN : PAD_UP));
			int tetto = giu ? V99 : m ? nt_pres : (nt_giu[0] | nt_giu[1] | nt_giu[2] | nt_giu[3]) ? nt_vel * V99 / 127 : 0;
			gest[m] = gest[m] < tetto ? clamp(gest[m] + V99 / 50, 0, tetto) : clamp(gest[m] - V99 / 100, tetto, V99);
		}
		// PC: 2 s senza messaggi di clock MIDI = di nuovo il BPM proprio
		if ((clk_est || clk_fermo) && ++clk_t >= 100) clk_est = clk_fermo = 0, clk_n = -1;

		// C1 Select da solo (al rilascio, se nel frattempo non si e' premuto altro) = PLAY.
		// C1 Start toccato da solo = MUTE (il RESET e' nella GUIDE, con conferma).
		// C2 Start toccato da solo = FREEZE
		static int sel_g, sel_a, sel_t, st_g, st_a, st2_g, st2_a;
		if (smsg_t) smsg_t--;
		if (scr_dopo) card_scrivi(); // PC: un SAVE mentre si scriveva il file del SAVE di prima
		if (SDL_CompareAndSwapAtomicInt(&scr_errore, 1, 0)) { gmsg = "SCENE FILE NOT WRITTEN"; gmsg_t = 100; }
		scena_auto();
		if (solo(0, PAD_SELECT, &sel_g, &sel_a) && sel_t < 15 && !guida) suona ^= 1; // PLAY: tocco breve
		sel_t = held[0] & PAD_SELECT ? sel_t + 1 : 0;
		if (solo(0, PAD_START, &st_g, &st_a) && !guida) muto ^= 1;          // RESET ora e' nella GUIDE
		if (solo(1, PAD_START, &st2_g, &st2_a) && st2_t < 15 && !guida) freeze ^= 1; // tenuto = SCENE
		st2_t = held[1] & PAD_START ? st2_t + 1 : 0;
		if (tut && !guida)
			tut_avanza((held[0] & PAD_START) ? !!(hit[0] & PAD_R1) - !!(hit[0] & PAD_L1) : 0);

		// Levette (con aggancio): C1 sinistra Y = cutoff, X = fold, destra Y = FM, X = width;
		// C2 sinistra Y = MACRO 1, X = MACRO 2, destra Y = MACRO 3, X = MACRO 4
		static const int8_t leva_k[8] = { K_FOLD, K_CUT, K_WIDTH, K_FM, K_M2, K_M1, K_M4, K_M3 };
		for (int j = 0; j < 8 && !guida; j++) {
			int dl = leve[j >> 2][j & 3] * (j & 1 ? -1 : 1); // Y: su = negativo
			if (!dl) { lacc[j] = 0; continue; }
			lacc[j] += dl;
			while (lacc[j] >= 16)  { lacc[j] -= 16; k[leva_k[j]] = clamp(k[leva_k[j]] + 1, 0, 99); }
			while (lacc[j] <= -16) { lacc[j] += 16; k[leva_k[j]] = clamp(k[leva_k[j]] - 1, 0, 99); }
		}

		patch_scivola();
		macro_somma();
		sfuma = clamp(sfuma + (muto ? -1 : 1), 0, 150);
		SPU_MASTER_VOL_L = 0x3fff * sfuma / 150;
		SPU_MASTER_VOL_R = 0x3fff * sfuma / 150;

		SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
		spu_aggiorna(); // PC: i blocchi del motore DSP li chiede la voce 0 mentre suona (dsp_scrivi)

		/* Schermata: testata, 6 pannelli 3D, aiuto in basso */
		char riga[64];
		int  n = nota();
		nz   = 0; // PC: zone cliccabili di questo fotogramma (mouse)
		mgiu = controlli_mouse(&mx, &my);
		mm   = mgiu >= 0;
		if (mm && (guida || !visual)) menu_disegna(); // per primo: sopra a tutto
		if (visual && !guida) {      // VISUAL: niente testo; col mouse un clic ovunque torna ai pannelli
			if (mm) zona(Z_PULSANTE, 0, 0, W, H)->nome = "VISUAL";
		} else if (tut) {
			tut_testata();
			if (mm) { zona(Z_TESTATA, 14, 14, W - 28, 28); testata_mouse(0); tut_frecce(); }
		} else {
		if (mm) { // mouse: STORM e il battito vanno in cima, gli stati diventano pulsanti (testata_mouse)
		sprintf(riga, "NOTE %s%d %s%s", nota_nome[n % 12], n / 12 - 1, modo_nome[k[K_MODE]],
		        controlli_lato() < 0 ? "" : controlli_lato() ? " C2" : " C1");
		zona(Z_TESTATA, 14, 14, testo(16, 16, riga, 128, 128, 128) - 14, 13)->aiuto = "NOTE AND FILTER MODE\nDOUBLE-CLICK: FULL SCREEN";
		} else {
		sprintf(riga, "STORM  NOTE %s%d  %s%s", nota_nome[n % 12], n / 12 - 1, modo_nome[k[K_MODE]],
		        controlli_lato() < 0 ? "" : controlli_lato() ? "  C2" : "  C1"); // PC: lato di tastiera / gamepad solo
		int xr = testo(16, 16, riga, 128, 128, 128);
		// stati che cambiano il modo di suonare: riquadri colorati ben visibili
		// (il tasto per toglierli e' nella quinta riga dell'aiuto): parola chiara su un riquadro colorato scuro
		static const struct { const char *t; uint8_t r, g, b; } st[5] = {
			{ "PLAY", 130, 100, 20 }, { "FREEZE", 20, 100, 130 }, { "MUTE", 90, 90, 100 }, { "LOCK", 140, 40, 40 },
			{ "AUTO", 40, 110, 60 } };
		int acceso[5] = { suona, freeze, muto, blocco, auto_on };
		for (int j = 0; j < 5; j++) {
			if (!acceso[j]) continue;
			int w = largo(st[j].t);
			tile(xr + 4, 14, w + 6, 13, st[j].r, st[j].g, st[j].b);
			xr = testo(xr + 7, 16, st[j].t, 128, 128, 128) + 3;
		}
		int xd = 304 - largo("DSP 0% B0"); // punto del CLOCK al posto della PS1 (li' scrive il carico del motore)
		int battito = (d.s16 & 3) == 0;                                    // un quarto del CLOCK: punto acceso
		tile(xd - 13, 17, 9, 9, battito ? 230 : 50, battito ? 230 : 50, battito ? 230 : 60);
		if (clk_est) testo(xd - 2, 16, "E", 128, 128, 128); // il CLOCK segue il clock MIDI
		}
		sprintf(riga, "NOW: CUT %d FOLD %d FM %d FB %d", d.eff[K_CUT] >> 8, d.eff[K_FOLD] >> 8, d.eff[K_FM] >> 8, d.eff[K_FEED] >> 8);
		testo(16, 29, riga, 110, 100, 60);
		// OUT: picco dell'uscita del motore (verde; giallo chiaro vicino al massimo)
		int ow = d.picco * 30 >> 15, alto = d.picco > 29000;
		testo(250, 29, "OUT", 110, 100, 60);
		tile(274, 31, ow, 7, alto ? 230 : 90, alto ? 220 : 200, alto ? 120 : 110);
		tile(274 + ow, 31, 30 - ow, 7, 30, 36, 50);
		if (mm) {
			zona(Z_TESTATA, 14, 27, W - 28, 14)->aiuto = "NOW: WHAT YOU HEAR. OUT: LEVEL\nDOUBLE-CLICK: FULL SCREEN";
			testata_mouse(1);
		}
		}

		// valori nuovi = lampo giallo di 4 fotogrammi; storia dei generatori per MOTION
		static int k_prima[N_K];
		for (int i = 0; i < N_K; i++)
			if (k[i] != k_prima[i]) { lampo[i] = 4; k_prima[i] = k[i]; }
			else if (lampo[i]) lampo[i]--;
		if (fot % 3 == 0) {
			for (int j = 0; j < 3; j++) storia[j][storia_i] = d.out[j];
			storia[3][storia_i] = d.drv;
			storia_i = (storia_i + 1) & 63;
		}
		if (!visual || guida) lissajous_calcola(); // serve solo ai pannelli

		if (guida) guida_disegna(fot);
		else if (visual) {
			// Spia: la pagina tenuta (o la riposo appena toccata) in una riga tenue in basso, fino a 1 s
			// dopo il rilascio; in alto a destra lettere piccole per PLAY, FREEZE, MUTE, LOCK, AUTO
			static int hud_t, hud_c, hud_p;
			for (int c = 0; c < 2; c++)
				if (pag[c] || tocco[c] > 70) { hud_t = 50; hud_c = c; hud_p = pag[c]; }
			if (tocco[0]) tocco[0]--;
			if (tocco[1]) tocco[1]--;
			if (hud_t) {
				hud_t--;
				char h[64];
				if (hud_p == P_SCENE)
					sprintf(h, "SCENE %d  MORPH %dS%s", gposto + 1, morph_s[k[K_MORPH]], smsg_t ? "  " : "");
				else if (hud_p == P_EDIT)
					sprintf(h, "%s %s %+d", macro_titolo[ed_posto >> 2], macro_nome[mdest[ed_posto >> 2][ed_posto & 3]],
					        mamt[ed_posto >> 2][ed_posto & 3]);
				else {
					int n = sprintf(h, "%s ", hud_c == 0 && hud_p == 0 && suona ? "PLAY" : pag_nome[hud_c][hud_p]);
					for (int j = 0; j < 4; j++) {
						int i = pag_k[hud_c][hud_p][j];
						if (i >= 0 && n < 50) n += sprintf(h + n, " %s", valore(i, 1));
					}
				}
				if (hud_p == P_SCENE && smsg_t) strcat(h, smsg);
				testo(16, 226, h, 70, 80, 90);
			}
			static const char lettera[5] = { 'P', 'F', 'M', 'L', 'A' };
			int acceso[5] = { suona, freeze, muto, blocco, auto_on }, x = 300;
			for (int j = 4; j >= 0; j--)
				if (acceso[j]) {
					char t[2] = { lettera[j], 0 };
					x -= largo(t) + 2;
					testo(x, 14, t, 80, 80, 90);
				}
			tempesta(fot, sfuma);
		}
		else {
		// Pannelli: quello della pagina tenuta (o di riposo appena toccata) viene avanti; se
		// lavorano tutti e due i controller su pannelli diversi, meta' schermo ciascuno
		int zc[2];
		for (int c = 0; c < 2; c++) {
			if (tocco[c]) tocco[c]--;
			zc[c] = pag[c] || tocco[c] || davanti[c] ? pag_pn[c][pag[c]] : -1; // PC: avanti = mouse
		}
		int due = zc[0] >= 0 && zc[1] >= 0 && zc[0] != zc[1], avanti = 0;
		static int pz[N_PN]; // quanto e' avanti ogni pannello, 0-4096 (insegue 1/3 della strada)
		if (vis_uscito) { for (int i = 0; i < N_PN; i++) pz[i] = 0; vis_uscito = 0; } // dopo la VISUAL: da fermi
		for (int i = 0; i < N_PN; i++) {
			int dz = (zc[0] == i || zc[1] == i ? 4096 : 0) - pz[i];
			pz[i] += dz / 3 + (dz > 0) - (dz < 0);
			if (pz[i] > 2048) avanti = 1;
		}
		int z0 = nz; // PC: zone dei pannelli e delle righe, riordinate dopo il ciclo
		for (int i = 0; i < N_PN; i++) {
			const int16_t *a0 = pn_riposo[i];
			int t[4] = { 160, 102, 144, 58 };                  // davanti, tutto lo schermo
			if (due) { t[0] = zc[0] == i ? 86 : 234; t[2] = 70; } // meta' schermo
			int ss = (pz[i] * pz[i] >> 12) * (3 * 4096 - 2 * pz[i]) >> 12; // smoothstep
			Quad q;
			pannello_quad(a0[0] + ((t[0] - a0[0]) * ss >> 12), a0[1] + ((t[1] - a0[1]) * ss >> 12),
			              a0[2] + ((t[2] - a0[2]) * ss >> 12), a0[3] + ((t[3] - a0[3]) * ss >> 12),
			              170 * (4096 - ss) >> 12, &q);
			int fronte = pz[i] > 2048, zp = fronte ? Z_PANZ : Z_PAN, zf = fronte ? Z_FIGZ : Z_FIG;
			int lato = pn_lato[i], l = avanti && !fronte ? 64 : 128;
			int c = zc[0] == i ? 0 : zc[1] == i ? 1 : lato;
			pieno(zp, &q, 0, 0, 4096, 4096, 16 * l >> 7, 18 * l >> 7, 28 * l >> 7);
			int fx = (fissa[c] && zc[c] == i) || (davanti[c] >= 2 && !pag[c] && zc[c] == i) || (tut && !tut_t && passi[tut_passo].pn == i && (fot & 31) < 24);
			int gelo = freeze && i == PN_MOT;
			contorno(zf, &q, 0, 0, 4096, 4096, fx ? 220 : gelo ? 80 : (lato ? 80 : 90) * l >> 7,
			         fx ? 190 : gelo ? 220 : (lato ? 160 : 190) * l >> 7, fx ? 50 : gelo ? 240 : (lato ? 230 : 120) * l >> 7);
			{ // mouse: il pannello apre la sua pagina (quello davanti: la pagina che mostra, cosi' un clic la chiude)
				static const int8_t pn_pag[N_PN] = { 1, 0, 4, 4, 1, 0 }; // OSC, SHAPE, LAYERS, SPACE, MOD A, MIX
				int dav = zc[0] == i || zc[1] == i;
				Zona *z = zona_uv(Z_PANNELLO, &q, 0, 0, 4096, 4096);
				z->lato = (int8_t) c, z->posto = !fronte; // posto: dietro (si riordina sotto)
				for (int o = 0; o < 11; o++)
					if (pag_ordine[c][o] == (dav ? pag[c] : pn_pag[i])) z->n = (int8_t) o;
			}
			if (fronte || !avanti) testo_uv(&q, 250, 150, pn_nome[i], lato ? 90 : 100, lato ? 170 : 200, lato ? 230 : 130);
			int pieno_pn = pz[i] > 3600 && (zc[0] == i || zc[1] == i); // arrivato: figura a sinistra, righe a destra
			if (pieno_pn && due) { // mezzo schermo: solo le righe, su tutta la larghezza
				celle(&q, zc[0] == i ? 0 : 1, zc[0] == i ? pag[0] : pag[1], ed_posto, 300);
				continue;
			}
			int u1 = pieno_pn ? 2000 : 3800, v0 = i >= PN_MOT && !pieno_pn ? 700 : 1100;
			int pp = zc[0] == i ? pag[0] : zc[1] == i ? pag[1] : 0;
			switch (i) {
			case PN_SRC: fig_sorgenti(zf, &q, 300, v0, u1, 3800, l); break;
			case PN_SHP: fig_forma(zf, &q, 300, v0, u1, 3800, l); break;
			case PN_LAY: fig_strati(zf, &q, 300, v0, u1, 3800, l); break;
			case PN_SPC: fig_spazio(zf, &q, 300, v0, u1, 3800, l, fot); break;
			case PN_MOT: fig_moto(zf, &q, i >= PN_MOT && !pieno_pn ? 1600 : 300, v0, u1, 3700, l,
			                      pp >= 1 && pp <= 3 && zc[1] == i ? pp - 1 : -1); break;
			default:     if (pp == P_SCENE) fig_scene(zf, &q, 300, v0, u1, 3700, l);
			             else fig_mix(zf, &q, 1200, v0, u1, 3700, l, pp >= P_MACROS);
			}
			if (pieno_pn) celle(&q, zc[0] == i ? 0 : 1, pp, ed_posto, 2200);
		}
		{ // mouse: i pannelli dietro prima, cosi' quello davanti e le sue righe stanno sopra
			static Zona tz[N_ZZ];
			int t = 0;
			for (int dietro = 1; dietro >= 0; dietro--)
				for (int j = z0; j < nz; j++)
					if ((zz[j].tipo == Z_PANNELLO && zz[j].posto) == dietro) {
						tz[t] = zz[j];
						if (tz[t].tipo == Z_PANNELLO) tz[t].posto = 0;
						t++;
					}
			memcpy(zz + z0, tz, (size_t) t * sizeof *tz);
		}

		static const char *aiuto[2][P_SCENE + 1][2] = {
			{ { "HOLD " CROCE " OSC " QUADR " GRAIN " TRIAN " NOTE " CERCH " LAYERS", SU_GIU " CUT " SX_DX " RES L1R1 MODE L2R2 DRIVE" },
			  { SU_GIU " SINE  " SX_DX " SQUARE", "L1R1 SAW  L2R2 SUB" },
			  { SU_GIU " FOLD  " SX_DX " NOISE", "L1R1 WIDTH  L2R2 FEEDBACK" },
			  { SU_GIU " ROOT  " SX_DX " OCTAVE", "L1R1 GLIDE" },
			  { SU_GIU " DETUNE  " SX_DX " BELLS", "L1R1 CHORD  L2R2 COLOR" },
			  { SU_GIU " FM  " SX_DX " AM", "L1R1 RATIO L2R2 PWM  +" CROCE " TEXTURE" },
			  { "PLAY: " SU_GIU " SWELL/SINK " SX_DX " NOTE", "L1R1 OCTAVE L2R2 CHORD SELECT OFF" },
			  { SU_GIU " THUMP  " SX_DX " WIND", "L1R1 PEDAL  L2R2 FINE" },
			  { "", "" }, { "", "" }, { "", "" } },
			{ { "HOLD " CROCE QUADR TRIAN " MOD " CERCH " SPACE SELECT MACRO", SU_GIU " M1 " SX_DX " REVERB L1R1 LAYERS L2R2 VOL" },
			  { SU_GIU " RATE  " SX_DX " DEPTH", "L1R1 WAVE  L2R2 DEST" },
			  { SU_GIU " RATE  " SX_DX " DEPTH", "L1R1 WAVE  L2R2 DEST" },
			  { SU_GIU " RATE  " SX_DX " DEPTH", "L1R1 WAVE  L2R2 DEST" },
			  { SU_GIU " WIDE  " SX_DX " TAIL", "L1R1 ROOM  L2R2 DAMP" },
			  { SU_GIU " M1  " SX_DX " M2  L1R1 M3  L2R2 M4", "+" CROCE " SEQ " QUADR " PULSE " TRIAN " CLOCK " CERCH " EDIT" },
			  { SU_GIU " STEPS  " SX_DX " MUTATE", "L1R1 SCALE  L2R2 EVERY" },
			  { SU_GIU " HITS 1  " SX_DX " STEPS 1", "L1R1 HITS 2  L2R2 STEPS 2" },
			  { SU_GIU " BPM  " SX_DX " RANGE", "L1R1 NUDGE  L2R2 TAP TEMPO" },
			  { SU_GIU " SLOT 1-16  " SX_DX " AMOUNT", "L1R1 DESTINATION" },
			  { SU_GIU " SCENE  " SX_DX " MORPH  R1 GO", "L1 HOME  R2 NUDGE  L2 AUTO" } },
		};
		if (mm) { // mouse: al posto dell'aiuto del pad le pagine (o la striscia di PLAY) e l'aiuto del puntatore
			zona(Z_NIENTE, 16, 166, W - 32, 86);
			int pl = suona && !pag[0];
			if (pl) striscia();
			for (int r = pl ? 2 : 0; r < 4; r++) schede(r, r >> 1, pag[r >> 1]);
			for (int r = 0; r < 4; r++) {
				testo(19, 170 + r * 14, r >> 1 ? "2" : "1", 128, 128, 128);
				if (r >> 1) tile(16, 168 + r * 14, 14, 12, 40, 100, 160);
				else        tile(16, 168 + r * 14, 14, 12, 50, 120, 62);
			}
			aiuto_mouse(20, 226, 2);
			tile(16, 166, W - 32, 2, 110, 110, 130);
			tile(16, 168, W - 32, 84, 20, 20, 30);
		} else if (*tut_aiuto[controlli_dispositivo()]) { // PC: tastiera o MIDI, righe da script/testi.txt
			paragrafo(20, 174, tut_aiuto[controlli_dispositivo()]);
			tile(16, 166, W - 32, 2, 110, 110, 130);
			tile(16, 168, W - 32, 72, 20, 20, 30);
		} else {
		for (int r = 0; r < 4; r++) {
			int l = r >> 1, y = 171 + r * 14;
			riga_aiuto(34, y - 3, aiuto[l][!l && !pag[0] && suona ? 6 : pag[l]][r & 1]);
			testo(19, y - 3, l ? "2" : "1", 128, 128, 128);
			if (l) tile(16, y - 1, 14, 12, 40, 100, 160);
			else   tile(16, y - 1, 14, 12, 50, 120, 62);
		}
		static const char *fondo[3] = { "START+SELECT: TAP STORM  HOLD GUIDE", "START 1 MUTE 2 FREEZE  SEL TAP 1 PLAY",
		                                "HOLD START 2 SCENES  START+" CROCE " 2S LOCK" };
		riga_aiuto(20, 224, fondo[fot / 150 % 3]);
		tile(16, 166, W - 32, 2, 110, 110, 130); // riga chiara sopra l'aiuto
		tile(16, 168, W - 32, 72, 20, 20, 30);   // riquadro scuro
		}
		}
		if (mm && guida) aiuto_mouse(16 + largo("OK") + largo("BACK") + 36, 244, 1);
		// PC: le zone di questo fotogramma vanno al mouse; LOCK tenuto si riempie in 1 s
		const Zona *zl = mgiu > 0 && mx >= 0 ? zona_sotto(zz, nz, mx, my) : NULL;
		lock_t = zl && zl->tipo == Z_PULSANTE && zl->tieni ? lock_t + 1 : 0;
		controlli_zone(zz, nz, W, H);

		DrawSync(0);
		VSync(0);
		PutDispEnv(&fb[cur ^ 1].disp);
		DrawOTagEnv(&fb[cur].ot[OT_LEN - 1], &fb[cur].draw);
		cur ^= 1;
		nextp = fb[cur].buf;
		ClearOTagR(fb[cur].ot, OT_LEN);
	}
}

/* PC: tabella dei parametri (standard/controlli/parametri.md), ricavata da pag_k, k_nome, k_max e
   k_init, nell'ordine delle pagine (pag_ordine). id = nome a schermo senza spazi; RATE, DEPTH, WAVE
   e DEST dei MOD prendono la lettera (RATE_A). Un parametro su due pagine (M1 RISE su MIX e MACROS)
   compare una volta sola, alla prima, con l'altra in "anche". */

static Param tab[N_K];
static char  tab_id[N_K][16], tab_et[N_K][160];

static void storm_tabella(void) {
	static uint8_t fatto[N_K];
	int n = 0;
	for (int c = 0; c < 2; c++)
		for (int o = 0; o < 11; o++) {
			int pp = pag_ordine[c][o];
			for (int j = 0; j < 4 && pp >= 0 && pp != P_EDIT; j++) {
				int i = pp == P_SCENE ? (j == 1 ? K_MORPH : -1) : pag_k[c][pp][j]; // SCENE: MORPH su SX/DX
				if (i < 0 || fatto[i]) continue;
				fatto[i] = 1;
				char *s = tab_id[i];
				for (const char *t = k_nome[i]; *t; t++)
					if (*t != ' ') *s++ = *t;
				if (i >= K_MOD && i < K_FINE) { *s++ = '_'; *s++ = (char) ('A' + (i - K_MOD) / 4); }
				*s = 0;
				int tipo = i == K_TAP || i == K_NUDGE ? P_PULSANTE : LISTA(i) ? P_SCELTA : P_CONTINUO;
				if (tipo == P_SCELTA) { // etichette: i valori a schermo
					int v = k[i];
					for (k[i] = 0; k[i] <= k_max[i]; k[i]++) {
						if (k[i]) strcat(tab_et[i], ", ");
						strcat(tab_et[i], valore(i, 0));
					}
					k[i] = v;
				}
				k_par[i] = &tab[n];
				tab[n++] = (Param) { tab_id[i], k_nome[i], pp == P_SCENE ? "SCENE" : pag_nome[c][pp],
				                     tipo == P_SCELTA ? tab_et[i] : NULL, i == K_M1 ? "MACROS 1" : NULL, c + 1, j + 1, 0,
				                     k_max[i], k_init[i], tipo, i == K_FINE, i != K_MORPH && tipo != P_PULSANTE, &k[i] };
			}
		}
	controlli_tabella(tab, n);
	controlli_synth(storm_pagina, storm_interruttore);
}

// Seme del caso (xorshift32, mai 0). Senza chiamarla resta quello della PS1.
void storm_seme(uint32_t s) { seme = s ? s : 1; }

/* PC: file delle scene (standard/controlli/scene.md). In memoria restano gli 8 posti nel formato
   della PS1 (versione 2): il testo c'e' solo qui, al bordo del file. Ogni byte di un posto e' una
   manopola k[]: il suo id e' quello della tabella. */
// ponytail: formato e lettura qui, solo per storm; generici in controlli.c quando arriva un secondo synth

static void scena_avviso(const char *nome, int riga, const char *perche) {
	fprintf(stderr, "%s:%d: %s, riga saltata\n", nome, riga, perche);
}

// Il testo di un file delle scene (nome per gli avvisi) nei posti. Dopo storm_avvia (serve la
// tabella), prima che parta il suono.
void storm_scene_leggi(const char *t, const char *nome) {
	uint8_t *p = NULL; // posto della sezione aperta
	int nr = 0, salta = 0; // salta: intestazione rotta o fuori dai posti, le sue righe non vanno altrove
	if (!strncmp(t, "\xef\xbb\xbf", 3)) t += 3; // BOM UTF-8 di alcuni editor
	while (*t) {
		char r[256], id[32];
		size_t l = strcspn(t, "\n");
		snprintf(r, sizeof r, "%.*s", (int) (l < sizeof r ? l : sizeof r - 1), t);
		t += l + (t[l] == '\n');
		nr++;
		r[strcspn(r, "#\r")] = 0;
		int a, b, v, q, e = -1;
		const Param *pr;
		if (sscanf(r, " %31s", id) != 1) continue; // vuota o solo commento
		if (l >= sizeof r) scena_avviso(nome, nr, "riga troppo lunga");
		else if (id[0] == '[') { // intestazione: buona apre il posto, rotta apre una sezione da saltare
			int ok = sscanf(r, " [ scena %d ] %n", &a, &e) == 1 && e >= 0 && !r[e];
			salta = !ok || a < 1 || a > N_POSTI;
			p = salta ? NULL : posto(a - 1);
			if (salta) { scena_avviso(nome, nr, ok ? "sezione fuori dai posti 1-8" : "intestazione non valida"); continue; }
			p[0] = 'P', p[1] = 2; // posto pieno; quello che manca resta al valore iniziale
			for (int j = 0; j < N_K; j++) p[2 + j] = (uint8_t) k_init[j];
			for (int m = 0; m < 16; m++) {
				p[2 + N_K + m]      = (uint8_t) mdest_init[m >> 2][m & 3];
				p[2 + N_K + 16 + m] = (uint8_t) mamt_init[m >> 2][m & 3];
			}
		} else if (salta) scena_avviso(nome, nr, "dentro una sezione saltata");
		else if (!p) scena_avviso(nome, nr, "riga fuori da una sezione [scena N]");
		else if (sscanf(r, " macro %d %d = %31s %d %n", &a, &b, id, &q, &e) == 4 && e >= 0 && !r[e]) {
			int d = -1; // destinazione: indice in macro_k
			pr = controlli_param(id);
			for (int i = 0; pr && i < N_MDEST; i++)
				if (pr->valore == &k[macro_k[i]]) d = i;
			if (a < 1 || a > 4 || b < 1 || b > 4) scena_avviso(nome, nr, "macro e posto vanno da 1 a 4");
			else if (d < 0) scena_avviso(nome, nr, "non e' una destinazione delle macro");
			else {
				p[2 + N_K + (a - 1) * 4 + b - 1]      = (uint8_t) d;
				p[2 + N_K + 16 + (a - 1) * 4 + b - 1] = (uint8_t) clamp(q, -9, 9);
			}
		} else if (sscanf(r, " %31[A-Za-z0-9_] = %d %n", id, &v, &e) == 2 && e >= 0 && !r[e]) {
			if (!(pr = controlli_param(id)) || !pr->scena) scena_avviso(nome, nr, pr ? "parametro che non sta nelle scene" : "parametro sconosciuto");
			else p[2 + (pr->valore - k)] = (uint8_t) clamp(v, pr->min, pr->max);
		} else scena_avviso(nome, nr, "riga non valida");
	}
}

// Thread principale: il testo del file dopo un SAVE (da scr_buf). 0 = niente da scrivere, -1 = t
// troppo piccolo. Gli id della tabella non cambiano dopo l'avvio: si leggono senza problemi.
int storm_scene_testo(char *t, int n) {
	if (!SDL_GetAtomicInt(&scr_pronto)) return 0;
	int l = 0;
#define SCRIVI(...) (l += snprintf(t + (l < n ? l : n), l < n ? (size_t) (n - l) : 0, __VA_ARGS__))
	SCRIVI("# scene di storm (versione 1)\n");
	for (int i = 0; i < N_POSTI; i++) {
		const uint8_t *p = scr_buf + i * 128;
		if (p[0] != 'P') continue;
		SCRIVI("[scena %d]\n", i + 1);
		for (int j = 0; j < N_K; j++) // nell'ordine della tabella
			if (tab[j].scena) SCRIVI("%s = %d\n", tab[j].id, p[2 + (tab[j].valore - k)]);
		for (int m = 0; m < 16; m++)
			SCRIVI("macro %d %d = %s %d\n", (m >> 2) + 1, (m & 3) + 1, k_par[macro_k[p[2 + N_K + m] % N_MDEST]]->id,
			       (int8_t) p[2 + N_K + 16 + m]);
	}
#undef SCRIVI
	SDL_SetAtomicInt(&scr_pronto, 0); // scr_buf di nuovo libero per il prossimo SAVE
	return l < n ? l : -1;
}

void storm_scene_errore(void) { SDL_SetAtomicInt(&scr_errore, 1); }

// Thread principale, solo a suono fermo (nessuno tocca piu' i posti): un SAVE rimandato (scr_dopo)
// va in scr_buf per l'ultima scrittura, dopo quella del SAVE di prima.
void storm_scene_ultimo(void) { if (scr_dopo) card_scrivi(); }

// Per il modo prova: passo del tutorial aperto (dal primo = 1), 0 = chiuso
int storm_passo(void) { return tut ? tut_passo + 1 : 0; }

/* PC, plugin: tutto lo stato da rimettere riaprendo un progetto della DAW. Versione 1, 1160 byte:
   patch attuale (come una scena), 8 posti, scena attiva, MUTE, PLAY, FREEZE, TAIL di prima di HOLD.
   Dal thread del suono (o a suono fermo). */
#define STATO_N (2 + 128 + N_POSTI * 128 + 6)
int storm_stato(uint8_t *b, int n) {
	if (n < STATO_N) return 0;
	b[0] = 'S', b[1] = 1;
	patch_salva(b + 2);
	memcpy(b + 130, posto(0), N_POSTI * 128);
	uint8_t *c = b + 130 + N_POSTI * 128;
	c[0] = (uint8_t) (scena_ora + 1), c[1] = (uint8_t) muto, c[2] = (uint8_t) suona, c[3] = (uint8_t) freeze;
	c[4] = (uint8_t) (hold_tail + 1), c[5] = 0;
	return STATO_N;
}

// Al contrario di LOAD, subito e anche MORPH: il suono riparte com'era quando si e' salvato
void storm_stato_carica(const uint8_t *b, int n) {
	if (n < STATO_N || b[0] != 'S' || b[1] != 1 || b[2] != 'P') return;
	const uint8_t *p = b + 2, *c = b + 130 + N_POSTI * 128;
	for (int m = 0; m < 16; m++) { mdest[m >> 2][m & 3] = p[2 + N_K + m] % N_MDEST; mamt[m >> 2][m & 3] = clamp((int8_t) p[2 + N_K + 16 + m], -9, 9); }
	for (int j = 0; j < N_K; j++) k[j] = clamp(p[2 + j], 0, k_max[j]);
	carica = 0;
	memcpy(posto(0), b + 130, N_POSTI * 128);
	memcpy(casa, p, 128);
	scena_ora = c[0] <= N_POSTI ? c[0] - 1 : -1;
	muto = !!c[1], suona = !!c[2], freeze = !!c[3];
	hold_tail = c[4] <= 100 ? c[4] - 1 : -1;
}

// Plugin: IMPORT premuto? (una volta, dal thread principale)
int storm_scene_chiesta(void) { return SDL_SetAtomicInt(&imp_chiesta, 0); }

// Plugin, dal thread del suono: le 8 scene di un file al posto di quelle di adesso (NULL = file che
// non c'e'). I posti che il file non ha restano vuoti.
void storm_scene_importa(const char *t, const char *nome) {
	if (!t) { smsg = "NO SCENE FILE"; smsg_t = 100; return; }
	for (int i = 0; i < N_POSTI; i++) posto(i)[0] = 0;
	storm_scene_leggi(t, nome);
	scena_ora = -1;
	smsg = "IMPORTED", smsg_t = 100;
}
