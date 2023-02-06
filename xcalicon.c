/* vim:ts=8
 *
 * Copyright (c) 2023 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

#include "icons/calendar.xpm"
#include "icons/digits.xpm"

struct {
	Display *dpy;
	int screen;
	Window win;
	XWMHints hints;
	GC gc;
	Pixmap calendar_pm;
	Pixmap calendar_pm_mask;
	XpmAttributes calendar_pm_attrs;
	Pixmap digits_pm;
	Pixmap digits_pm_mask;
	XpmAttributes digits_pm_attrs;
	Pixmap icon_pm;
} xinfo = { 0 };

extern char *__progname;

void	killer(int);
void	usage(void);
void	redraw_icon(int);

int	exit_msg[2];
int	last_day = 0;
int	last_min = -1;

#define WINDOW_WIDTH	200
#define WINDOW_HEIGHT	100

int
main(int argc, char* argv[])
{
	XEvent event;
	XSizeHints *hints;
	XGCValues gcv;
	XWindowAttributes xgwa;
	struct pollfd pfd[2];
	struct sigaction act;
	char *display = NULL;
	int ch;

	while ((ch = getopt(argc, argv, "d:")) != -1) {
		switch (ch) {
		case 'd':
			display = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (!(xinfo.dpy = XOpenDisplay(display)))
		errx(1, "can't open display %s", XDisplayName(display));

#ifdef __OpenBSD_
	if (pledge("stdio") == -1)
		err(1, "pledge");
#endif

	/* setup exit handler pipe that we'll poll on */
	if (pipe2(exit_msg, O_CLOEXEC) != 0)
		err(1, "pipe2");
	act.sa_handler = killer;
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	xinfo.screen = DefaultScreen(xinfo.dpy);
	xinfo.win = XCreateSimpleWindow(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
	    BlackPixel(xinfo.dpy, xinfo.screen),
	    WhitePixel(xinfo.dpy, xinfo.screen));
	gcv.foreground = 1;
	gcv.background = 0;
	xinfo.gc = XCreateGC(xinfo.dpy, xinfo.win, GCForeground | GCBackground,
	    &gcv);
	XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);

	/* load XPMs */
	if (XpmCreatePixmapFromData(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    calendar_xpm, &xinfo.calendar_pm, &xinfo.calendar_pm_mask,
	    &xinfo.calendar_pm_attrs) != 0)
		errx(1, "XpmCreatePixmapFromData failed");

	if (XpmCreatePixmapFromData(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    digits_xpm, &xinfo.digits_pm, &xinfo.digits_pm_mask,
	    &xinfo.digits_pm_attrs) != 0)
		errx(1, "XpmCreatePixmapFromData failed");

	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	xinfo.icon_pm = XCreatePixmap(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    xinfo.calendar_pm_attrs.width,
	    xinfo.calendar_pm_attrs.height,
	    xgwa.depth);

	hints = XAllocSizeHints();
	if (!hints)
		err(1, "XAllocSizeHints");
	hints->flags = PMinSize | PMaxSize;
	hints->min_width = WINDOW_WIDTH;
	hints->min_height = WINDOW_HEIGHT;
	hints->max_width = WINDOW_WIDTH;
	hints->max_height = WINDOW_HEIGHT;
#if 0	/* disabled until progman displays minimize on non-dialog wins */
	XSetWMNormalHints(xinfo.dpy, xinfo.win, hints);
#endif

	redraw_icon(1);

	xinfo.hints.initial_state = IconicState;
	xinfo.hints.flags |= StateHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);
	XMapWindow(xinfo.dpy, xinfo.win);

	memset(&pfd, 0, sizeof(pfd));
	pfd[0].fd = ConnectionNumber(xinfo.dpy);
	pfd[0].events = POLLIN;
	pfd[1].fd = exit_msg[0];
	pfd[1].events = POLLIN;

	/* we need to know when we're exposed */
	XSelectInput(xinfo.dpy, xinfo.win, ExposureMask);

	for (;;) {
		if (!XPending(xinfo.dpy)) {
			poll(pfd, 2, 900);
			if (pfd[1].revents)
				/* exit msg */
				break;

			if (!XPending(xinfo.dpy)) {
				redraw_icon(0);
				continue;
			}
		}

		XNextEvent(xinfo.dpy, &event);

		switch (event.type) {
		case Expose:
			redraw_icon(1);
			break;
		}
	}

	XFreePixmap(xinfo.dpy, xinfo.calendar_pm);
	XFreePixmap(xinfo.dpy, xinfo.calendar_pm_mask);
	XFreePixmap(xinfo.dpy, xinfo.digits_pm);
	XFreePixmap(xinfo.dpy, xinfo.icon_pm);
	XDestroyWindow(xinfo.dpy, xinfo.win);
	XFree(hints);
	XCloseDisplay(xinfo.dpy);

	return 0;
}

void
killer(int sig)
{
	if (write(exit_msg[1], &exit_msg, 1))
		return;

	warn("failed to exit cleanly");
	exit(1);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"[-d display]");
	exit(1);
}

void
redraw_icon(int update_win)
{
	XTextProperty title_prop;
	XWindowAttributes xgwa;
	char title[50];
	char *titlep = (char *)&title;
	int rc, dwidth, xo, yo;
	struct tm *tm;
	time_t now;

	time(&now);
	tm = localtime(&now);

	if (tm->tm_min != last_min) {
#ifdef DEBUG
		printf("minute changed, updating title\n");
#endif
		last_min = tm->tm_min;

		strftime(title, sizeof(title), "%a %H:%M", tm);

		/* update icon and window titles */
		if (!(rc = XStringListToTextProperty(&titlep, 1, &title_prop)))
			errx(1, "XStringListToTextProperty");
		XSetWMIconName(xinfo.dpy, xinfo.win, &title_prop);
		XStoreName(xinfo.dpy, xinfo.win, title);
	}

	if (tm->tm_mday == last_day && !update_win)
		return;

	if (tm->tm_mday != last_day) {
#ifdef DEBUG
		printf("day changed, rebuilding icon\n");
#endif
		last_day = tm->tm_mday;

		XCopyArea(xinfo.dpy, xinfo.calendar_pm, xinfo.icon_pm, xinfo.gc,
		    0, 0,
		    xinfo.calendar_pm_attrs.width,
		    xinfo.calendar_pm_attrs.height,
		    0, 0);

		dwidth = (xinfo.digits_pm_attrs.width / 10);
		if (tm->tm_mday >= 10) {
			XCopyArea(xinfo.dpy,
			    xinfo.digits_pm, xinfo.icon_pm, xinfo.gc,
			    dwidth * (tm->tm_mday / 10),
			    0,
			    dwidth,
			    xinfo.digits_pm_attrs.height,
			    19, 28);
			XCopyArea(xinfo.dpy,
			    xinfo.digits_pm, xinfo.icon_pm, xinfo.gc,
			    dwidth * (tm->tm_mday % 10),
			    0,
			    dwidth,
			    xinfo.digits_pm_attrs.height,
			    33, 28);
		} else {
			XCopyArea(xinfo.dpy,
			    xinfo.digits_pm, xinfo.icon_pm, xinfo.gc,
			    dwidth * tm->tm_mday, 0,
			    dwidth,
			    xinfo.digits_pm_attrs.height,
			    26, 28);
		}

		/* update the icon */
		xinfo.hints.icon_pixmap = xinfo.icon_pm;
		xinfo.hints.icon_mask = xinfo.calendar_pm_mask;
		xinfo.hints.flags = IconPixmapHint | IconMaskHint;
		XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);

		update_win = 1;
	}

	if (update_win) {
		/* draw it in the center of the window */
#ifdef DEBUG
		printf("updating window\n");
#endif
		XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
		xo = (xgwa.width / 2) - (xinfo.calendar_pm_attrs.width / 2);
		yo = (xgwa.height / 2) - (xinfo.calendar_pm_attrs.height / 2);
		XClearWindow(xinfo.dpy, xinfo.win);
		XSetClipMask(xinfo.dpy, xinfo.gc, xinfo.calendar_pm_mask);
		XSetClipOrigin(xinfo.dpy, xinfo.gc, xo, yo);
		XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);
		XCopyArea(xinfo.dpy, xinfo.icon_pm, xinfo.win, xinfo.gc,
		    0, 0,
		    xinfo.calendar_pm_attrs.width,
		    xinfo.calendar_pm_attrs.height,
		    xo, yo);
		XSetClipMask(xinfo.dpy, xinfo.gc, xinfo.calendar_pm_mask);
		XSetClipOrigin(xinfo.dpy, xinfo.gc, 0, 0);
	}
}
