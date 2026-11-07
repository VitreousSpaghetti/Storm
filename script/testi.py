"""Scrive src/testi.h (testi della GUIDE e del TUTORIAL) da script/testi.txt. Solo libreria standard.

Uso: python script/testi.py   (dalla cartella del progetto o da qualsiasi altra)
src/testi.h sta nel repository: per compilare non serve Python, solo per cambiare i testi.
Le righe si misurano col font di originale_psx/font.h (font_w): una riga troppo lunga ferma lo
script con il numero di riga. Le regole del file sono scritte in cima a script/testi.txt.
"""
import pathlib
import re
import sys

RADICE = pathlib.Path(__file__).resolve().parent.parent
SIMBOLI = {'{UD}': '\x80', '{LR}': '\x81', '{X}': '\x82', '{Q}': '\x83', '{T}': '\x84', '{C}': '\x85'}
LARGO_TESTO, LARGO_VOCE, LARGO_PASSO, RIGHE = 186, 98, 248, 9
LARGO_AIUTO, RIGHE_AIUTO = 280, 5  # [[AIUTO]]: riquadro in basso dello schermo principale
DISP = ('PAD', 'TASTIERA', 'MOUSE', 'MIDI')  # stesso ordine di D_PAD.. in src/controlli.h
RIPIEGO = {'PAD': ('PAD',), 'TASTIERA': ('TASTIERA', 'PAD'), 'MOUSE': ('MOUSE', 'PAD'), 'MIDI': ('MIDI', 'MOUSE', 'PAD')}

font = (RADICE / 'originale_psx' / 'font.h').read_text(encoding='utf-8')
larg = [int(v) for v in re.search(r'font_w\[\d+\] = \{([^}]*)\}', font).group(1).split(',')]


def largo(s):
    return sum(larg[ord(c) - 32 if ord(c) < 0x80 else ord(c) - 0x80 + 96] for c in s)


def errore(n, msg):
    sys.exit('script/testi.txt riga %d: %s' % (n, msg))


class Pagina:  # righe per tutti, poi quelle di ogni dispositivo (@PAD, @TASTIERA, @MOUSE, @MIDI)
    def __init__(self):
        self.tutti, self.var, self.ora, self.senti = [], {}, None, []

    def aggiungi(self, riga, n):
        (self.var[self.ora] if self.ora else self.tutti).append((riga, n))

    def per(self, d):
        for v in RIPIEGO[d]:
            if v in self.var:
                return self.tutti + self.var[v]
        return self.tutti


voci, pagine, speciali, passi = [], [], {}, []
pag = None
for n, riga in enumerate((RADICE / 'script' / 'testi.txt').read_text(encoding='utf-8').splitlines(), 1):
    if not riga.strip() or riga.startswith('#'):
        continue
    for k, v in SIMBOLI.items():
        riga = riga.replace(k, v)
    if riga.startswith('[['):
        nome = riga.strip('[] ')
        pag = Pagina()
        if nome == 'TUTORIAL':
            passi.append(pag)
        else:
            speciali[nome] = pag
        tutorial, aiuto = nome == 'TUTORIAL', nome == 'AIUTO'
        continue
    if riga.startswith('['):
        voci.append(riga.strip('[] '))
        pag = Pagina()
        pagine.append(pag)
        tutorial = aiuto = False
        if largo(voci[-1]) > LARGO_VOCE:
            errore(n, 'voce troppo lunga')
        continue
    if pag is None:
        errore(n, 'riga fuori da una pagina')
    if riga.startswith('@'):
        if riga.strip()[1:] not in DISP:
            errore(n, 'dispositivo sconosciuto (PAD, TASTIERA, MOUSE, MIDI)')
        pag.ora = riga.strip()[1:]
        if pag.ora in pag.var:
            errore(n, 'dispositivo gia\' scritto in questa pagina')
        pag.var[pag.ora] = []
        continue
    if tutorial:
        if riga.startswith('~'):
            pag = Pagina()
            passi.append(pag)
            continue
        if riga[0] not in '=+':
            errore(n, 'nel tutorial le righe iniziano con = o +')
        r = riga[1:].strip()
        if largo(r) > LARGO_PASSO:
            errore(n, '%d pixel (massimo %d): %s' % (largo(r), LARGO_PASSO, r))
        if riga[0] == '+':
            if pag.ora:
                errore(n, 'le righe + (cosa si sente) valgono per tutti: prima delle righe @')
            pag.senti.append(r)
        else:
            pag.aggiungi(r, n)
        continue
    lmax = LARGO_AIUTO if aiuto else LARGO_TESTO
    if largo(riga.lstrip('> ')) > lmax:
        errore(n, '%d pixel (massimo %d): %s' % (largo(riga.lstrip('> ')), lmax, riga))
    pag.aggiungi(riga, n)

# righe di ogni dispositivo, coi limiti di righe
for p in pagine + list(speciali.values()):
    for d in DISP:
        m = RIGHE_AIUTO if p is speciali.get('AIUTO') else RIGHE
        if len(p.per(d)) > m:
            errore(p.per(d)[m][1], 'troppe righe nella pagina (%s)' % d)
for p in passi:
    if len(p.senti) > 2:
        errore(0, 'al massimo 2 righe + in un passo')
    for d in DISP:
        if len(p.per(d)) > 2:
            errore(p.per(d)[2][1], 'al massimo 2 righe = in un passo (%s)' % d)


def c_str(s):
    return '"' + ''.join('\\x%02x""' % ord(c) if ord(c) >= 0x80 else c.replace('"', '\\"') for c in s) + '"'


A_CAPO = ' "\\n"'  # tra due righe nel .h


def righe(r, sep):
    return sep.join(c_str(x) for x, _ in r) if r else '""'


def per_disp(f):
    return ''.join('\t{ // %s\n%s\t},\n' % (d, f(d)) for d in DISP)


(RADICE / 'src' / 'testi.h').write_text(
    '// Generato da script/testi.py da script/testi.txt: correggere li\', non qui.\n'
    '// Testi per dispositivo nell\'ordine D_PAD, D_TASTIERA, D_MOUSE, D_MIDI (controlli.h).\n'
    '#define N_GUIDA %d\n' % len(voci) +
    ''.join('#define G_%s %d\n' % (v.replace(' ', '_'), i) for i, v in enumerate(voci)) +
    'static const char *const guida_voce[N_GUIDA] = { %s };\n' % ', '.join(c_str(v) for v in voci) +
    'static const char *const guida_testo[4][N_GUIDA] = {\n' +
    per_disp(lambda d: ''.join('\t\t' + righe(p.per(d), A_CAPO + '\n\t\t') + ',\n' for p in pagine)) + '};\n' +
    ''.join('static const char *const tut_%s[4] = {\n%s};\n' % (k.lower(), ''.join('\t%s,\n' % righe(v.per(d), A_CAPO + ' ') for d in DISP))
            for k, v in speciali.items()) +
    '#define N_PASSI %d\n' % len(passi) +
    'static const char *const passo_fai[4][N_PASSI] = {\n' +
    per_disp(lambda d: ''.join('\t\t%s,\n' % righe(p.per(d), A_CAPO + ' ') for p in passi)) + '};\n' +
    'static const char *const passo_senti[N_PASSI] = {\n' +
    ''.join('\t%s,\n' % ((A_CAPO + ' ').join(c_str(r) for r in p.senti) or '""') for p in passi) + '};\n',
    encoding='utf-8', newline='\n')
print('src/testi.h: %d voci, %d passi del tutorial' % (len(voci), len(passi)))
