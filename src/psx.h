/*
 * Piccola PS1 finta: le funzioni e i registri del PSn00bSDK che usa storm.c, fatti in software.
 * Al posto di psxgpu.h, psxgte.h, inline_c.h, psxapi.h, psxetc.h, psxpad.h, psxspu.h e hwregs_c.h.
 * Stessi nomi, stessi campi e stesse costanti dell'SDK, cosi' il codice PS1 compila quasi uguale.
 * psx_spu.c = SPU (24 voci, RAM 512 KB, riverbero) e timer 2; psx_gpu.c = GPU, GTE, pad, card.
 * SPU, GTE e ordering table girano nel thread di psx_suona e del fotogramma di storm (niente lock);
 * la rasterizzazione nel thread di psx_disegna, che riceve le liste da DrawOTagEnv (vedi psx_gpu.c).
 */
#ifndef PSX_H
#define PSX_H
#include <stdint.h>
#include <stddef.h>

/* ---- SPU (registri 0x1F801C00-0x1F801FFF come array di mezze parole) ---- */
extern uint16_t spu_reg[512];
#define SPU_R(a)            spu_reg[((a) - 0x1c00) >> 1]
#define SPU_CH_VOL_L(n)     SPU_R(0x1c00 + 16 * (n))
#define SPU_CH_VOL_R(n)     SPU_R(0x1c02 + 16 * (n))
#define SPU_CH_FREQ(n)      SPU_R(0x1c04 + 16 * (n))
#define SPU_CH_ADDR(n)      SPU_R(0x1c06 + 16 * (n))
#define SPU_CH_ADSR1(n)     SPU_R(0x1c08 + 16 * (n))
#define SPU_CH_ADSR2(n)     SPU_R(0x1c0a + 16 * (n))
#define SPU_CH_ADSR_VOL(n)  SPU_R(0x1c0c + 16 * (n))
#define SPU_CH_LOOP_ADDR(n) SPU_R(0x1c0e + 16 * (n))
#define SPU_MASTER_VOL_L    SPU_R(0x1d80)
#define SPU_MASTER_VOL_R    SPU_R(0x1d82)
#define SPU_REVERB_VOL_L    SPU_R(0x1d84)
#define SPU_REVERB_VOL_R    SPU_R(0x1d86)
#define SPU_KEY_ON1         SPU_R(0x1d88)
#define SPU_KEY_ON2         SPU_R(0x1d8a)
#define SPU_KEY_OFF1        SPU_R(0x1d8c)
#define SPU_KEY_OFF2        SPU_R(0x1d8e)
#define SPU_FM_MODE1        SPU_R(0x1d90)
#define SPU_FM_MODE2        SPU_R(0x1d92)
#define SPU_NOISE_MODE1     SPU_R(0x1d94)
#define SPU_NOISE_MODE2     SPU_R(0x1d96)
#define SPU_REVERB_ON1      SPU_R(0x1d98)
#define SPU_REVERB_ON2      SPU_R(0x1d9a)
#define SPU_CHAN_STATUS1    SPU_R(0x1d9c)
#define SPU_CHAN_STATUS2    SPU_R(0x1d9e)
#define SPU_REVERB_ADDR     SPU_R(0x1da2)
#define SPU_CTRL            SPU_R(0x1daa)

typedef enum { SPU_TRANSFER_BY_DMA = 0, SPU_TRANSFER_BY_IO = 1 } SPU_TransferMode;
typedef enum { SPU_TRANSFER_PEEK = 0, SPU_TRANSFER_WAIT = 1 } SPU_WaitMode;

// Come l'SDK, ma i key-on si sommano finche' la SPU non li ha letti (sulla PS1 la SPU li legge
// subito; qui al campione dopo: due SpuSetKey di fila non si devono cancellare)
#define SpuSetKey(enable, voice_bit) \
	((enable) ? (SPU_KEY_ON1 |= (uint16_t) (voice_bit), SPU_KEY_ON2 |= (uint16_t) ((voice_bit) >> 16)) \
	          : (SPU_KEY_OFF1 |= (uint16_t) (voice_bit), SPU_KEY_OFF2 |= (uint16_t) ((voice_bit) >> 16)))

void             SpuInit(void);
size_t           SpuWrite(const uint32_t *data, size_t size);
SPU_TransferMode SpuSetTransferMode(SPU_TransferMode mode);
uint32_t         SpuSetTransferStartAddr(uint32_t addr);
int              SpuIsTransferCompleted(int mode);

// Solo PC: chiamata prima che una voce decodifichi il blocco ADPCM all'indirizzo ind (unita' da
// 8 byte). storm la usa per scrivere il blocco del motore DSP proprio quando la voce 0 lo legge.
extern void (*spu_prima_del_blocco)(int voce, uint32_t ind);
// Solo PC: n campioni stereo a 44100 Hz (timer 2 + SPU), come passa il tempo sulla PS1
void psx_suona(int16_t *out, int n);

/* ---- Timer e interrupt ---- */
extern uint32_t psx_timer[3][3]; // valore, controllo, ricarica
#define TIMER_VALUE(n)  psx_timer[n][0]
#define TIMER_CTRL(n)   psx_timer[n][1]
#define TIMER_RELOAD(n) psx_timer[n][2]
#define IRQ_TIMER2 6
void *InterruptCallback(int irq, void (*func)(void));
// con altri nomi: su Windows EnterCriticalSection e' gia' di kernel32
#define EnterCriticalSection psx_entra_critica
#define ExitCriticalSection  psx_esci_critica
int  psx_entra_critica(void);
void psx_esci_critica(void);

/* ---- GPU (stesse struct dell'SDK; l'etichetta e' un indice a 32 bit, non un puntatore) ---- */
typedef struct { int16_t x, y, w, h; } RECT;
typedef struct { uint32_t addr; uint8_t len, pad_[3]; uint8_t r0, g0, b0, code; } P_TAG;
#define _TAG uint32_t tag; uint8_t len_, tpad_[3];
typedef struct { _TAG uint8_t r0, g0, b0, code; int16_t x0, y0, x1, y1, x2, y2; } POLY_F3;
typedef struct { _TAG uint8_t r0, g0, b0, code; int16_t x0, y0, x1, y1, x2, y2, x3, y3; } POLY_F4;
typedef struct {
	_TAG uint8_t r0, g0, b0, code;
	int16_t x0, y0; uint8_t u0, v0; uint16_t clut;
	int16_t x1, y1; uint8_t u1, v1; uint16_t tpage;
	int16_t x2, y2; uint8_t u2, v2; uint16_t pad0;
	int16_t x3, y3; uint8_t u3, v3; uint16_t pad1;
} POLY_FT4;
typedef struct {
	_TAG uint8_t r0, g0, b0, code;
	int16_t x0, y0; uint8_t r1, g1, b1, pad0;
	int16_t x1, y1; uint8_t r2, g2, b2, pad1;
	int16_t x2, y2; uint8_t r3, g3, b3, pad2;
	int16_t x3, y3;
} POLY_G4;
typedef struct { _TAG uint8_t r0, g0, b0, code; int16_t x0, y0; int16_t w, h; } TILE;
typedef struct { _TAG uint32_t code[1]; } DR_TPAGE;
typedef struct { _TAG uint32_t code[15]; } DR_ENV;
typedef struct { RECT disp, screen; uint8_t isinter, isrgb24, reverse, _reserved; } DISPENV;
typedef struct {
	RECT clip; int16_t ofs[2]; RECT tw; uint16_t tpage; uint8_t dtd, dfe, isbg, r0, g0, b0; DR_ENV dr_env;
} DRAWENV;

// Riferimento a 32 bit di una primitiva (offset da un punto fisso del programma)
uint32_t psx_rif(const void *p);
#define setRGB0(p, r, g, b) ((p)->r0 = (r), (p)->g0 = (g), (p)->b0 = (b))
#define setRGB1(p, r, g, b) ((p)->r1 = (r), (p)->g1 = (g), (p)->b1 = (b))
#define setRGB2(p, r, g, b) ((p)->r2 = (r), (p)->g2 = (g), (p)->b2 = (b))
#define setRGB3(p, r, g, b) ((p)->r3 = (r), (p)->g3 = (g), (p)->b3 = (b))
#define setXY0(p, _x0, _y0) (p)->x0 = (_x0), (p)->y0 = (_y0)
#define setXY3(p, _x0, _y0, _x1, _y1, _x2, _y2) \
	(p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), (p)->x2 = (_x2), (p)->y2 = (_y2)
#define setXY4(p, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3) \
	(p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), \
	(p)->x2 = (_x2), (p)->y2 = (_y2), (p)->x3 = (_x3), (p)->y3 = (_y3)
#define setWH(p, _w, _h) (p)->w = (_w), (p)->h = (_h)
#define setXYWH(p, _x0, _y0, _w, _h) \
	(p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = ((_x0) + (_w)), (p)->y1 = (_y0), \
	(p)->x2 = (_x0), (p)->y2 = ((_y0) + (_h)), (p)->x3 = ((_x0) + (_w)), (p)->y3 = ((_y0) + (_h))
#define setUVWH(p, _u0, _v0, _w, _h) \
	(p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = ((_u0) + (_w)), (p)->v1 = (_v0), \
	(p)->u2 = (_u0), (p)->v2 = ((_v0) + (_h)), (p)->u3 = ((_u0) + (_w)), (p)->v3 = ((_v0) + (_h))
#define setlen(p, _len)   (((P_TAG *) (p))->len = (uint8_t) (_len))
#define setcode(p, _code) (((P_TAG *) (p))->code = (uint8_t) (_code))
#define getcode(p)        (((P_TAG *) (p))->code)
#define addPrim(ot, p)    (((P_TAG *) (p))->addr = *(uint32_t *) (ot), *(uint32_t *) (ot) = psx_rif(p))
#define setSemiTrans(p, abe) ((abe) ? (getcode(p) |= 2) : (getcode(p) &= ~2))
#define getTPage(tp, abr, x, y) \
	((((x) & 0x3c0) >> 6) | (((y) & 0x100) >> 4) | (((y) & 0x200) << 2) | (((abr) & 3) << 5) | (((tp) & 3) << 7))
#define getClut(x, y) (((y) << 6) | (((x) >> 4) & 0x3f))
#define setPolyF3(p)  setlen(p, 4), setcode(p, 0x20)
#define setPolyF4(p)  setlen(p, 5), setcode(p, 0x28)
#define setPolyFT4(p) setlen(p, 9), setcode(p, 0x2c)
#define setPolyG4(p)  setlen(p, 8), setcode(p, 0x38)
#define setTile(p)    setlen(p, 3), setcode(p, 0x60)
#define setDrawTPage(p, dfe, dtd, tpage) \
	setlen(p, 1), (p)->code[0] = (0xe1000000 | (tpage) | (((dtd) & 1) << 9) | (((dfe) & 1) << 10))
#define MODE_NTSC 0
#define MODE_PAL  1

void      ResetGraph(int mode);
void      SetVideoMode(int mode);
void      SetDispMask(int mask);
void      SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
void      SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
void      PutDispEnv(const DISPENV *env);
void      DrawOTagEnv(const uint32_t *ot, DRAWENV *env);
void      ClearOTagR(uint32_t *ot, size_t n);
void      LoadImage(const RECT *r, const uint32_t *data);
int       DrawSync(int mode);
int       VSync(int mode);
// Solo PC: rasterizza l'ultima lista passata da DrawOTagEnv in psx_schermo (320x256 RGB);
// 0 se non c'e' un fotogramma nuovo. Da chiamare da un thread solo.
int            psx_disegna(void);
extern uint8_t psx_schermo[256][320][3];

/* ---- GTE (virgola fissa 4.12) ---- */
typedef struct { int16_t vx, vy, vz; } SVECTOR; // senza pad: storm li scrive con 3 valori
typedef struct { int16_t m[3][3]; int32_t t[3]; } MATRIX;
void    InitGeom(void);
MATRIX *RotMatrix(const SVECTOR *r, MATRIX *m);
int     isin(int a);
int     icos(int a);
void    gte_SetGeomOffset(int x, int y);
void    gte_SetGeomScreen(int h);
void    gte_SetRotMatrix(const MATRIX *m);
void    gte_SetTransMatrix(const MATRIX *m);
void    gte_ldv0(const SVECTOR *v);
void    gte_rtps(void);
void    gte_stsxy(int32_t *xy);

/* ---- Pad ---- */
enum {
	PAD_SELECT = 1 << 0, PAD_L3 = 1 << 1, PAD_R3 = 1 << 2, PAD_START = 1 << 3, PAD_UP = 1 << 4,
	PAD_RIGHT = 1 << 5, PAD_DOWN = 1 << 6, PAD_LEFT = 1 << 7, PAD_L2 = 1 << 8, PAD_R2 = 1 << 9,
	PAD_L1 = 1 << 10, PAD_R1 = 1 << 11, PAD_TRIANGLE = 1 << 12, PAD_CIRCLE = 1 << 13,
	PAD_CROSS = 1 << 14, PAD_SQUARE = 1 << 15
};
#define PAD_ID_DIGITAL 0x4
#define PAD_ID_ANALOG  0x7
// Come quello dell'SDK, senza campi a bit: type e' il byte intero (solo il tipo)
typedef struct { uint8_t stat, type; uint16_t btn; uint8_t rs_x, rs_y, ls_x, ls_y; } PADTYPE;
void InitPAD(uint8_t *buf1, int len1, uint8_t *buf2, int len2);
void StartPAD(void);
void ChangeClearPAD(int mode);
// Solo PC: copia lo stato dei pad (da controlli.c) nei buffer di InitPAD (sulla PS1 lo fa il BIOS al VSync)
void psx_pad_leggi(void);

/* ---- Memory card (su PC le scene vanno su file: per ora nessuna card) ---- */
void InitCARD(int pad_enable);
void StartCARD(void);
void _bu_init(void);
#endif
