/* Copyright © 2012 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Jasper St. Pierre
 */

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>

static int dpy_w, dpy_h;

static int barrier_y;
static Display *dpy;
static PointerBarrier barrier;
static Window window;

static int velocity_limit = 0;
static int block = 0;

static int show_barrier = 1;

static void
draw_barrier (GC gc)
{
	XGCValues gc_values;

	if (block) {
		gc_values.foreground = 0xFF0000;
	} else {
		gc_values.foreground = 0x00FF00 + (velocity_limit * 255 / 100);
	}
	XChangeGC(dpy, gc, GCForeground, &gc_values);
	XSetLineAttributes(dpy, gc, 4, LineSolid, CapRound, JoinRound);
	XDrawLine(dpy, window, gc, 0, barrier_y, dpy_w, barrier_y);
}

static void
draw (void)
{
	static GC gc = NULL;
	XGCValues gc_values;
	char instr[255];
	int h = dpy_h - 10;

	if (!gc)
		gc = XCreateGC(dpy, window, GCForeground, &gc_values);

	gc_values.foreground = 0xFFFFFF;
	XChangeGC(dpy, gc, GCForeground, &gc_values);

	XFillRectangle(dpy, window, gc, 0, 0, dpy_w, dpy_h);

	if (show_barrier)
		draw_barrier(gc);

	gc_values.foreground = 0;
	XChangeGC(dpy, gc, GCForeground, &gc_values);

#define HEIGHT 15

#define DRAW() \
	XDrawString(dpy, window, gc, 10, h, instr, strlen (instr)); \
	h -= HEIGHT;

#define TEXT(...) \
	snprintf(instr, sizeof (instr), __VA_ARGS__); \
	DRAW();

	TEXT("Press Q/Escape to exit.");
	TEXT("Velocity threshold: %d", velocity_limit);
	TEXT("Use up/down arrow keys to adjust.");
	TEXT(" ");
	TEXT("Press R to put the barrier at a random Y location");
	TEXT("Press S to %s the barrier", show_barrier ? "hide" : "show");
	TEXT("Press B to globally %s", block ? "release" : "block");
}

static void
create_barrier (void)
{
	if (barrier > 0) {
		XFixesDestroyPointerBarrier(dpy, barrier);
		barrier = 0;
	}

	barrier = XFixesCreatePointerBarrier(dpy, window, 0, barrier_y, dpy_w, barrier_y,
	                                     0, 0, NULL);
}

static void
randomize_barrier (void)
{
	barrier_y = rand() % (dpy_h - 200) + 100;
	create_barrier();
}

static void
process_barrier_event (XIBarrierEvent *event)
{
	printf("   root coordinates: %.2f/%.2f\n", event->root_x, event->root_y);
	printf("   delta: %.2f/%.2f\n", event->dx, event->dy);

	int vel = fabs(event->dy);
	if (!block && vel > velocity_limit) {
		XIBarrierReleasePointer(dpy, event->deviceid, barrier, event->eventid);
		XFlush(dpy);
	}
}

static void
set_fullscreen (void)
{
	Atom fullscreen;
	fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	XChangeProperty(dpy, window, XInternAtom(dpy, "_NET_WM_STATE", False),
	                XA_ATOM, 32, PropModeReplace, (unsigned char *) &fullscreen, 1);
}

int
main (int argc, char **argv)
{
	XEvent xev;
	XIEventMask mask;
	int major, minor;
	int opcode, evt, err;
	unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0, };

	dpy = XOpenDisplay(NULL);

	if (!dpy) {
		printf("Failed to connect to X server\n");
		return 1;
	}

	if (!XQueryExtension(dpy, "XFIXES", &opcode, &evt, &err)) {
		printf("Need XFixes\n");
		return 1;
	}

	if (!XFixesQueryVersion(dpy, &major, &minor)
	    || (major * 10 + minor) < 50) {
		printf("Need XFixes 5.0\n");
		return 1;
	}

	if (!XQueryExtension(dpy, "XInputExtension", &opcode, &evt, &err)) {
		printf("Need XInput\n");
		return 1;
	}

	major = 2;
	minor = 3;

	if (XIQueryVersion(dpy, &major, &minor) != Success) {
		printf("Need XInput 2.3\n");
		return 1;
	}

	if (((major * 10) + minor) < 22) {
		printf("Need XInput 2.2\n");
		return 1;
	}

	srand(time (NULL));

	dpy_w = DisplayWidth(dpy, DefaultScreen (dpy));
	dpy_h = DisplayHeight(dpy, DefaultScreen (dpy));

	window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0,
	                             dpy_w, dpy_h, 0, BlackPixel(dpy, 0),
	                             WhitePixel(dpy, 0));
	set_fullscreen();

	barrier_y = 100;
	create_barrier();

	XISetMask(mask_bits, XI_BarrierHit);
	XISetMask(mask_bits, XI_BarrierLeave);
	mask.deviceid = XIAllMasterDevices;
	mask.mask = mask_bits;
	mask.mask_len = sizeof (mask_bits);
	XISelectEvents(dpy, window, &mask, 1);
	XSelectInput(dpy, window, KeyPressMask|ExposureMask);

	XMapWindow(dpy, window);
	XSync(dpy, False);
	while (1) {
		XNextEvent(dpy, &xev);
		switch (xev.type) {
		case GenericEvent:
		{
			XGenericEventCookie *cookie = &xev.xcookie;
			const char *type;

			if (cookie->extension != opcode)
				break;

			switch (cookie->evtype) {
			case XI_BarrierHit: type = "BarrierHit"; break;
			case XI_BarrierLeave: type = "BarrierLeave"; break;
			}

			printf("Event type %s\n", type);
			if (cookie->evtype != XI_BarrierHit)
				break;

			if (XGetEventData(dpy, cookie)) {
				process_barrier_event(cookie->data);
			}
			XFreeEventData(dpy, cookie);
		}
		break;
		case KeyPress:
		{
			int k = XKeycodeToKeysym(dpy, xev.xkey.keycode, 0);
			if (k == XK_Escape || k == XK_q) {
				goto out;
			} else if (k == XK_s) {
				show_barrier = !show_barrier;
			} else if (k == XK_b) {
				block = !block;
			} else if (k == XK_r) {
				randomize_barrier();
			} else if (k == XK_Up) {
				if (velocity_limit < 100)
					velocity_limit += 10;
			} else if (k == XK_Down) {
				if (velocity_limit > 0)
					velocity_limit -= 10;
			}
			draw();
		}
		break;
		case Expose:
			draw();
		}
	}

out:
	XCloseDisplay(dpy);
	return 0;
}
