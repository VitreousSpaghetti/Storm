/*
 * SPU della PS1 in software (e il timer 2, che conta lo stesso tempo): 24 voci ADPCM con
 * interpolazione gaussiana a 4 punti, pitch 4.12 con modulazione dalla voce prima (PMON),
 * generatore di rumore, inviluppo ADSR, volumi sinistro/destro con segno, ENDX e loop, riverbero
 * a 22050 Hz nella RAM della SPU con i filtri di ricampionamento a 39 punti.
 * Formule e tabelle di psx-spx (problemkaputt.de/psx-spx.htm); ADSR, ordine del mix, riverbero e
 * arrotondamenti come gli emulatori di riferimento (Mednafen, DuckStation), su cui suona l'originale.
 * Solo interi: stesso ingresso = stessa uscita, su ogni piattaforma.
 */
#include <string.h>
#include "psx.h"

uint16_t spu_reg[512];
uint32_t psx_timer[3][3];
void (*spu_prima_del_blocco)(int voce, uint32_t ind);

static uint16_t ram[0x40000];  // 512 KB in mezze parole (little-endian come la PS1)
static uint32_t trasf;         // indirizzo di trasferimento, unita' da 8 byte (come l'SDK)
static void (*irq_timer2)(void);

static const int16_t gauss[512] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
	3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
	39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
	84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
	156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
	266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
	424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
	643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
	935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
	1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
	1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
	2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
	3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
	3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
	4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
	5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
	7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
	8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
	9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
	11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
	12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
	14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
	15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
	17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
	18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
	19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
	21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
	21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
	22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
	22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

typedef struct {
	uint32_t ind;        // blocco che suona (unita' da 8 byte)
	uint32_t cont;       // contatore del pitch: bit 12-16 campione nel blocco, bit 4-11 indice gaussiano
	int16_t  c[3 + 28];  // ultimi 3 campioni del blocco prima + i 28 del blocco
	int16_t  s1, s2;     // stato del decodificatore ADPCM
	int      pieno;      // blocco gia' decodificato
	uint8_t  flag;       // flag del blocco: 1 fine, 2 ripeti, 4 inizio loop
	int      fase;       // ADSR: 0 spenta, 1 attacco, 2 decadimento, 3 sostegno, 4 rilascio
	int32_t  env, div;   // livello e divisore dell'ADSR
	int32_t  ultimo;     // ultima uscita dopo l'ADSR (modula la voce dopo)
} Voce;

static Voce     voce[24];
static uint32_t endx;
static uint32_t rum_c;
static int16_t  rum = 1;
static uint16_t base_prima = 0xffff; // per vedere quando cambia mBASE
static uint32_t rv_base, rv_cur;     // riverbero: inizio dell'area e posizione, in mezze parole
static int16_t  rv_giu[2][128], rv_su[2][64];
static uint32_t rv_pos;

static int16_t sat(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : (int16_t) v; }
static int32_t vol(uint16_t r) { return (int16_t) (r << 1); } // volume fisso: registro = volume / 2
static int32_t per(int32_t s, int32_t v) { return s * v >> 15; }

/* ---- Trasferimenti (come l'SDK: sotto 0x1000 non si scrive) ---- */

void SpuInit(void)
{
	memset(spu_reg, 0, sizeof spu_reg);
	memset(voce, 0, sizeof voce);
	memset(ram, 0, sizeof ram);
	ram[0x1000 / 2] = 0x0500; // blocco muto dell'SDK a 0x1000: flag inizio loop + fine
	// L'SDK mette tutte le voci sul blocco muto e le accende: il blocco (inizio loop + fine senza
	// ripeti) le spegne subito e lascia l'indirizzo di loop a 0x1000. Qui si scrive solo il risultato.
	for (int v = 0; v < 24; v++) {
		SPU_CH_FREQ(v)      = 0x1000;
		SPU_CH_ADDR(v)      = 0x200;
		SPU_CH_LOOP_ADDR(v) = 0x200;
	}
	endx             = 0xffffff;
	SPU_REVERB_ADDR  = 0xfffe;
	SPU_MASTER_VOL_L = SPU_MASTER_VOL_R = 0x3fff;
	SPU_CTRL         = 0xc001;
	trasf            = 0x200;
}

uint32_t SpuSetTransferStartAddr(uint32_t addr)
{
	if (addr >= 0x80000)
		return 0;
	trasf = (addr + 7) / 8;
	return addr;
}

size_t SpuWrite(const uint32_t *data, size_t size)
{
	if (trasf < 0x200)
		return 0;
	size_t n = size < 0x80000 - trasf * 8 ? size : 0x80000 - trasf * 8;
	memcpy((uint8_t *) ram + trasf * 8, data, n);
	return n;
}

SPU_TransferMode SpuSetTransferMode(SPU_TransferMode mode) { return mode; }
int              SpuIsTransferCompleted(int mode) { (void) mode; return 1; }

void *InterruptCallback(int irq, void (*func)(void))
{
	void (*prima)(void) = irq_timer2;
	if (irq == IRQ_TIMER2)
		irq_timer2 = func;
	return (void *) prima;
}
int  EnterCriticalSection(void) { return 1; }
void ExitCriticalSection(void) {}

/* ---- Voci ---- */

static void decodifica(Voce *v)
{
	static const int fp[5] = { 0, 60, 115, 98, 122 }, fn[5] = { 0, 0, -52, -55, -60 };
	const uint8_t *b = (const uint8_t *) ram + (v->ind & 0xfffe) * 8;
	int sh = b[0] & 15, f = b[0] >> 4 & 7;
	if (sh > 12) sh = 9;
	if (f > 4) f = 4;
	v->c[0] = v->c[28];
	v->c[1] = v->c[29];
	v->c[2] = v->c[30];
	int s1 = v->s1, s2 = v->s2;
	for (int i = 0; i < 28; i++) {
		int nib = b[2 + i / 2] >> (i & 1 ? 4 : 0) & 15;
		int32_t s = (int16_t) (nib << 12) >> sh;
		s += s1 * fp[f] >> 6;
		s += s2 * fn[f] >> 6;
		s2 = s1;
		s1 = v->c[3 + i] = sat(s);
	}
	v->s1 = (int16_t) s1;
	v->s2 = (int16_t) s2;
	v->flag = b[1];
	v->pieno = 1;
}

// Un passo dell'inviluppo ADSR (a ogni campione, a 44100 Hz)
static void adsr(Voce *v, uint16_t a1, uint16_t a2)
{
	if (v->fase == 1 && v->env == 0x7fff)
		v->fase = 2;
	int rate, giu, ex, zero;
	switch (v->fase) {
	case 1:  rate = a1 >> 8 & 0x7f; giu = 0; ex = a1 >> 15; zero = 0x7f; break;
	case 2:  rate = (a1 >> 4 & 15) << 2; giu = 1; ex = 1; zero = 0x7c; break;
	case 3:  rate = a2 >> 6 & 0x7f; giu = a2 >> 14 & 1; ex = a2 >> 15; zero = 0x7f; break;
	default: rate = (a2 & 0x1f) << 2; giu = 1; ex = a2 >> 5 & 1; zero = 0x7c; break;
	}
	int32_t inc = 7 - (rate & 3), dv = 0x8000;
	if (giu)
		inc = ~inc; // -8..-5
	if (rate < 0x2c)
		inc *= 1 << ((0x2f - rate) >> 2);
	if (rate >= 0x30)
		dv >>= (rate - 0x2c) >> 2;
	if (ex) {
		if (giu)
			inc = inc * v->env >> 15;
		else if (v->env >= 0x6000) { // l'esponenziale in salita e' un lineare piu' lento in alto
			if (rate < 0x28)
				inc >>= 2;
			else if (rate >= 0x2c)
				dv >>= 2;
			else {
				inc >>= 1;
				dv >>= 1;
			}
		}
	}
	if ((rate & zero) == zero) // velocita' massima = fermo
		dv = 0;
	v->div += dv;
	if (v->div & 0x8000) {
		v->div = 0;
		int32_t e = v->env + inc;
		v->env = e < 0 ? 0 : e > 0x7fff ? 0x7fff : e;
		if (v->fase == 2 && v->env < ((a1 & 15) + 1) << 11)
			v->fase = 3;
	}
}

// Una voce per un campione: uscita sinistra e destra
static void suona_voce(int n, int32_t *l, int32_t *r)
{
	Voce *v = &voce[n];
	if (!v->fase) {
		v->ultimo = 0;
		*l = *r = 0;
		return;
	}
	uint32_t bit = 1u << n, fm = SPU_FM_MODE1 | (uint32_t) SPU_FM_MODE2 << 16;
	uint32_t rum_on = (SPU_NOISE_MODE1 | (uint32_t) SPU_NOISE_MODE2 << 16) & bit;
	if (!v->pieno) {
		if (spu_prima_del_blocco)
			spu_prima_del_blocco(n, v->ind & 0xfffe);
		decodifica(v);
		if (v->flag & 4)
			SPU_CH_LOOP_ADDR(n) = (uint16_t) v->ind;
	}
	int32_t s = 0;
	if (v->env) {
		if (rum_on)
			s = rum;
		else {
			int i = v->cont >> 4 & 0xff, k = v->cont >> 12;
			s = (gauss[0xff - i] * v->c[k] + gauss[0x1ff - i] * v->c[k + 1] + gauss[0x100 + i] * v->c[k + 2] +
			     gauss[i] * v->c[k + 3]) >> 15;
		}
		s = per(s, v->env);
	}
	v->ultimo = s;
	adsr(v, SPU_CH_ADSR1(n), SPU_CH_ADSR2(n));
	SPU_CH_ADSR_VOL(n) = (uint16_t) v->env;

	uint32_t step = SPU_CH_FREQ(n);
	if ((fm & bit) && n > 0) {
		int32_t f = sat(voce[n - 1].ultimo) + 0x8000;
		step = (uint32_t) ((int32_t) (int16_t) step * f >> 15) & 0xffff;
	}
	if (step > 0x3fff)
		step = 0x4000;
	v->cont += step;
	if ((v->cont >> 12) >= 28) {
		v->cont -= 28 << 12;
		v->pieno = 0;
		v->ind = (v->ind + 2) & 0xffff;
		if (v->flag & 1) {
			endx |= bit;
			v->ind = SPU_CH_LOOP_ADDR(n) & 0xfffe;
			if (!(v->flag & 2) && !rum_on) { // fine senza ripeti: la voce si spegne (col rumore no)
				v->fase = 0;
				v->env  = 0;
			}
		}
	}
	*l = per(s, vol(SPU_CH_VOL_L(n)));
	*r = per(s, vol(SPU_CH_VOL_R(n)));
}

static void tasti(void)
{
	uint32_t on = SPU_KEY_ON1 | (uint32_t) SPU_KEY_ON2 << 16, off = SPU_KEY_OFF1 | (uint32_t) SPU_KEY_OFF2 << 16;
	SPU_KEY_ON1 = SPU_KEY_ON2 = SPU_KEY_OFF1 = SPU_KEY_OFF2 = 0;
	for (int n = 0; n < 24; n++) {
		Voce *v = &voce[n];
		if (on >> n & 1) {
			v->ind  = SPU_CH_ADDR(n) & 0xfffe;
			v->cont = 0;
			v->env  = 0;
			v->div  = 0;
			v->s1 = v->s2 = 0;
			memset(v->c, 0, sizeof v->c);
			v->pieno = 0;
			v->fase  = 1;
			endx &= ~(1u << n);
		} else if (off >> n & 1 && v->fase) {
			v->fase = 4;
			v->div  = 0;
		}
	}
	SPU_CHAN_STATUS1 = (uint16_t) endx;
	SPU_CHAN_STATUS2 = (uint16_t) (endx >> 16);
}

// Rumore: registro a scorrimento (parita' dei bit 15, 12, 11, 10) con il clock da SPU_CTRL
// (shift bit 10-13, step bit 8-9). Contatore come DuckStation (src/core/spu.cpp, UpdateNoise),
// dove l'originale e' stato accordato: con step 1-3 il registro va al doppio dello step 0
// (psx-spx e Mednafen: x1,25 / x1,5 / x1,75).
static void rumore(void)
{
	static const uint16_t add[5] = {0, 84, 140, 180, 210};
	int clk = SPU_CTRL >> 8 & 0x3f;
	uint32_t liv = (0x8000u >> (clk >> 2)) << 16;
	rum_c += 0x10000u + add[clk & 3];
	if ((rum_c & 0xffff) >= add[4])
		rum_c += 0x10000u - add[clk & 3];
	if (rum_c < liv)
		return;
	rum_c %= liv;
	int par = (rum >> 15 ^ rum >> 12 ^ rum >> 11 ^ rum >> 10 ^ 1) & 1;
	rum = (int16_t) (rum * 2 + par);
}

/* ---- Riverbero (registri 0x1F801DC0-0x1F801DFF) ---- */

#define RV(i)  ((int16_t) SPU_R(0x1dc0 + 2 * (i)))
#define RVA(i) ((int32_t) SPU_R(0x1dc0 + 2 * (i)) << 2) // indirizzo: unita' da 8 byte -> mezze parole
enum { dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1,
       mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4,
       dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN };

// Filtro di ricampionamento (39 punti; quelli a zero tolti, quello centrale vale 0x4000)
static const int16_t ricampiona[20] = { -1, 2, -10, 35, -103, 266, -616, 1332, -2960, 10246,
                                        10246, -2960, 1332, -616, 266, -103, 35, -10, 2, -1 };

static uint32_t rv_ind(int32_t off)
{
	uint32_t a = rv_cur + ((uint32_t) off & 0x3ffff);
	if (a >= 0x40000) // oltre la fine della RAM si riparte dall'inizio dell'area
		a = a - 0x40000 + rv_base;
	return a & 0x3ffff;
}
static int32_t rv_leggi(int32_t off) { return (int16_t) ram[rv_ind(off)]; }
static void    rv_scrivi(int32_t off, int32_t v) { ram[rv_ind(off)] = (uint16_t) sat(v); }
static int32_t neg(int32_t v) { return v == -32768 ? 32767 : -v; }
static int32_t iir_m(int32_t a, int32_t s)
{
	if (a == -32768)
		return s == -32768 ? 0 : s * -65536;
	return s * (32768 - a);
}

static void riverbero(int32_t in_l, int32_t in_r, int32_t *out_l, int32_t *out_r)
{
	if (SPU_REVERB_ADDR != base_prima) { // nuovo inizio dell'area: si riparte da li'
		base_prima = SPU_REVERB_ADDR;
		rv_base = rv_cur = (uint32_t) SPU_REVERB_ADDR << 2 & 0x3ffff;
	}
	int     scrive = SPU_CTRL >> 7 & 1;
	int32_t in[2] = { in_l, in_r }, out[2];
	for (int lr = 0; lr < 2; lr++)
		rv_giu[lr][rv_pos] = rv_giu[lr][rv_pos | 0x40] = (int16_t) in[lr];
	if (rv_pos & 1) { // un campione a 22050 Hz
		for (int lr = 0; lr < 2; lr++) {
			const int16_t *g = &rv_giu[lr][(rv_pos - 38) & 0x3f];
			int32_t x = 0x4000 * g[19];
			for (int i = 0; i < 20; i++)
				x += ricampiona[i] * g[i * 2];
			x = sat(x >> 15);
			int32_t lin = lr ? RV(vRIN) : RV(vLIN), wall = RV(vWALL), iir = RV(vIIR);
			if (scrive) {
				int32_t ia = sat(((rv_leggi(RVA(dLSAME + lr)) * wall >> 14) + (x * lin >> 14)) >> 1);
				int32_t ib = sat(((rv_leggi(RVA(dRDIFF - lr)) * wall >> 14) + (x * lin >> 14)) >> 1);
				int32_t a  = sat(((ia * iir >> 14) + (iir_m(iir, rv_leggi(RVA(mLSAME + lr) - 1)) >> 14)) >> 1);
				int32_t b  = sat(((ib * iir >> 14) + (iir_m(iir, rv_leggi(RVA(mLDIFF + lr) - 1)) >> 14)) >> 1);
				rv_scrivi(RVA(mLSAME + lr), a);
				rv_scrivi(RVA(mLDIFF + lr), b);
			}
			int32_t acc = (rv_leggi(RVA(mLCOMB1 + lr)) * RV(vCOMB1) >> 14) + (rv_leggi(RVA(mLCOMB2 + lr)) * RV(vCOMB2) >> 14) +
			              (rv_leggi(RVA(mLCOMB3 + lr)) * RV(vCOMB3) >> 14) + (rv_leggi(RVA(mLCOMB4 + lr)) * RV(vCOMB4) >> 14);
			int32_t fa  = rv_leggi(RVA(mLAPF1 + lr) - RVA(dAPF1)), fb = rv_leggi(RVA(mLAPF2 + lr) - RVA(dAPF2));
			int32_t mda = sat((acc + (fa * neg(RV(vAPF1)) >> 14)) >> 1);
			int32_t mdb = sat(fa + (((mda * RV(vAPF1) >> 14) + (fb * neg(RV(vAPF2)) >> 14)) >> 1));
			int32_t ivb = sat(fb + (mdb * RV(vAPF2) >> 15));
			if (scrive) {
				rv_scrivi(RVA(mLAPF1 + lr), mda);
				rv_scrivi(RVA(mLAPF2 + lr), mdb);
			}
			rv_su[lr][rv_pos >> 1] = rv_su[lr][(rv_pos >> 1) | 0x20] = (int16_t) ivb;
		}
		rv_cur = (rv_cur + 1) & 0x3ffff;
		if (rv_cur == 0)
			rv_cur = rv_base;
		for (int lr = 0; lr < 2; lr++) {
			const int16_t *u = &rv_su[lr][((rv_pos >> 1) - 19) & 0x1f];
			int32_t x = 0;
			for (int i = 0; i < 20; i++)
				x += ricampiona[i] * u[i];
			out[lr] = sat(x >> 14);
		}
	} else
		for (int lr = 0; lr < 2; lr++)
			out[lr] = rv_su[lr][((((rv_pos >> 1) - 19) & 0x1f) + 9)];
	rv_pos = (rv_pos + 1) & 0x3f;
	*out_l = per(out[0], (int16_t) SPU_REVERB_VOL_L);
	*out_r = per(out[1], (int16_t) SPU_REVERB_VOL_R);
}

/* ---- Il tempo che passa: timer 2 e SPU, un campione a 44100 Hz alla volta ---- */

void psx_suona(int16_t *out, int n)
{
	for (int i = 0; i < n; i++) {
		// timer 2 a CLK/8 = 4233600 Hz: 96 conteggi per campione della SPU
		if (irq_timer2 && TIMER_RELOAD(2)) {
			TIMER_VALUE(2) += 96;
			while (TIMER_VALUE(2) >= TIMER_RELOAD(2)) {
				TIMER_VALUE(2) -= TIMER_RELOAD(2);
				irq_timer2();
			}
		}
		tasti();
		int32_t sl = 0, sr = 0, rl = 0, rr = 0;
		uint32_t eon = SPU_REVERB_ON1 | (uint32_t) SPU_REVERB_ON2 << 16;
		for (int v = 0; v < 24; v++) {
			int32_t l, r;
			suona_voce(v, &l, &r);
			sl += l;
			sr += r;
			if (eon >> v & 1) {
				rl += l;
				rr += r;
			}
		}
		rumore();
		int32_t ol, orr;
		riverbero(sat(rl), sat(rr), &ol, &orr);
		sl += ol;
		sr += orr;
		int muto = !(SPU_CTRL & 0x4000);
		out[2 * i]     = muto ? 0 : sat(per(sat(sl), vol(SPU_MASTER_VOL_L)));
		out[2 * i + 1] = muto ? 0 : sat(per(sat(sr), vol(SPU_MASTER_VOL_R)));
	}
}
