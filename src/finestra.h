/*
 * Finestra del plugin dentro la finestra della DAW: una per sistema (finestra_win.c, finestra_mac.m,
 * finestra_x11.c), scelta da CMakeLists.txt. Mostra lo schermo 320x256 ingrandito senza sfumare e
 * raccoglie il mouse in coordinate del disegno (fuori = negative o oltre). Tutto nel thread
 * principale della DAW.
 */
#ifndef FINESTRA_H
#define FINESTRA_H
#include <stdint.h>

typedef struct Finestra Finestra;

const char *finestra_api(void); // "win32", "cocoa" o "x11", come le chiama CLAP
Finestra   *finestra_apri(uintptr_t genitore, int w, int h); // NULL se non si puo'
void        finestra_chiudi(Finestra *f);
void        finestra_misura(Finestra *f, int w, int h);
void        finestra_disegna(Finestra *f, const uint8_t rgb[256][320][3]);
// Un evento del mouse alla volta (MOUSE_* di controlli.h), 0 quando sono finiti
int         finestra_mouse(Finestra *f, int *ev, int *x, int *y, int *n, uint32_t *ms);
#endif
