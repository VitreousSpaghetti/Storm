"""Genera le tabelle di storm (suoni.h) e il font (font.h). Lo chiama build.py prima di compilare.

suoni.h:
- seno: 256 valori a 16 bit (oscillatori del motore DSP, LFO).
- inc_nota: incremento di fase a 32 bit per campione a 22050 Hz, note MIDI 21-69 (LA0-LA4).
- svf_g: g = tan(pi fc / 22050) in 1/4096 del filtro state variable (ZDF), fc = 30 Hz x 2^(i/32)
  per i = 0-255 (30 Hz - ~7,9 kHz), come in nucleo ma su tutto il cutoff 0-99.
- inc_mod: incremento di fase a 32 bit per blocco (787,5 blocchi al secondo) dei generatori di
  MOTION, RATE 0-99 esponenziale da 0,005 Hz a 20 Hz.
- blob per la SPU RAM (strati e campane, tappa c): MUTO (blocco silenzioso che gira su se stesso),
  6 seghe (LAYERS, COLOR: passa-basso a 1 polo a 2/5/12/40 armoniche, passa-banda e passa-alto) e 8 seni a ampiezza
  crescente (modulatori delle BELLS, 0 = nessuna modulazione). Ogni onda = 2 cicli da 112
  campioni = 8 blocchi = 128 byte; a pitch 0x1000 (44,1 kHz) un ciclo suona 393,75 Hz, quindi il
  pitch massimo 0x3FFF arriva a ~1,5 kHz. Nessun flag "inizio loop": l'indirizzo di loop lo
  scrive il programma (cosi' si cambia onda al giro, senza perdere la fase); l'ultimo blocco ha
  fine + ripeti. Il primo blocco del 2deg ciclo usa il filtro ADPCM 0 (come nucleo): il salto in
  un'altra onda non porta stato sbagliato. Encoder ADPCM copiato da nucleo.
Font: codice di nucleo/script/prepara.py (a sua volta da ui_demo), con i simboli dei tasti.
"""
import math
from pathlib import Path

QUI = Path(__file__).resolve().parent.parent  # cartella del gioco
FS = 22050
NOTA0, N_NOTE = 21, 49

inc = [round(440 * 2 ** ((m - 69) / 12) / FS * 2 ** 32) for m in range(NOTA0, NOTA0 + N_NOTE)]
assert max(inc) * 7 < 2 ** 31  # rapporto FM fino a 7/2 sull'incremento / 2


CICLO = 112
ARM = CICLO // 2 - 1
# Colori delle LAYERS: (modo, armonica) del filtro a 1 o 2 poli applicato alla sega.
# LP = scuro..chiaro; THIN = passa-banda stretto in alto (strato sottile), AIR = passa-alto.
COLORI = (('LP', 2), ('LP', 5), ('LP', 12), ('LP', 40), ('BP', 14), ('HP', 10))
N_AMP = 8

POS = (0, 60, 115, 98, 122)  # coefficienti dei 5 filtri ADPCM SPU, in 1/64
NEG = (0, 0, -52, -55, -60)
PICCO = 0x6000                # margine contro la saturazione


def clamp16(v):
    return max(-0x8000, min(0x7FFF, v))


def blocco(x, s1, s2, filtri=range(5)):
    """28 campioni -> (filtro, shift, nibble, stato finale): prova tutti e tiene l'errore minore."""
    best = None
    for f in filtri:
        for sh in range(13):
            passo = 1 << (12 - sh)
            a, b, nib, err = s1, s2, [], 0
            for v in x:
                pred = (a * POS[f] + b * NEG[f] + 32) >> 6
                n = max(-8, min(7, round((v - pred) / passo)))
                d = clamp16(n * passo + pred)
                err += (v - d) ** 2
                nib.append(n)
                a, b = d, a
            if best is None or err < best[0]:
                best = (err, f, sh, nib, a, b)
    return best[1:]


def codifica(x):
    """Campioni (2 cicli) -> lista di blocchi (filtro, shift, nibble).
    Il primo blocco del loop usa il filtro 0 (non guarda i campioni di prima): il loop si richiude
    da solo e si puo' saltare nel loop di un'altra onda senza click dallo stato ADPCM."""
    n = len(x) // 28
    blocchi, s1, s2 = [], 0, 0
    for i in range(n):
        f, sh, nib, s1, s2 = blocco(x[i * 28:(i + 1) * 28], s1, s2, (0,) if i == n // 2 else range(5))
        blocchi.append((f, sh, nib))
    return blocchi


def decodifica(blocchi, s1=0, s2=0):
    out = []
    for f, sh, nib in blocchi:
        for n in nib:
            d = clamp16(((n << 12) >> sh) + ((s1 * POS[f] + s2 * NEG[f] + 32) >> 6))
            out.append(d)
            s1, s2 = d, s1
    return out, s1, s2


def in_byte(blocchi, flag):
    """flag(i, n) -> byte dei flag del blocco i di n: 0x04 inizio loop, 0x01 fine, 0x02 ripeti."""
    out = bytearray()
    for i, (f, sh, nib) in enumerate(blocchi):
        out += bytes([sh | f << 4, flag(i, len(blocchi))])
        out += bytes((nib[k] & 15) | (nib[k + 1] & 15) << 4 for k in range(0, 28, 2))
    return out



def prova(nome, ciclo, blocchi):
    """Suona il loop 4 volte come la SPU: vicino all'onda ideale e uguale a ogni giro."""
    meta = len(blocchi) // 2
    out, s1, s2 = decodifica(blocchi[:meta])
    giri = []
    for _ in range(4):
        g, s1, s2 = decodifica(blocchi[meta:], s1, s2)
        giri.append(g)
    peggio = max(abs(a - b) for g in giri for a, b in zip(g, ciclo))
    assert peggio <= 0x1400, (nome, hex(peggio))  # fronti di quadra/impulso: uno shift per blocco
    assert giri[1] == giri[2] == giri[3], nome + ': il loop non si richiude'
    return peggio




def ciclo(amp):
    """Ampiezze delle armoniche -> un ciclo, che parte a meta' periodo (come nucleo)."""
    return [sum(a * math.sin(h * 2 * math.pi * (i + CICLO // 2) / CICLO) for h, a in enumerate(amp) if a)
            for i in range(CICLO)]


def loop_senza_inizio(i, n):  # solo fine + ripeti sull'ultimo blocco
    return 0x03 if i == n - 1 else 0


def onda(x, nome):
    x = [int(v) for v in x]
    b = codifica(x * 2)
    prova(nome, x, b)
    return in_byte(b, loop_senza_inizio)


MUTO = in_byte([(0, 0, [0] * 28)], lambda i, n: 0x07)  # fine + ripeti + inizio loop: su se stesso
strati = bytearray()
def colore(h, modo, fc):
    r = h / fc
    if modo == 'LP': return 1 / math.sqrt(1 + r * r)
    if modo == 'HP': return r * r / math.sqrt((1 - r * r) ** 2 + (r / 0.707) ** 2)
    return (r / 3) / math.sqrt((1 - r * r) ** 2 + (r / 3) ** 2)  # BP, Q 3


for modo, fc in COLORI:
    amp = [0] + [(2 / math.pi) * (-1) ** (h + 1) / h * colore(h, modo, fc) for h in range(1, ARM + 1)]
    x = ciclo(amp)
    m = max(abs(v) for v in x)
    strati += onda([v * PICCO / m for v in x], 'sega %s %d' % (modo, fc))
campane = bytearray()
for i in range(N_AMP):
    a = 0.9 * 32767 * i / (N_AMP - 1)
    campane += onda([a * math.sin(2 * math.pi * (j + CICLO // 2) / CICLO) for j in range(CICLO)], 'seno %d' % i)
blob = bytearray(MUTO)
offs = '#define OFF_MUTO 0\n#define OFF_STRATI %d\n' % len(blob)
blob += strati
offs += '#define OFF_CAMPANE %d\n#define ONDA_LEN 128\n' % len(blob)
blob += campane
blob += bytes(-len(blob) % 64)  # la DMA va a blocchi da 64 byte

(QUI / 'suoni.h').write_text(
    '// Generato da script/prepara.py: non modificare a mano.\n'
    'static const int16_t seno[256] = {\n' +
    ''.join('\t' + ','.join('%d' % round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(j, j + 16))
            + ',\n' for j in range(0, 256, 16)) + '};\n' +
    '#define NOTA0 %d // nota MIDI di inc_nota[0] (LA0)\n#define N_NOTE %d\n' % (NOTA0, N_NOTE) +
    'static const int32_t inc_nota[N_NOTE] = {\n' +
    ''.join('\t' + ','.join('%d' % v for v in inc[i:i + 12]) + ',\n' for i in range(0, N_NOTE, 12)) + '};\n' +
    'static const uint16_t svf_g[256] = {\n' +
    ''.join('\t' + ','.join('%d' % round(4096 * math.tan(math.pi * 30 * 2 ** (i / 32) / FS)) for i in range(j, j + 16))
            + ',\n' for j in range(0, 256, 16)) + '};\n' +
    '// PULSE: quanto ricade l\'inviluppo per blocco (in 1/4096), RATE 0-99 = da 30 ms a 3 s\n'
    'static const uint16_t dec_pulse[100] = {\n' +
    ''.join('\t' + ','.join('%d' % max(1, round(4096 * (1 - math.exp(-1 / (FS / 28 * 0.03 * 100 ** (r / 99))))))
                            for r in range(j, j + 10)) + ',\n' for j in range(0, 100, 10)) + '};\n' +
    'static const uint32_t inc_mod[100] = {\n' +
    ''.join('\t' + ','.join('%d' % round(0.005 * 4000 ** (r / 99) * 2 ** 32 / (FS / 28)) for r in range(j, j + 10))
            + ',\n' for j in range(0, 100, 10)) + '};\n' + offs +
    '#define SUONI_LEN %d\n' % len(blob) +
    'static const uint8_t suoni[SUONI_LEN] __attribute__((aligned(4))) = {\n' +
    ''.join('\t' + ','.join('0x%02x' % v for v in blob[i:i + 16]) + ',\n' for i in range(0, len(blob), 16)) + '};\n')
print('suoni.h: seno, %d note, svf_g, inc_mod, blob SPU %d byte' % (N_NOTE, len(blob)))


# --- curva.h: figura del pannello SHAPE (tappa d) ---
# Altezza 0-64 della risposta del filtro (dB da -36 a +18) per modo LP/BP/HP, 4 livelli di
# risonanza (RES 0, 33, 66, 99: k = 1/Q come nel motore) e 33 distanze dal cutoff a quarti
# d'ottava (indice 16 = sul cutoff). Il programma interpola tra i livelli di risonanza.
def risposta(modo, kq, r):
    den = math.sqrt((1 - r * r) ** 2 + (kq * r) ** 2)
    return (1, kq * r, r * r)[modo] / den


curva = [[[max(0, min(64, round((20 * math.log10(max(1e-6, risposta(m, kq, 2 ** ((i - 16) / 4)))) + 36) * 64 / 54)))
           for i in range(33)] for kq in (1.414, 0.96, 0.51, 0.05)] for m in range(3)]
(QUI / 'curva.h').write_text(
    '// Generato da script/prepara.py: non modificare a mano.\n'
    'static const uint8_t curva[3][4][33] = {\n' +
    ''.join('\t{ ' + ', '.join('{ ' + ','.join(map(str, row)) + ' }' for row in m) + ' },\n' for m in curva) + '};\n')
print('curva.h: 3 modi x 4 risonanze x 33 punti')


# --- font.h: font proprio a 4 bit con bordo nero (al posto del font di debug) ---
# Caratteri ASCII 32-126 in celle 16x16, 16 per riga, poi (riga 6) i simboli dei tasti ai codici
# 0x80-0x85: SU/GIU, SX/DX, X, QUADRATO, TRIANGOLO, CERCHIO. Texture 256x112 texel = 64x112 pixel
# di VRAM. Indice di un carattere: codice - 32 (ASCII) oppure codice - 0x80 + 96 (simboli).
# CLUT: 0 = trasparente, 1 = nero (bordo), 2 = bianco (il colore lo da' il poligono).
# Tratti larghi 2 pixel. DIM 10: maiuscole alte 8 pixel + bordo = 10 (righe di testo da 9 pixel).
# Cifre larghe uguali: i valori non "ballano" quando cambiano. # e * disegnati a mano (tappa d):
# col font a 10 pixel erano una macchia e un triangolino.
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

DIM = 10
FONT = ImageFont.load_default(DIM)  # Aileron incluso in Pillow >= 10.1: uguale su ogni PC
UGUALI = '0123456789'


def simbolo(d, s):
    """Simboli dei tasti nel riquadro x 1-9, y 2-9 (come le maiuscole), tratti di 2 pixel."""
    if s == 0:    # su/giu': freccia doppia verticale
        d.rectangle((4, 1, 5, 10), fill=255)
        d.polygon([(4.5, 0), (1.5, 3), (7.5, 3)], fill=255)
        d.polygon([(4.5, 11), (1.5, 8), (7.5, 8)], fill=255)
    elif s == 1:  # sinistra/destra: freccia doppia orizzontale
        d.rectangle((1, 5, 12, 6), fill=255)
        d.polygon([(0, 5.5), (3, 2.5), (3, 8.5)], fill=255)
        d.polygon([(13, 5.5), (10, 2.5), (10, 8.5)], fill=255)
    elif s == 2:  # croce
        d.line((2, 2, 9, 9), fill=255, width=2)
        d.line((2, 9, 9, 2), fill=255, width=2)
    elif s == 3:  # quadrato
        d.rectangle((2, 2, 9, 9), outline=255, width=2)
    elif s == 4:  # triangolo
        d.line((1, 9, 5.5, 2, 10, 9, 1, 9), fill=255, width=2)
    else:         # cerchio
        d.ellipse((1, 1, 10, 10), outline=255, width=2)


tex = Image.new('L', (256, 112))  # indici CLUT 0-2
larg = []
for i in range(96 + 6):
    cella = Image.new('L', (16, 16))
    d = ImageDraw.Draw(cella)
    if chr(32 + i) == '#':  # due barre verticali e due orizzontali di 2 pixel, buchi di 2
        for r in ((3, 2, 4, 9), (7, 2, 8, 9), (1, 3, 10, 4), (1, 7, 10, 8)):
            d.rectangle(r, fill=255)
        pieno = cella
    elif chr(32 + i) == '*':  # asterisco: barra verticale e due diagonali, piu' in alto
        d.rectangle((4, 1, 5, 7), fill=255)
        d.line((1, 2, 8, 6), fill=255, width=2)
        d.line((1, 6, 8, 2), fill=255, width=2)
        pieno = cella.point(lambda v: 255 if v >= 110 else 0)
    elif i < 95:
        d.text((1, 0), chr(32 + i), fill=255, font=FONT)
        pieno = cella.point(lambda v: 255 if v >= 110 else 0)
        pieno = ImageChops.lighter(pieno, ImageChops.offset(pieno, 1, 0))  # grassetto: tratti di 2 pixel
        if chr(32 + i) == 'Q':  # coda ben visibile: a questa misura la Q sembrava una O ("SEQ" letto "SEO")
            x0, y0, x1, y1 = pieno.getbbox()
            ImageDraw.Draw(pieno).line((x1 - 4, y1 - 3, x1, y1 + 1), fill=255, width=2)
    elif i == 95:  # cella vuota (codice 127)
        pieno = cella
    else:
        simbolo(d, i - 96)
        pieno = cella.point(lambda v: 255 if v >= 110 else 0)
    bordo = pieno.filter(ImageFilter.MaxFilter(3))  # 1 pixel di nero tutto attorno
    box = bordo.getbbox()
    larg.append(min(box[2] - 1, 15) if box else 4)  # avanzamento: i bordi vicini si sovrappongono
    idx = Image.eval(bordo, lambda v: 1 if v else 0)
    idx.paste(2, mask=pieno)
    tex.paste(idx, ((i % 16) * 16, (i // 16) * 16))
w = max(larg[ord(c) - 32] for c in UGUALI)
for c in UGUALI:
    larg[ord(c) - 32] = w

px = list(tex.getdata())
parole = [px[k] | px[k + 1] << 4 | px[k + 2] << 8 | px[k + 3] << 12 for k in range(0, len(px), 4)]
clut = [0x0000, 0x0421, 0x7fff] + [0] * 13  # 0x0421 = quasi nero opaco (0x0000 e' trasparente)

(QUI / 'font.h').write_text(
    '// Generato da script/prepara.py: non modificare a mano.\n'
    'static const uint16_t font_tex[64 * 112] = {\n' +
    ',\n'.join(','.join('0x%x' % v for v in parole[r:r + 32]) for r in range(0, len(parole), 32)) +
    '\n};\nstatic const uint16_t font_clut[16] = {' + ','.join('0x%x' % v for v in clut) + '};\n'
    'static const uint8_t font_w[%d] = {' % len(larg) + ','.join(map(str, larg)) + '};\n')
print('font.h: %d caratteri, larghezze' % len(larg), min(larg), '-', max(larg))


# --- testi.h: testi del menu GUIDE da script/testi.txt (tappa e), misurati col font qui sopra ---
SIMBOLI = {'{UD}': '\x80', '{LR}': '\x81', '{X}': '\x82', '{Q}': '\x83', '{T}': '\x84', '{C}': '\x85'}
LARGO_TESTO, LARGO_VOCE, RIGHE = 186, 98, 9


def largo(s):
    return sum(larg[ord(c) - 32 if ord(c) < 0x80 else ord(c) - 0x80 + 96] for c in s)


# [VOCE] = pagina del menu; [[NOME]] = pagina speciale (CONFERMA, ATTIVO) o, per [[TUTORIAL]], i
# passi del tutorial: separati da ~, righe = (cosa fare) e + (cosa si sente), al massimo 2 + 2 da
# LARGO_PASSO pixel (in testata, dopo il numero del passo).
LARGO_PASSO = 248
voci, pagine, speciali, passi = [], [], {}, []
sezione = None
for n, riga in enumerate((QUI / 'script' / 'testi.txt').read_text(encoding='utf-8').splitlines(), 1):
    if not riga.strip() or riga.startswith('#'):
        continue
    for k, v in SIMBOLI.items():
        riga = riga.replace(k, v)
    if riga.startswith('[['):
        sezione = riga.strip('[] ')
        if sezione == 'TUTORIAL': passi.append(([], []))
        else:                     speciali[sezione] = []
        continue
    if riga.startswith('['):
        sezione = None
        voci.append(riga.strip('[] '))
        pagine.append([])
        assert largo(voci[-1]) <= LARGO_VOCE, 'testi.txt riga %d: voce troppo lunga' % n
        continue
    if sezione == 'TUTORIAL':
        if riga.startswith('~'):
            passi.append(([], []))
            continue
        assert riga[0] in '=+', 'testi.txt riga %d: nel tutorial le righe iniziano con = o +' % n
        r = riga[1:].strip()
        w = largo(r)
        assert w <= LARGO_PASSO, 'testi.txt riga %d: %d pixel (massimo %d): %s' % (n, w, LARGO_PASSO, r)
        passi[-1]['=+'.index(riga[0])].append(r)
        assert len(passi[-1][0]) <= 2 and len(passi[-1][1]) <= 2, 'testi.txt riga %d: al massimo 2 righe' % n
        continue
    w = largo(riga.lstrip('> '))
    assert w <= LARGO_TESTO, 'testi.txt riga %d: %d pixel (massimo %d): %s' % (n, w, LARGO_TESTO, riga)
    pag = speciali[sezione] if sezione else pagine[-1]
    pag.append(riga)
    assert len(pag) <= RIGHE, 'testi.txt riga %d: troppe righe nella pagina' % n


def c_str(s):
    return '"' + ''.join('\\x%02x""' % ord(c) if ord(c) >= 0x80 else c.replace('"', '\\"') for c in s) + '"'


(QUI / 'testi.h').write_text(
    '// Generato da script/prepara.py da script/testi.txt: correggere li\', non qui.\n'
    '#define N_GUIDA %d\n' % len(voci) +
    ''.join('#define G_%s %d\n' % (v.replace(' ', '_'), i) for i, v in enumerate(voci)) +
    'static const char *const guida_voce[N_GUIDA] = { %s };\n' % ', '.join(c_str(v) for v in voci) +
    'static const char *const guida_testo[N_GUIDA] = {\n' +
    ''.join('\t' + ' "\\n"\n\t'.join(c_str(r) for r in p) + ',\n' for p in pagine) + '};\n' +
    ''.join('static const char tut_%s[] = %s;\n' % (k.lower(), ' "\\n" '.join(c_str(r) for r in v))
            for k, v in speciali.items()) +
    '#define N_PASSI %d\n' % len(passi) +
    'static const char *const passo_fai[N_PASSI] = {\n' +
    ''.join('\t%s,\n' % ' "\\n" '.join(c_str(r) for r in f) for f, _ in passi) + '};\n' +
    'static const char *const passo_senti[N_PASSI] = {\n' +
    ''.join('\t%s,\n' % (' "\\n" '.join(c_str(r) for r in h) or '""') for _, h in passi) + '};\n')
print('testi.h: %d voci, %d passi del tutorial, riga piu\' larga %d pixel'
      % (len(voci), len(passi), max(largo(r.lstrip('> ')) for p in pagine for r in p)))



# --- card.h: intestazione del file sulla memory card (tappa g) ---
# Blocco 0 (128 byte): "SC", icona di 1 fotogramma (0x11), 1 blocco, titolo in Shift-JIS a tutta
# larghezza (64 byte), riservati, palette dell'icona (16 colori a 15 bit). Blocco 1: l'icona 16x16
# a 4 bit (un fulmine sul mare). Poi il programma aggiunge 8 settori da 128 byte, uno per patch.
def sjis(t):
    out = bytearray()
    for ch in t:
        if ch == ' ':
            out += b'\x81\x40'
        elif ch.isdigit():
            out += bytes((0x82, 0x4F + ord(ch) - ord('0')))
        else:
            out += bytes((0x82, 0x60 + ord(ch) - ord('A')))
    return out


titolo = sjis('STORM DRONE PATCHES')
testa = bytearray(b'SC\x11\x01') + titolo + bytes(64 - len(titolo)) + bytes(28)
assert len(testa) == 96
icona_pal = [(0, 0, 0), (4, 8, 20), (10, 30, 60), (20, 60, 90), (240, 240, 255), (200, 200, 120), (60, 70, 90)]
icona_pal += [(0, 0, 0)] * (16 - len(icona_pal))
for r, g, b in icona_pal:  # 15 bit BGR, 0x0000 = trasparente: il nero e' 0x8000
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    testa += (v or 0x8000).to_bytes(2, 'little')
assert len(testa) == 128
pix = Image.new('L', (16, 16), 1)                          # cielo notte
dd = ImageDraw.Draw(pix)
dd.rectangle((0, 10, 15, 15), fill=2)                       # mare
dd.line((0, 11, 15, 11), fill=3)                            # cresta
dd.rectangle((0, 3, 9, 5), fill=6)                          # nuvola
dd.line((8, 4, 5, 8, 8, 8, 5, 13), fill=4, width=1)         # fulmine
ico = list(pix.getdata())
testa += bytes(ico[k] | ico[k + 1] << 4 for k in range(0, 256, 2))
assert len(testa) == 256
(QUI / 'card.h').write_text(
    '// Generato da script/prepara.py: non modificare a mano.\n'
    '// Intestazione (titolo + palette) e icona del file delle patch sulla memory card.\n'
    'static const uint8_t card_testa[256] = {\n' +
    ''.join('\t' + ','.join('0x%02x' % v for v in testa[i:i + 16]) + ',\n' for i in range(0, 256, 16)) + '};\n')
print('card.h: intestazione e icona del file delle patch')
