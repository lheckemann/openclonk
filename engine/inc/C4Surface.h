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

/* Extension to CSurface that handles bitmaps in C4Group files */

#ifndef INC_C4Surface
#define INC_C4Surface

#include <StdSurface2.h>

class C4Group;
class C4GroupSet;

class C4Surface: public CSurface
	{
	private:
		C4Surface(const C4Surface &rCpy);		// do NOT copy
		C4Surface &operator = (const C4Surface &rCpy);	// do NOT copy
	public:
		C4Surface() : CSurface() {};	// ctor
#ifdef _DEBUG
		~C4Surface();
#endif

		C4Surface *Duplicate();	// create identical copy

		BOOL LoadAny(C4Group &hGroup, const char *szFilename, bool fOwnPal=false, bool fNoErrIfNotFound=false);
		BOOL LoadAny(C4GroupSet &hGroupset, const char *szFilename, bool fOwnPal=false, bool fNoErrIfNotFound=false);
		BOOL Load(C4Group &hGroup, const char *szFilename, bool fOwnPal=false, bool fNoErrIfNotFound=false);
		BOOL Save(C4Group &hGroup, const char *szFilename);
		BOOL SavePNG(C4Group &hGroup, const char *szFilename, bool fSaveAlpha=true, bool fApplyGamma=false, bool fSaveOverlayOnly=false);
		BOOL Copy(C4Surface &fromSfc);
		BOOL ReadPNG(CStdStream &hGroup);
		bool ReadJPEG(CStdStream &hGroup);
	};

#endif
