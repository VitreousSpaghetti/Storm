/*
 * Finestra del plugin su Linux (X11, come le DAW): una finestra figlia di quella della DAW su una
 * connessione propria, disegnata con XPutImage dopo l'ingrandimento fatto qui (punti pieni). Col
 * tasto giu' X11 manda il movimento anche fuori dalla finestra. Supporta lo schermo a 24 bit.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include "finestra.h"
#include "controlli.h"

#define W    320
#define H    256
#define N_EV 256

typedef struct { int ev, x, y, n; uint32_t ms; } Ev;
struct Finestra {
	Display *d;
	Window   w;
	GC       gc;
	XImage  *im;        // grande come la finestra (si rifa' quando cambia misura)
	int      lw, lh;
	uint8_t  ult[H][W][3]; // ultimo fotogramma, per Expose e cambi di misura
	Ev       ev[N_EV];
	int      t, f, giu;
};

const char *finestra_api(void) { return "x11"; }

static void metti(Finestra *f, int ev, int x, int y, int n, Time ms)
{
	if (f->t - f->f >= N_EV)
		return;
	f->ev[f->t++ % N_EV] = (Ev) { ev, x, y, n, (uint32_t) ms };
}

static int scala(int v, int lato, int tot) { return v >= 0 ? v * lato / tot : -((-v * lato + tot - 1) / tot); }

Finestra *finestra_apri(uintptr_t genitore, int w, int h)
{
	Finestra *f = calloc(1, sizeof *f);
	if (!f || !(f->d = XOpenDisplay(NULL))) {
		free(f);
		return NULL;
	}
	f->w = XCreateSimpleWindow(f->d, (Window) genitore, 0, 0, (unsigned) w, (unsigned) h, 0, 0, 0);
	XSelectInput(f->d, f->w, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask);
	f->gc = XCreateGC(f->d, f->w, 0, NULL);
	f->lw = w, f->lh = h;
	XMapWindow(f->d, f->w);
	XFlush(f->d);
	return f;
}

void finestra_chiudi(Finestra *f)
{
	if (!f)
		return;
	if (f->im)
		XDestroyImage(f->im); // libera anche i pixel
	XFreeGC(f->d, f->gc);
	XDestroyWindow(f->d, f->w);
	XCloseDisplay(f->d);
	free(f);
}

static void mostra(Finestra *f)
{
	if (!f->im || f->im->width != f->lw || f->im->height != f->lh) {
		if (f->im)
			XDestroyImage(f->im);
		char *pix = malloc((size_t) f->lw * (size_t) f->lh * 4);
		int   s   = DefaultScreen(f->d);
		f->im     = pix ? XCreateImage(f->d, DefaultVisual(f->d, s), 24, ZPixmap, 0, pix, (unsigned) f->lw, (unsigned) f->lh, 32, 0) : NULL;
		if (!f->im) {
			free(pix);
			return;
		}
	}
	for (int y = 0; y < f->lh; y++) {
		const uint8_t(*r)[3] = f->ult[y * H / f->lh];
		uint32_t *o          = (uint32_t *) (f->im->data + (size_t) y * (size_t) f->im->bytes_per_line);
		for (int x = 0; x < f->lw; x++) {
			const uint8_t *c = r[x * W / f->lw];
			o[x]             = (uint32_t) c[0] << 16 | (uint32_t) c[1] << 8 | c[2];
		}
	}
	XPutImage(f->d, f->w, f->gc, f->im, 0, 0, 0, 0, (unsigned) f->lw, (unsigned) f->lh);
	XFlush(f->d);
}

void finestra_misura(Finestra *f, int w, int h)
{
	f->lw = w, f->lh = h;
	XResizeWindow(f->d, f->w, (unsigned) w, (unsigned) h);
	mostra(f);
}

void finestra_disegna(Finestra *f, const uint8_t rgb[256][320][3])
{
	memcpy(f->ult, rgb, sizeof f->ult);
	mostra(f);
}

int finestra_mouse(Finestra *f, int *ev, int *x, int *y, int *n, uint32_t *ms)
{
	while (XPending(f->d)) { // la connessione e' nostra: gli eventi si leggono qui
		XEvent e;
		XNextEvent(f->d, &e);
		int px = scala(e.xbutton.x, W, f->lw), py = scala(e.xbutton.y, H, f->lh);
		if (e.type == Expose)
			mostra(f);
		else if (e.type == ButtonPress) {
			unsigned b = e.xbutton.button;
			if (b == Button1)
				f->giu = 1, metti(f, MOUSE_GIU, px, py, 0, e.xbutton.time);
			else if (b == Button3)
				metti(f, MOUSE_DESTRO, px, py, 0, e.xbutton.time);
			else if (b == Button4 || b == Button5)
				metti(f, MOUSE_ROTELLA, px, py, b == Button4 ? 1 : -1, e.xbutton.time);
		} else if (e.type == ButtonRelease && e.xbutton.button == Button1 && f->giu)
			f->giu = 0, metti(f, MOUSE_SU, px, py, 0, e.xbutton.time);
		else if (e.type == MotionNotify)
			metti(f, MOUSE_MUOVI, scala(e.xmotion.x, W, f->lw), scala(e.xmotion.y, H, f->lh), 0, e.xmotion.time);
		else if (e.type == LeaveNotify && !f->giu)
			metti(f, MOUSE_MUOVI, -1, -1, 0, e.xcrossing.time);
	}
	if (f->f == f->t)
		return 0;
	Ev e = f->ev[f->f++ % N_EV];
	*ev = e.ev, *x = e.x, *y = e.y, *n = e.n, *ms = e.ms;
	return 1;
}
