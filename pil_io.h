/*********************************************************/
/* Generic I/O and memory routines for Linux             */
/*********************************************************/
// Copyright 2018 BitBank Software, Inc. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//===========================================================================

#ifndef _PIL_IO_H_
#define _PIL_IO_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef PILBOOL
#define PILBOOL int
#endif // PILBOOL

// PILHALError Is a typedef that is equivalent to the native environment's
// filesystem error code
typedef void * PILHALError;
typedef signed long PILOffset;
//typedef signed long long int PILOffset;

// OS independent date structure
typedef struct pil_date_tag
{
	int iYear;
	int iMonth;
	int iDay;
	int iHour;
	int iMinute;
	int iSecond;
} PIL_DATE;

#ifndef TCHAR
#define TCHAR char
#endif

extern PILBOOL PILIOExists(void *szName);
extern unsigned long PILIOSize(void *iHandle);
extern int PILIOMsgBox(TCHAR *, TCHAR *);
extern void * PILIOOpen(void *);
extern void * PILIOOpenRO(void *);
extern void * PILIOCreate(TCHAR *);
extern int PILIODelete(TCHAR *);
extern int PILIORename(TCHAR *, TCHAR *);
extern unsigned long PILIOSeek(void *, unsigned long, int);
extern signed int PILIORead(void *, void *, unsigned int);
extern unsigned int PILIOWrite(void *, void *, unsigned int);
extern void PILIOClose(void *);
void * PILIOAlloc(unsigned long size);
void * PILIOReAlloc(void *, unsigned long size);
void * PILIOAllocNoClear(unsigned long size);
void * PILIOAllocOutbuf(void);
int PILIONumProcessors(void);
int PILIOCreateThread(void *pFunc, void *pStruct, int iAffinity);
void PILIOSleep(int iTime);
    
//extern void *PILIOAllocInternal(unsigned long size, char *pu8Module, int iLineNumber, PILBOOL iClearBlock);
//#define	PILIOAlloc(x)				PILIOAllocInternal(x, __FILE__, __LINE__, TRUE)
//#define PILIOAllocNoClear(x)		PILIOAllocInternal(x, __FILE__, __LINE__, FALSE)
//#define PILIOAllocOutbufInternal()	PILIOAllocInternal(MAX_SIZE, __FILE__, __LINE__, TRUE)
extern void PILIOFree(void *);
extern void PILIOFreeOutbuf(void *);
extern void PILIOSignalThread(unsigned long dwTID, unsigned int iMsg, unsigned long wParam, unsigned long lParam);
extern void PILAssertHandlerProc(char *pExpression, char *pFile, unsigned long int ulLineNumber);
// Assertions
#define	PILASSERT(expr)	if (!(expr)) { PILAssertHandlerProc(#expr, __FILE__, (unsigned long int) __LINE__); }
void PILIODate(PIL_DATE *pDate);

#ifdef __cplusplus
}
#endif

#endif // #ifndef _PIL_IO_H_
