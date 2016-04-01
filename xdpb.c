/*
 * Derived (roughly) from pointer-barriers-interactive.c by Jasper St. Pierre.
 */

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>

static Display *dpy;

static void handle_barrier_event(XIBarrierEvent *event)
{
	printf("   root coordinates: %.2f/%.2f\n", event->root_x, event->root_y);
	printf("   delta: %.2f/%.2f\n", event->dx, event->dy);

	if (fabs(event->dy) > 15.0) {
		XIBarrierReleasePointer(dpy, event->deviceid, event->barrier, event->eventid);
		XFlush(dpy);
	}
}

/* Check for necessary extensions, returning XI2 opcode */
static int check_extensions(void)
{
	int major, minor, opcode, evt, err;

	if (!XQueryExtension(dpy, "XFIXES", &opcode, &evt, &err)) {
		fprintf(stderr, "XFixes extension not found\n");
		exit(1);
	}

	if (!XFixesQueryVersion(dpy, &major, &minor) || (major * 10 + minor) < 50) {
		fprintf(stderr, "XFixes too old (have %d.%d, need 5.0+)\n", major, minor);
		exit(1);
	}

	if (!XQueryExtension(dpy, "XInputExtension", &opcode, &evt, &err)) {
		fprintf(stderr, "XInput extension not found\n");
		exit(1);
	}

	major = 2;
	minor = 3;

	if (XIQueryVersion(dpy, &major, &minor) != Success || ((major * 10) + minor) < 22) {
		fprintf(stderr, "XInput too old (have %d.%d, need 2.2+)\n", major, minor);
		exit(1);
	}

	return opcode;
}

int main(int argc, char **argv)
{
	XEvent xev;
	int xi2_opcode;
	int dpy_w;
	Window rootwin;
	XGenericEventCookie *cookie;
	const char *type;
	unsigned char mask_bits[XIMaskLen(XI_LASTEVENT)] = { 0, };
	XIEventMask mask = {
		.deviceid = XIAllMasterDevices,
		.mask = mask_bits,
		.mask_len = sizeof(mask_bits),
	};

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Failed to connect to X server\n");
		return 1;
	}

	xi2_opcode = check_extensions();

	dpy_w = DisplayWidth(dpy, DefaultScreen(dpy));
	rootwin = XDefaultRootWindow(dpy);

	XFixesCreatePointerBarrier(dpy, rootwin, 0, 600, dpy_w, 600, 0, 0, NULL);

	XISetMask(mask_bits, XI_BarrierHit);
	XISetMask(mask_bits, XI_BarrierLeave);
	XISelectEvents(dpy, rootwin, &mask, 1);

	XSync(dpy, False);
	for (;;) {
		XNextEvent(dpy, &xev);
		if (xev.type == GenericEvent || xev.xcookie.extension != xi2_opcode) {
			cookie = &xev.xcookie;

			switch (cookie->evtype) {
			case XI_BarrierHit: type = "BarrierHit"; break;
			case XI_BarrierLeave: type = "BarrierLeave"; break;
			}

			printf("Event: %s\n", type);
			if (cookie->evtype == XI_BarrierHit) {
				if (XGetEventData(dpy, cookie))
					handle_barrier_event(cookie->data);
				XFreeEventData(dpy, cookie);
			}
		}
	}
}
