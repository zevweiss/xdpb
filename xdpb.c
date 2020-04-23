/*
 * xdpb -- X Display Pointer Barriers.
 *
 * Copyright (c), Zev Weiss <zev@bewilderbeest.net>
 *
 * Sets up pointer barriers at the edges of each display so that it's easier
 * to position the mouse at screen edges (e.g. for scroll bars, etc.).
 *
 * This file began life as a modified version of
 * pointer-barriers-interactive.c by Jasper St. Pierre (not that there's much
 * left of it).
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* To expose tdestroy(3) in search.h */
#define _GNU_SOURCE 1

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <search.h>
#include <sys/time.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>

#ifdef DEBUG
#define dbg(...) (void)fprintf(stderr, __VA_ARGS__)
#else
/* Define as an empty function so arguments get used */
static inline void dbg(const char* fmt, ...) { }
#endif

#define internal_error(msg, ...) do { \
		fprintf(stderr, "Internal error: "msg"\n", ##__VA_ARGS__); \
		abort(); \
	} while (0)

struct pbinfo {
	PointerBarrier bar;
	int dir;

	union {
		double distance;
		struct {
			double last_tap;
			int on;
		} taphist;
	};
};

static const char* progname;

/* What mechanism we use to trigger release */
static enum {
	REL__UNSET_,
	REL_SPEED,
	REL_DISTANCE,
	REL_DOUBLETAP,
} releasemode = REL__UNSET_;

/* Pixels or speed needed to release pointer from barrier */
static double threshold;

static Display* dpy;
static Window rootwin;
static int xi2_opcode, xrr_opcode, xrr_event_base;
static int sigpipe[2];

/* Root of a tsearch() tree, so we can look up a struct pbinfo from a PointerBarrier */
static void* pbmap = NULL;

static int pbcmp(const void* va, const void* vb)
{
	const struct pbinfo* a = va;
	const struct pbinfo* b = vb;
	return (a->bar > b->bar) ? 1 : (a->bar < b->bar) ? -1 : 0;
}

/* Add pbi to pbmap */
static inline void add_pbi(const struct pbinfo* pbi)
{
	struct pbinfo* old = tsearch(pbi, &pbmap, pbcmp);
	if (*(struct pbinfo**)old != pbi)
		internal_error("PointerBarrier %lu already in pbmap", pbi->bar);
}

/* Lookup pb in pbmap */
static inline struct pbinfo* find_pbi(PointerBarrier pb)
{
	void* v;
	struct pbinfo k = { .bar = pb, };
	v = tfind(&k, &pbmap, pbcmp);
	if (!v)
		return NULL;
	return *(struct pbinfo**)v;
}

/* Error-checking malloc() wrapper */
static inline void* xmalloc(size_t s)
{
	void* p = malloc(s);
	if (!p) {
		fprintf(stderr, "malloc: %s", strerror(errno));
		exit(1);
	}
	return p;
}

/* Return the current seconds-since-epoch time as a double */
static inline double dnow(void)
{
	struct timeval t;
	if (gettimeofday(&t, NULL))
		internal_error("gettimeofday: %s\n", strerror(errno));
	return (double)t.tv_sec + (((double)t.tv_usec) / 1000000);
}

static void handle_barrier_leave(XIBarrierEvent* event)
{
	struct pbinfo* pbi = find_pbi(event->barrier);
	if (!pbi) {
		dbg("BarrierLeave on unknown (stale?) barrier %lu\n", event->barrier);
		return;
	}

	dbg("BarrierLeave [%lu], delta: %.2f/%.2f\n", event->barrier, event->dx, event->dy);

	switch (releasemode) {
	case REL_DISTANCE:
		pbi->distance = 0.0;
		break;

	case REL_SPEED:
		break;

	case REL_DOUBLETAP:
		pbi->taphist.on = 0;
		break;

	default:
		internal_error("invalid pbi->dir (%u)", pbi->dir);
	}
}

static void handle_barrier_hit(XIBarrierEvent* event)
{
	int release;
	double d, now;
	struct pbinfo* pbi = find_pbi(event->barrier);
	if (!pbi) {
		dbg("BarrierHit on unknown (stale?) barrier %lu\n", event->barrier);
		return;
	}

	dbg("BarrierHit [%lu], delta: %.2f/%.2f\n", event->barrier, event->dx, event->dy);

	switch (pbi->dir) {
	case BarrierPositiveX:
		d = -event->dx;
		break;
	case BarrierNegativeX:
		d = event->dx;
		break;
	case BarrierPositiveY:
		d = -event->dy;
		break;
	case BarrierNegativeY:
		d = event->dy;
		break;
	default:
		internal_error("invalid pbi->dir (%u)", pbi->dir);
	}

	/*
	 * Apparent movement *away* from the barrier on a *hit* event seems to
	 * happen sometimes; ignore it.
	 */
	if (d < 0.0)
		return;

	switch (releasemode) {
	case REL_SPEED:
		release = d > threshold;
		break;

	case REL_DISTANCE:
		pbi->distance += d;
		release = pbi->distance > threshold;
		if (release)
			pbi->distance = 0.0;
		break;

	case REL_DOUBLETAP:
		if (!pbi->taphist.on) {
			now = dnow();
			release = (now - pbi->taphist.last_tap) < threshold;
			if (!release) {
				pbi->taphist.last_tap = now;
				pbi->taphist.on = 1;
			}
		} else
			release = 0;
		break;

	default:
		internal_error("invalid releasemode (%d)", releasemode);
	}

	if (release) {
		XIBarrierReleasePointer(dpy, event->deviceid, event->barrier, event->eventid);
		XFlush(dpy);
	}
}

/* Check for necessary extensions, initializing {xi2,xrr}_* globals. */
static void check_extensions(void)
{
	int major, minor, opcode, evt, err;

	if (!XQueryExtension(dpy, "RANDR", &xrr_opcode, &evt, &err)) {
		fprintf(stderr, "XRandr extension not found\n");
		exit(1);
	}

	if (!XRRQueryExtension(dpy, &xrr_event_base, &err)) {
		fprintf(stderr, "XRandr present...but also not?\n");
		exit(1);
	}

	if (!XQueryExtension(dpy, "XFIXES", &opcode, &evt, &err)) {
		fprintf(stderr, "XFixes extension not found\n");
		exit(1);
	}

	if (!XFixesQueryVersion(dpy, &major, &minor) || (major * 10 + minor) < 50) {
		fprintf(stderr, "XFixes too old (have %d.%d, need 5.0+)\n", major, minor);
		exit(1);
	}

	if (!XQueryExtension(dpy, "XInputExtension", &xi2_opcode, &evt, &err)) {
		fprintf(stderr, "XInput extension not found\n");
		exit(1);
	}

	major = 2;
	minor = 3;

	if (XIQueryVersion(dpy, &major, &minor) != Success || ((major * 10) + minor) < 22) {
		fprintf(stderr, "XInput too old (have %d.%d, need 2.2+)\n", major, minor);
		exit(1);
	}
}

static void mkbar(int x1, int y1, int x2, int y2, int directions)
{
	struct pbinfo* pbi;
	PointerBarrier pb = XFixesCreatePointerBarrier(dpy, rootwin, x1, y1, x2, y2,
	                                              directions, 0, NULL);
	dbg("mkbar(%d, %d, %d, %d, %d) = %lu\n", x1, y1, x2, y2, directions, pb);
#ifdef DEBUG
	XSync(dpy, False);
#endif

	pbi = xmalloc(sizeof(*pbi));
	memset(pbi, 0, sizeof(*pbi));
	pbi->bar = pb;
	pbi->dir = directions;

	add_pbi(pbi);
}

static void setup_crtc_barriers(XRRCrtcInfo* ci)
{
	unsigned int xmin = ci->x, xmax = ci->x + ci->width - 1;
	unsigned int ymin = ci->y, ymax = ci->y + ci->height - 1;

	dbg("%s(%u, %u, %u, %u)\n", __func__, xmin, xmax, ymin, ymax);

	mkbar(xmin, ymin, xmin, ymax, BarrierPositiveX);
	mkbar(xmax, ymin, xmax, ymax, BarrierNegativeX);
	mkbar(xmin, ymin, xmax, ymin, BarrierPositiveY);
	mkbar(xmin, ymax, xmax, ymax, BarrierNegativeY);
}

static void setup_barriers(void)
{
	int i;
	XRRCrtcInfo* crtcinfo;
	XRRScreenResources* resources = XRRGetScreenResources(dpy, rootwin);

	for (i = 0; i < resources->ncrtc; i++) {
		crtcinfo = XRRGetCrtcInfo(dpy, resources, resources->crtcs[i]);

		/*
		 * For some reason there seems to be some magical N+1th
		 * pseudo-CRTC with width == 0 and height == 0; let's not try
		 * to set up pointer barriers around that one.
		 */
		if (crtcinfo->width > 0 && crtcinfo->height > 0)
			setup_crtc_barriers(crtcinfo);

		XRRFreeCrtcInfo(crtcinfo);
	}

	XSync(dpy, False);
	XRRFreeScreenResources(resources);
}

static void destroy_pbi(const void* nodep, const VISIT which, const int depth)
{
	struct pbinfo* pb;

	if (which != postorder && which != leaf)
		return;

	pb = *(struct pbinfo**)nodep;
	XFixesDestroyPointerBarrier(dpy, pb->bar);
	dbg("delbar(%lu)\n", pb->bar);
#ifdef DEBUG
	XSync(dpy, False);
#endif
}

static void teardown_barriers(void)
{
	twalk(pbmap, destroy_pbi);
	tdestroy(pbmap, free);
	pbmap = NULL;
}

static void reset_barriers(void)
{
	teardown_barriers();
	setup_barriers();
}

static void usage(FILE* out, int full)
{
	fprintf(out, "Usage: %s [ -h | -d DISTANCE | -s SPEED | -m SECONDS ]\n", progname);
	if (!full)
		return;

	fprintf(out, "Flags:\n");
	fprintf(out, "\t-h %-12s print this usage message\n", "");
	fprintf(out, "\t-d %-12s release after DISTANCE pixels of (suppressed) pointer travel\n", "DISTANCE");
	fprintf(out, "\t-s %-12s release when cursor speed (against barrier) exceeds SPEED\n", "SPEED");
	fprintf(out, "\t-m %-12s release on two taps against barrier within SECONDS seconds\n", "SECONDS");
}

static void set_options(int argc, char** argv)
{
	int opt;
	char* end;

	while ((opt = getopt(argc, argv, "d:s:m:h")) != -1) {
		switch (opt) {
		case 'd':
		case 's':
		case 'm':
			if (releasemode != REL__UNSET_) {
				fprintf(stderr, "Error: multiple release modes selected\n");
				usage(stderr, 0);
				exit(1);
			}
			releasemode = opt == 'd' ? REL_DISTANCE
				: opt == 's' ? REL_SPEED
				: REL_DOUBLETAP;
			threshold = strtod(optarg, &end);
			if (!*optarg || *end || threshold < 0.0) {
				fprintf(stderr, "Invalid threshold '%s' (must be numeric and non-negative)\n",
				        optarg);
				exit(1);
			}
			break;

		case 'h':
			usage(stdout, 1);
			exit(0);

		default:
			usage(stderr, 0);
			exit(1);
		}
	}

	/* Apply defaults if nothing specified */
	if (releasemode == REL__UNSET_) {
		releasemode = REL_DISTANCE;
		threshold = 50.0;
	}
}

static void sighdlr(int signo)
{
	if (write(sigpipe[1], &signo, sizeof(signo)) != sizeof(signo))
		abort();
}

static void setup_sighdlr(void)
{
	struct sigaction sa = {
		.sa_handler = sighdlr,
		.sa_flags = 0,
	};
	if (sigfillset(&sa.sa_mask))
		abort();

	if (pipe(sigpipe)) {
		perror("pipe");
		exit(1);
	}

	if (sigaction(SIGINT, &sa, NULL)) {
		perror("sigaction(SIGINT)");
		abort();
	}

	if (sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction(SIGTERM)");
		abort();
	}
}

static void handle_xevent(void)
{
	XEvent xev;
	XGenericEventCookie* cookie;
	XNextEvent(dpy, &xev);

	if (xev.type == GenericEvent) {
		cookie = &xev.xcookie;
		if (!XGetEventData(dpy, cookie))
			return;

		if (cookie->extension == xi2_opcode) {
			switch (cookie->evtype) {
			case XI_BarrierHit:
				handle_barrier_hit(cookie->data);
				break;
			case XI_BarrierLeave:
				handle_barrier_leave(cookie->data);
				break;
			default:
				break;
			}
		}

		XFreeEventData(dpy, cookie);
	} else if (xev.type == xrr_event_base + RRScreenChangeNotify)
		reset_barriers();
	else
		dbg("[unexpected event; type=%d]\n", xev.type);
}

int main(int argc, char** argv)
{
	int xfd, nfds;
	fd_set rfds;
	unsigned char mask_bits[XIMaskLen(XI_LASTEVENT)] = { 0, };
	XIEventMask mask = {
		.deviceid = XIAllMasterDevices,
		.mask = mask_bits,
		.mask_len = sizeof(mask_bits),
	};

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	set_options(argc, argv);

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Failed to connect to X server\n");
		return 1;
	}

	check_extensions();

	rootwin = XDefaultRootWindow(dpy);

	setup_barriers();

	XISetMask(mask_bits, XI_BarrierHit);
	XISetMask(mask_bits, XI_BarrierLeave);
	XISelectEvents(dpy, rootwin, &mask, 1);

	XRRSelectInput(dpy, rootwin, RRScreenChangeNotifyMask);

	setup_sighdlr();

	xfd = XConnectionNumber(dpy);
	nfds = (xfd > sigpipe[0]) ? xfd + 1 : sigpipe[0] + 1;

	XSync(dpy, False);
	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(xfd, &rfds);
		FD_SET(sigpipe[0], &rfds);

		if (select(nfds, &rfds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			exit(1);
		}

		if (FD_ISSET(sigpipe[0], &rfds)) {
			teardown_barriers();
			XCloseDisplay(dpy);
			exit(0);
		}

		if (FD_ISSET(xfd, &rfds))
			handle_xevent();
	}
}
