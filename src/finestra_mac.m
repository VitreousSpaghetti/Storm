/*
 * Finestra del plugin su macOS: una NSView figlia di quella della DAW; il disegno e' il contenuto del
 * suo layer (punti pieni). Il mouse si legge con un monitor locale degli eventi, senza classi nuove:
 * le copie del plugin (una per istanza) definirebbero tutte la stessa classe Objective-C, che nel
 * processo e' una sola. Senza ARC (retain/release a mano).
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>
#include "finestra.h"
#include "controlli.h"

#define W    320
#define H    256
#define N_EV 256

typedef struct { int ev, x, y, n; uint32_t ms; } Ev;
struct Finestra {
	NSView *v;
	id      monitor;
	Ev      ev[N_EV];
	int     t, f;
	int     giu, dentro;
	double  rot;
};

const char *finestra_api(void) { return "cocoa"; }

static void metti(Finestra *f, int ev, int x, int y, int n, NSEvent *e)
{
	if (f->t - f->f >= N_EV)
		return;
	f->ev[f->t++ % N_EV] = (Ev) { ev, x, y, n, (uint32_t) ([e timestamp] * 1000) };
}

static void evento(Finestra *f, NSEvent *e)
{
	if ([e window] != [f->v window])
		return;
	NSPoint p  = [f->v convertPoint:[e locationInWindow] fromView:nil];
	NSRect  b  = [f->v bounds];
	double  bw = b.size.width > 0 ? b.size.width : 1, bh = b.size.height > 0 ? b.size.height : 1;
	int     x = (int) floor(p.x * W / bw), y = (int) floor((bh - p.y) * H / bh); // la vista ha y in su
	int     dentro = x >= 0 && y >= 0 && x < W && y < H;
	switch ([e type]) {
	case NSEventTypeLeftMouseDown:
		if (dentro) {
			f->giu = 1;
			metti(f, MOUSE_GIU, x, y, 0, e);
		}
		break;
	case NSEventTypeLeftMouseUp:
		if (f->giu) {
			f->giu = 0;
			metti(f, MOUSE_SU, x, y, 0, e);
		}
		break;
	case NSEventTypeLeftMouseDragged: // anche fuori dalla vista, finche' il tasto e' giu'
		if (f->giu)
			metti(f, MOUSE_MUOVI, x, y, 0, e);
		break;
	case NSEventTypeMouseMoved:
		if (dentro)
			metti(f, MOUSE_MUOVI, x, y, 0, e);
		else if (f->dentro)
			metti(f, MOUSE_MUOVI, -1, -1, 0, e);
		f->dentro = dentro;
		break;
	case NSEventTypeRightMouseDown:
		if (dentro)
			metti(f, MOUSE_DESTRO, x, y, 0, e);
		break;
	case NSEventTypeScrollWheel:
		if (dentro) {
			double d = [e scrollingDeltaY] / ([e hasPreciseScrollingDeltas] ? 10.0 : 1.0);
			if ([e isDirectionInvertedFromDevice]) // "scorrimento naturale": il verso della mano
				d = -d;
			f->rot += d;
			int n = (int) f->rot;
			f->rot -= n;
			if (n)
				metti(f, MOUSE_ROTELLA, x, y, n, e);
		}
		break;
	default:
		break;
	}
}

Finestra *finestra_apri(uintptr_t genitore, int w, int h)
{
	Finestra *f = calloc(1, sizeof *f);
	if (!f)
		return NULL;
	NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
	[v setWantsLayer:YES];
	[[v layer] setMagnificationFilter:kCAFilterNearest];
	[[v layer] setBackgroundColor:CGColorGetConstantColor(kCGColorBlack)];
	[(NSView *) genitore addSubview:v];
	f->v = v;
	NSEventMask m = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged | NSEventMaskMouseMoved |
	                NSEventMaskRightMouseDown | NSEventMaskScrollWheel;
	f->monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:m
	                                                     handler:^NSEvent *(NSEvent *e) {
		                                                     evento(f, e);
		                                                     return e;
	                                                     }] retain];
	return f;
}

void finestra_chiudi(Finestra *f)
{
	if (!f)
		return;
	[NSEvent removeMonitor:f->monitor];
	[f->monitor release];
	[f->v removeFromSuperview];
	[f->v release];
	free(f);
}

void finestra_misura(Finestra *f, int w, int h) { [f->v setFrameSize:NSMakeSize(w, h)]; }

void finestra_disegna(Finestra *f, const uint8_t rgb[256][320][3])
{
	NSWindow *fin = [f->v window];
	if (fin && ![fin acceptsMouseMovedEvents]) // per l'aiuto sotto il puntatore
		[fin setAcceptsMouseMovedEvents:YES];
	CFDataRef         d  = CFDataCreate(NULL, &rgb[0][0][0], W * H * 3);
	CGDataProviderRef dp = CGDataProviderCreateWithCFData(d);
	CGColorSpaceRef   cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGImageRef        im = CGImageCreate(W, H, 8, 24, W * 3, cs, kCGBitmapByteOrderDefault | kCGImageAlphaNone, dp, NULL, false,
	                                     kCGRenderingIntentDefault);
	[CATransaction begin];
	[CATransaction setDisableActions:YES]; // niente dissolvenza tra un fotogramma e l'altro
	[[f->v layer] setContents:(id) im];
	[CATransaction commit];
	CGImageRelease(im);
	CGColorSpaceRelease(cs);
	CGDataProviderRelease(dp);
	CFRelease(d);
}

int finestra_mouse(Finestra *f, int *ev, int *x, int *y, int *n, uint32_t *ms)
{
	if (f->f == f->t)
		return 0;
	Ev e = f->ev[f->f++ % N_EV];
	*ev = e.ev, *x = e.x, *y = e.y, *n = e.n, *ms = e.ms;
	return 1;
}
