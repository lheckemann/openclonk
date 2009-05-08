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

/* Log file handling */

#ifndef INC_C4Log
#define INC_C4Log

#include <StdBuf.h>
#include <StdCompiler.h>

BOOL OpenLog();
BOOL CloseLog();
/* Declared in Standard.h
BOOL LogSilent(const char *szMessage);
BOOL Log(const char *szMessage);
BOOL LogSilentF(const char *strMessage, ...) GNUC_FORMAT_ATTRIBUTE;
BOOL LogF(const char *strMessage, ...) GNUC_FORMAT_ATTRIBUTE;*/
BOOL DebugLog(const char *strMessage);
BOOL DebugLogF(const char *strMessage ...) GNUC_FORMAT_ATTRIBUTE;

bool LogFatal(const char *szMessage); // log message and store it as a fatal error
void ResetFatalError();               // clear any fatal error message
const char *GetFatalError();          // return message that was set as fatal error, if any

BOOL CompileError(bool fWarning, StdCompiler::Exception *Exc);
size_t GetLogPos(); // get current log position;
bool GetLogSection(size_t iStart, size_t iLength, StdStrBuf &rsOut); // re-read log data from file

// Used to print a backtrace after a crash
int GetLogFD();

#endif
