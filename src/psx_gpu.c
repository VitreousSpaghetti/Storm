/*
 * Il resto della piccola PS1 finta: GPU, GTE, pad e memory card.
 * GPU: storm costruisce l'ordering table come sulla PS1; DrawOTagEnv (thread audio, dentro il
 * fotogramma di storm) la percorre e copia i comandi GP0 in una lista compatta, poi la passa al
 * thread principale con tre liste a turno e un intero atomico (niente lock, niente allocazioni):
 * chi scrive riempie la sua lista e la scambia con quella "pronta"; chi disegna, se c'e' una lista
 * nuova, la scambia con la sua. psx_disegna() (thread principale) rasterizza l'ultima lista nella
 * VRAM finta 1024x512 a 15 bit come la GPU (copertura, Gouraud, dithering, semitrasparenza,
 * texture 4 bit con CLUT) e converte l'area disegnata in RGB in psx_schermo.
 * GTE: RTPS con la divisione UNR, le saturazioni e gli arrotondamenti della GTE; RotMatrix, isin e
 * icos come la libreria dell'SDK.
 */
#include <string.h>
#include <SDL3/SDL.h>
#include "psx.h"
#include "controlli.h"

/* ---- GPU: VRAM, LoadImage, ordering table ---- */

uint8_t         psx_schermo[256][320][3];
static uint16_t vram[512][1024];
static const uint8_t ancora = 0; // punto fisso: i riferimenti delle primitive sono offset da qui

uint32_t psx_rif(const void *p) { return (uint32_t) ((uintptr_t) p - (uintptr_t) &ancora); }
static const uint32_t *da_rif(uint32_t r) { return (const uint32_t *) (&ancora + r); }

void ResetGraph(int mode) { (void) mode; }
void SetVideoMode(int mode) { (void) mode; } // su PC sempre 320x256 a 50 Hz logici
void SetDispMask(int mask) { (void) mask; }
int  DrawSync(int mode) { (void) mode; return 0; }
int  VSync(int mode) { (void) mode; return 0; } // il ritmo di 50 Hz lo da' chi chiama il fotogramma
void PutDispEnv(const DISPENV *env) { (void) env; } // si mostra sempre l'ultimo fotogramma disegnato

// Solo all'avvio, prima che parta il thread audio (font e CLUT di storm)
void LoadImage(const RECT *r, const uint32_t *data)
{
	const uint16_t *s = (const uint16_t *) data;
	for (int y = 0; y < r->h; y++)
		for (int x = 0; x < r->w; x++)
			vram[(r->y + y) & 511][(r->x + x) & 1023] = *s++;
}

// Come l'SDK: dithering acceso, texture page 0x0a, finestra texture 256 (nessuna)
void SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
	memset(env, 0, sizeof *env);
	env->clip  = (RECT) { (int16_t) x, (int16_t) y, (int16_t) w, (int16_t) h };
	env->tw    = (RECT) { 0, 0, 256, 256 };
	env->tpage = 0x0a;
	env->dtd   = 1;
}

void SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
	memset(env, 0, sizeof *env);
	env->disp = (RECT) { (int16_t) x, (int16_t) y, (int16_t) w, (int16_t) h };
}

// Le ordering table viste da ClearOTagR: le loro voci sono solo parole (senza lunghezza)
static const uint32_t *ot_base[4];
static size_t          ot_len[4];

static int voce_ot(const uint32_t *p)
{
	for (int i = 0; i < 4; i++)
		if (ot_base[i] && p >= ot_base[i] && p < ot_base[i] + ot_len[i])
			return 1;
	return 0;
}

// Lista all'indietro: ot[n-1] -> ot[n-2] -> ... -> ot[0] -> fine
void ClearOTagR(uint32_t *ot, size_t n)
{
	int i = 0;
	while (i < 3 && ot_base[i] && ot_base[i] != ot)
		i++;
	ot_base[i] = ot, ot_len[i] = n;
	ot[0] = 0xffffffff;
	for (size_t j = 1; j < n; j++)
		ot[j] = psx_rif(&ot[j - 1]);
}

/* ---- Passaggio del fotogramma tra i thread ---- */

#define LISTA_MAX 32768 // parole: storm ne usa al massimo ~10000 (40 KB di primitive)
#define NUOVA     4     // bit di "pronta": la lista non e' ancora stata presa

typedef struct {
	RECT     clip;
	int16_t  ofs[2];
	uint16_t tpage;
	uint8_t  dtd, isbg, r0, g0, b0;
	int      n;            // parole usate in w: per ogni comando la lunghezza e poi le parole GP0
	uint32_t w[LISTA_MAX];
} Lista;

static Lista         liste[3];
static SDL_AtomicInt pronta = { 1 }; // indice dell'ultima lista finita (+ NUOVA se non presa)
static int           scrivo = 0;     // del thread audio
static int           leggo  = 2;     // del thread principale

void DrawOTagEnv(const uint32_t *ot, DRAWENV *env)
{
	Lista *l = &liste[scrivo];
	l->clip = env->clip, l->ofs[0] = env->ofs[0], l->ofs[1] = env->ofs[1];
	l->tpage = env->tpage, l->dtd = env->dtd, l->isbg = env->isbg;
	l->r0 = env->r0, l->g0 = env->g0, l->b0 = env->b0;
	l->n = 0;
	// le primitive stanno nei buffer di storm, che il fotogramma dopo riusa: si copiano adesso
	for (int giri = 0; giri < 1 << 20; giri++) {
		uint32_t dopo;
		if (voce_ot(ot))
			dopo = *ot;
		else {
			const P_TAG *t = (const P_TAG *) ot;
			if (t->len && l->n + 1 + t->len <= LISTA_MAX) {
				l->w[l->n] = t->len;
				memcpy(&l->w[l->n + 1], &t->r0, t->len * 4u);
				l->n += 1 + t->len;
			}
			dopo = t->addr;
		}
		if (dopo == 0xffffffff)
			break;
		ot = da_rif(dopo);
	}
	scrivo = SDL_SetAtomicInt(&pronta, scrivo | NUOVA) & 3;
}

/* ---- Rasterizzatore (thread principale) ---- */

static int     cx0, cy0, cx1, cy1; // area di disegno (compresa)
static int     dox, doy;           // offset di disegno
static int     abr, dither;        // modo della semitrasparenza, dithering acceso (texture page)
static int     tbx, tby;           // texture page corrente
static int     clx, cly;           // CLUT corrente
static uint8_t lut[4][4][512];     // 8 bit + dithering -> 5 bit, come la GPU
static uint8_t rgb[32768][3];      // 15 bit -> RGB (5 bit -> 8 bit come DuckStation: c << 3 | c >> 2)

static void lut_prepara(void)
{
	for (int c = 0; c < 32768; c++)
		for (int k = 0; k < 3; k++) {
			int v = c >> (5 * k) & 31;
			rgb[c][k] = (uint8_t) (v << 3 | v >> 2);
		}
	static const int8_t m[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			for (int v = 0; v < 512; v++) {
				int c = v + m[y][x];
				lut[y][x][v] = (uint8_t) ((c < 0 ? 0 : c > 255 ? 255 : c) >> 3);
			}
}

static void set_tpage(uint32_t t)
{
	tbx = (int) (t & 15) * 64, tby = (int) (t >> 4 & 1) * 256, abr = (int) (t >> 5 & 3);
}

// B = fondo, F = primitiva, per canale a 5 bit
static uint16_t mescola(uint16_t b, uint16_t f)
{
	int r = 0;
	for (int s = 0; s < 15; s += 5) {
		int cb = b >> s & 31, cf = f >> s & 31;
		int v  = abr == 0 ? (cb + cf) >> 1 : abr == 1 ? cb + cf : abr == 2 ? cb - cf : cb + (cf >> 2);
		r |= (v < 0 ? 0 : v > 31 ? 31 : v) << s;
	}
	return (uint16_t) (r | (f & 0x8000));
}

static int x11(uint32_t w) { return (int) (w << 21) >> 21; } // coordinata a 11 bit con segno

typedef struct { int x, y, c[3], u, v; } Vert;
enum { M_GOUR = 1, M_TEX = 2, M_SEMI = 4, M_RAW = 8 };

// Il lato a->b e' alto o sinistro: i suoi pixel si disegnano (quelli a destra e in basso no)
static int alto_sx(const Vert *a, const Vert *b) { return b->y < a->y || (b->y == a->y && b->x > a->x); }

static void triangolo(const Vert *a, const Vert *b, const Vert *c, int modo)
{
	int mnx = SDL_min(a->x, SDL_min(b->x, c->x)), mxx = SDL_max(a->x, SDL_max(b->x, c->x));
	int mny = SDL_min(a->y, SDL_min(b->y, c->y)), mxy = SDL_max(a->y, SDL_max(b->y, c->y));
	if (mxx - mnx >= 1024 || mxy - mny >= 512) // troppo grande: la GPU lo salta
		return;
	int64_t area = (int64_t) (b->x - a->x) * (c->y - a->y) - (int64_t) (b->y - a->y) * (c->x - a->x);
	if (area == 0)
		return;
	if (area < 0) {
		const Vert *t = b;
		b = c, c = t, area = -area;
	}
	int x0 = SDL_max(mnx, cx0), x1 = SDL_min(mxx, cx1), y0 = SDL_max(mny, cy0), y1 = SDL_min(mxy, cy1);
	if (x0 > x1 || y0 > y1)
		return;

	// lati: E(x, y) >= 0 dentro; sui lati non alti/sinistri serve > 0
	const Vert *p[3] = { b, c, a }, *q[3] = { c, a, b };
	int         ex[3], ey[3], e0[3];
	for (int i = 0; i < 3; i++) {
		ex[i] = -(q[i]->y - p[i]->y);
		ey[i] = q[i]->x - p[i]->x;
		e0[i] = ey[i] * (y0 - p[i]->y) + ex[i] * (x0 - p[i]->x) - (alto_sx(p[i], q[i]) ? 0 : 1);
	}
	// attributi (r, g, b, u, v) in virgola fissa 12 bit, piani sul triangolo dal vertice a
	int     na = (modo & M_GOUR ? 3 : 0) + (modo & M_TEX ? 2 : 0), ia = modo & M_GOUR ? 0 : 3;
	int64_t ax[5], ay[5], av[5];
	for (int i = 0; i < na; i++) {
		int     k  = ia + i;
		int     k0 = k < 3 ? a->c[k] : k == 3 ? a->u : a->v;
		int     k1 = k < 3 ? b->c[k] : k == 3 ? b->u : b->v;
		int     k2 = k < 3 ? c->c[k] : k == 3 ? c->u : c->v;
		ax[i] = ((int64_t) (k1 - k0) * (c->y - a->y) - (int64_t) (k2 - k0) * (b->y - a->y)) * 4096 / area;
		ay[i] = ((int64_t) (k2 - k0) * (b->x - a->x) - (int64_t) (k1 - k0) * (c->x - a->x)) * 4096 / area;
		av[i] = ((int64_t) k0 << 12) + 0x800 + ax[i] * (x0 - a->x) + ay[i] * (y0 - a->y);
	}
	int dit = dither && (modo & M_GOUR || (modo & (M_TEX | M_RAW)) == M_TEX);
	for (int y = y0; y <= y1; y++) {
		// tratto della riga dentro i tre lati: E(x) = e0 + ex * (x - x0) >= 0
		int xa = x0, xb = x1;
		for (int i = 0; i < 3; i++) {
			int e = e0[i], d = ex[i];
			if (d > 0 && e < 0)
				xa = SDL_max(xa, x0 + (-e + d - 1) / d);
			else if (d < 0)
				xb = e < 0 ? x0 - 1 : SDL_min(xb, x0 + e / -d);
			else if (d == 0 && e < 0)
				xb = x0 - 1;
		}
		int64_t v[5];
		for (int i = 0; i < na; i++)
			v[i] = av[i] + ax[i] * (xa - x0);
		const uint8_t(*ld)[512] = lut[y & 3];
		uint16_t *riga = vram[y];
		for (int x = xa; x <= xb; x++) {
			int r = a->c[0], g = a->c[1], bl = a->c[2], j = 0;
			if (modo & M_GOUR) {
				r  = SDL_clamp((int) (v[0] >> 12), 0, 255);
				g  = SDL_clamp((int) (v[1] >> 12), 0, 255);
				bl = SDL_clamp((int) (v[2] >> 12), 0, 255);
				j  = 3;
			}
			const uint8_t *d    = dit ? ld[x & 3] : lut[2][3]; // [2][3]: scarto 0 = senza dithering
			int            semi = modo & M_SEMI;
			uint16_t       col;
			if (modo & M_TEX) {
				int u = (int) (v[j] >> 12) & 255, tv = (int) (v[j + 1] >> 12) & 255;
				int i = vram[(tby + tv) & 511][(tbx + (u >> 2)) & 1023] >> ((u & 3) * 4) & 15; // 4 bit
				uint16_t t = vram[cly][(clx + i) & 1023], stp = t & 0x8000;
				semi = semi && stp;
				col  = modo & M_RAW ? t
				                    : (uint16_t) (d[(t & 31) * r >> 4] | d[(t >> 5 & 31) * g >> 4] << 5 |
				                                  d[(t >> 10 & 31) * bl >> 4] << 10 | stp);
				if (t == 0)
					col = riga[x], semi = 0; // trasparente
			} else
				col = (uint16_t) (d[r] | d[g] << 5 | d[bl] << 10);
			riga[x] = semi ? mescola(riga[x], col) : col;
			for (int i = 0; i < na; i++)
				v[i] += ax[i];
		}
		for (int i = 0; i < 3; i++)
			e0[i] += ey[i];
		for (int i = 0; i < na; i++)
			av[i] += ay[i];
	}
}

// GP0 20-3F: poligoni (le quadrangolari sono due triangoli, 0-1-2 e 1-2-3, come la GPU)
static void poligono(const uint32_t *w, int len)
{
	int  cmd = (int) (w[0] >> 24), modo = (cmd & 0x10 ? M_GOUR : 0) | (cmd & 4 ? M_TEX : 0) | (cmd & 2 ? M_SEMI : 0) | (cmd & 1 ? M_RAW : 0);
	int  nv = cmd & 8 ? 4 : 3, k = 0;
	Vert v[4];
	for (int i = 0; i < nv; i++) {
		if (k >= len) return;
		uint32_t c = i == 0 || (modo & M_GOUR) ? w[k++] : w[0];
		v[i].c[0] = (int) (c & 255), v[i].c[1] = (int) (c >> 8 & 255), v[i].c[2] = (int) (c >> 16 & 255);
		if (k >= len) return;
		v[i].x = x11(w[k]) + dox, v[i].y = x11(w[k] >> 16) + doy, k++;
		if (modo & M_TEX) {
			if (k >= len) return;
			v[i].u = (int) (w[k] & 255), v[i].v = (int) (w[k] >> 8 & 255);
			if (i == 0)
				clx = (int) (w[k] >> 16 & 63) * 16, cly = (int) (w[k] >> 22 & 511);
			if (i == 1) // la texture page del poligono cambia quella corrente (non il dithering)
				set_tpage(w[k] >> 16);
			k++;
		}
	}
	triangolo(&v[0], &v[1], &v[2], modo);
	if (nv == 4)
		triangolo(&v[1], &v[2], &v[3], modo);
}

// GP0 60-7F: rettangoli a tinta unita (storm non usa gli sprite con texture)
static void rettangolo(const uint32_t *w, int len)
{
	int cmd = (int) (w[0] >> 24), dim = cmd >> 3 & 3, rw, rh;
	if (cmd & 4 || len < (dim ? 2 : 3))
		return;
	if (dim == 0)
		rw = (int) (w[2] & 0x3ff), rh = (int) (w[2] >> 16 & 0x1ff);
	else
		rw = rh = dim == 1 ? 1 : dim == 2 ? 8 : 16;
	int      x = x11(w[1]) + dox, y = x11(w[1] >> 16) + doy;
	uint16_t col = (uint16_t) ((w[0] >> 3 & 31) | (w[0] >> 11 & 31) << 5 | (w[0] >> 19 & 31) << 10);
	for (int yy = SDL_max(y, cy0); yy < y + rh && yy <= cy1; yy++)
		for (int xx = SDL_max(x, cx0); xx < x + rw && xx <= cx1; xx++)
			vram[yy][xx] = cmd & 2 ? mescola(vram[yy][xx], col) : col;
}

static void esegui(const Lista *l)
{
	cx0 = SDL_clamp(l->clip.x, 0, 1023), cy0 = SDL_clamp(l->clip.y, 0, 511);
	cx1 = SDL_clamp(l->clip.x + l->clip.w - 1, 0, 1023), cy1 = SDL_clamp(l->clip.y + l->clip.h - 1, 0, 511);
	dox = l->clip.x + l->ofs[0], doy = l->clip.y + l->ofs[1];
	set_tpage(l->tpage);
	dither = l->dtd & 1;
	if (l->isbg) { // riempimento col colore di fondo (come GP0 02: niente dithering ne' semitrasparenza)
		uint16_t c = (uint16_t) (l->r0 >> 3 | (l->g0 >> 3) << 5 | (l->b0 >> 3) << 10);
		for (int y = cy0; y <= cy1; y++)
			for (int x = cx0; x <= cx1; x++)
				vram[y][x] = c;
	}
	for (int i = 0; i < l->n;) {
		int             len = (int) l->w[i];
		const uint32_t *w   = &l->w[i + 1];
		int             cmd = (int) (w[0] >> 24);
		i += 1 + len;
		if (cmd == 0xe1) {
			set_tpage(w[0]);
			dither = (int) (w[0] >> 9 & 1);
		} else if (cmd >= 0x20 && cmd < 0x40)
			poligono(w, len);
		else if (cmd >= 0x60 && cmd < 0x80)
			rettangolo(w, len);
	}
}

int psx_disegna(void)
{
	static int lut_ok;
	if (!lut_ok)
		lut_prepara(), lut_ok = 1;
	if (!(SDL_GetAtomicInt(&pronta) & NUOVA))
		return 0;
	leggo = SDL_SetAtomicInt(&pronta, leggo) & 3;
	const Lista *l = &liste[leggo];
	esegui(l);
	// area disegnata -> RGB (il bit 15, maschera, non conta)
	for (int y = 0; y < 256; y++)
		for (int x = 0; x < 320; x++) {
			const uint8_t *c = rgb[vram[(l->clip.y + y) & 511][(l->clip.x + x) & 1023] & 0x7fff];
			psx_schermo[y][x][0] = c[0], psx_schermo[y][x][1] = c[1], psx_schermo[y][x][2] = c[2];
		}
	return 1;
}

/* ---- GTE ---- */

static int16_t  rot[3][3];
static int32_t  tr[3], ofx, ofy, sxy;
static uint16_t hh;
static SVECTOR  v0;
static uint8_t  unr[257]; // tabella della divisione della GTE (psx-spx)

void InitGeom(void)
{
	for (int i = 0; i < 257; i++) {
		int u = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
		unr[i] = (uint8_t) (u > 0 ? u : 0);
	}
}
void gte_SetGeomOffset(int x, int y) { ofx = (int32_t) ((uint32_t) x << 16), ofy = (int32_t) ((uint32_t) y << 16); }
void gte_SetGeomScreen(int h) { hh = (uint16_t) h; }
void gte_SetRotMatrix(const MATRIX *m) { memcpy(rot, m->m, sizeof rot); }
void gte_SetTransMatrix(const MATRIX *m) { memcpy(tr, m->t, sizeof tr); }
void gte_ldv0(const SVECTOR *v) { v0 = *v; }
void gte_stsxy(int32_t *xy) { *xy = sxy; }

static int32_t sat(int64_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : (int32_t) v; }

// H / SZ3 in 16.16 come la GTE (Newton-Raphson con la tabella), al massimo 0x1FFFF
static uint32_t dividi(uint32_t h, uint32_t sz)
{
	if (h >= sz * 2)
		return 0x1ffff;
	int z = 0;
	while (z < 15 && !(sz & (0x8000u >> z)))
		z++;
	uint32_t n = h << z, d = sz << z, u = unr[(d - 0x7fc0) >> 7] + 0x101u;
	d = (0x2000080u - d * u) >> 8;
	d = (0x0000080u + d * u) >> 8;
	uint64_t q = ((uint64_t) n * d + 0x8000) >> 16;
	return q > 0x1ffff ? 0x1ffff : (uint32_t) q;
}

// RTPS (sf = 1, lm = 0): rotazione + traslazione, poi proiezione; SX/SY saturate a -1024..1023
void gte_rtps(void)
{
	int64_t mac[3];
	for (int i = 0; i < 3; i++)
		mac[i] = (((int64_t) tr[i] << 12) + rot[i][0] * v0.vx + rot[i][1] * v0.vy + rot[i][2] * v0.vz) >> 12;
	int32_t  ir1 = sat(mac[0], -0x8000, 0x7fff), ir2 = sat(mac[1], -0x8000, 0x7fff);
	uint32_t q = dividi(hh, (uint32_t) sat(mac[2], 0, 0xffff));
	int32_t  x = sat(((int64_t) ofx + (int64_t) ir1 * q) >> 16, -0x400, 0x3ff);
	int32_t  y = sat(((int64_t) ofy + (int64_t) ir2 * q) >> 16, -0x400, 0x3ff);
	sxy = (int32_t) ((uint32_t) (uint16_t) x | (uint32_t) y << 16);
}

// Seno in 4.12 con 4096 = un giro: la stessa approssimazione di isin.c dell'SDK
int isin(int x)
{
	int c = (int) ((uint32_t) x << (30 - 10));
	x -= 1 << 10;
	x = (int) ((uint32_t) x << (31 - 10)) >> (31 - 10);
	x = x * x >> (2 * 10 - 14);
	int y = 19900 - (x * 3516 >> 14);
	y = (1 << 12) - (x * y >> 16);
	return c >= 0 ? y : -y;
}
int icos(int x) { return isin(x + 1024); }

// MulMatrix0 della GTE: ogni elemento (somma dei 3 prodotti) >> 12, saturato a 16 bit
static void mul(int16_t a[3][3], int16_t b[3][3], int16_t r[3][3])
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i][j] = (int16_t) sat(((int64_t) a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]) >> 12, -0x8000, 0x7fff);
}

// Come l'SDK: R = X * Y * Z, con isin/icos e due MulMatrix0
MATRIX *RotMatrix(const SVECTOR *r, MATRIX *m)
{
	int16_t s0 = (int16_t) isin(r->vx), s1 = (int16_t) isin(r->vy), s2 = (int16_t) isin(r->vz);
	int16_t c0 = (int16_t) icos(r->vx), c1 = (int16_t) icos(r->vy), c2 = (int16_t) icos(r->vz);
	int16_t mx[3][3] = { { 4096, 0, 0 }, { 0, c0, (int16_t) -s0 }, { 0, s0, c0 } };
	int16_t my[3][3] = { { c1, 0, s1 }, { 0, 4096, 0 }, { (int16_t) -s1, 0, c1 } };
	int16_t mz[3][3] = { { c2, (int16_t) -s2, 0 }, { s2, c2, 0 }, { 0, 0, 4096 } }, t[3][3];
	mul(mx, my, t);
	mul(t, mz, m->m);
	return m;
}

/* ---- Pad: stato dei due lati da controlli.c (gamepad e tastiera), copiato a ogni fotogramma ---- */

static uint8_t *padbuf[2];

void InitPAD(uint8_t *buf1, int len1, uint8_t *buf2, int len2)
{
	(void) len1, (void) len2;
	padbuf[0] = buf1;
	padbuf[1] = buf2;
}
void StartPAD(void) {}
void ChangeClearPAD(int mode) { (void) mode; }

void psx_pad_leggi(void)
{
	for (int i = 0; i < 2; i++)
		if (padbuf[i]) {
			PADTYPE *p = (PADTYPE *) padbuf[i];
			uint8_t  l[4];
			p->stat = 0;
			p->type = PAD_ID_ANALOG; // DualShock; senza gamepad le levette stanno ferme a 128
			p->btn  = (uint16_t) ~controlli_pad(i, l); // 0 = premuto, come la PS1
			p->ls_x = l[0], p->ls_y = l[1], p->rs_x = l[2], p->rs_y = l[3];
		}
}

/* ---- Memory card: nessuna (le scene su file arrivano dopo) ---- */

void InitCARD(int pad_enable) { (void) pad_enable; }
void StartCARD(void) {}
void _bu_init(void) {}
