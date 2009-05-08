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

/* Main class to execute the game fullscreen mode */

#ifndef INC_C4FullScreen
#define INC_C4FullScreen

#include "C4MainMenu.h"
#include <StdWindow.h>

#ifndef BIG_C4INCLUDE
#include "C4Game.h"
#endif

bool IsKeyDown(int iKey);

class C4FullScreen: public CStdWindow
	{
	public:
		C4MainMenu *pMenu;
	public:
		C4FullScreen();
		~C4FullScreen();
		void Execute();
		BOOL Restart();
		BOOL ViewportCheck();
		BOOL OpenGame(const char *szCmdLine);
		bool ShowAbortDlg(); // show game abort dialog (Escape pressed)
		bool ActivateMenuMain();
		BOOL MenuCommand(const char *szCommand);
		void CloseMenu();
		bool MenuKeyControl(BYTE byCom); // direct keyboard callback
		// User requests close
		virtual void Close();
		virtual void CharIn(const char * c);
#ifdef USE_X11
		virtual void HandleMessage (XEvent &e);
#elif USE_SDL_MAINLOOP
		virtual void HandleMessage (SDL_Event &e);
#endif
	};

extern C4FullScreen		FullScreen;

#endif
