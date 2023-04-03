/*
 * ssct - suckless set color temperature (X11)
 *
 * Public domain, do as you wish.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define GAMMA_K0GR -1.47751309139817
#define GAMMA_K1GR 0.28590164772055
#define GAMMA_K0BR -4.38321650114872
#define GAMMA_K1BR 0.6212158769447
#define GAMMA_K0RB 1.75390204039018
#define GAMMA_K1RB -0.1150805671482
#define GAMMA_K0GB     1.49221604915144
#define GAMMA_K1GB     -0.07513509588921
#define BRIGHTHESS_DIV 65470.988
#define DELTA_MIN      -1000000

static Display *dpy;
static int vflag;
char *argv0;

struct temp_status {
	int temp;
	double brightness;
};

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
	die("usage: %s [-vd] [-c CRTC] [-s screen] [temperature] "
	    "[brightness]\n", argv0);
}

static double
DoubleTrim(double x, double a, double b)
{
	double buff[3] = { a, x, b };
	return buff[(int)(x > a) + (int)(x > b)];
}

static void
get_sct_for_screen(struct temp_status *ts, int screen, int icrtc)
{
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	int n, c;
	double t = 0.0;
	double gammar = 0.0, gammag = 0.0, gammab = 0.0, gammad = 0.0;

	n = res->ncrtc;
	if ((icrtc >= 0) && (icrtc < n))
		n = 1;
	else
		icrtc = 0;
	for (c = icrtc; c < (icrtc + n); c++) {
		RRCrtc crtcxid;
		int size;
		XRRCrtcGamma *crtc_gamma;
		crtcxid = res->crtcs[c];
		crtc_gamma = XRRGetCrtcGamma(dpy, crtcxid);
		size = crtc_gamma->size;
		gammar += crtc_gamma->red[size - 1];
		gammag += crtc_gamma->green[size - 1];
		gammab += crtc_gamma->blue[size - 1];

		XRRFreeGamma(crtc_gamma);
	}
	XFree(res);
	ts->brightness = (gammar > gammag) ? gammar : gammag;
	ts->brightness = (gammab > ts->brightness) ? gammab : ts->brightness;
	if (ts->brightness > 0.0 && n > 0) {
		gammar /= ts->brightness;
		gammag /= ts->brightness;
		gammab /= ts->brightness;
		ts->brightness /= n;
		ts->brightness /= BRIGHTHESS_DIV;
		ts->brightness = DoubleTrim(ts->brightness, 0.0, 1.0);
		if (vflag)
			fprintf(stderr,
			        "DEBUG: Gamma: %f, %f, %f, brightness: %f\n",
			        gammar, gammag, gammab, ts->brightness);
		gammad = gammab - gammar;
		if (gammad < 0.0) {
			if (gammab > 0.0) {
				t = exp((gammag + 1.0 + gammad
				         - (GAMMA_K0GR + GAMMA_K0BR))
				        / (GAMMA_K1GR + GAMMA_K1BR))
				    + TEMPERATURE_ZERO;
			} else {
				t = (gammag > 0.0) ? (
					exp((gammag - GAMMA_K0GR) / GAMMA_K1GR)
					+ TEMPERATURE_ZERO)
				                   : TEMPERATURE_ZERO;
			}
		} else {
			t = exp((gammag + 1.0 - gammad
			         - (GAMMA_K0GB + GAMMA_K0RB))
			        / (GAMMA_K1GB + GAMMA_K1RB))
			    + (TEMPERATURE_NORM - TEMPERATURE_ZERO);
		}
	} else
		ts->brightness = DoubleTrim(ts->brightness, 0.0, 1.0);

	ts->temp = (int)(t + 0.5);
}

static void
sct_for_screen(int screen, int icrtc, struct temp_status *ts)
{
	double t = 0.0, b = 1.0, g = 0.0, gammar, gammag, gammab;
	int n, c;
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	if (ts->temp < TEMPERATURE_ZERO) {
		fprintf(stderr, "%s: can't set temperature less than: %d\n",
		        argv0, TEMPERATURE_ZERO);
		t = (double)TEMPERATURE_ZERO;
	} else {
		t = (double)ts->temp;
	}

	b = DoubleTrim(ts->brightness, 0.0, 1.0);
	if (ts->temp < TEMPERATURE_NORM) {
		gammar = 1.0;
		if (ts->temp > TEMPERATURE_ZERO) {
			g = log(t - TEMPERATURE_ZERO);
			gammag =
			    DoubleTrim(GAMMA_K0GR + GAMMA_K1GR * g, 0.0, 1.0);
			gammab =
			    DoubleTrim(GAMMA_K0BR + GAMMA_K1BR * g, 0.0, 1.0);
		} else {
			gammag = 0.0;
			gammab = 0.0;
		}
	} else {
		g = log(t - (TEMPERATURE_NORM - TEMPERATURE_ZERO));
		gammar = DoubleTrim(GAMMA_K0RB + GAMMA_K1RB * g, 0.0, 1.0);
		gammag = DoubleTrim(GAMMA_K0GB + GAMMA_K1GB * g, 0.0, 1.0);
		gammab = 1.0;
	}
	if (vflag)
		fprintf(stderr, "DEBUG: Gamma: %f, %f, %f, brightness: %f\n",
		        gammar, gammag, gammab, b);
	n = res->ncrtc;
	if ((icrtc >= 0) && (icrtc < n))
		n = 1;
	else
		icrtc = 0;
	for (c = icrtc; c < (icrtc + n); c++) {
		int size, i;
		RRCrtc crtcxid;
		XRRCrtcGamma *crtc_gamma;
		crtcxid = res->crtcs[c];
		size = XRRGetCrtcGammaSize(dpy, crtcxid);

		crtc_gamma = XRRAllocGamma(size);

		for (i = 0; i < size; i++) {
			g = GAMMA_MULT * b * (double)i / (double)size;
			crtc_gamma->red[i] =
			    (unsigned short int)(g * gammar + 0.5);
			crtc_gamma->green[i] =
			    (unsigned short int)(g * gammag + 0.5);
			crtc_gamma->blue[i] =
			    (unsigned short int)(g * gammab + 0.5);
		}

		XRRSetCrtcGamma(dpy, crtcxid, crtc_gamma);
		XRRFreeGamma(crtc_gamma);
	}

	XFree(res);
}

int
main(int argc, char **argv)
{
	int screen, screens;
	int screen_specified, screen_first, screen_last, crtc_specified;
	struct temp_status temp = { .temp = DELTA_MIN, .brightness = -1.0 };
	int dflag = 0;

	argv0 = argv[0];

	ARGBEGIN {
	case 'd':
		dflag = 1;
		break;
	case 'v':
		vflag = 1;
		break;
	case 's':
		screen_specified = atoi(EARGF(usage()));
		break;
	case 'c':
		crtc_specified = atoi(EARGF(usage()));
		break;
	default:
		usage();
		break;
	} ARGEND;

	switch (argc) {
	case 2:
		temp.brightness = atof(argv[1]);
		/* FALLTHROUGH */
	case 1:
		temp.temp = atoi(argv[0]);
		/* FALLTHROUGH */
	default:
		break;
	}

	if (!(dpy = XOpenDisplay(NULL)))
		die("XOpenDisplay: can't open display\n");

	screens = XScreenCount(dpy);
	screen_first = 0;
	screen_last = screens - 1;
	screen_specified = -1;
	crtc_specified = -1;

	if (screen_specified >= screens) {
		XCloseDisplay(dpy);
		die("Invalid screen: %d\n", screen_specified);
	}
	if (temp.brightness < 0.0)
		temp.brightness = 1.0;
	if (screen_specified >= 0) {
		screen_first = screen_specified;
		screen_last = screen_specified;
	}
	if ((temp.temp < 0) && !dflag) {
		// No arguments, so print estimated temperature for each
		// screen
		for (screen = screen_first; screen <= screen_last; screen++) {
			get_sct_for_screen(&temp, screen, crtc_specified);
			printf("Screen %d: temperature ~ %d %f\n", screen,
			       temp.temp, temp.brightness);
		}
	} else {
		struct temp_status tempd = {.temp = 0, .brightness = 1.0};
		if (!dflag && temp.temp == 0)
			temp.temp = TEMPERATURE_NORM;
		for (screen = screen_first; screen <= screen_last; screen++) {
			if (dflag) {
				get_sct_for_screen(&tempd, screen, crtc_specified);
				tempd.temp += temp.temp;
			}
			sct_for_screen(screen, crtc_specified, dflag? &tempd : &temp);
		}
	}

	XCloseDisplay(dpy);

	return 0;
}
