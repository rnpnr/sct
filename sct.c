/*
 * sct - set color temperature (X11)
 *
 * Public domain, do as you wish.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/Xrandr.h>

#include "arg.h"

#define TEMPERATURE_NORM 6500
#define TEMPERATURE_ZERO 700
#define GAMMA_MULT       65535.0
// Approximation of the `redshift` table from
// https://github.com/jonls/redshift/blob/master/src/colorramp.c
// without limits:
// GAMMA = K0 + K1 * ln(T - T0)
#define GAMMA_K0GR     -1.47751309139817
#define GAMMA_K1GR     0.28590164772055
#define GAMMA_K0BR     -4.38321650114872
#define GAMMA_K1BR     0.6212158769447
#define GAMMA_K0RB     1.75390204039018
#define GAMMA_K1RB     -0.1150805671482
#define GAMMA_K0GB     1.49221604915144
#define GAMMA_K1GB     -0.07513509588921
#define BRIGHTHESS_DIV 65470.988
#define DELTA_MIN      -1000000

#define CLAMP(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))

typedef float f32;

static Display *dpy;
static int vflag;
char *argv0;

struct temp_status { f32 temp, brightness; };

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static void
usage(void)
{
	die("usage: %s [-v] [-d dT] [-c CRTC] [-s screen] [temperature] "
	    "[brightness]\n", argv0);
}

static struct temp_status
get_sct_for_screen(int screen, int icrtc)
{
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	int n, c;
	f32 t = TEMPERATURE_ZERO;
	f32 gr = 0.0, gg = 0.0, gb = 0.0, gd = 0.0;
	struct temp_status ts;

	n = res->ncrtc;
	if ((icrtc >= 0) && (icrtc < n))
		n = 1;
	else
		icrtc = 0;
	for (c = icrtc; c < (icrtc + n); c++) {
		XRRCrtcGamma *cg = XRRGetCrtcGamma(dpy, res->crtcs[c]);
		gr += cg->red[cg->size - 1];
		gg += cg->green[cg->size - 1];
		gb += cg->blue[cg->size - 1];
		XRRFreeGamma(cg);
	}
	XFree(res);
	ts.brightness = (gr > gg) ? gr : gg;
	ts.brightness = (gb > ts.brightness) ? gb : ts.brightness;
	if (ts.brightness > 0.0 && n > 0) {
		gr /= ts.brightness;
		gg /= ts.brightness;
		gb /= ts.brightness;
		ts.brightness /= n;
		ts.brightness /= BRIGHTHESS_DIV;
		if (vflag)
			fprintf(stderr,
			        "%s: gamma: %f, %f, %f, brightness: %f\n",
			        argv0, gr, gg, gb, ts.brightness);
		gd = gb - gr;
		if (gd < 0.0) {
			if (gb > 0.0)
				t += expf((1.0 + gg + gd - (GAMMA_K0GR + GAMMA_K0BR)) / (GAMMA_K1GR + GAMMA_K1BR));
			else if (gg > 0.0)
				t += expf((gg - GAMMA_K0GR) / GAMMA_K1GR);
		} else {
			t = expf((1.0 + gg - gd - (GAMMA_K0GB + GAMMA_K0RB)) / (GAMMA_K1GB + GAMMA_K1RB))
			    + (TEMPERATURE_NORM - TEMPERATURE_ZERO);
		}
	}

	ts.brightness = CLAMP(ts.brightness, 0.0, 1.0);
	ts.temp = (int)(t + 0.5);
	return ts;
}

static void
sct_for_screen(int screen, int icrtc, struct temp_status ts)
{
	f32 t = 0.0, b = 1.0, g = 0.0, gr, gg, gb;
	int n, c;
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	if (ts.temp < TEMPERATURE_ZERO) {
		fprintf(stderr, "%s: can't set temperature less than: %d\n",
		        argv0, TEMPERATURE_ZERO);
		ts.temp = TEMPERATURE_ZERO;
	}
	t = ts.temp;

	b = CLAMP(ts.brightness, 0.0, 1.0);
	if (ts.temp < TEMPERATURE_NORM) {
		g  = logf(t - TEMPERATURE_ZERO);
		gr = 1.0;
		gg = CLAMP(GAMMA_K0GR + GAMMA_K1GR * g, 0.0, 1.0);
		gb = CLAMP(GAMMA_K0BR + GAMMA_K1BR * g, 0.0, 1.0);
	} else {
		g  = logf(t - (TEMPERATURE_NORM - TEMPERATURE_ZERO));
		gr = CLAMP(GAMMA_K0RB + GAMMA_K1RB * g, 0.0, 1.0);
		gg = CLAMP(GAMMA_K0GB + GAMMA_K1GB * g, 0.0, 1.0);
		gb = 1.0;
	}
	if (vflag)
		fprintf(stderr, "%s: gamma: %f, %f, %f, brightness: %f\n",
		        argv0, gr, gg, gb, b);
	n = res->ncrtc;
	if ((icrtc >= 0) && (icrtc < n))
		n = 1;
	else
		icrtc = 0;
	for (c = icrtc; c < (icrtc + n); c++) {
		int i, size = XRRGetCrtcGammaSize(dpy, res->crtcs[c]);
		XRRCrtcGamma *cg = XRRAllocGamma(size);
		for (i = 0; i < size; i++) {
			g = GAMMA_MULT * b * (f32)i / (f32)size;
			cg->red[i]   = (unsigned short int)(g * gr + 0.5);
			cg->green[i] = (unsigned short int)(g * gg + 0.5);
			cg->blue[i]  = (unsigned short int)(g * gb + 0.5);
		}
		XRRSetCrtcGamma(dpy, res->crtcs[c], cg);
		XRRFreeGamma(cg);
	}

	XFree(res);
}

int
main(int argc, char **argv)
{
	int screen, screens;
	int screen_specified = -1, screen_first = 0, screen_last = -1;
	int crtc_specified = -1;
	struct temp_status ts = { .temp = DELTA_MIN, .brightness = -1.0 };
	int delta = 0;

	argv0 = argv[0];

	ARGBEGIN {
	case 'd':
		delta = atoi(EARGF(usage()));
		if (delta == 0)
			usage();
		break;
	case 'v': vflag = 1;                               break;
	case 's': screen_specified = atoi(EARGF(usage())); break;
	case 'c': crtc_specified   = atoi(EARGF(usage())); break;
	default:  usage();                                 break;
	} ARGEND;

	if (delta != 0 && argc)
		usage();

	switch (argc) {
	case 2: ts.brightness = atof(argv[1]); /* FALLTHROUGH */
	case 1: ts.temp       = atoi(argv[0]); /* FALLTHROUGH */
	default: break;
	}

	if (!(dpy = XOpenDisplay(NULL)))
		die("XOpenDisplay: can't open display\n");

	screens = XScreenCount(dpy);
	screen_last = screens - 1;

	if (screen_specified >= screens) {
		XCloseDisplay(dpy);
		die("Invalid screen: %d\n", screen_specified);
	}
	if (ts.brightness < 0.0)
		ts.brightness = 1.0;
	if (screen_specified >= 0) {
		screen_first = screen_specified;
		screen_last = screen_specified;
	}
	if (ts.temp < 0 && delta == 0) {
		// No arguments, so print estimated temperature for each screen
		for (screen = screen_first; screen <= screen_last; screen++) {
			ts = get_sct_for_screen(screen, crtc_specified);
			printf("Screen %d: temperature ~ %f\n", screen, ts.temp);
		}
	} else {
		if (delta == 0 && ts.temp == 0)
			ts.temp = TEMPERATURE_NORM;
		for (screen = screen_first; screen <= screen_last; screen++) {
			if (delta) {
				ts = get_sct_for_screen(screen, crtc_specified);
				ts.temp += delta;
			}
			sct_for_screen(screen, crtc_specified, ts);
		}
	}

	XCloseDisplay(dpy);

	return 0;
}
