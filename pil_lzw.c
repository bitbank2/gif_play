//
// PIL - portable imaging library
// Written by Larry Bank
// Project started 12/9/2000
// A highly optimized imaging library designed for resource-constrained
// environments such as mobile/embedded devices
//
// pil_lzw.c - functions related to GIF/LZW compression
//
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
//#include "my_windows.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pil.h"
#include "pil_io.h"

/* GIF Defines and variables */
#define CTLINK 0
#define CTLAST 4096
#define CTFIRST 8192
#define CT_OLD 5911 // 0x1717 to use memset
#define CT_END 5912
#define MAX_HASH 5003
#define MAXMAXCODE 4096
unsigned char cGIFBits[9] = {1,4,4,4,8,8,8,8,8}; // convert odd bpp values to ones we can handle
unsigned char cGIFPass[8] = {8,0,8,4,4,2,2,1}; // GIF interlaced y delta

extern void PILFixGIFRGB(unsigned char *pPal);
extern void PILTIFFHoriz(PIL_PAGE *InPage, PILBOOL bDecode);
extern int PILReadAtOffset(PIL_FILE *pf, int iOffset, unsigned char *pDest, int iLen);
extern void PILFlushBits(BUFFERED_BITS *bb);
extern void PILTIFFHoriz_SIMD(PIL_PAGE *InPage, PILBOOL bDecode);

int PILReadGIF(PIL_PAGE *pPage, PIL_FILE *pFile, int iRequestedPage)
{
int iOffset, iErr, i, j, iMap;
int iDataLen, codestart;
unsigned char c, *d, *p, *pOldPage;

	iErr = 0;
         pPage->iStripCount = 0; // no strips
         iOffset = pPage->iOffset = 0;
         if (pFile->iPageTotal > 1)
            {
            if (pFile->cState == PIL_FILE_STATE_LOADED)
            pPage->iOffset = pFile->pPageList[iRequestedPage];
            pPage->iDataSize = pFile->pPageList[iRequestedPage+1] - pFile->pPageList[iRequestedPage];
            }
         else
            {
            pPage->iDataSize = pFile->iFileSize;
            }
         if (pPage->iDataSize <= 0 || pPage->iOffset < 0 || pPage->iOffset > pFile->iFileSize)
            {
            // something went very wrong
            iErr = PIL_ERROR_UNKNOWN;
            goto quit_gif;
            }
         pPage->cCompression = PIL_COMP_GIF;
         pPage->cFlags = PIL_PAGEFLAGS_TOPDOWN;
         if (pFile->cState == PIL_FILE_STATE_LOADED)
	 {
         	p = pOldPage = &pFile->pData[pPage->iOffset];
		pPage->pData = PILIOAlloc(pPage->iDataSize);
	 }
       	else
            p = pOldPage = &pPage->pData[pPage->iOffset];
         if (iRequestedPage == 0)
            {
            pPage->iPageWidth = INTELSHORT(&p[6]);
            pPage->iPageHeight = INTELSHORT(&p[8]);
            pPage->cBitsperpixel = (p[10] & 7) + 1;
            if (pPage->cBitsperpixel == 1)
                pFile->cPhotometric = PIL_PHOTOMETRIC_BLACKISZERO;
            else
                pFile->cPhotometric = PIL_PHOTOMETRIC_PALETTECOLOR;
            pPage->cBackground = p[11]; // background color
            iOffset = 13;
            if (p[10] & 0x80) // global color table?
               {
               if (pPage->cBitsperpixel == 1)
                  {
                  if (p[13])
                     pPage->cPhotometric = PIL_PHOTOMETRIC_WHITEISZERO;
                  else
                     pPage->cPhotometric = PIL_PHOTOMETRIC_BLACKISZERO;
                  }
			   pPage->pPalette = (unsigned char *) PILIOAlloc(768); /* Allocate fixed size color palette */
			   if (pPage->pPalette == NULL)
			   {
				   iErr = PIL_ERROR_MEMORY;
				   goto quit_gif;
			   }
               memcpy(pPage->pPalette, &p[iOffset], 3*(1 << pPage->cBitsperpixel));
               PILFixGIFRGB(pPage->pPalette); /* Fix RGB byte order */
               iOffset += 3 * (1 << pPage->cBitsperpixel);
               }
            }
         while (p[iOffset] != ',') /* Wait for image separator */
            {
            if (p[iOffset] == '!') /* Extension block */
               {
               iOffset++;
               switch(p[iOffset++]) /* Block type */
                  {
                  case 0xf9: /* Graphic extension */
                     if (p[iOffset] == 4) // correct length
                        {
                        pPage->cGIFBits = p[iOffset+1]; // packed fields
                        pPage->iFrameDelay = (INTELSHORT(&p[iOffset+2]))*10; // delay in ms
						if (pPage->iFrameDelay < 30) // 0-2 is going to make it run at 60fps; use 100 (10fps) as a reasonable substitute
							pPage->iFrameDelay = 100;
						if (pPage->cGIFBits & 1) // transparent color is used
                           pPage->iTransparent = (int)p[iOffset+4]; // transparent color index
                        iOffset += 6;
                        }
//                     else   // error
                     break;
                  case 0xff: /* App extension */
                     c = 1;
                     while (c) /* Skip all data sub-blocks */
                        {
                        c = p[iOffset++]; /* Block length */
                        if (c == 11) // fixed block length
                           { // Netscape app block contains the repeat count
                           if (memcmp(&p[iOffset], "NETSCAPE2.0", 11) == 0)
						      {
                              if (p[iOffset+11] == 3 && p[iOffset+12] == 1) // loop count
								  pPage->iRepeatCount = INTELSHORT(&p[iOffset+13]);
						      }
                           }
                        iOffset += (int)c; /* Skip to next sub-block */
                        }
                     break;
                  case 0x01: /* Text extension */
                     c = 1;
                     j = 0;
                     while (c) /* Skip all data sub-blocks */
                        {
                        c = p[iOffset++]; /* Block length */
                        if (j == 0) // use only first block
                           {
                           j = c;
                           if (j > 127)   // max comment length = 127
                              j = 127;
                           memcpy(pPage->szInfo1, &p[iOffset], j);
                           pPage->szInfo1[j] = '\0';
                           j = 1;
                           }
                        iOffset += (int)c; /* Skip this sub-block */
                        }
                     break;
                  case 0xfe: /* Comment */
                     c = 1;
                     j = 0;
                     while (c) /* Skip all data sub-blocks */
                        {
                        c = p[iOffset++]; /* Block length */
                        if (j == 0) // use only first block
                           {
                           j = c;
                           if (j > 127)   // max comment length = 127
                              j = 127;
                           memcpy(pPage->szComment, &p[iOffset], j);
                           pPage->szComment[j] = '\0';
                           j = 1;
                           }
                        iOffset += (int)c; /* Skip this sub-block */
                        }
                     break;
                  default:
                     iErr = PIL_ERROR_BADHEADER; /* Bad header info */
                     if (pPage->cBitsperpixel != 1)
                        PILIOFree(pPage->pPalette);
                     goto quit_gif;
                  } /* switch */
               }
            else // invalid byte, stop decoding
               {
               iErr = PIL_ERROR_BADHEADER; /* Bad header info */
               if (pPage->cBitsperpixel != 1)
                  PILIOFree(pPage->pPalette);
               goto quit_gif;
               }
            } /* while */
         if (p[iOffset] == ',')
            iOffset++;
         pPage->iX = INTELSHORT(&p[iOffset]);
         pPage->iY = INTELSHORT(&p[iOffset+2]);
         pPage->iWidth = INTELSHORT(&p[iOffset+4]);
         pPage->iHeight = INTELSHORT(&p[iOffset+6]);
         iOffset += 8;
   /* Image descriptor
     7 6 5 4 3 2 1 0    M=0 - use global color map, ignore pixel
     M I 0 0 0 pixel    M=1 - local color map follows, use pixel
                        I=0 - Image in sequential order
                        I=1 - Image in interlaced order
                        pixel+1 = # bits per pixel for this image
   */
         iMap = p[iOffset++];
         if (iMap & 0x80) // local color table?
            {
            if (iRequestedPage == 0 && pPage->pPalette == NULL) // no global color table defined, use local as global
               {
				pPage->pPalette = (unsigned char *) PILIOAlloc(768);
				if (pPage->pPalette == NULL)
				{
					iErr = PIL_ERROR_MEMORY;
					goto quit_gif;
				}
               i = 3 * (1 << ((iMap & 7)+1)); // get the size of the color table specified
               memcpy(pPage->pPalette, &p[iOffset], i);
               PILFixGIFRGB(pPage->pPalette); /* Fix RGB byte order */
               }
            else
               { // keep both global and local color tables
				if (pPage->pLocalPalette == NULL) // need to allocate it
				{
					pPage->pLocalPalette = (unsigned char *) PILIOAlloc(768);
					if (pPage->pLocalPalette == NULL)
					{
						iErr = PIL_ERROR_MEMORY;
						goto quit_gif;
					}
				}
               i = 3 * (1 << ((iMap & 7)+1)); // get the size of the color table specified
               memcpy(pPage->pLocalPalette, &p[iOffset], i);
               PILFixGIFRGB(pPage->pLocalPalette); /* Fix RGB byte order */
               }
            iOffset += i;
            }
         codestart = p[iOffset++]; /* initial code size */
         /* Since GIF can be 1-8 bpp, we only allow 1,4,8 */
         pPage->cBitsperpixel = cGIFBits[codestart];
         // we are re-using the same buffer turning GIF file data
         // into "pure" LZW
         d = pPage->pData;
         *d++ = (unsigned char)iMap; /* Store the map attributes */
         *d++ = (unsigned char)codestart; /* And the starting code */
         iDataLen = 2; /* Length of GIF data */
         c = p[iOffset++]; /* This block length */
         while (c && (iOffset+c) <= pPage->iDataSize) /* Collect all of the data packets together */
            {
            memcpy(d, &p[iOffset], c);
            iOffset += c;
            d += c;
            iDataLen += c; /* Overall data length */
            c = p[iOffset++];
            }
         if ((iOffset+c) > pPage->iDataSize) /* Error, we went beyond end of buffer */
            {
            PILIOFree(pPage->pData);
            pPage->pData = NULL;
            if (pPage->pPalette)
               {
               PILIOFree(pPage->pPalette);
               pPage->pPalette = NULL;
               }
            if (pPage->pLocalPalette)
               {
               PILIOFree(pPage->pLocalPalette);
               pPage->pLocalPalette = NULL;
               }
            iErr = PIL_ERROR_DECOMP;
            goto quit_gif;
            }
         pPage->iDataSize = iDataLen; /* Total bytes of GIF data */
         pPage->cState = PIL_PAGE_STATE_LOADED; // since we have to repack the data
         pPage->iOffset = 0; // since we repacked the data
         pPage->iPitch = PILCalcSize(pPage->iWidth, pPage->cBitsperpixel);
         pFile->cBpp = pPage->cBitsperpixel;
         pFile->cCompression = PIL_COMP_LZW;
quit_gif:
	return iErr;
} /* PILReadGIF() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILAnimateGIF()                                            *
 *                                                                          *
 *  PURPOSE    : BitBlt the new data onto the old page for animation (GIF). *
 *                                                                          *
 ****************************************************************************/
int PILAnimateGIF(PIL_PAGE *pDestPage, PIL_PAGE *pSrcPage)
{
#ifndef JPEG_DECODE_ONLY

unsigned char c, *s, *d, cTransparent;
int x, y;
unsigned char ucDisposalFlags;
//unsigned char *pTempLine;
//unsigned char *irlcptr;
//unsigned char **indexbuf;
unsigned char *pPalette;
unsigned char r, g, b;
unsigned short usColor, *ds, *pusPalette;
uint32_t ul, *pul;
uint32_t *pulPalette = NULL;

   pusPalette = NULL;
   if (pDestPage == NULL || pSrcPage == NULL)
	   return PIL_ERROR_INVPARAM;
   if (pDestPage->pData == NULL || pSrcPage->pData == NULL || pDestPage->pPalette == NULL) // must have destination buffer, src buffer & global color table
	   return PIL_ERROR_INVPARAM;
   if (pDestPage->cBitsperpixel != 24 && pDestPage->cBitsperpixel != 16 && pDestPage->cBitsperpixel != 32)
	   return PIL_ERROR_BITDEPTH;
   if (pSrcPage->iX < 0 || pSrcPage->iY < 0 || (pSrcPage->iX + pSrcPage->iWidth) > pDestPage->iWidth || (pSrcPage->iY + pSrcPage->iHeight) > pDestPage->iHeight)
         return PIL_ERROR_INVPARAM; // bad parameter

   pPalette = (unsigned char *)PILIOAlloc(2048); // use global or local palette
   if (pPalette == NULL)
      return PIL_ERROR_MEMORY;
   memcpy(pPalette, pDestPage->pPalette, 768); // start with the global color table
// get local color table changes (if present)
   if (pSrcPage->pLocalPalette) // use local palette
      memcpy(pPalette, pSrcPage->pLocalPalette, 768);
   // Create a RGB565 palette
   if (pDestPage->cBitsperpixel == 16)
   {
	   ds = pusPalette = (unsigned short *)&pPalette[1024];
	   for (x=0; x<256; x++)
	   {
		   usColor = pPalette[x*3]>>3; // b
		   usColor |= ((pPalette[(x*3)+1]>>2)<<5); // g
		   usColor |= ((pPalette[(x*3)+2]>>3)<<11); // r
		   *ds++ = usColor;
	   }
   }
   else if (pDestPage->cBitsperpixel == 32)
   {
	   pul = pulPalette = (uint32_t *)&pPalette[1024];
	   for (x=0; x<256; x++)
	   {
           ul = 0xff000000;
		   ul |= (pPalette[(x*3)+2]<<16); // b
		   ul |= (pPalette[(x*3)+1]<<8); // g
		   ul |= pPalette[(x*3)+0]; // r
		   *pul++ = ul;
	   }
   }

// Dispose of the last frame according to the previous page disposal flags
	ucDisposalFlags = (pDestPage->cGIFBits & 0x1c)>>2; // bits 2-4 = disposal flags
	switch (ucDisposalFlags)
	{
	case 0: // not specified - nothing to do
	case 1: // do not dispose
		break;
	case 2: // restore to background color
		if (pDestPage->cBitsperpixel == 24)
		{
		   b = pPalette[pDestPage->cBackground * 3];
		   g = pPalette[(pDestPage->cBackground * 3) + 1];
		   r = pPalette[(pDestPage->cBackground * 3) + 2];
		   if (pDestPage->cBackground == pDestPage->iTransparent) // transparent background, default to white
		      {
		      r = b = g = 0xff;
		      }
		   for (y=0; y<pDestPage->iCY; y++)
			  {
			  d = pDestPage->pData + (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 3);
			  for (x=0; x<pDestPage->iCX; x++)
				 {
				 *d++ = b;
				 *d++ = g;
				 *d++ = r;
				 }
			  }
		}
		else if (pDestPage->cBitsperpixel == 32)
		{
           ul = 0xff000000;
		   ul |= (pPalette[pDestPage->cBackground * 3]<<16);
		   ul |= (pPalette[(pDestPage->cBackground * 3) + 1]<<8);
		   ul |= pPalette[(pDestPage->cBackground * 3) + 2];
		   if (pDestPage->cBackground == pDestPage->iTransparent) // transparent background, default to white
		      {
		      ul = 0xffffffff;
		      }
		   for (y=0; y<pDestPage->iCY; y++)
			  {
			  pul = (uint32_t *)(pDestPage->pData + (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX << 2));
			  for (x=0; x<pDestPage->iCX; x++)
				 {
				 *pul++ = ul;
				 }
			  }
		}
		else if (pDestPage->cBitsperpixel == 16)// 16bpp
		{
		   usColor = pusPalette[pDestPage->cBackground];
		   if (pDestPage->cBackground == pDestPage->iTransparent) // transparent background, default to white
		      {
		      usColor = 0xffff;
		      }
		   for (y=0; y<pDestPage->iCY; y++)
			  {
			  ds = (unsigned short *)&pDestPage->pData[(pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 2)];
			  for (x=0; x<pDestPage->iCX; x++)
				 {
				 *ds++ = usColor;
				 }
			  }
		}
		break;
	case 3: // restore to previous frame
	   if (pDestPage->lUser) // if we saved it
	      {
	      for (y=0; y<pDestPage->iCY; y++)
	         {
			 if (pDestPage->cBitsperpixel == 24)
				 {
				 d = pDestPage->pData + (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 3);
				 s = (unsigned char *)pDestPage->lUser;
				 s += (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 3);
				 memcpy(d, s, pDestPage->iCX * 3);
				 }
			 if (pDestPage->cBitsperpixel == 32)
				 {
				 d = pDestPage->pData + (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX << 2);
				 s = (unsigned char *)pDestPage->lUser;
				 s += (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX << 2);
				 memcpy(d, s, pDestPage->iCX << 2);
				 }
			 else if (pDestPage->cBitsperpixel == 16)
				{
				 d = &pDestPage->pData[(pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 2)];
				 s = (unsigned char *)pDestPage->lUser;
				 s += (pDestPage->iPitch * (pDestPage->iY + y)) + (pDestPage->iX * 2);
				 memcpy(d, s, pDestPage->iCX * 2);
				}
	         }
	      }
		break;
	default: // not defined
		break;
	}

 // if this frame uses disposal method 3, we need to prepare for the next frame
 // by saving the current image before it gets modified
 	if (((pSrcPage->cGIFBits & 0x1c)>>2) == 3)
 	   {
      // Copy this frame to a swap buffer so that the disposal method 3 can work
      // this can be done more efficiently (memory, time), but keep it simple for now
      if (pDestPage->lUser == NULL) // not allocated yet
         {
		  pDestPage->lUser = (void *) PILIOAlloc(pDestPage->iDataSize);
		 if (pDestPage->lUser == NULL)
			 return PIL_ERROR_MEMORY;
         }
      memcpy((void *)pDestPage->lUser, pDestPage->pData, pDestPage->iDataSize);
      }

   switch (pSrcPage->cBitsperpixel)
      {
      case 4:
         // Draw new sub-image onto animation bitmap
         if (pSrcPage->cGIFBits & 1) // if transparency used
            {
            cTransparent = (unsigned char)pSrcPage->iTransparent;
            }
         else
            {
            cTransparent = 16; // a value which will never match
            }
         for (y=0; y<pSrcPage->iHeight; y++)
            {
            s = pSrcPage->pData + (y * pSrcPage->iPitch);
			if (pDestPage->cBitsperpixel == 24)
			    {
				d = pDestPage->pData + (pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 3);
				for (x=0; x<pSrcPage->iWidth; x++)
				   {
				   if (!(x & 1))
					  c = ((*s)>> 4) & 0xf;
				   else
					  c = (*s++) & 0xf;
				  if (c != cTransparent)
					 {
					 *d++ = pPalette[c*3];
					 *d++ = pPalette[(c*3)+1];
					 *d++ = pPalette[(c*3)+2];
					 }
				  else
					 {
					 d += 3;
					 }
				   } // for x
			    }
			else if (pDestPage->cBitsperpixel == 16)
			    {
				ds = (unsigned short *)&pDestPage->pData[(pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 2)];
				for (x=0; x<pSrcPage->iWidth; x++)
				   {
				   if (!(x & 1))
					  c = ((*s)>> 4) & 0xf;
				   else
					  c = (*s++) & 0xf;
				  if (c != cTransparent)
					 {
					 *ds++ = pusPalette[c];
					 }
				  else
					 {
					 ds++;
					 }
				   } // for x
		     	}
			else if (pDestPage->cBitsperpixel == 32)
			    {
				pul = (uint32_t *)&pDestPage->pData[(pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 4)];
				for (x=0; x<pSrcPage->iWidth; x++)
				   {
				   if (!(x & 1))
					  c = ((*s)>> 4) & 0xf;
				   else
					  c = (*s++) & 0xf;
				  if (c != cTransparent)
					 {
					 *pul++ = pulPalette[c];
					 }
				  else
					 {
					 pul++;
					 }
				   } // for x
		     	}
            } // for y
         break;
      case 8:
         // Draw new sub-image onto animation bitmap
         if (pSrcPage->cGIFBits & 1) // if transparency used
            {
            cTransparent = (unsigned char)pSrcPage->iTransparent;
            for (y=0; y<pSrcPage->iHeight; y++)
               {
               s = pSrcPage->pData + (y * pSrcPage->iPitch);
			   if (pDestPage->cBitsperpixel == 24)
			       {
				   d = pDestPage->pData + (pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 3);
				   for (x=0; x<pSrcPage->iWidth; x++)
					  {
					  c = *s++;
					  if (c != cTransparent)
						 {
						 *d++ = pPalette[c*3];
						 *d++ = pPalette[(c*3)+1];
						 *d++ = pPalette[(c*3)+2];
						 }
					  else
						 {
						 d += 3;
						 }
					  } // for x
				   }
 			    else if (pDestPage->cBitsperpixel == 32)
			  	   {
				   pul = (uint32_t *)&pDestPage->pData[(pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 4)];
				   for (x=0; x<pSrcPage->iWidth; x++)
					  {
					  c = *s++;
					  if (c != cTransparent)
						 {
						 *pul++ = pulPalette[c];
						 }
					  else
						 {
						 pul++;
						 }
					  } // for x
			       }
 			    else if (pDestPage->cBitsperpixel == 16)
			  	   {
				   ds = (unsigned short *)&pDestPage->pData[(pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 2)];
				   for (x=0; x<pSrcPage->iWidth; x++)
					  {
					  c = *s++;
					  if (c != cTransparent)
						 {
						 *ds++ = pusPalette[c];
						 }
					  else
						 {
						 ds++;
						 }
					  } // for x
			       }
               } // for y
            }
         else // no transparency
            {
            for (y=0; y<pSrcPage->iHeight; y++)
               {
               s = pSrcPage->pData + (y * pSrcPage->iPitch);
			   if (pDestPage->cBitsperpixel == 24)
			       {
				   d = pDestPage->pData + (pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 3);
				   for (x=0; x<pSrcPage->iWidth; x++)
					  {
					  c = *s++;
					  *d++ = pPalette[c*3];
					  *d++ = pPalette[(c*3)+1];
					  *d++ = pPalette[(c*3)+2];
					  } // for x
				   }
			   else if (pDestPage->cBitsperpixel == 32)
			       {
				   pul = (uint32_t *)(pDestPage->pData + (pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 4));
				   for (x=0; x<pSrcPage->iWidth; x++)
					  {
					  c = *s++;
					  *pul++ = pulPalette[c];
					  } // for x
				   }
			   else if (pDestPage->cBitsperpixel == 16)
			       {
			       uint32_t ul, *pul;
				   pul = (uint32_t *)&pDestPage->pData[(pDestPage->iPitch * (pSrcPage->iY + y)) + (pSrcPage->iX * 2)];
                   x = 0;
                   if ((pSrcPage->iX & 1) == 0) // must be dword-aligned
                      {
				      for (; x<pSrcPage->iWidth-1; x+=2)
					     {
					     ul = pusPalette[*s++];
					     ul |= (pusPalette[*s++] << 16);
					     *pul++ = ul;
					     } // for x
                      }
                   ds = (unsigned short *)pul;
				   for (; x<pSrcPage->iWidth; x++) // odd starting point and/or odd width
				      {
				      *ds++ = pusPalette[*s++];
				      }
			       }
               } // for y
            }
         break;
      }
#endif // JPEG_DECODE_ONLY

// need to hold last frame info for posible "disposition" on the next frame
   pDestPage->cGIFBits = pSrcPage->cGIFBits;
   pDestPage->iFrameDelay = pSrcPage->iFrameDelay;
   pDestPage->iX = pSrcPage->iX;
   pDestPage->iY = pSrcPage->iY;
   pDestPage->iCX = pSrcPage->iWidth;
   pDestPage->iCY = pSrcPage->iHeight;
   PILIOFree(pPalette); // free temp palette
   return 0;

} /* PILAnimateGIF() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILMakeGifPels(signed short *, unsigned char *, short, int *, PILBOOL) *
 *                                                                          *
 *  PURPOSE    : Convert a linked list of codes into pixel data.            *
 *                                                                          *
 ****************************************************************************/
unsigned char *PILMakeGifPels(unsigned short *giftabs, unsigned char *templine, unsigned char *linebuf, unsigned int code, int *xcount, unsigned char *buf, int *y, PIL_PAGE *pPage, unsigned char **irlcptr, int lsize, PILBOOL bGIF)
{
int i, k, iPixCount;
unsigned char *s, *pEnd;
//unsigned char **index = NULL;

//   if (pPage->cBitsperpixel == 1)
//      index = (unsigned char **)pPage->pData;

   /* Copy this string of sequential pixels to output buffer */
//   iPixCount = 0;
   s = linebuf + 65536; /* Pixels will come out in reversed order */

   while (code < CT_OLD)
      {
      if (s == linebuf) /* Houston, we have a problem */
         {
         return NULL; /* Exit with error */
         }
      *(--s) = (unsigned char)giftabs[CTLAST + code];
      code = giftabs[CTLINK + code];
      }
   iPixCount = (int)(intptr_t)(linebuf + 65536 - s);

   while (iPixCount && *y > 0)
      {
      if (*xcount > iPixCount)  /* Pixels fit completely on the line */
         {
         if (pPage->cBitsperpixel == 4 && bGIF)
            {
            k = pPage->iWidth - *xcount; /* Current x */
            for (i=0; i < iPixCount; i++)
               {
               if ((k & 1) == 0)
                  *buf = (*s++) << 4;
               else
                  *buf++ |= *s++;
               k++;
               }
            }
         else
            {
//            memcpy(buf, s, iPixCount);
//            buf += iPixCount;
            pEnd = buf + iPixCount;
#ifdef _X86
			while (buf < pEnd - 3) // at least 4 bytes to copy
			{
				*(uint32_t *) buf = *(uint32_t *) s;
				buf += 4; s += 4;
			}
#endif
			while (buf < pEnd)
               {
               *buf++ = *s++;
               }
            }
         *xcount -= iPixCount;
//         iPixCount = 0;
         return buf;
         }
      else  /* Pixels cross into next line */
         {
         if (pPage->cBitsperpixel == 4 && bGIF)
            {
            k = pPage->iWidth - *xcount; /* Current x */
            for (i=0; i < *xcount; i++)
               {
               if ((k & 1) == 0)
                  *buf = (*s++) << 4;
               else
                  *buf++ |= *s++;
               k++;
               }
            }
         else
            {
//            memcpy(buf, s, *xcount);
//            buf += *xcount;
//            s += *xcount;
            pEnd = buf + *xcount;
            while (buf < pEnd)
               {
               *buf++ = *s++;
               }
            }
         iPixCount -= *xcount;
         if (!bGIF)
            {
            if (pPage->cFlags & PIL_PAGEFLAGS_PLANAR) // planar data treated differently
               *xcount = pPage->iWidth;
            else
               *xcount = PILCalcBSize(pPage->iWidth, pPage->cBitsperpixel);
            }
         else
            *xcount = pPage->iWidth; /* Reset pixel count */
         if (bGIF)
            buf += lsize - PILCalcBSize(pPage->iWidth, pPage->cBitsperpixel); /* Advance to next line */
         if (pPage->cBitsperpixel == 4 && bGIF && (pPage->iWidth & 1) == 1)
            buf++;
         (*y)--;
         }
      } /* while */
   return buf;
} /* PILMakeGifPels() */

#define SYM_OFFSETS 0x0000
#define SYM_LENGTHS 0x1000
#define SYM_EXTRAS  0x2000

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : LZWCopyBytes()                                             *
 *                                                                          *
 *  PURPOSE    : Output the bytes for a single code (checks for buffer len) *
 *                                                                          *
 ****************************************************************************/
int LZWCopyBytes(unsigned char *buf, int iOffset, int iUncompressedLen, uint32_t *pSymbols)
{
int iLen;
unsigned char *s, *d;
#ifdef _X86
int iTempLen;
#else
unsigned char *pEnd;
#endif

    iLen = pSymbols[SYM_LENGTHS];
    // Make sure data does not write past end of buffer (which does occur frequently)
    if (iLen > (iUncompressedLen - iOffset))
       iLen = iUncompressedLen - iOffset;
    s = &buf[pSymbols[SYM_OFFSETS]];
    d = &buf[iOffset];
#ifdef _X86
	iTempLen = iLen;
	while (iTempLen > 0) // most frequent are 1-3 bytes in length, copy 4 bytes in these cases too
	{
		*(uint32_t *)d = *(uint32_t *) s;
		d += 4; s += 4;
		iTempLen -= 4;
	}
	d += iTempLen; // in case we overshot
#else
	pEnd = &d[iLen];
	while (d < pEnd)
       *d++ = *s++;
#endif
	if (pSymbols[SYM_EXTRAS] != -1) // was a newly used code
	{
		*d = buf[pSymbols[SYM_EXTRAS]];
		iLen++;
		// since the code with extension byte has now been written to the output, fix the code
		pSymbols[SYM_OFFSETS] = iOffset;
		pSymbols[SYM_EXTRAS] = 0xffffffff;
		pSymbols[SYM_LENGTHS]++;
//		pSymbols[SYM_LENGTHS] = iLen; - this is slower on x86, but faster on ARM
	}
    return iLen;

} /* LZWCopyBytes() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILDecodeLZW()                                             *
 *                                                                          *
 *  PURPOSE    : Decompress an LZW image.                                   *
 *                                                                          *
 ****************************************************************************/
int PILDecodeLZW(PIL_PAGE *InPage, PIL_PAGE *OutPage, PILBOOL bGIF, int iOptions)
{
	int i, y, iTotalY, xcount;
	int bitnum, bitoff, lsize, iMap;
	int iDelta, iStripNum;
	unsigned short oldcode, codesize, nextcode, nextlim;
	unsigned short *giftabs, cc, eoi;
	signed short sMask;
	unsigned char *pOutPtr, *irlcptr, *p, *buf, *linebuf, *templine, codestart;
//	int iStripSize;
	unsigned char **index;
	int iEndRow;
#ifdef _64BITS
	uint64_t ulBits;
#else
	uint32_t ulBits;
#endif
	unsigned short code;
	PILBOOL bMoreStrips = FALSE;
	int iPlanarAdjust = 1;
	// if output can be used for string table, do it faster
//	   if (bGIF && (OutPage->cBitsperpixel == 8 && ((OutPage->iWidth & 3) == 0)))
//	      return PILFastLZW(InPage, OutPage, bGIF, iOptions);

	irlcptr = pOutPtr = NULL; // suppress compiler warning
	index = NULL;

	if (InPage->pPalette != NULL)
	{
		OutPage->pPalette = PILIOAlloc(768);
		memcpy(OutPage->pPalette, InPage->pPalette, 768); // copy to destination page
	}
	if (InPage->pLocalPalette != NULL)
	{
		OutPage->pLocalPalette = PILIOAlloc(768);
		memcpy(OutPage->pLocalPalette, InPage->pLocalPalette, 768);
	}
	OutPage->cBitsperpixel = InPage->cBitsperpixel;
	OutPage->iWidth = InPage->iWidth;
	OutPage->iHeight = InPage->iHeight;
	OutPage->iFrameDelay = InPage->iFrameDelay;
	OutPage->iPitch = PILCalcSize(InPage->iWidth, InPage->cBitsperpixel);

	/* Code limit is different for TIFF and GIF */
	if (!bGIF)
	{
		iDelta = 1;
		//      if (OutPage->cBitsperpixel == 1) // TIFF LZW has the photometric inverted for our use
		//         OutPage->cPhotometric = 1 - OutPage->cPhotometric;
	}
	else
		iDelta = 0;
	templine = NULL; /* Suppress compiler warning */
	OutPage->iOffset = 0; // new data offset is 0 (for our uncompressed output)
	if (InPage->cBitsperpixel == 1)
	{
		OutPage->pData = (unsigned char *) PILIOAllocNoClear((InPage->iHeight * 4) + ((InPage->iWidth*InPage->iHeight) / 4)); /* Allocate the irlc buffer structure */
		index = (unsigned char **) OutPage->pData;
		if (index == NULL)
			return PIL_ERROR_MEMORY;
		OutPage->iLinesDecoded = OutPage->iRowCount; // kludge for stripped TIFF images to handle last strip if less than total lines needed
		irlcptr = (unsigned char *) &index[InPage->iHeight]; // irlc data begins immediately after index
		lsize = (InPage->iWidth + 7) >> 3; /* Bytes per line */
		templine = (unsigned char *) PILIOAllocNoClear(InPage->iWidth);
		if (templine == NULL)
		{
			PILIOFree(OutPage->pData);
			OutPage->pData = NULL;
			return PIL_ERROR_MEMORY;
		}
		buf = templine; /* Output buffer is a temporary line */
	}
	else /* Color image */
	{
		if (!bGIF)
		{
			if (InPage->cPhotometric == PIL_PHOTOMETRIC_YCBCR) // special case for YCbCr images
			{
				int iMCU = 3; // assume 1:1 subsampling
				if (InPage->cJPEGSubSample == 0x21) // horizontal only
					iMCU = 4; // 2 Y, 1 Cb, 1Cr
				else // we only support 2:1 and 2:2 subsampling
					iMCU = 6; // 4 Y, 1 Cb, 1Cr
				lsize = InPage->iWidth * iMCU;
				if (InPage->cJPEGSubSample == 0x22)
					lsize /= 2;
			}
			else
			{
				lsize = PILCalcBSize(InPage->iWidth, InPage->cBitsperpixel); // must be TIFF
			}
		}
		else
			lsize = PILCalcSize(InPage->iWidth, InPage->cBitsperpixel);
		i = lsize * (InPage->iHeight + 1); /* color bitmap size - can leak past the end of the last line*/
		if (!(iOptions & PIL_CONVERT_NOALLOC))
			OutPage->pData = (unsigned char *) PILIOAlloc(i); /* Output buffer is actual bitmap */
		OutPage->iDataSize = i;
		if (OutPage->pData == NULL)
			return PIL_ERROR_MEMORY;
		buf = pOutPtr = OutPage->pData;
		if (!bGIF && InPage->cFlags & PIL_PAGEFLAGS_PLANAR) // different rules for planar image; one plane = width bytes
			lsize = InPage->iWidth;
	}
	p = &InPage->pData[InPage->iOffset];
	iStripNum = 0;
	iTotalY = 0; // for checking multi-strip images
	iEndRow = InPage->iHeight;
//	iStripSize = InPage->iDataSize;
	if (InPage->iStripCount)
	{
//		iStripSize = InPage->plStripSize[0];
		p = &InPage->pData[InPage->plStrips[0]];
		iEndRow = InPage->iRowCount;
	}

	if (!bGIF)
	{
		codestart = 8; /* Always 8 bits for TIFF LZW */
		bitoff = 0;
		iMap = 0; /* Don't let it get accidentally un-interlaced */
	}
	else
	{
		iMap = p[0]; /* Get the GIF flags */
		codestart = p[1]; /* Starting code size */
		bitoff = 2; /* Offset into data */
	}
	sMask = -1 << (codestart + 1);
	sMask = 0xffff - sMask;
	cc = (sMask >> 1) + 1; /* Clear code */
	eoi = cc + 1;
	linebuf = (unsigned char *) PILIOAlloc(65536);
	giftabs = (unsigned short *)PILIOAlloc(33000);  /* 3 8K dictionary tables with extra space */
	if (linebuf == NULL || giftabs == NULL)
	{
		PILIOFree(OutPage->pData);
		OutPage->pData = NULL;
		PILIOFree(templine);
		PILIOFree(linebuf);
		PILIOFree(giftabs);
		return PIL_ERROR_MEMORY;
	}
	OutPage->iOffset = 0; // used for strip counts on bilevel images
	if (InPage->cFlags & PIL_PAGEFLAGS_PLANAR && InPage->iStripCount > InPage->iHeight) // special case where each strip is a partial line
	{
		iPlanarAdjust = InPage->cBitsperpixel / 8; // number of planes
		InPage->iHeight *= iPlanarAdjust;
		bMoreStrips = TRUE; // fool it into decoding n * height lines, otherwise we'll just decode red
	}
lzwdoitagain: /* Go here to do more strips */
	if (!bGIF)
	{
		if (InPage->cPhotometric == PIL_PHOTOMETRIC_YCBCR) // special case for YCbCr images
			xcount = lsize;
		else
			xcount = PILCalcBSize(InPage->iWidth, InPage->cBitsperpixel);
		if (InPage->cFlags & PIL_PAGEFLAGS_PLANAR) // different rules for planar image; one plane = width bytes
			xcount = InPage->iWidth;
	}
	else
		xcount = InPage->iWidth;
	bitnum = 0;
	// Initialize code table
	// this part only needs to be initialized once
	for (i = 0; i < cc; i++)
	{
		giftabs[CTFIRST + i] = giftabs[CTLAST + i] = (unsigned short) i;
		giftabs[CTLINK + i] = CT_END;
	}
	if (InPage->cPhotometric == PIL_PHOTOMETRIC_YCBCR && (InPage->cJPEGSubSample & 2) == 2) // subsampled images have different pitch requirements
		y = iEndRow/2; // count down
	else
		y = iEndRow;
init_codetable:
	codesize = codestart + 1;
	sMask = -1 << (codestart + 1);
	sMask = 0xffff - sMask;
	nextcode = cc + 2;
	nextlim = (unsigned short) ((1 << codesize) - iDelta);
	// This part of the table needs to be reset multiple times
	memset(&giftabs[CTLINK + cc], CT_OLD, (4096 - cc)*sizeof(short));
	oldcode = CT_END;
	code = CT_END;
#ifdef _64BITS
	if (bGIF)
		ulBits = INTELEXTRALONG(&p[bitoff]);
	else
		ulBits = MOTOEXTRALONG(&p[bitoff]);
#else
	if (bGIF)
		ulBits = INTELLONG(&p[bitoff]);
	else
		ulBits = MOTOLONG(&p[bitoff]);
#endif
	while (code != eoi && y > 0 && y < InPage->iHeight+1) /* Loop through all lines of the image (or strip) */
	{
		if (!bGIF)
		{
			if (bitnum > (REGISTER_WIDTH - codesize))
			{
				bitoff += (bitnum >> 3);
				bitnum &= 7;
#ifdef _64BITS
				ulBits = MOTOEXTRALONG(&p[bitoff]);
#else
				ulBits = MOTOLONG(&p[bitoff]);
#endif
			}
			code = (unsigned short) (ulBits >> (REGISTER_WIDTH - codesize - bitnum));
			code &= sMask;
		}
		else
		{
			if (bitnum > (REGISTER_WIDTH - codesize))
			{
				bitoff += (bitnum >> 3);
				bitnum &= 7;
#ifdef _64BITS
				ulBits = INTELEXTRALONG(&p[bitoff]);
#else
				ulBits = INTELLONG(&p[bitoff]);
#endif
			}
			code = (unsigned short) (ulBits >> bitnum); /* Read a 32-bit chunk */
			code &= sMask;
		}
		bitnum += codesize;
		if (code == cc) /* Clear code?, and not first code */
			goto init_codetable;
		if (code != eoi)
		{
			if (oldcode != CT_END)
			{
				if (nextcode < nextlim) // for deferred cc case, don't let it overwrite the last entry (fff)
				{
					giftabs[CTLINK + nextcode] = oldcode;
					giftabs[CTFIRST + nextcode] = giftabs[CTFIRST + oldcode];
					if (giftabs[CTLINK + code] == CT_OLD) /* Old code */
						giftabs[CTLAST + nextcode] = giftabs[CTFIRST + oldcode];
					else
						giftabs[CTLAST + nextcode] = giftabs[CTFIRST + code];
				}
				nextcode++;
				if (nextcode >= nextlim && codesize < 12)
				{
					codesize++;
					nextlim <<= 1;
					nextlim += (unsigned short) iDelta; /* TIFF LZW irregularity */
					sMask = (sMask << 1) | 1;
				}
			}
			buf = PILMakeGifPels(giftabs, templine, linebuf, code, &xcount, buf, &y, OutPage, &irlcptr, lsize, bGIF);
			if (buf == NULL)
				goto lzwnextline; /* Leave with error */
			oldcode = code;
		}
	} /* while not end of LZW code stream */
lzwnextline:
	if (InPage->iStripCount && y >= 0) /* If there are multiple strips (TIFF only) */
	{
		//       if (InPage->iStripCount > 1 && InPage->cFlags & PIL_PAGEFLAGS_PLANAR && InPage->iRowCount == InPage->iHeight)
		//          y = iEndRow; // need to reset this when there are 3 strips (1 for each plane)
		if (InPage->cBitsperpixel == 1)
			buf = templine; // Reset to start of line
		else
		{
			if (InPage->cPhotometric == PIL_PHOTOMETRIC_YCBCR && (InPage->cJPEGSubSample & 2) == 2) // subsampled images have different pitch requirements
				pOutPtr += (lsize * InPage->iRowCount / 2);
			else
				pOutPtr += (lsize * InPage->iRowCount); // point to start of next strip
			buf = pOutPtr;
		}
		iStripNum++;
		//       if (iStripNum == 0x91)
		//          iStripNum |= 0;
		if (InPage->cBitsperpixel == 1 && y > 0 && iStripNum < InPage->iStripCount) // data stopped short in a multistrip bilevel image; we can't handle this case
			goto gif_forced_error;

		iTotalY += iEndRow;
		if (InPage->cFlags & PIL_PAGEFLAGS_PLANAR) // we decode the whole height N times, so the total lines is not relevant
		{
			if (iTotalY > InPage->iHeight)
				iTotalY = InPage->iHeight;
		}
		else
		{
			if (iTotalY + iEndRow > InPage->iHeight) // last strip has too many rows
			{
				iEndRow = InPage->iHeight - iTotalY;
				OutPage->iLinesDecoded = iEndRow; // fewer lines decoded (needed for MakeGIFPels() )
			}
		}
		if (InPage->cBitsperpixel == 1)
			OutPage->iOffset = iStripNum; // for bilevel images
		p = &InPage->pData[InPage->plStrips[iStripNum]];
		bitoff = 0;
		//       iEndRow += InPage->iRowCount;
		//       if (iEndRow > InPage->iHeight)
		//          iEndRow = InPage->iHeight;
		if (iStripNum < InPage->iStripCount)
			goto lzwdoitagain; /* keep decoding... */
	}
	if (y > 0 && (iTotalY - y) != InPage->iHeight && (!(iOptions & PIL_CONVERT_IGNORE_ERRORS)))
		goto giferror; // short page, report error
gifshort:
		OutPage->cCompression = PIL_COMP_NONE;
		if (iMap & 0x40) /* Interlaced? */
		{ /* re-order the scan lines */
			unsigned char *buf2;
			int iDest, iGifPass = 0;
			i = lsize * InPage->iHeight; /* color bitmap size */
			buf = (unsigned char *) OutPage->pData; /* reset ptr to start of bitmap */
			buf2 = (unsigned char *) PILIOAlloc(i);
			if (buf2 == NULL)
			{
				PILIOFree(giftabs);
				PILIOFree(linebuf);
				if (!(iOptions & PIL_CONVERT_NOALLOC))
					PILIOFree(OutPage->pData); /* Free the image buffer */
				return PIL_ERROR_MEMORY;
			}
			y = 0;
			for (i = 0; i<InPage->iHeight; i++)
			{
				iDest = y * lsize;
				bitoff = i * lsize;
				memcpy(&buf2[iDest], &buf[bitoff], lsize);
				y += cGIFPass[iGifPass * 2];
				if (y >= InPage->iHeight)
				{
					iGifPass++;
					y = cGIFPass[iGifPass * 2 + 1];
				}
			}
			PILIOFree(buf); /* Free the old buffer */
			i = lsize * InPage->iHeight; /* color bitmap size */
			OutPage->pData = buf2; /* Replace with corrected bitmap */
		} /* If interlaced GIF */
	PILIOFree(giftabs);
	PILIOFree(linebuf);
    // Planes must be merged BEFORE applying the predictor
//    if (InPage->cFlags & PIL_PAGEFLAGS_PLANAR) // need to convert to chunky
//    {
//        i = PILFixPlanar(OutPage);
//        if (i != PIL_ERROR_SUCCESS)
//            return i; // error
//    }
	if (!bGIF && InPage->cFlags & PIL_PAGEFLAGS_PREDICTOR) /* Check for horizontal differencing */
	{
		PILTIFFHoriz(OutPage, TRUE); /* Perform horizontal differencing */
	}
	if (InPage->cBitsperpixel == 16 || InPage->cBitsperpixel == 48 || InPage->cBitsperpixel == 64)
	{ // 16-bits per sample
		unsigned char *s, *d;
		int j;
		s = d = OutPage->pData;
		if ((InPage->cFlags & PIL_PAGEFLAGS_MOTOROLA) == 0)
			s++; // point to high byte
		for (i = 0; i<InPage->iHeight; i++)
		{
			for (j = 0; j<InPage->iPitch / 2; j++)
			{
				*d++ = *s;
				s += 2;
			}
		}
		OutPage->cBitsperpixel = InPage->cBitsperpixel >> 1; // 16->8
		InPage->cBitsperpixel = OutPage->cBitsperpixel;
		lsize = OutPage->iPitch = PILCalcBSize(OutPage->iWidth, OutPage->cBitsperpixel);
		OutPage->iDataSize = OutPage->iPitch * OutPage->iHeight;
		if (OutPage->cBitsperpixel == 8 && OutPage->pPalette == NULL)
		{
			OutPage->pPalette = PILGrayPalette(8);
		}
	}
	if (bMoreStrips) // odd planar image
	{
		InPage->iHeight /= iPlanarAdjust; // set it back to the original height
	}
	/* Swap red and blue */
//	if (!bGIF && InPage->cBitsperpixel == 24 && (InPage->cFlags & PIL_PAGEFLAGS_PLANAR) == 0 && InPage->cPhotometric == PIL_PHOTOMETRIC_RGB)
//	{
//		for (y = 0; y<InPage->iHeight; y++)
//		{
//			p = (unsigned char *) OutPage->pData + lsize*y;
//			PILFixTIFFRGB(p, InPage);
//		}
//	}
//	if (InPage->cPhotometric == PIL_PHOTOMETRIC_YCBCR)
//	{
//		return PILFixYCbCr(OutPage);
//	}
//	if (InPage->cPhotometric == PIL_PHOTOMETRIC_CIELAB1 || InPage->cPhotometric == PIL_PHOTOMETRIC_CIELAB2) // Convert/fix CIE L*a*b* colorspace uncompressed images
//	{
//		return PILFixCIELAB(OutPage);
//	}
	return 0;
giferror:
	if (iOptions & PIL_CONVERT_IGNORE_ERRORS) // suppress the error
		goto gifshort;
gif_forced_error:
	PILIOFree(templine);
	PILIOFree(giftabs);
	PILIOFree(linebuf);
	if (!(iOptions & PIL_CONVERT_NOALLOC))
		PILIOFree(OutPage->pData); /* Free the image buffer */
	return PIL_ERROR_DECOMP;
} /* PILDecodeLZW() */

/****************************************************************************
 *                                                                          *
 *  FUNCTION   : PILCountGIFPages()                                         *
 *                                                                          *
 *  PURPOSE    : Determine the number of pages in a GIF file.               *
 *                                                                          *
 ****************************************************************************/
void PILCountGIFPages(PIL_FILE *pFile)
{
int iOff, iNumPages;
int iReadAmount;
int iDataAvailable = 0;
int iDataRemaining = 0;
int iBufferSize, iHighWater;
uint32_t lFileOff = 0;
PILBOOL bDone = FALSE;
PILBOOL bExt;
unsigned char c, *cBuf;

    iBufferSize = 0x100000; // 1MB should be good
    iHighWater = iBufferSize - 512;
   iNumPages = 0;
   iDataRemaining = pFile->iFileSize;
   pFile->pPageList = (int *)PILIOAlloc(MAX_PAGES * sizeof(int));
   if (pFile->pPageList == NULL)
	   return;
//   pFile->pSoundList = PILIOAlloc(MAX_PAGES * sizeof(int)); // page delays
   pFile->pPageList[iNumPages++] = 0; /* First page starts at 0 */
   if (pFile->cState == PIL_FILE_STATE_LOADED) // use provided pointer
      {
      cBuf = pFile->pData;
	  iDataAvailable = pFile->iFileSize;
      }
   else
      {
	   cBuf = (unsigned char *) PILIOAlloc(iBufferSize);
	  iDataAvailable = PILReadAtOffset(pFile, 0, cBuf, iBufferSize); // read some data to start
      }
   iOff = 6;
   pFile->iX = cBuf[iOff] + (cBuf[iOff+1]<<8); // get width
   iOff += 2;
   pFile->iY = cBuf[iOff] + (cBuf[iOff+1]<<8); // get height
   iOff += 2;
   c = cBuf[iOff]; // get info bits
   pFile->cBpp = cGIFBits[(c & 7)]; // bits per pixel of the page (converted to supported values)
   iOff += 3;   /* Skip flags, background color & aspect ratio */
   if (c & 0x80) /* Deal with global color table */
      {
      c &= 7;  /* Get the number of colors defined */
      iOff += (2<<c)*3; /* skip color table */
      }
   while (!bDone && iNumPages < MAX_PAGES)
      {
//	  pFile->pSoundList[iNumPages] = 0; // assume no delay for this frame
      bExt = TRUE; /* skip extension blocks */
      while (bExt && iOff < iDataAvailable)
         {
         switch(cBuf[iOff])
            {
            case 0x3b: /* End of file */
               /* we were fooled into thinking there were more pages */
               iNumPages--;
               goto gifpagesz;
// F9 = Graphic Control Extension (fixed length of 4 bytes)
// FE = Comment Extension
// FF = Application Extension
// 01 = Plain Text Extension
            case 0x21: /* Extension block */
               if (cBuf[iOff+1] == 0xf9 && cBuf[iOff+2] == 4) // Graphic Control Extension
               {
            	   uint32_t ul;
            	   // pack the 4 bytes into a long
            	   ul = cBuf[iOff+3]; // page disposition flags
            	   ul |= (cBuf[iOff+4] << 8); // delay low byte
            	   ul |= (cBuf[iOff+5] << 16); // delay high byte
            	   ul |= (cBuf[iOff+6] << 24); // transparent color index
//             	   pFile->pSoundList[iNumPages] = (int)ul;
               }
               iOff += 2; /* skip to length */
               iOff += (int)cBuf[iOff]; /* Skip the data block */
               iOff++;
               // block terminator or optional sub blocks
               c = cBuf[iOff++]; /* Skip any sub-blocks */
               while (c && iOff < (iDataAvailable - c))
                  {
                  iOff += (int)c;
                  c = cBuf[iOff++];
                  }
               if (c != 0) // problem, we went past the end
                  {
                  iNumPages--; // possible corrupt data; stop
                  goto gifpagesz;
                  }
               break;
            case 0x2c: /* Start of image data */
               bExt = FALSE; /* Stop doing extension blocks */
               break;
            default:
               /* Corrupt data, stop here */
               iNumPages--;
               goto gifpagesz;
            }
         }
      if (iOff >= iDataAvailable) // problem
         {
         iNumPages--; // possible corrupt data; stop
         goto gifpagesz;
         }
      /* Start of image data */
      c = cBuf[iOff+9]; /* Get the flags byte */
      iOff += 10; /* Skip image position and size */
      if (c & 0x80) /* Local color table */
         {
         c &= 7;
         iOff += (2<<c)*3;
         }
      iOff++; /* Skip LZW code size byte */
      c = cBuf[iOff++];
      while (c) /* While there are more data blocks */
         {
         if (pFile->cState != PIL_FILE_STATE_LOADED && iOff > iHighWater && iDataRemaining > 0) /* Near end of buffer, re-align */
            {
            lFileOff += iOff; /* adjust total file pointer */
            iDataRemaining -= iOff;
            iReadAmount = (iDataRemaining > iBufferSize) ? iBufferSize:iDataRemaining;
			iDataAvailable = PILReadAtOffset(pFile, lFileOff, cBuf, iReadAmount); // read a new block
            iOff = 0; /* Start at beginning of buffer */
            }
         iOff += (int)c;  /* Skip this data block */
         if ((int)lFileOff + iOff > pFile->iFileSize) // past end of file, stop
            {
            iNumPages--; // don't count this page
            break; // last page is corrupted, don't use it
            }
         c = cBuf[iOff++]; /* Get length of next */
         }
      /* End of image data, check for more pages... */
      if (((int)lFileOff + iOff > pFile->iFileSize) || cBuf[iOff] == 0x3b)
         {
         bDone = TRUE; /* End of file has been reached */
         }
      else /* More pages to scan */
         {
         pFile->pPageList[iNumPages++] = lFileOff + iOff;
         // read new page data starting at this offset
         if (pFile->cState != PIL_FILE_STATE_LOADED &&
            pFile->iFileSize > iBufferSize && iDataRemaining > 0) // since we didn't read the whole file in one shot
            {
            lFileOff += iOff; /* adjust total file pointer */
            iDataRemaining -= iOff;
			iReadAmount = (iDataRemaining > iBufferSize) ? iBufferSize : iDataRemaining;
			iDataAvailable = PILReadAtOffset(pFile, lFileOff, cBuf, iReadAmount); // read a new block
            iOff = 0; /* Start at beginning of buffer */
            }
         }
      } /* while !bDone */
gifpagesz:
      pFile->pPageList[iNumPages] = pFile->iFileSize; /* Store end of file for length calc */
      pFile->iPageTotal = iNumPages;
      if (iNumPages == 1) // no need for pagelist structure
         {
         PILIOFree(pFile->pPageList);
         pFile->pPageList = NULL;
         }
      if (pFile->cState != PIL_FILE_STATE_LOADED)
         PILIOFree(cBuf); // free the temp buffer

} /* PILCountGIFPages() */

