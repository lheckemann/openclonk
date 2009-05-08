/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de
 *
 * Portions might be copyrighted by other authors who have contributed
 * to OpenClonk.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * See isc_license.txt for full license and disclaimer.
 *
 * "Clonk" is a registered trademark of Matthes Bender.
 * See clonk_trademark_license.txt for full license.
 */

/* A wrapper class to OS dependent event and window interfaces, X11 version */

#include <Standard.h>
#ifdef USE_X11
#include <StdWindow.h>
#include <StdGL.h>
#include <StdDDraw2.h>
#include <StdFile.h>
#include <StdBuf.h>

//  Xmd.h typedefs BOOL to CARD8, but we want int
#define BOOL _BOOL
#include <X11/Xmd.h>
#undef BOOL

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/xf86vmode.h>
#include <X11/extensions/Xrandr.h>
#include <X11/XKBlib.h>

#include <string>
#include <sstream>
#include <map>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  endif
static void readline_callback (char *);
static CStdApp * readline_callback_use_this_app = 0;
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  endif
#endif /* HAVE_READLINE_HISTORY */

/* CStdApp */

#ifdef WITH_GLIB
# include <glib/gmain.h>
# include <glib/giochannel.h>
# include <gtk/gtk.h>
#endif

#include "StdXPrivate.h"

namespace
{
#ifdef WITH_GLIB
	// Callbacks from Glib main loop
	gboolean OnXInputStatic(GIOChannel* channel, GIOCondition condition, gpointer data)
	{
		static_cast<CStdApp*>(data)->OnXInput();
		return TRUE;
	}

	gboolean OnPipeInputStatic(GIOChannel* channel, GIOCondition condition, gpointer data)
	{
		static_cast<CStdApp*>(data)->OnPipeInput();
		return TRUE;
	}

	gboolean OnStdInInputStatic(GIOChannel* channel, GIOCondition condition, gpointer data)
	{
		static_cast<CStdApp*>(data)->OnStdInInput();
		return TRUE;
	}
#endif

	unsigned int KeyMaskFromKeyEvent(Display* dpy, XKeyEvent* xkey)
	{
		unsigned int mask = xkey->state;
		KeySym sym = XKeycodeToKeysym(dpy, xkey->keycode, 1);
		// We need to correct the keymask since the event.xkey.state
		// is the state _before_ the event, but we want to store the
		// current state.
		if(sym == XK_Control_L || sym == XK_Control_R) mask ^= MK_CONTROL;
		if(sym == XK_Shift_L || sym == XK_Shift_L) mask ^= MK_SHIFT;
		if(sym == XK_Alt_L || sym == XK_Alt_R) mask ^= (1 << 3);
		return mask;
	}
}

CStdAppPrivate::WindowListT CStdAppPrivate::WindowList;

CStdApp::CStdApp(): Active(false), fQuitMsgReceived(false), dpy(0), Priv(new CStdAppPrivate(this)),
	Location(""), DoNotDelay(false),
	// main thread
#ifdef HAVE_PTHREAD
	MainThread (pthread_self()),
#endif
	fDspModeSet(false),
	// 36 FPS
	Delay(27777)
{
	Add(&Priv->X11Proc);
#ifdef WITH_GLIB
	Add(&Priv->GLibProc);
#endif
}

CStdApp::~CStdApp() {
	Remove(&Priv->X11Proc);
#ifdef WITH_GLIB
	Remove(&Priv->GLibProc);
#endif
	delete Priv;
}

bool CStdApp::Init(int argc, char * argv[]) {
	// Set locale
	setlocale(LC_ALL,"");
	// Try to figure out the location of the executable
	Priv->argc=argc; Priv->argv=argv;
	static char dir[PATH_MAX];
	SCopy(argv[0], dir);
	if (dir[0] != '/') {
		SInsert(dir, "/");
		SInsert(dir, GetWorkingDirectory());
		Location = dir;
	} else {
		Location = dir;
	}
	// botch arguments
	static std::string s("\"");
	for (int i = 1; i < argc; ++i) {
		s.append(argv[i]);
		s.append("\" \"");
	}
	s.append("\"");
	szCmdLine = s.c_str();

#ifdef WITH_GLIB
	Priv->loop = g_main_loop_new(NULL, FALSE);

	Priv->pipe_channel = NULL;
	Priv->x_channel = NULL;
	Priv->stdin_channel = NULL;
#endif

	if(!(dpy = XOpenDisplay (0))) {
		Log("Error opening display.");
		return false;
	}

	int xf86vmode_event_base, xf86vmode_error_base;
	if (!XF86VidModeQueryExtension(dpy, &xf86vmode_event_base, &xf86vmode_error_base)
		|| !XF86VidModeQueryVersion(dpy, &xf86vmode_major_version, &xf86vmode_minor_version)) {
		xf86vmode_major_version = -1;
		xf86vmode_minor_version = 0;
	}
	int xrandr_error_base;
	if (!XRRQueryExtension(dpy, &Priv->xrandr_event, &xrandr_error_base)
	    || !XRRQueryVersion(dpy, &xrandr_major_version, &xrandr_minor_version)) {
		xrandr_major_version = -1;
		xrandr_minor_version = 0;
	}
	XRRSelectInput(dpy, DefaultRootWindow(dpy), RRScreenChangeNotifyMask);
	if (xrandr_major_version < 0 && xf86vmode_major_version < 0)
		Log("Xrandr and xf86vmode extensions are missing. Resolution switching will not work.");
	else if (xrandr_major_version >= 0)
		LogF("  Using XRandR version %d.%d", xrandr_major_version, xrandr_minor_version);
	else
		LogF("  Using XF86VidMode version %d.%d", xf86vmode_major_version, xf86vmode_minor_version);
	// So a repeated keypress-event is not preceded with a keyrelease.
	XkbSetDetectableAutoRepeat(dpy, True, &Priv->detectable_autorepeat_supported);

	XSetLocaleModifiers("");
	Priv->xim = XOpenIM(dpy, 0, 0, 0);
	if (!Priv->xim) Log("Failed to open input method.");

	// Get the Atoms for the Clipboard
	Atom PrefetchAtoms[8];
	const char * PrefetchAtomnames[] = { "CLIPBOARD", "WM_CHANGE_STATE", "WM_DELETE_WINDOW",
		"_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_PING", "_NET_WM_PID",
		"_NET_WM_STATE_DEMANDS_ATTENTION" };
	XInternAtoms(dpy, const_cast<char **>(PrefetchAtomnames), 8, true, PrefetchAtoms);

#ifdef WITH_GLIB
	Priv->x_channel = g_io_channel_unix_new(XConnectionNumber(dpy));
	g_io_add_watch(Priv->x_channel, G_IO_IN, &OnXInputStatic, this);
#endif
#if USE_CONSOLE && HAVE_LIBREADLINE
	rl_callback_handler_install (">", readline_callback);
	readline_callback_use_this_app = this;

#ifdef WITH_GLIB
	Priv->stdin_channel = g_io_channel_unix_new(STDIN_FILENO);
	g_io_add_watch(Priv->stdin_channel, G_IO_IN, &OnStdInInputStatic, this);
#endif
#endif
	// create pipe
	if(pipe(Priv->Pipe) != 0) {
		Log("Error creating Pipe");
		return false;
	}

#ifdef WITH_GLIB
	Priv->pipe_channel = g_io_channel_unix_new(Priv->Pipe[0]);
	g_io_add_watch(Priv->pipe_channel, G_IO_IN, &OnPipeInputStatic, this);
#endif

	// Custom initialization
	return DoInit ();
}

void CStdApp::Clear() {
	XCloseDisplay(dpy);
	dpy = 0;
#if USE_CONSOLE && HAVE_LIBREADLINE
	rl_callback_handler_remove();
#endif
	// close pipe
	close(Priv->Pipe[0]);
	close(Priv->Pipe[1]);
#ifdef WITH_GLIB
	g_main_loop_unref(Priv->loop);

	if(Priv->pipe_channel) g_io_channel_unref(Priv->pipe_channel);
	if(Priv->x_channel) g_io_channel_unref(Priv->x_channel);
	if(Priv->stdin_channel) g_io_channel_unref(Priv->stdin_channel);
#endif
}

void CStdApp::Quit() {
	fQuitMsgReceived = true;
#ifdef WITH_GLIB
	//g_main_loop_quit(Priv->loop);
#endif
}

bool CStdApp::FlushMessages() {

	// Always fail after quit message
	if(fQuitMsgReceived)
		return false;

	return Priv->X11Proc.Execute(0);
}

/*
void CStdApp::Execute () {
	time_t seconds = LastExecute.tv_sec;
	timeval tv;
	gettimeofday(&tv, 0);
	// Too slow?
	if(DoNotDelay) {
		DoNotDelay = false;
		LastExecute = tv;
	} else if (LastExecute.tv_sec < tv.tv_sec - 2) {
		LastExecute = tv;
	} else {
		LastExecute.tv_usec += Delay;
		if (LastExecute.tv_usec > 1000000) {
			++LastExecute.tv_sec;
			LastExecute.tv_usec -= 1000000;
		}
	}
	// This will make the FPS look "prettier" in some situations
	// But who cares...
	if (seconds != LastExecute.tv_sec) {
		pWindow->Sec1Timer();
	}
}
void CStdApp::NextTick(bool fYield) {
	DoNotDelay = true;
}*/
/*
void CStdApp::Run() {
	// Main message loop
	while (true) if (HandleMessage(INFINITE, true) == HR_Failure) return;
}
void CStdApp::ResetTimer(unsigned int d) { Delay = 1000 * d; }
*/

#ifdef WITH_GLIB
namespace {
	// Just indicate that the timeout elapsed
	gboolean HandleMessageTimeout(gpointer data) { *static_cast<bool*>(data) = true; return FALSE; }
}
#endif
/*
C4AppHandleResult CStdApp::HandleMessage(unsigned int iTimeout, bool fCheckTimer) {
	// quit check for nested HandleMessage-calls
	if (fQuitMsgReceived) return HR_Failure;
	bool do_execute = fCheckTimer;
	// Wait Delay microseconds.
	timeval tv = { 0, 0 };
	if (DoNotDelay) {
		// nothing to do
	} else if (fCheckTimer) {
		gettimeofday(&tv, 0);
		tv.tv_usec = LastExecute.tv_usec - tv.tv_usec + Delay
			- 1000000 * (tv.tv_sec - LastExecute.tv_sec);
		// Check if the given timeout comes first
		// (don't call Execute then, because it assumes it has been called because of a timer event!)
		if (iTimeout != INFINITE && iTimeout * 1000 < tv.tv_usec) {
			tv.tv_usec = iTimeout * 1000;
			do_execute = false;
		}
		if (tv.tv_usec < 0)
			tv.tv_usec = 0;
		tv.tv_sec = 0;
	} else {
		tv.tv_usec = iTimeout * 1000;
	}

	// Handle pending X messages
	//while (XEventsQueued(dpy, QueuedAlready)) {
	while (XPending(dpy)) {
		HandleXMessage();
	}

#ifdef WITH_GLIB
	// Timeout in milliseconds
	unsigned int timeout = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	bool timeout_elapsed = false;
	guint timeout_handle = 0;

	// Guarantee that we do not block until something interesting occurs
	// when using a timeout
	if(fCheckTimer || iTimeout != INFINITE)
	{
		// The timeout handler sets timeout_elapsed to true when
		// the timeout elpased, this is required for a correct return
		// value.
		timeout_handle = g_timeout_add_full(
			G_PRIORITY_HIGH,
			timeout,
			&HandleMessageTimeout,
			&timeout_elapsed,
			NULL
		);
	}

	g_main_context_iteration(g_main_loop_get_context(Priv->loop), TRUE);

	if(timeout_handle && !timeout_elapsed)
	{
		// FIXME: do not add a new timeout instead of deleting the old one in the next call
		g_source_remove(timeout_handle);
	}

	if(timeout_elapsed && do_execute)
	{
		Execute();
	}
	while(g_main_context_pending(g_main_loop_get_context(Priv->loop)))
		g_main_context_iteration(g_main_loop_get_context(Priv->loop), FALSE);

	return timeout_elapsed ? (do_execute ? HR_Timer : HR_Timeout) : HR_Message;
#else // WITH_GLIB
	// Watch dpy to see when it has input.
	int max_fd = 0;
	fd_set rfds;
	FD_ZERO(&rfds);

	// Stop waiting for the next frame when more events arrive
	XFlush(dpy);
	FD_SET(XConnectionNumber(dpy), &rfds);
	max_fd = Max(XConnectionNumber(dpy), max_fd);
#ifdef USE_CONSOLE
	// Wait for commands from stdin
	FD_SET(0, &rfds);
#endif
	// And for events from the network thread
	FD_SET(Priv->Pipe[0], &rfds);
	max_fd = Max(Priv->Pipe[0], max_fd);
	switch (select(max_fd + 1, &rfds, NULL, NULL, (fCheckTimer || iTimeout != INFINITE) ? &tv : NULL)) {
		// error
		case -1:
		if (errno == EINTR) return HR_Message;
		Log(strerror(errno));
		Log("select error.");
		return HR_Failure;

		// timeout
		case 0:
		if (do_execute) {
			Execute();
			return HR_Timer;
		}
		return HR_Timeout;

		default:
		// flush pipe
		if(FD_ISSET(Priv->Pipe[0], &rfds)) {
			OnPipeInput();
		}
		if(FD_ISSET(XConnectionNumber(dpy), &rfds)) {
		  OnXInput();
		}
#ifdef USE_CONSOLE
		// handle commands
		if(FD_ISSET(0, &rfds))
		{
			// Do not call OnStdInInput to be able to return
			// HR_Failure when ReadStdInCommand returns false
			if(!ReadStdInCommand())
				return HR_Failure;
		}
#endif
		return HR_Message;
	}
#endif // !WITH_GLIB
}
*/
bool CStdApp::SignalNetworkEvent() {
	char c = 1;
	write(Priv->Pipe[1], &c, 1);
	return true;
}

void CStdApp::HandleXMessage() {
	XEvent event;
	XNextEvent(dpy, &event);
	// Needed for input methods
	if (XFilterEvent(&event, event.xany.window)) return;
	switch (event.type) {
		case KeyPress: {
			char c[10] = "";
			Status blub;
			if (Priv->xic) {
				XSetICFocus(Priv->xic);
				XmbLookupString(Priv->xic, &event.xkey, c, 10, 0, &blub);
			} else {
				static XComposeStatus state;
				XLookupString(&event.xkey, c, 10, 0, &state);
			}
			if (c[0]) {
				CStdWindow * pWindow = Priv->GetWindow(event.xany.window);
				if (pWindow) {
					pWindow->CharIn(c);
				}
			}
		// Fallthrough
		}
		case KeyRelease:
		KeyMask = KeyMaskFromKeyEvent(dpy, &event.xkey);
		Priv->LastEventTime = event.xkey.time;
		break;
		case ButtonPress:
		// We can take this directly since there are no key presses
		// involved. TODO: We probably need to correct button state
		// here though.
		KeyMask = event.xbutton.state;
		Priv->LastEventTime = event.xbutton.time;
		break;
		case SelectionRequest: {
		// We should compare the timestamp with the timespan when we owned the selection
		// But slow network connections are not supported anyway, so do not bother
			CStdAppPrivate::ClipboardData & d = (event.xselectionrequest.selection == XA_PRIMARY) ?
				Priv->PrimarySelection : Priv->ClipboardSelection;
			XEvent responseevent;
			XSelectionEvent & re = responseevent.xselection;
			re.type = SelectionNotify;
			re.display = dpy;
			re.selection = event.xselectionrequest.selection;
			re.target = event.xselectionrequest.target;
			re.time = event.xselectionrequest.time;
			re.requestor = event.xselectionrequest.requestor;
			// Note: we're implementing the spec only partially here
			if (d.Text.getData()) {
				re.property = event.xselectionrequest.property;
				XChangeProperty(dpy, re.requestor, re.property, re.target, 8, PropModeReplace,
					(const unsigned char *) d.Text.getData(), d.Text.getLength());
			} else {
				re.property = None;
			}
			XSendEvent(dpy, re.requestor, false, NoEventMask, &responseevent);
			break;
		}
		case SelectionClear: {
			CStdAppPrivate::ClipboardData & d = (event.xselectionrequest.selection == XA_PRIMARY) ?
				Priv->PrimarySelection : Priv->ClipboardSelection;
			d.Text.Clear();
			break;
		}
		case ClientMessage:
		if (!strcmp(XGetAtomName(dpy, event.xclient.message_type), "WM_PROTOCOLS")) {
			if (!strcmp(XGetAtomName(dpy, event.xclient.data.l[0]), "WM_DELETE_WINDOW")) {
				CStdWindow * pWindow = Priv->GetWindow(event.xclient.window);
				if (pWindow) pWindow->Close();
			} else if (!strcmp(XGetAtomName(dpy, event.xclient.data.l[0]), "_NET_WM_PING")) {
				// We're still alive
				event.xclient.window = DefaultRootWindow(dpy);
				XSendEvent(dpy, DefaultRootWindow(dpy), false,
					SubstructureNotifyMask | SubstructureRedirectMask, &event);
			}
		}
		break;
		case MappingNotify:
		XRefreshKeyboardMapping(&event.xmapping);
		break;
		case DestroyNotify: {
			CStdWindow * pWindow = Priv->GetWindow(event.xany.window);
			if (pWindow) {
				pWindow->wnd = 0;
				pWindow->Clear();
			}
			Priv->SetWindow(event.xany.window, 0);
			break;
		}
		case FocusIn:
		if (Priv->pending_desktop)
			Priv->pending_desktop = false;
		if (pWindow && event.xany.window == pWindow->wnd && Priv->tasked_out) {
			fDspModeSet = Priv->SwitchToFullscreen(this, pWindow->wnd);
			Priv->tasked_out = false;
		}
		break;
		case FocusOut: case UnmapNotify:
		if (pWindow && event.xany.window == pWindow->wnd && fDspModeSet) {
			Priv->pending_desktop = true;
		}
		break;
		case ConfigureNotify:
		if (pWindow && event.xany.window == pWindow->wnd) {
			OnResolutionChanged(event.xconfigure.width, event.xconfigure.height);
		}
		break;
		default:
		if (event.type == Priv->xrandr_event)
			XRRUpdateConfiguration(&event);
		break;
	}
	CStdWindow * pWindow = Priv->GetWindow(event.xany.window);
	if (pWindow)
		pWindow->HandleMessage(event);
}

bool CStdApp::SetVideoMode(unsigned int iXRes, unsigned int iYRes, unsigned int iColorDepth, unsigned int iMonitor, bool fFullScreen) {
	if (Priv->tasked_out)
		return false;
	bool modefound = false;
	if (fDspModeSet) {
		Priv->SwitchToDesktop(this, pWindow->wnd);
		fDspModeSet = false;
	}
	if (!fFullScreen) {
		XResizeWindow(dpy, pWindow->wnd, iXRes, iYRes);
		OnResolutionChanged(iXRes, iYRes);
		return true;
	}
	if (Priv->xf86vmode_targetmode.hdisplay == iXRes && Priv->xf86vmode_targetmode.vdisplay == iYRes)
		modefound = true;
	// randr spec says to always get fresh info, so don't cache.
	if (xrandr_major_version >= 0) {
		modefound = true;
		Priv->wdt = iXRes; Priv->hgt = iYRes;
	}
	if (xf86vmode_major_version >= 0 && !modefound) {
		// save desktop-resolution before switching modes
		// XF86VidMode has a really weird API.
		XF86VidModeGetModeLine(dpy, DefaultScreen(dpy), (int*)&Priv->xf86vmode_oldmode.dotclock,
			(XF86VidModeModeLine*)(((char *)&Priv->xf86vmode_oldmode) + sizeof(Priv->xf86vmode_oldmode.dotclock)));
		//Priv->oldmode = *modes[0];
		// Change resolution
		int mode_num;
		XF86VidModeModeInfo **modes;
		XF86VidModeGetAllModeLines(dpy, DefaultScreen(dpy), &mode_num, &modes);
		// look for mode with requested resolution
		for (int i = 0; i < mode_num; i++) {
			if ((modes[i]->hdisplay == iXRes) && (modes[i]->vdisplay == iYRes)) {
				if (!modefound) Priv->xf86vmode_targetmode = *modes[i];
				modefound = true;
			}
		}
		XFree(modes);
	}
	if (!modefound) return false;
	fDspModeSet = Priv->SwitchToFullscreen(this, pWindow->wnd);
	return fDspModeSet;
}

void CStdApp::RestoreVideoMode() {
	if (fDspModeSet) {
		Priv->SwitchToDesktop(this, pWindow->wnd);
		fDspModeSet = false;
		// Minimize
		if (pWindow->wnd) {
			XEvent e;
			e.xclient.type = ClientMessage;
			e.xclient.window = pWindow->wnd;
			e.xclient.message_type = XInternAtom(dpy, "WM_CHANGE_STATE", true);
			e.xclient.format = 32;
			e.xclient.data.l[0] = IconicState;
			XSendEvent(dpy, DefaultRootWindow(dpy), false, SubstructureRedirectMask | SubstructureNotifyMask, &e);
		}
	}
}

bool CStdApp::GetIndexedDisplayMode(int32_t iIndex, int32_t *piXRes, int32_t *piYRes, int32_t *piBitDepth, uint32_t iMonitor) {
	if (xf86vmode_major_version < 0) return false;
	bool r = false;
	int mode_num;
	XF86VidModeModeInfo **modes;
	XF86VidModeGetAllModeLines(dpy, DefaultScreen(dpy), &mode_num, &modes);
	if (iIndex < mode_num) {
		*piXRes = modes[iIndex]->hdisplay;
		*piYRes = modes[iIndex]->vdisplay;
		*piBitDepth = 32;
		r = true;
	}
	XFree(modes);
	return r;
}

void CStdAppPrivate::SetEWMHFullscreen (CStdApp * pApp, bool fFullScreen, Window wnd) {
	static Atom atoms[2];
	static const char * names[] = { "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN" };
	if (!atoms[0]) XInternAtoms(pApp->dpy, const_cast<char **>(names), 2, false, atoms);
	XEvent e;
	e.xclient.type = ClientMessage;
	e.xclient.window = wnd;
	e.xclient.message_type = atoms[0];
	e.xclient.format = 32;
	if (fFullScreen) {
		e.xclient.data.l[0] = 1; //_NET_WM_STATE_ADD
	} else {
		e.xclient.data.l[0] = 0; //_NET_WM_STATE_REMOVE
	}
	e.xclient.data.l[1] = atoms[1];
	e.xclient.data.l[2] = 0; //second property to alter
	e.xclient.data.l[3] = 1; //source indication
	e.xclient.data.l[4] = 0;
	XSendEvent(pApp->dpy, DefaultRootWindow(pApp->dpy), false, SubstructureNotifyMask | SubstructureRedirectMask, &e);
}

bool CStdAppPrivate::SwitchToFullscreen(CStdApp * pApp, Window wnd) {
	if (pApp->xrandr_major_version >= 0) {
		XRRScreenConfiguration * conf = XRRGetScreenInfo (pApp->dpy, wnd);
		xrandr_oldmode = XRRConfigCurrentConfiguration (conf, &xrandr_rot);
		int n;
		XRRScreenSize * sizes = XRRConfigSizes(conf, &n);
		for (int i = 0; i < n; ++i) {
			if (sizes[i].width == wdt && sizes[i].height == hgt) {
#ifdef _DEBUG
				LogF("XRRSetScreenConfig %d", i);
#endif
				XRRSetScreenConfig (pApp->dpy, conf, wnd, i, xrandr_rot, CurrentTime);
				break;
			}
		}
		XRRFreeScreenConfigInfo(conf);
		SetEWMHFullscreen(pApp, true, wnd);
	} else if (pApp->xf86vmode_major_version >= 0) {
		XF86VidModeModeInfo & mode = xf86vmode_targetmode;
		XResizeWindow(pApp->dpy, wnd, mode.hdisplay, mode.vdisplay);
		XSizeHints hints;
		hints.flags = PMinSize | PMaxSize;
		hints.min_width = mode.hdisplay;
		hints.min_height = mode.vdisplay;
		hints.max_width = mode.hdisplay;
		hints.max_height = mode.vdisplay;
		XSetWMNormalHints(pApp->dpy, wnd, &hints);
		// Changing not necessary
		if (!memcmp(&xf86vmode_targetmode, &xf86vmode_oldmode, sizeof(XF86VidModeModeInfo))) {
			// Set the window to fullscreen mode to get rid of window manager decorations
			SetEWMHFullscreen(pApp, true, wnd);
		} else {
			XF86VidModeSwitchToMode(pApp->dpy, DefaultScreen(pApp->dpy), &mode);
			// Move the viewport on the virtual screen
			Window bla; int wnd_x = 0; int wnd_y = 0;
			XTranslateCoordinates(pApp->dpy, wnd, DefaultRootWindow(pApp->dpy), 0, 0, &wnd_x, &wnd_y, &bla);
			XF86VidModeSetViewPort(pApp->dpy, DefaultScreen(pApp->dpy), wnd_x, wnd_y);
		}
	}
	XGrabPointer(pApp->dpy, wnd, true, 0, GrabModeAsync, GrabModeAsync, wnd, None, LastEventTime);
	pApp->OnResolutionChanged(wdt, hgt);
	return true;
}

void CStdAppPrivate::SwitchToDesktop(CStdApp * pApp, Window wnd) {
	XUngrabPointer(pApp->dpy, LastEventTime);
	// Restore resolution
	if (pApp->xrandr_major_version >= 0) {
		XRRScreenConfiguration * conf = XRRGetScreenInfo (pApp->dpy, wnd);
#ifdef _DEBUG
		LogF("XRRSetScreenConfig %d (back)", xrandr_oldmode);
#endif
		XRRSetScreenConfig (pApp->dpy, conf, wnd, xrandr_oldmode, xrandr_rot, CurrentTime);
		XRRFreeScreenConfigInfo(conf);
	} else if (pApp->xf86vmode_major_version >= 0) {
		XF86VidModeModeInfo & mode = xf86vmode_oldmode;
		XF86VidModeSwitchToMode(pApp->dpy, DefaultScreen(pApp->dpy), &mode);
		XF86VidModeSetViewPort(pApp->dpy, DefaultScreen(pApp->dpy), 0, 0);
	}
	XSizeHints hints;
	hints.flags = 0;
	XSetWMNormalHints(pApp->dpy, wnd, &hints);
	SetEWMHFullscreen(pApp, false, wnd);
}

// Copy the text to the clipboard or the primary selection
void CStdApp::Copy(const StdStrBuf & text, bool fClipboard) {
	CStdAppPrivate::ClipboardData & d = fClipboard ? Priv->ClipboardSelection : Priv->PrimarySelection;
	XSetSelectionOwner(dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY, pWindow->wnd, Priv->LastEventTime);
	Window owner = XGetSelectionOwner(dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY);
	if (owner != pWindow->wnd) return;
	d.Text.Copy(text);
	d.AcquirationTime = Priv->LastEventTime;
}
// Paste the text from the clipboard or the primary selection
StdStrBuf CStdApp::Paste(bool fClipboard) {
	Window owner = XGetSelectionOwner (dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY);
	if (owner == None) return StdStrBuf(0);
	// Retrieve the selection into the XA_STRING property of our main window
	XConvertSelection (dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY, XA_STRING, XA_STRING,
					pWindow->wnd, Priv->LastEventTime);
	// Give the owner some time to respond
	Priv->X11Proc.ExecuteUntil(50);
	// Get the length of the data, so we can request it all at once
	Atom type;
	int format;
	unsigned long len, bytes_left;
	unsigned char *data;
	XGetWindowProperty (dpy, pWindow->wnd,
		XA_STRING,  // property
		0, 0,	  	// offset - len
		0, 	 	    // do not delete it now
		AnyPropertyType, // flag
		&type,		  // return type
		&format,	  // return format
		&len, &bytes_left, //that
		&data);
	//printf ("type:%i len:%li format:%d byte_left:%ld\n", (int)type, len, format, bytes_left);
	// nothing to read?
	if (bytes_left == 0) return StdStrBuf(0);
	int result = XGetWindowProperty (dpy, pWindow->wnd,
			XA_STRING, 0, bytes_left,
			1, // delete it now
			AnyPropertyType,
			&type, &format, &len, &bytes_left, &data);
	if (result != Success) return StdStrBuf(0);
	StdStrBuf res (reinterpret_cast<char *>(data), true);
	XFree (data);
	return res;
}
// Is there something in the clipboard?
bool CStdApp::IsClipboardFull(bool fClipboard) {
	return None != XGetSelectionOwner (dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY);
}
// Give up Selection ownership
void CStdApp::ClearClipboard(bool fClipboard) {
	CStdAppPrivate::ClipboardData & d = fClipboard ? Priv->ClipboardSelection : Priv->PrimarySelection;
	if (!d.Text.getData()) return;
	XSetSelectionOwner(dpy, fClipboard ? XInternAtom(dpy,"CLIPBOARD",false) : XA_PRIMARY,
		None, d.AcquirationTime);
	d.Text.Clear();
}

CStdWindow * CStdAppPrivate::GetWindow(unsigned long wnd) {
	WindowListT::iterator i = WindowList.find(wnd);
	if (i != WindowList.end()) return i->second;
	return 0;
}
void CStdAppPrivate::SetWindow(unsigned long wnd, CStdWindow * pWindow) {
	if (!pWindow) {
		WindowList.erase(wnd);
	} else {
		WindowList[wnd] = pWindow;
	}
}

bool CStdApp::ReadStdInCommand() {
#if HAVE_LIBREADLINE
	rl_callback_read_char();
	return true;
#else
	// Surely not the most efficient way to do it, but we won't have to read much data anyway.
	char c;
	if(read(0, &c, 1) != 1)
		return false;
  if(c == '\n') {
		if(!CmdBuf.isNull()) {
			OnCommand(CmdBuf.getData()); CmdBuf.Clear();
		}
	} else if(isprint((unsigned char)c))
		CmdBuf.AppendChar(c);
	return true;
#endif
}
#if HAVE_LIBREADLINE
static void readline_callback (char * line) {
	if (!line) {
		readline_callback_use_this_app->Quit();
	} else {
		readline_callback_use_this_app->OnCommand(line);
	}
#if HAVE_READLINE_HISTORY
	if (line && *line) {
		add_history (line);
	}
#endif
	free(line);
}
#endif

void CStdApp::OnXInput()
{
	while (XEventsQueued(dpy, QueuedAfterReading)) {
		HandleXMessage();
	}
	if (Priv->pending_desktop) {
		RestoreVideoMode();
		fDspModeSet = false;
		Priv->tasked_out = true;
		Priv->pending_desktop = false;
	}
}

void CStdApp::OnPipeInput()
{
	char c;
	::read(Priv->Pipe[0], &c, 1);
	// call network class to handle it
	/*OnNetworkEvents();*/
}

void CStdApp::OnStdInInput()
{
	if(!ReadStdInCommand())
	{
		// TODO: This should only cause HandleMessage to return
		// HR_Failure...
		Quit();
	}
}

void CStdApp::MessageDialog(const char * message) {
#ifdef WITH_GLIB
	GtkWidget * dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
#endif
}
#endif /* USE_X11 */
