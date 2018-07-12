//
// PIL - portable imaging library
// Copyright (c) 2000-2018 BitBank Software, Inc.
// Written by Larry Bank
// Project started 12/9/2000
// A highly optimized imaging library designed for resource-constrained
// environments such as mobile/embedded devices
//

#ifndef __cplusplus
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#else
//#include "my_windows.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
//#include <zlib.h>
#endif
#endif // __cplusplus
#include <pil_io.h>
#include <pil.h>

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILClose()                                                 *
 *                                                                          *
 *  PURPOSE    : Close an image file.                                       *
 *                                                                          *
 ****************************************************************************/
int PILClose(PIL_FILE *pFile)
{

   if (pFile->cState == PIL_FILE_STATE_OPEN)
      {
      PILIOClose(pFile->iFile);
      pFile->iFile = (void *)-1;
      }
#ifdef USE_JPEG_CODEC
   if (pFile->pJPEG)
      {
      PILFreeHuffTables(pFile->pJPEG);
      PILIOFree(pFile->pJPEG);
      pFile->pJPEG = NULL;
      }
#endif
   if (pFile->pSoundList)
      {
      PILIOFree(pFile->pSoundList);
      pFile->pSoundList = NULL;
      }
   if (pFile->pKeyFlags)
      {
      PILIOFree(pFile->pKeyFlags);
      pFile->pKeyFlags = NULL;
      }
   if (pFile->pSoundLens)
      {
      PILIOFree(pFile->pSoundLens);
      pFile->pSoundLens = NULL;
      }
   if (pFile->pPageList)
      {
      PILIOFree(pFile->pPageList);
      pFile->pPageList = NULL;
      }
   if (pFile->pPageLens)
      {
      PILIOFree(pFile->pPageLens);
      pFile->pPageLens = NULL;
      }
   if (pFile->lUser != NULL && pFile->lUser != (void *)-1)
      {
	  PILIOFree((void *) pFile->lUser);
	  pFile->lUser = NULL;
      }
   pFile->cState = PIL_FILE_STATE_CLOSED; // reset state flags
   return 0;
} /* PILClose() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILFree()                                                  *
 *                                                                          *
 *  PURPOSE    : Free the page                                              *
 *                                                                          *
 ****************************************************************************/
int PILFree(PIL_PAGE *pPage)
{
int iErr = 0;
int i;

   if (pPage == NULL)
      return PIL_ERROR_INVPARAM;

   i = 0; // total memory blocks freed
   if (pPage->pData)
      {
      PILIOFree(pPage->pData);
      pPage->pData = NULL;
      i++;
      }
	if (pPage->pPalette)
	    {
        PILIOFree(pPage->pPalette);
        pPage->pPalette = NULL;
		i++;
	    }
	if (pPage->pLocalPalette)
	    {
	    PILIOFree(pPage->pLocalPalette);
	    pPage->pLocalPalette = NULL;
        i++;
	    }
   if (pPage->pAnnotations)
      {
      PILIOFree(pPage->pAnnotations);
      pPage->pAnnotations = NULL;
      i++;
      }
   if (pPage->pMeta)
      {
      PILIOFree(pPage->pMeta);
      pPage->pMeta = NULL;
      i++;
      }
   if (pPage->lUser)
      {
//#ifdef _WIN32
//      if (!IsBadReadPtr((void *)pPage->lUser, 4))
//#endif
//         {
         PILIOFree((void *)pPage->lUser);
         pPage->lUser = NULL;
//         }
      }
   if (pPage->plStrips)
      {
      PILIOFree(pPage->plStrips);
      pPage->plStrips = NULL;
      i++;
      }
   if (pPage->plStripSize)
      {
      PILIOFree(pPage->plStripSize);
      pPage->plStripSize = NULL;
      i++;
      }
   pPage->iStripCount = 0;
   if (i == 0) // nothing to free
      iErr = PIL_ERROR_INVPARAM;
   return iErr;

} /* PILFree() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILFixGIFRGB()                                             *
 *                                                                          *
 *  PURPOSE    : Swap RED and BLUE palette order for GIF file.              *
 *                                                                          *
 ****************************************************************************/
void PILFixGIFRGB(unsigned char *p)
{
int i;
unsigned char c;

   for(i=0; i<256; i++)
      {
      c = p[0];  /* Swap red and blue */
      p[0] = p[2];
      p[2] = c;
      p += 3;
      }

} /* PILFixGIFRGB() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILCalcSize(int, int)                                      *
 *                                                                          *
 *  PURPOSE    : Calculate the cache-aligned line size in bytes.            *
 *                                                                          *
 ****************************************************************************/
int PILCalcSize(int x, int bpp)
{
int i;

   switch(bpp)
      {
      case 1:
         i = (x + 31) >> 3;
         i &= 0xfffc;
         break;
      case 2:
         i = (x + 3) >> 2;
         i = (i + 3) & 0xfffc;
         break;
      case 4:
         i = (x + 1) >> 1;
         i = (i+3) & 0xfffc;
         break;
      case 8:
         i = (x + 3) & 0xfffc;
         break;
      case 15:
      case 16:
      case 17:
         i = x * 2;
         i = (i+3) & 0xfffc;
//         i = (i + 31) & 0xffe0;
         break;
      case 24:
         i = x * 3;
         i = (i+3) & 0xfffc;
//         i = (i + 31) & 0xffe0;
         break;
      case 32:
         i = x * 4;
         break;
      default: /* odd bit densities */
         i = (x * bpp) >> 3;
         i = (i+3) & 0xfffc;
         break;
      }
   return i;

} /* PILCalcSize() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILCalcBSize(int, int)                                     *
 *                                                                          *
 *  PURPOSE    : Calculate the byte-aligned line size in bytes.             *
 *                                                                          *
 ****************************************************************************/
int PILCalcBSize(int x, int bpp)
{
int i;

   switch(bpp)
      {
      case 1:
         i = (x + 7) >> 3;
         break;
      case 2:
         i = (x + 3) >> 2;
         break;
      case 4:
         i = (x + 1) >> 1;
         break;
      case 8:
         i = x;
         break;
      case 15:
      case 16:
      case 17:
         i = x * 2;
         break;
      case 24:
         i = x * 3;
         break;
      case 32:
         i = x * 4;
         break;
      default: // for odd bit depths
         i = (x*bpp)>>3;
         break;
      }
   return i;

} /* PILCalcBSize() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILTIFFHoriz(PIL_PAGE *, PILBOOL)                             *
 *                                                                          *
 *  PURPOSE    : Perform horizontal differencing on TIFF LZW data.          *
 *                                                                          *
 ****************************************************************************/
void PILTIFFHoriz(PIL_PAGE *InPage, PILBOOL bDecode)
{
unsigned char *p, *buf;
int i, y;
int lsize;

   buf = &InPage->pData[InPage->iOffset];
   lsize = PILCalcBSize(InPage->iWidth, InPage->cBitsperpixel);

   if (bDecode)
      {
      if (InPage->cBitsperpixel == 24)
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * lsize;
	    for (i = 0; i<(InPage->iWidth-1); i++)
               {
               p[3] += p[0];
               p[4] += p[1];
               p[5] += p[2];
               p += 3;
               }
            }
         }
      else if (InPage->cBitsperpixel == 32)
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * lsize;
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               p[4] += p[0];
               p[5] += p[1];
               p[6] += p[2];
               p[7] += p[3];
               p += 4;
               }
            }
         }
      else if (InPage->cBitsperpixel == 48)
         {
         unsigned short *pus;
         for (y = 0; y<InPage->iHeight; y++)
            {
            pus = (unsigned short *)&buf[y * lsize];
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               pus[3] += pus[0];
               pus[4] += pus[1];
               pus[5] += pus[2];
               pus += 3;
               }
            }
         }
      else if (InPage->cBitsperpixel == 64)
         {
         unsigned short *pus;
         for (y = 0; y<InPage->iHeight; y++)
            {
            pus = (unsigned short *)&buf[y * lsize];
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               pus[4] += pus[0];
               pus[5] += pus[1];
               pus[6] += pus[2];
               pus[7] += pus[3];
               pus += 4;
               }
            }
         }
      else // must be 8 or 16-bit grayscale
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * lsize;
            for (i = 0; i<(InPage->iPitch-1); i++)
               {
               p[1] += p[0];
               p++;
               }
            }
         }
      }
   else // encoding
      {
      if (InPage->cBitsperpixel == 24)
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * InPage->iPitch;
            p += (InPage->iWidth-1) * 3;
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               p[0] -= p[-3];
               p[1] -= p[-2];
               p[2] -= p[-1];
               p -= 3;
               }
            }
         }
      else if (InPage->cBitsperpixel == 32)
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * lsize;
            p += (InPage->iWidth-1) * 4;
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               p[0] -= p[-4];
               p[1] -= p[-3];
               p[2] -= p[-2];
               p[3] -= p[-1];
               p -= 4;
               }
            }
         }
      else
         {
         for (y = 0; y<InPage->iHeight; y++)
            {
            p = buf + y * lsize;
            p += (InPage->iWidth-1);
            for (i = 0; i<(InPage->iWidth-1); i++)
               {
               p[0] -= p[-1];
               p--;
               }
            }
         }
      }
} /* PILTIFFHoriz() */

int PILReadAtOffset(PIL_FILE *pf, int iOffset, unsigned char *pDest, int iLen)
{
	int iDataRead = 0;

	if ((unsigned int)iOffset > (unsigned int)pf->iFileSize || iOffset < 0 || pDest == NULL) // trying to read past the end of the file or into a null pointer
		return 0;
	if (iOffset + iLen > pf->iFileSize)
		iLen = pf->iFileSize - iOffset;

	if (pf->cState == PIL_FILE_STATE_LOADED) // we have everything in memory
	{
		memcpy(pDest, &pf->pData[iOffset], iLen);
		iDataRead = iLen;
	}
	else // need to read it from the file
	{
		PILIOSeek(pf->iFile, (PILOffset) iOffset, 0);
		iDataRead = PILIORead(pf->iFile, pDest, iLen);
	}
	return iDataRead;
} /* PILReadAtOffset() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILGrayPalette(int)                                        *
 *                                                                          *
 *  PURPOSE    : Allocate a 4 or 8bpp grayscale palette.                    *
 *                                                                          *
 ****************************************************************************/
unsigned char * PILGrayPalette(int iBpp)
{
int i, iVal, iDelta;
unsigned char *p, *d;

   iVal = 0;
   if (iBpp == 8)
      iDelta = 1;
   else if (iBpp == 4)
      iDelta = 0x11;
   else
      iDelta = 0x3f;
   d = p = (unsigned char *) PILIOAlloc(780); /* Allocate memory for a palette */
   if (d == NULL)
	   return NULL;
   for (i=0; i<256; i++)
      {
      *d++ = (unsigned char)iVal;  /* RGB values are all equal for gray */
      *d++ = (unsigned char)iVal;
      *d++ = (unsigned char)iVal;
      iVal += iDelta;
      }
   return p;

} /* PILGrayPalette() */
