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

/* A wrapper to DirectSound - derived from DXSDK samples */

struct CSoundObject;

CSoundObject *DSndObjCreate(BYTE *bpWaveBuf, int iConcurrent = 1);
BOOL DSndObjPlay(CSoundObject *hSO, DWORD dwPlayFlags);
BOOL DSndObjStop(CSoundObject *hSO);
BOOL DSndObjPlaying(CSoundObject *hSO);
void DSndObjDestroy(CSoundObject *hSO);
BOOL DSndObjSetVolume(CSoundObject *pSO, long lVolume);

#define DSBPLAY_LOOPING 0x00000001
