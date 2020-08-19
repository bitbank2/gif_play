//
// GIF Play
//
// Project started 6/19/2018
// Play animated GIF files directly onto a framebuffer or SPI LCD
// Written by Larry Bank
// bitbank@pobox.com
// Copyright (c) 2018 BitBank Software, Inc. All rights reserved.
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
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "pil.h"
#include "pil_io.h"

#ifdef SPI_LCD
#include <spi_lcd.h>
#endif

#define MAX_PATH 260
static char szIn[MAX_PATH];
int bCenter, iLoopCount;
int fbfd, iPitch;
char szDev[32];
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
long int screensize = 0;
char *fbp = 0;
static int bLCD;
extern void PILCountGIFPages(PIL_FILE *pFile);
extern int PILReadGIF(PIL_PAGE *pPage, PIL_FILE *pFile, int iRequestedPage);
extern int PILDecodeLZW(PIL_PAGE *pIn, PIL_PAGE *pOut, PILBOOL bGIF, int iOptions);
//
// Current time in milliseconds
//
int MilliTime(void)
{
int iTime;
struct timespec res;

	clock_gettime(CLOCK_MONOTONIC, &res);
	iTime = 1000*res.tv_sec + res.tv_nsec / 1000000;
	return iTime;
} /* MilliTime() */
//
// ShowHelp
//
// Display the help info when incorrect or no command line parameters are passed
//
void ShowHelp(void)
{
    printf(
	"gp - play GIF files directly onto the framebuffer"
	"usage: ./gp <options>\n"
	"valid options:\n\n"
        " --in <infile>       Input file\n"
	" --c                 Center on the display\n"
        " --dev <device>      Destination device (defaults to fb0), or lcd\n"
	" --loop N            Loop the animation N times\n"
    );
}
//
// Display the current GIF frame on the framebuffer
//
void ShowFrame(PIL_PAGE *pPage)
{
int tx, ty, cx, cy, x, y, w, h;
unsigned char *s, *d;

	tx = ty = 16;
	w = pPage->iWidth;
	h = pPage->iHeight;
	d = NULL;
	if (bCenter)
	{
		if (bLCD)
		{
#ifdef SPI_LCD
		if (w > 320) w = 320;
		if (h > 240) h = 240;
		cx = (320 - w)/2;
		cy = (240 - h)/2;
#else
		printf("Error: SPI_LCD support is not compiled in");
#endif
		}
		else
		{
		cx = (vinfo.xres - w) / 2; // center on page
		cy = (vinfo.yres - h) / 2;
		}
	}
	else
	{
		cx = cy = 0;
	}
	if (bLCD)
	{
#ifdef SPI_LCD
	for (y=0; y<h; y+=16)
	{
		ty = 16;
		if (h - y < 16)
			ty = h - y;
		for (x=0; x<w; x+=16)
		{
			s = pPage->pData + (y * pPage->iPitch) + x * 2;
			tx = 16;
			if (w - x < 16)
				tx = w - x;
			spilcdDrawTile(x+cx,y+cy, tx, ty, s, pPage->iPitch);
		}
	} // for y
#else
	printf("Error: SPI_LCD support is not compiled in");
#endif
	}
	else
	{
	d = (unsigned char *)fbp + (cy * iPitch) + ((cx * vinfo.bits_per_pixel)/8);	
	s = pPage->pData;
	for (y=0; y<pPage->iHeight; y++)
	{
		memcpy(d, s, pPage->iPitch);
		d += iPitch;
		s += pPage->iPitch;
	}
	}
} /* ShowFrame() */

static void parse_opts(int argc, char *argv[])
{
// set default options
int i = 1;

    bCenter = 0;
    bLCD = 0;
    iLoopCount = 1;
    strcpy(szDev, "fb0"); // destination frame buffer
    szIn[0] = '\0';

    while (i < argc)
    {
        /* if it isn't a cmdline option, we're done */
        if (0 != strncmp("--", argv[i], 2))
            break;
        /* GNU-style separator to support files with -- prefix
         * example for a file named "--baz": ./foo --bar -- --baz
         */
        if (0 == strcmp("--", argv[i]))
        {
            i += 1;
            break;
        }
        /* test for each specific flag */
        if (0 == strcmp("--in", argv[i])) {
            strcpy(szIn, argv[i+1]);
            i += 2;
	} else if (0 == strcmp("--dev", argv[i])) {
	    strcpy(szDev,argv[i+1]);
	    bLCD = (strcmp(szDev,"lcd") == 0);
            i += 2;
        } else if (0 == strcmp("--c", argv[i])) {
            i ++;
            bCenter = 1;
        } else if (0 == strcmp("--loop", argv[i])) {
            iLoopCount = atoi(argv[i+1]);
            i += 2;
	}  else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }
    }
    if (strlen(szIn) == 0)
    {
       printf("Must specify an input filename\n");
       exit(1);
    }
} /* parse_opts() */

int main( int argc, char *argv[ ], char *envp[ ] )
{
PIL_FILE pf;
PIL_PAGE pp1, pp2;
int err;
int i, rc, iLoop;
int iTime;
char szTemp[32];
void *pFile;

   if (argc < 2)
      {
      ShowHelp();
      return 0;
      }
   parse_opts(argc, argv);
	pFile = PILIOOpenRO(szIn);
	if (pFile != (void *)-1)
	{
		// read the file entirely into memory
		i = (int)PILIOSize(pFile);
		memset(&pf, 0, sizeof(pf));
		pf.iFileSize = i;
		pf.pData = PILIOAlloc(i);
		pf.cState = PIL_FILE_STATE_LOADED;
		//pf.cState = PIL_FILE_STATE_OPEN;
		pf.cFileType = PIL_FILE_GIF;
		pf.iFile = pFile;
		PILIORead(pFile, pf.pData, i);
		PILIOClose(pFile);
		if (memcmp(pf.pData,"GIF89",5) != 0) // not a GIF
		{
			printf("Not a GIF file\n");
			PILIOFree(pf.pData);
			return -1;
		}
		PILCountGIFPages(&pf);
   if (bLCD)
   {
#ifdef SPI_LCD
// LCD type, flip 180, SPI channel, D/C, RST, LCD
   rc = spilcdInit(LCD_ILI9342, 0, 0, 32000000, 13, 11, 18);
   if (rc != 0)
   {
	   printf("Error initializing LCD\n");
	   PILClose(&pf);
	   return -1;
   }
//   spilcdSetOrientation(LCD_ORIENTATION_ROTATED);
#else
	printf("Error: SPI_LCD support is not compiled in");
#endif
   }
   else
   {
   // Open the file for reading and writing
   sprintf(szTemp, "/dev/%s", szDev); // destination framebuffer (defaults to fb0)
   fbfd = open(szTemp, O_RDWR);
   if (fbfd <= 0) {
      printf("Error: cannot open framebuffer device %s; need to run as sudo?\n", szTemp);
      return 1 ;
   }
#ifdef DEBUG_LOG
   printf("The framebuffer device was opened successfully.\n");
#endif

  // Get fixed screen information
   if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
      printf("Error reading fixed information.\n");
      return 1;
   }
#ifdef DEBUG_LOG
printf("panning xstep=%d, ystep=%d, ywrap=%d (non-zero means it scan scroll)\n", finfo.xpanstep, finfo.ypanstep, finfo.ywrapstep);
printf("smem_len=%08x, line_length=%08x, mem can hold %d lines\n", finfo.smem_len, finfo.line_length, finfo.smem_len / finfo.line_length);
#endif

   // Get variable screen information
   if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
      printf("Error reading variable information.\n");
      return 1;
   }
#ifdef DEBUG_LOG
  printf("visible res %dx%d, virtual res %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.yres_virtual,
         vinfo.bits_per_pixel );
#endif

    // map framebuffer to user memory 
    screensize = finfo.smem_len;
    iPitch = (vinfo.xres * vinfo.bits_per_pixel) / 8;
    fbp = (char*)mmap(0, 
                    screensize, 
                    PROT_READ | PROT_WRITE, 
                    MAP_SHARED, 
                    fbfd, 0);

    if ((signed long)fbp == -1) {
       printf("Failed to mmap.\n");
       return 1;
    }
   } // !LCD

		// Read each frame one at a time
		memset(&pp2, 0, sizeof(pp2));
		pp2.iWidth = pf.iX;
		pp2.iHeight = pf.iY;
		if (bLCD)
#ifdef SPI_LCD
			pp2.cBitsperpixel = 16;
#else
			printf("Error: SPI_LCD support is not compiled in");
#endif
		else
			pp2.cBitsperpixel = vinfo.bits_per_pixel; // has to be same as display
		pp2.iPitch = (pp2.iWidth * pp2.cBitsperpixel)/8;
		pp2.pData = PILIOAlloc(pp2.iPitch * pp2.iHeight);
		pp2.iDataSize = pp2.iPitch * pp2.iHeight;
		pp2.cFlags = PIL_PAGEFLAGS_TOPDOWN;
		pp2.cCompression = PIL_COMP_NONE;
		pp2.pPalette = PILIOAlloc(2048);
		for (iLoop=0; iLoop<iLoopCount; iLoop++)
		{
		for (i=0; i<pf.iPageTotal; i++)
		{
		PIL_PAGE ppSrc;

			iTime = MilliTime(); // get the current time in milliseconds
//			printf("About to call PILReadGIF\n");
	                err = PILReadGIF(&pp1, &pf, i);
        	        if (err)
                	{       
                        	printf("PILReadGIF returned %d, datasize=%d\n", err, pp1.iDataSize);
                        	return -1;
                	}

			memset(&ppSrc, 0, sizeof(ppSrc));
			ppSrc.cCompression = PIL_COMP_NONE;
			err = PILDecodeLZW(&pp1, &ppSrc, 1, 0);
			if (err)
			{
				printf("PILDecodeLZW returned %d\n", err);
				return -1;
			}
			if (i == 0) // get global color table from first frame
			{
				memcpy(pp2.pPalette, ppSrc.pPalette, 768);
			}
//			printf("About to call PILAnimateGIF, framedelay = %d\n", pp2.iFrameDelay);
			err = PILAnimateGIF(&pp2, &ppSrc);
//			printf("returned from PILAnimateGIF\n");
			PILFree(&ppSrc);
			PILFree(&pp1);
			if (err == 0)
			{
				ShowFrame(&pp2);
				iTime = MilliTime() - iTime; // number of milliseconds that have passed so far for this frame
				iTime = pp1.iFrameDelay - iTime; // any time left for the frame delay?
				if (iTime > 0)
					usleep(iTime * 1000); // frame delay in ms (accounting for time spent decoding + displaying)
			}
			else
			{
				printf("Frame: %d, PILAnimate returned %d\n", i, err);
			}
		} // for each frame
		} // for each loop over the animation
		PILClose(&pf);
	} // if file loaded successfully
   return 0;
}
