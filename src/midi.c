/*
 * Porte MIDI d'ingresso (RtMidi: WinMM, CoreMIDI o ALSA, la sceglie lei): tutte aperte insieme, e
 * ogni 2 s si rilegge l'elenco per nome (una porta nuova si apre, una sparita si chiude).
 * La callback di RtMidi gira nel thread di RtMidi: mette solo il messaggio in un evento SDL
 * (SDL_PushEvent e' sicura da altri thread e tiene l'ordine); il traduttore (controlli.c) lo legge
 * nel thread principale. Solo per la finestra: il modo prova non apre porte.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <rtmidi_c.h>

#define N_PORTE 16
static struct {
	RtMidiInPtr in;        // NULL = posto libero
	char        nome[128];
} porta[N_PORTE];
static RtMidiInPtr elenco;          // solo per leggere l'elenco delle porte
// porte che non si aprono: non si riprovano finche' restano nell'elenco, perche' RtMidi scrive il suo
// errore in console a ogni tentativo (l'API C non permette di zittirlo); staccata e riattaccata, si riprova
static char        rifiutata[N_PORTE][128];
static Uint32      tipo_evento;

static void richiama(double t, const unsigned char *m, size_t n, void *u)
{
	(void) t;
	(void) u;
	if (n < 1 || n > 3) // SysEx (lungo) e messaggi vuoti: non servono
		return;
	SDL_Event e = { 0 };
	e.type      = tipo_evento;
	e.user.code = (Sint32) (m[0] | (n > 1 ? m[1] : 0) << 8 | (n > 2 ? m[2] : 0) << 16 | (int) n << 24);
	SDL_PushEvent(&e); // il tempo lo mette SDL (e.common.timestamp)
}

Uint32 midi_avvia(void)
{
	tipo_evento = SDL_RegisterEvents(1);
	elenco      = rtmidi_in_create(RTMIDI_API_UNSPECIFIED, "storm", 100);
	if (!elenco || !elenco->ok) {
		fprintf(stderr, "MIDI: %s\n", elenco && elenco->msg ? elenco->msg : "non disponibile");
		if (elenco)
			rtmidi_in_free(elenco);
		elenco = NULL;
	}
	return tipo_evento;
}

int midi_messaggio(const SDL_Event *e, uint8_t m[3])
{
	m[0] = (uint8_t) e->user.code, m[1] = (uint8_t) (e->user.code >> 8), m[2] = (uint8_t) (e->user.code >> 16);
	return e->user.code >> 24 & 3;
}

static void chiudi(int i)
{
	rtmidi_close_port(porta[i].in);
	rtmidi_in_free(porta[i].in);
	porta[i].in = NULL;
}

static void apri(unsigned int k, const char *nome)
{
	int i = 0;
	while (i < N_PORTE && porta[i].in)
		i++;
	if (i == N_PORTE)
		return;
	RtMidiInPtr in = rtmidi_in_create(RTMIDI_API_UNSPECIFIED, "storm", 100);
	if (in && in->ok) {
		rtmidi_in_set_callback(in, richiama, NULL);
		rtmidi_in_ignore_types(in, true, false, true); // via SysEx e active sensing; il clock resta
		rtmidi_open_port(in, k, "storm in");
	}
	if (in && in->ok) {
		porta[i].in = in;
		snprintf(porta[i].nome, sizeof porta[i].nome, "%.127s", nome);
		fprintf(stderr, "MIDI: aperta %s\n", nome);
		return;
	}
	for (int j = 0; j < N_PORTE; j++) // su Windows una porta puo' averla gia' un altro programma
		if (!rifiutata[j][0]) {
			snprintf(rifiutata[j], sizeof rifiutata[j], "%.127s", nome);
			fprintf(stderr, "MIDI: non riesco ad aprire %s (e' usata da un altro programma?)\n", nome);
			break;
		}
	if (in)
		rtmidi_in_free(in);
}

int midi_porte(void)
{
	if (!elenco)
		return 0;
	char         nomi[N_PORTE][128];
	unsigned int n = rtmidi_get_port_count(elenco);
	n = n < N_PORTE ? n : N_PORTE;
	for (unsigned int k = 0; k < n; k++) {
		int l = (int) sizeof nomi[k];
		if (rtmidi_get_port_name(elenco, k, nomi[k], &l) < 0)
			nomi[k][0] = 0;
	}
	// prima si chiudono le sparite: su Windows il nome porta l'indice, e una porta che cambia indice
	// va chiusa prima di riaprirla
	int aperte = 0;
	for (int i = 0; i < N_PORTE; i++) {
		int c_e = 0;
		for (unsigned int k = 0; porta[i].in && k < n; k++)
			c_e |= !strcmp(porta[i].nome, nomi[k]);
		if (porta[i].in && !c_e) {
			fprintf(stderr, "MIDI: chiusa %s\n", porta[i].nome);
			chiudi(i);
		}
	}
	for (int j = 0; j < N_PORTE; j++) { // una rifiutata sparita dall'elenco si potra' riprovare
		int c_e = 0;
		for (unsigned int k = 0; rifiutata[j][0] && k < n; k++)
			c_e |= !strcmp(rifiutata[j], nomi[k]);
		if (!c_e)
			rifiutata[j][0] = 0;
	}
	for (unsigned int k = 0; k < n; k++) {
		int c_e = !nomi[k][0];
		for (int i = 0; i < N_PORTE; i++)
			c_e |= (porta[i].in && !strcmp(porta[i].nome, nomi[k])) || (rifiutata[i][0] && !strcmp(rifiutata[i], nomi[k]));
		if (!c_e)
			apri(k, nomi[k]);
	}
	for (int i = 0; i < N_PORTE; i++)
		aperte += porta[i].in != NULL;
	return aperte;
}

void midi_chiudi(void)
{
	for (int i = 0; i < N_PORTE; i++)
		if (porta[i].in)
			chiudi(i);
	if (elenco)
		rtmidi_in_free(elenco);
	elenco = NULL;
}
