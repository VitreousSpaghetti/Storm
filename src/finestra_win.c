/*
 * Finestra del plugin su Windows: una finestra figlia di quella della DAW, disegnata con
 * StretchDIBits (punti pieni, niente sfumatura). Durante il trascinamento il mouse e' catturato:
 * le coordinate continuano anche fuori dalla finestra, come nel programma.
 */
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include "finestra.h"
#include "controlli.h"

#define W    320
#define H    256
#define N_EV 256

typedef struct { int ev, x, y, n; uint32_t ms; } Ev;
struct Finestra {
	HWND     h;
	uint32_t pix[H][W]; // BGRX
	Ev       ev[N_EV];
	int      t, f;      // eventi scritti e letti (stesso thread)
	int      giu, dentro, rot;
};

static const wchar_t classe[] = L"storm_finestra";
static HINSTANCE     modulo;
static int           aperte;

const char *finestra_api(void) { return "win32"; }

static void metti(Finestra *f, int ev, int x, int y, int n)
{
	if (f->t - f->f >= N_EV)
		return; // nessuno legge (la DAW non chiama il timer): si perde
	f->ev[f->t++ % N_EV] = (Ev) { ev, x, y, n, (uint32_t) GetMessageTime() };
}

// pixel della finestra -> pixel del disegno, arrotondando verso il basso anche sotto lo zero
static int scala(int v, int lato, int tot) { return v >= 0 ? v * lato / tot : -((-v * lato + tot - 1) / tot); }

static void punto(Finestra *f, int px, int py, int ev, int n)
{
	RECT r;
	GetClientRect(f->h, &r);
	int w = r.right > 0 ? r.right : 1, h = r.bottom > 0 ? r.bottom : 1;
	metti(f, ev, scala(px, W, w), scala(py, H, h), n);
}

static void dipingi(Finestra *f, HDC dc)
{
	RECT r;
	GetClientRect(f->h, &r);
	BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB, 0, 0, 0, 0, 0 }, { { 0, 0, 0, 0 } } };
	SetStretchBltMode(dc, COLORONCOLOR); // punti pieni
	StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, W, H, f->pix, &bi, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	if (m == WM_NCCREATE)
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) ((CREATESTRUCTW *) lp)->lpCreateParams);
	Finestra *f = (Finestra *) GetWindowLongPtrW(h, GWLP_USERDATA);
	if (!f)
		return DefWindowProcW(h, m, wp, lp);
	switch (m) {
	case WM_MOUSEMOVE:
		if (!f->dentro) { // per sapere quando esce
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
			TrackMouseEvent(&t);
			f->dentro = 1;
		}
		punto(f, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), MOUSE_MUOVI, 0);
		return 0;
	case WM_MOUSELEAVE:
		f->dentro = 0;
		if (!f->giu) // col tasto giu' si trascina ancora
			metti(f, MOUSE_MUOVI, -1, -1, 0);
		return 0;
	case WM_LBUTTONDOWN:
		SetCapture(h);
		f->giu = 1;
		punto(f, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), MOUSE_GIU, 0);
		return 0;
	case WM_LBUTTONUP:
		if (f->giu) {
			f->giu = 0;
			punto(f, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), MOUSE_SU, 0);
		}
		ReleaseCapture();
		return 0;
	case WM_CAPTURECHANGED: // la cattura persa (Alt+Tab...): il tasto si lascia
		if (f->giu && (HWND) lp != h) {
			f->giu = 0;
			metti(f, MOUSE_SU, -1, -1, 0);
		}
		return 0;
	case WM_RBUTTONDOWN:
		punto(f, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), MOUSE_DESTRO, 0);
		return 0;
	case WM_MOUSEWHEEL: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; // qui in coordinate dello schermo
		ScreenToClient(h, &p);
		f->rot += GET_WHEEL_DELTA_WPARAM(wp);
		int n = f->rot / WHEEL_DELTA; // scatti interi (un trackpad ne manda pezzi)
		f->rot -= n * WHEEL_DELTA;
		if (n)
			punto(f, p.x, p.y, MOUSE_ROTELLA, n);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC         dc = BeginPaint(h, &ps);
		dipingi(f, dc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_PRINTCLIENT: // PrintWindow: anche da nascosta
		dipingi(f, (HDC) wp);
		return 0;
	}
	return DefWindowProcW(h, m, wp, lp);
}

Finestra *finestra_apri(uintptr_t genitore, int w, int h)
{
	// la classe e' di questo modulo: ogni copia del plugin ha la sua
	if (!aperte++) {
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                   (LPCWSTR) (void *) proc, &modulo);
		WNDCLASSEXW c = { sizeof c, 0, proc, 0, 0, modulo, NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, classe, NULL };
		RegisterClassExW(&c);
	}
	Finestra *f = calloc(1, sizeof *f);
	if (f)
		f->h = CreateWindowExW(0, classe, L"storm", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, w, h,
		                       (HWND) genitore, NULL, modulo, f);
	if (!f || !f->h) {
		free(f);
		if (!--aperte)
			UnregisterClassW(classe, modulo);
		return NULL;
	}
	return f;
}

void finestra_chiudi(Finestra *f)
{
	if (!f)
		return;
	DestroyWindow(f->h);
	free(f);
	if (!--aperte) // prima che il modulo si scarichi: la classe punterebbe a codice che non c'e' piu'
		UnregisterClassW(classe, modulo);
}

void finestra_misura(Finestra *f, int w, int h) { SetWindowPos(f->h, NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE); }

void finestra_disegna(Finestra *f, const uint8_t rgb[256][320][3])
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			f->pix[y][x] = (uint32_t) rgb[y][x][0] << 16 | (uint32_t) rgb[y][x][1] << 8 | rgb[y][x][2];
	InvalidateRect(f->h, NULL, FALSE);
}

int finestra_mouse(Finestra *f, int *ev, int *x, int *y, int *n, uint32_t *ms)
{
	if (f->f == f->t)
		return 0;
	Ev e = f->ev[f->f++ % N_EV];
	*ev = e.ev, *x = e.x, *y = e.y, *n = e.n, *ms = e.ms;
	return 1;
}
