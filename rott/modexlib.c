/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef DOS
#include <malloc.h>
#include <dos.h>
#include <conio.h>
#include <io.h>
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include "modexlib.h"
//MED
#include "memcheck.h"
#include "rt_util.h"
#include "rt_net.h" // for GamePaused
#include "myprint.h"

static void StretchMemPicture ();
// GLOBAL VARIABLES

boolean StretchScreen=0;//bn�++
extern boolean iG_aimCross;
extern boolean sdl_fullscreen;
extern int iG_X_center;
extern int iG_Y_center;
char 	   *iG_buf_center;
  
int    linewidth;
//int    ylookup[MAXSCREENHEIGHT];
int    ylookup[600];//just set to max res
byte  *page1start;
byte  *page2start;
byte  *page3start;
int    screensize;
byte  *bufferofs;
byte  *displayofs;
boolean graphicsmode=false;
char        *bufofsTopLimit;
char        *bufofsBottomLimit;

void DrawCenterAim ();

#ifdef DOS

/*
====================
=
= GraphicsMode
=
====================
*/
void GraphicsMode ( void )
{
union REGS regs;

regs.w.ax = 0x13;
int386(0x10,&regs,&regs);
graphicsmode=true;
}

/*
====================
=
= SetTextMode
=
====================
*/
void SetTextMode ( void )
{

union REGS regs;

regs.w.ax = 0x03;
int386(0x10,&regs,&regs);
graphicsmode=false;

}

/*
====================
=
= TurnOffTextCursor
=
====================
*/
void TurnOffTextCursor ( void )
{

union REGS regs;

regs.w.ax = 0x0100;
regs.w.cx = 0x2000;
int386(0x10,&regs,&regs);

}

#if 0
/*
====================
=
= TurnOnTextCursor
=
====================
*/
void TurnOnTextCursor ( void )
{

union REGS regs;

regs.w.ax = 0x03;
int386(0x10,&regs,&regs);

}
#endif

/*
====================
=
= WaitVBL
=
====================
*/
void WaitVBL( void )
{
   unsigned char i;

   i=inp(0x03da);
   while ((i&8)==0)
      i=inp(0x03da);
   while ((i&8)==1)
      i=inp(0x03da);
}


/*
====================
=
= VL_SetLineWidth
=
= Line witdh is in WORDS, 40 words is normal width for vgaplanegr
=
====================
*/

void VL_SetLineWidth (unsigned width)
{
   int i,offset;

//
// set wide virtual screen
//
   outpw (CRTC_INDEX,CRTC_OFFSET+width*256);

//
// set up lookup tables
//
   linewidth = width*2;

   offset = 0;

   for (i=0;i<iGLOBAL_SCREENHEIGHT;i++)
      {
      ylookup[i]=offset;
      offset += linewidth;
      }
}

/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void VL_SetVGAPlaneMode ( void )
{
    GraphicsMode();
    VL_DePlaneVGA ();
    VL_SetLineWidth (48);
    screensize=208*iGLOBAL_SCREENBWIDE*2;//bna++ *2
    page1start=0xa0200;
    page2start=0xa0200+screensize;
    page3start=0xa0200+(2u*screensize);
    displayofs = page1start;
    bufferofs = page2start;
    XFlipPage ();
}

/*
=======================
=
= VL_CopyPlanarPage
=
=======================
*/
void VL_CopyPlanarPage ( byte * src, byte * dest )
{
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      memcpy(dest,src,screensize);
      }
}

/*
=======================
=
= VL_CopyPlanarPageToMemory
=
=======================
*/
void VL_CopyPlanarPageToMemory ( byte * src, byte * dest )
{
   byte * ptr;
   int plane,a,b;

   for (plane=0;plane<4;plane++)
      {
      ptr=dest+plane;
      VGAREADMAP(plane);
      for (a=0;a<200;a++)
         for (b=0;b<80;b++,ptr+=4)
            *(ptr)=*(src+(a*linewidth)+b);
      }
}

/*
=======================
=
= VL_CopyBufferToAll
=
=======================
*/
void VL_CopyBufferToAll ( byte *buffer )
{
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      if (page1start!=buffer)
         memcpy((byte *)page1start,(byte *)buffer,screensize);
      if (page2start!=buffer)
         memcpy((byte *)page2start,(byte *)buffer,screensize);
      if (page3start!=buffer)
         memcpy((byte *)page3start,(byte *)buffer,screensize);
      }
}

/*
=======================
=
= VL_CopyDisplayToHidden
=
=======================
*/
void VL_CopyDisplayToHidden ( void )
{
   VL_CopyBufferToAll ( displayofs );
}

/*
=================
=
= VL_ClearBuffer
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearBuffer (unsigned buf, byte color)
{
  VGAMAPMASK(15);
  memset((byte *)buf,color,screensize);
}

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (byte color)
{
  VGAMAPMASK(15);
  memset((byte *)(0xa000<<4),color,0x10000);
}

/*
=================
=
= VL_DePlaneVGA
=
=================
*/

void VL_DePlaneVGA (void)
{

//
// change CPU addressing to non linear mode
//

//
// turn off chain 4 and odd/even
//
        outp (SC_INDEX,SC_MEMMODE);
        outp (SC_DATA,(inp(SC_DATA)&~8)|4);

        outp (SC_INDEX,SC_MAPMASK);         // leave this set throughout

//
// turn off odd/even and set write mode 0
//
        outp (GC_INDEX,GC_MODE);
        outp (GC_DATA,inp(GC_DATA)&~0x13);

//
// turn off chain
//
        outp (GC_INDEX,GC_MISCELLANEOUS);
        outp (GC_DATA,inp(GC_DATA)&~2);

//
// clear the entire buffer space, because int 10h only did 16 k / plane
//
        VL_ClearVideo (0);

//
// change CRTC scanning from doubleword to byte mode, allowing >64k scans
//
        outp (CRTC_INDEX,CRTC_UNDERLINE);
        outp (CRTC_DATA,inp(CRTC_DATA)&~0x40);

        outp (CRTC_INDEX,CRTC_MODE);
        outp (CRTC_DATA,inp(CRTC_DATA)|0x40);
}


/*
=================
=
= XFlipPage
=
=================
*/

void XFlipPage ( void )
{
   displayofs=bufferofs;

//   _disable();

   outp(CRTC_INDEX,CRTC_STARTHIGH);
   outp(CRTC_DATA,((displayofs&0x0000ffff)>>8));

//   _enable();

   bufferofs += screensize;
   if (bufferofs > page3start)
      bufferofs = page1start;
}

#else

#include "SDL.h"

#ifndef STUB_FUNCTION

/* rt_def.h isn't included, so I just put this here... */
#if !defined(ANSIESC)
#define STUB_FUNCTION fprintf(stderr,"STUB: %s at " __FILE__ ", line %d, thread %d\n",__FUNCTION__,__LINE__,getpid())
#else
#define STUB_FUNCTION
#endif

#endif

/*
====================
=
= GraphicsMode
=
====================
*/
SDL_Window *sdl_window = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture *sdl_texture = NULL;
SDL_Surface *sdl_surface = NULL;
static SDL_Surface *sdl_surface32 = NULL;
static SDL_Surface *unstretch_sdl_surface = NULL;
static SDL_Texture *sdl_hint_texture = NULL;
extern unsigned int lastInteraction;

void BuildHintTexture()
{
	lastInteraction = SDL_GetTicks();
	SDL_RWops *file = SDL_RWFromFile("buttons.bmp", "rb");
	if (file)
	{
		SDL_Surface *image = SDL_LoadBMP_RW(file, SDL_TRUE);
		if (!image)
		{
			Error("Bitmap error: %s\n", SDL_GetError());
		}
		SDL_SetColorKey(image, SDL_TRUE, SDL_MapRGBA(image->format, 255, 255, 255, 0));
		sdl_hint_texture = SDL_CreateTextureFromSurface(sdl_renderer, image);
		if (!sdl_hint_texture)
		{
			Error("Texture error: %s\n", SDL_GetError());
		}
		SDL_FreeSurface(image);
	}
}

void GraphicsMode ( void )
{
    Uint32 flags = 0;

	if (SDL_InitSubSystem (SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	{
	    Error ("Could not initialize SDL: %s\n", SDL_GetError());
	}

    #if defined(PLATFORM_MACOSX)
        // FIXME: remove this.  --ryan.
        flags = SDL_WINDOW_FULLSCREEN;
        SDL_WM_GrabInput(SDL_GRAB_ON);
    #endif
    SDL_ShowCursor (0);
    if (sdl_fullscreen)
        flags = SDL_WINDOW_FULLSCREEN;
	sdl_window = SDL_CreateWindow("Rise of the Triad",
							  SDL_WINDOWPOS_UNDEFINED,
							  SDL_WINDOWPOS_UNDEFINED,
							  iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT,
							  flags);
	if (sdl_window == NULL)
	{
		Error ("Could not open window: %s\n", SDL_GetError());
	}
	sdl_renderer = SDL_CreateRenderer(sdl_window, -1, /*SDL_RENDERER_SOFTWARE*/ 0);
	if (sdl_renderer == NULL)
	{
		Error ("Could not create renderer: %s\n", SDL_GetError());
	}
	SDL_RenderSetLogicalSize(sdl_renderer, iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT);
	sdl_texture = SDL_CreateTexture(sdl_renderer,
			SDL_PIXELFORMAT_ARGB8888, //SDL_PIXELFORMAT_RGB565,
			SDL_TEXTUREACCESS_STREAMING,
			iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT);
	if (sdl_texture == NULL)
	{
		Error ("Texture error: %s\n", SDL_GetError());
	}
	sdl_surface = SDL_CreateRGBSurface(0, iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, 8, 0, 0, 0, 0);
	if (sdl_surface == NULL)
	{
		Error ("Could not create surface: %s\n", SDL_GetError());
	}
	sdl_surface32 = SDL_CreateRGBSurface(0, iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, 32, 0, 0, 0, 0);
	if (sdl_surface32 == NULL)
	{
		Error ("Could not create surface: %s\n", SDL_GetError());
	}
	BuildHintTexture();
}

void blitScreen32(uint32_t *dst)
{
    uint8_t *src = sdl_surface->pixels;
    SDL_Palette* palette = sdl_surface->format->palette;
    int x, y;

    for (y = iGLOBAL_SCREENHEIGHT; y > 0; y--)
		for (x = iGLOBAL_SCREENWIDTH; x > 0; x--)
		{
			SDL_Color color = palette->colors[*src++];
			*(dst++) = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
		}
}

/*
====================
=
= SetTextMode
=
====================
*/
void SetTextMode ( void )
{
	if (SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO) {
		if (sdl_renderer != NULL) {
			SDL_DestroyRenderer(sdl_renderer);
			sdl_renderer = NULL;
		}
		if (sdl_window != NULL) {
			SDL_DestroyWindow(sdl_window);
			sdl_window = NULL;
		}
		
		SDL_QuitSubSystem (SDL_INIT_VIDEO);
	}
}

/*
====================
=
= TurnOffTextCursor
=
====================
*/
void TurnOffTextCursor ( void )
{
}

/*
====================
=
= WaitVBL
=
====================
*/
void WaitVBL( void )
{
	SDL_Delay (16667/1000);
}

/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void VL_SetVGAPlaneMode ( void )
{
   int i,offset;

    GraphicsMode();

//
// set up lookup tables
//
//bna--   linewidth = 320;
   linewidth = iGLOBAL_SCREENWIDTH;

   offset = 0;

   for (i=0;i<iGLOBAL_SCREENHEIGHT;i++)
      {
      ylookup[i]=offset;
      offset += linewidth;
      }

//    screensize=MAXSCREENHEIGHT*MAXSCREENWIDTH;
    screensize=iGLOBAL_SCREENHEIGHT*iGLOBAL_SCREENWIDTH;



    page1start=sdl_surface->pixels;
    page2start=sdl_surface->pixels;
    page3start=sdl_surface->pixels;
    displayofs = page1start;
    bufferofs = page2start;

	iG_X_center = iGLOBAL_SCREENWIDTH / 2;
	iG_Y_center = (iGLOBAL_SCREENHEIGHT / 2)+10 ;//+10 = move aim down a bit

	iG_buf_center = bufferofs + (screensize/2);//(iG_Y_center*iGLOBAL_SCREENWIDTH);//+iG_X_center;

	bufofsTopLimit =  bufferofs + screensize - iGLOBAL_SCREENWIDTH;
	bufofsBottomLimit = bufferofs + iGLOBAL_SCREENWIDTH;

    // start stretched
    EnableScreenStretch();
    XFlipPage ();
}

/*
=======================
=
= VL_CopyPlanarPage
=
=======================
*/
void VL_CopyPlanarPage ( byte * src, byte * dest )
{
#ifdef DOS
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      memcpy(dest,src,screensize);
      }
#else
      memcpy(dest,src,screensize);
#endif
}

/*
=======================
=
= VL_CopyPlanarPageToMemory
=
=======================
*/
void VL_CopyPlanarPageToMemory ( byte * src, byte * dest )
{
#ifdef DOS
   byte * ptr;
   int plane,a,b;

   for (plane=0;plane<4;plane++)
      {
      ptr=dest+plane;
      VGAREADMAP(plane);
      for (a=0;a<200;a++)
         for (b=0;b<80;b++,ptr+=4)
            *(ptr)=*(src+(a*linewidth)+b);
      }
#else
      memcpy(dest,src,screensize);
#endif
}

/*
=======================
=
= VL_CopyBufferToAll
=
=======================
*/
void VL_CopyBufferToAll ( byte *buffer )
{
#ifdef DOS
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      if (page1start!=buffer)
         memcpy((byte *)page1start,(byte *)buffer,screensize);
      if (page2start!=buffer)
         memcpy((byte *)page2start,(byte *)buffer,screensize);
      if (page3start!=buffer)
         memcpy((byte *)page3start,(byte *)buffer,screensize);
      }
#endif
}

/*
=======================
=
= VL_CopyDisplayToHidden
=
=======================
*/
void VL_CopyDisplayToHidden ( void )
{
   VL_CopyBufferToAll ( displayofs );
}

/*
=================
=
= VL_ClearBuffer
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearBuffer (byte *buf, byte color)
{
#ifdef DOS
  VGAMAPMASK(15);
  memset((byte *)buf,color,screensize);
#else
  memset((byte *)buf,color,screensize);
#endif
}

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (byte color)
{
#ifdef DOS
  VGAMAPMASK(15);
  memset((byte *)(0xa000<<4),color,0x10000);
#else
//  SDL_Color renderColor = sdl_surface->format->palette->colors[color];
//  if (!SDL_SetRenderDrawColor(sdl_renderer, renderColor.r, renderColor.g, renderColor.b, renderColor.a))
//  {
//	  Error(SDL_GetError());
//  }
//  if (!SDL_RenderClear(sdl_renderer))
//  {
//	  Error(SDL_GetError());
//  }
  memset (sdl_surface->pixels, color, iGLOBAL_SCREENWIDTH*iGLOBAL_SCREENHEIGHT);
#endif
}

/*
=================
=
= VL_DePlaneVGA
=
=================
*/

void VL_DePlaneVGA (void)
{
}

static void RenderCopyHintTexture()
{
	if (sdl_hint_texture)
	{
		unsigned int deltaInteraction = SDL_GetTicks() - lastInteraction;
		const unsigned int minDelay = 10000;
		if (deltaInteraction < minDelay + 4600)
		{
			if (deltaInteraction > minDelay + 4000)
			{
				unsigned int shade = (minDelay + 4600 - deltaInteraction) / 3;
				SDL_SetTextureAlphaMod(sdl_hint_texture, shade < 1 ? 0 : shade);
				SDL_RenderCopy(sdl_renderer, sdl_hint_texture, NULL, NULL);
			}
			else if (deltaInteraction > minDelay)
			{
				unsigned int shade = (deltaInteraction - minDelay) / 3;
				SDL_SetTextureAlphaMod(sdl_hint_texture, shade > 200 ? 200 : shade);
				SDL_RenderCopy(sdl_renderer, sdl_hint_texture, NULL, NULL);
			}
		}
	}
}

/* C version of rt_vh_a.asm */

void VH_UpdateScreen (void)
{ 	

	if (StretchScreen){//bna++
		StretchMemPicture ();
	}else{
		DrawCenterAim ();
	}
//	SDL_UpdateRect (SDL_GetVideoSurface (), 0, 0, 0, 0);
	blitScreen32(sdl_surface32->pixels);
	SDL_UpdateTexture(sdl_texture, NULL, sdl_surface32->pixels, sdl_surface32->pitch);
	SDL_RenderClear(sdl_renderer);
	SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
	RenderCopyHintTexture();
	SDL_RenderPresent(sdl_renderer);
}


/*
=================
=
= XFlipPage
=
=================
*/

void XFlipPage ( void )
{
#ifdef DOS
   displayofs=bufferofs;

//   _disable();

   outp(CRTC_INDEX,CRTC_STARTHIGH);
   outp(CRTC_DATA,((displayofs&0x0000ffff)>>8));

//   _enable();

   bufferofs += screensize;
   if (bufferofs > page3start)
      bufferofs = page1start;
#else
 	if (StretchScreen){//bna++
		StretchMemPicture ();
	}else{
		DrawCenterAim ();
	}
//   SDL_UpdateRect (sdl_surface, 0, 0, 0, 0);
	blitScreen32(sdl_surface32->pixels);
	SDL_UpdateTexture(sdl_texture, NULL, sdl_surface32->pixels, sdl_surface32->pitch);
	SDL_RenderClear(sdl_renderer);
	SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
	RenderCopyHintTexture();
	SDL_RenderPresent(sdl_renderer);
 
#endif
}

#endif


void EnableScreenStretch(void)
{
   int i,offset;

   if (iGLOBAL_SCREENWIDTH <= 320 || StretchScreen) return;

   if (unstretch_sdl_surface == NULL)
   {
      /* should really be just 320x200, but there is code all over the
         places which crashes then */
      unstretch_sdl_surface = SDL_CreateRGBSurface(SDL_SWSURFACE,
         iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, 8, 0, 0, 0, 0);
   }

   displayofs = (byte*)unstretch_sdl_surface->pixels +
	(displayofs - (byte *)sdl_surface->pixels);
   bufferofs  = unstretch_sdl_surface->pixels;
   page1start = unstretch_sdl_surface->pixels;
   page2start = unstretch_sdl_surface->pixels;
   page3start = unstretch_sdl_surface->pixels;
   StretchScreen = 1;
}

void DisableScreenStretch(void)
{
   if (iGLOBAL_SCREENWIDTH <= 320 || !StretchScreen) return;

   displayofs = (byte*)sdl_surface->pixels +
	(displayofs - (byte *)unstretch_sdl_surface->pixels);
   bufferofs  = sdl_surface->pixels;
   page1start = sdl_surface->pixels;
   page2start = sdl_surface->pixels;
   page3start = sdl_surface->pixels;
   StretchScreen = 0;
}


// bna section -------------------------------------------
static void StretchMemPicture ()
{
  SDL_Rect src;
  SDL_Rect dest;

  src.x = 0;
  src.y = 0;
  src.w = 320;
  src.h = 200;

  dest.x = 0;
  dest.y = 0;
  dest.w = iGLOBAL_SCREENWIDTH;
  dest.h = iGLOBAL_SCREENHEIGHT;
  SDL_SoftStretch(unstretch_sdl_surface, &src, sdl_surface, &dest);
}

// bna function added start
extern	boolean ingame;
int		iG_playerTilt;

void DrawCenterAim ()
{
	int x;

	int percenthealth = (locplayerstate->health * 10) / MaxHitpointsForCharacter(locplayerstate);
	int color = percenthealth < 3 ? egacolor[RED] : percenthealth < 4 ? egacolor[YELLOW] : egacolor[GREEN];

	if (iG_aimCross && !GamePaused){
		if (( ingame == true )&&(iGLOBAL_SCREENWIDTH>320)){
			  if ((iG_playerTilt <0 )||(iG_playerTilt >iGLOBAL_SCREENHEIGHT/2)){
					iG_playerTilt = -(2048 - iG_playerTilt);
			  }
			  if (iGLOBAL_SCREENWIDTH == 640){ x = iG_playerTilt;iG_playerTilt=x/2; }
			  iG_buf_center = bufferofs + ((iG_Y_center-iG_playerTilt)*iGLOBAL_SCREENWIDTH);//+iG_X_center;

			  for (x=iG_X_center-10;x<=iG_X_center-4;x++){
				  if ((iG_buf_center+x < bufofsTopLimit)&&(iG_buf_center+x > bufofsBottomLimit)){
					 *(iG_buf_center+x) = color;
				  }
			  }
			  for (x=iG_X_center+4;x<=iG_X_center+10;x++){
				  if ((iG_buf_center+x < bufofsTopLimit)&&(iG_buf_center+x > bufofsBottomLimit)){
					 *(iG_buf_center+x) = color;
				  }
			  }
			  for (x=10;x>=4;x--){
				  if (((iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) < bufofsTopLimit)&&((iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) > bufofsBottomLimit)){
					 *(iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) = color;
				  }
			  }
			  for (x=4;x<=10;x++){
				  if (((iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) < bufofsTopLimit)&&((iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) > bufofsBottomLimit)){
					 *(iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) = color;
				  }
			  }
		}
	}
}
// bna function added end




// bna section -------------------------------------------



