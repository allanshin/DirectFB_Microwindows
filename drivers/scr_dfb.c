/*
 * Copyright (c) 1999, 2000, 2001, 2002 Greg Haerr <greg@censoft.com>
 * Portions Copyright (c) 2002 Koninklijke Philips Electronics
 *
 * Microwindows Screen Driver for Linux kernel framebuffers
 *
 * Portions used from Ben Pfaff's BOGL <pfaffben@debian.org>
 * 
 * Note: modify select_fb_driver() to add new framebuffer subdrivers
 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <directfb.h>
#include "device.h"
#include "genfont.h"
#include "genmem.h"
#include "fb.h"
#include <windows.h>

#ifdef CreateWindow
	#undef CreateWindow
#endif


#ifndef FB_TYPE_VGA_PLANES
#define FB_TYPE_VGA_PLANES 4
#endif

/* macro for a safe call to DirectFB functions */
#define DFBCHECK(x...) \
     {                                                                \
          err = x;                                                    \
          if (err != DFB_OK) {                                        \
               fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
               DirectFBErrorFatal( #x, err );                         \
          }                                                           \
     }

int USE_DWINDOW = 1;

static PSD  dfb_open(PSD psd);
static void dfb_close(PSD psd);
static void dfb_setportrait(PSD psd, int portraitmode);
static void dfb_setpalette(PSD psd,int first, int count, MWPALENTRY *palette);
void dfb_flush(void);
static void gen_getscreeninfo(PSD psd,PMWSCREENINFO psi);



SCREENDEVICE	scrdev = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL,
	dfb_open,
	dfb_close,
	gen_getscreeninfo,
	dfb_setpalette,
	NULL,			/* DrawPixel subdriver*/
	NULL,			/* ReadPixel subdriver*/
	NULL,			/* DrawHorzLine subdriver*/
	NULL,			/* DrawVertLine subdriver*/
	NULL,			/* FillRect subdriver*/
	gen_fonts,
	NULL,			/* Blit subdriver*/
	NULL,			/* PreSelect*/
	NULL,			/* DrawArea subdriver*/
	NULL,			/* SetIOPermissions*/
	gen_allocatememgc,
	fb_mapmemgc,
	gen_freememgc,
	NULL,			/* StretchBlit subdriver*/
	dfb_setportrait,		/* SetPortrait*/
	0,	 /* screen portrait mode*/
	NULL,  /* original subdriver for portrait modes*/
	NULL, /*	(*StretchBlitEx) */
	NULL, /* (*DrawImage) */
	NULL, /* (*BatchBlit) */
	NULL, 	/* (*Flush) */
	NULL,	/* (*InitPaletteLookup) */
	NULL, 		/* (*FindNearestColor)	*/
	0			/* physical address for Bogart	*/
};


/* static variables*/
static int fb;			/* Framebuffer file handle. */
static int status;		/* 0=never inited, 1=once inited, 2=inited. */
static short saved_red[16];	/* original hw palette*/
static short saved_green[16];
static short saved_blue[16];


IDirectFB              *dfb;
IDirectFBDisplayLayer  *DFB_layer;
IDirectFBSurface       *dwindow_surface;
IDirectFBWindow        *dwindow;     
//IDirectFBEventBuffer   *buffer;


extern SUBDRIVER fbportrait_left, fbportrait_right, fbportrait_down;
static PSUBDRIVER pdrivers[4] = { /* portrait mode subdrivers*/
	NULL, &fbportrait_left, &fbportrait_right, &fbportrait_down
};

/* local functions*/
static void	set_directcolor_palette(PSD psd);


inline void dfb_unlockSurface(void)
{
	unsigned char * src;
	int pitch;
	dwindow_surface->Lock( dwindow_surface, DSLF_WRITE , &src, &pitch);
}

inline void dfb_lockSurface(void)
{
	dwindow_surface->Unlock( dwindow_surface );//release the buffer
}


void dfb_flush(void)
{
#if 1
	unsigned char * src;
	int i, j = 0;
	int pitch;
	int bytes_per_pixel = 4;

	dwindow_surface->Lock( dwindow_surface, DSLF_WRITE , &src, &pitch);
	for( i = 0; i < 768; i++ ){ //vert loop 
	   unsigned char *pos= src + i * pitch;//to calc next line position I used the pitch 
	   	for( j = 0; j < pitch; j += bytes_per_pixel ){//horz loop 
//	     	*(pos + j) = 0; //b
//	     	*(pos + j + 1) = 0; //g
//	     	*(pos + j + 2) = j; //r
	     	*(pos + j + 3) = 0xe0;//a
	     	
	  	}
	}
	dwindow_surface->Unlock( dwindow_surface );//release the buffer	
#endif

	dwindow_surface->Flip(dwindow_surface, NULL, 0 );
}



/* init framebuffer*/
static PSD
dfb_open(PSD psd)
{
	char *	env;
	int	type, visual;
	PSUBDRIVER subdriver;


	struct fb_fix_screeninfo fb_fix;
	struct fb_var_screeninfo fb_var;


	 DFBSurfacePixelFormat format_of_the_surface;    
     DFBDisplayLayerConfig         layer_config;
     DFBGraphicsDeviceDescription  gdesc;
	 DFBWindowDescription  desc;
	 int xres = 1024, yres = 768;
	 int bits_per_pixel = 32;
	 int err;


	assert(status < 2);


	{
	     DFBCHECK(DirectFBInit( 0, 0));
	     DFBCHECK(DirectFBCreate( &dfb ));

	     dfb->GetDeviceDescription( dfb, &gdesc );

	     DFBCHECK(dfb->GetDisplayLayer( dfb, DLID_PRIMARY, &DFB_layer ));

	     DFB_layer->SetCooperativeLevel( DFB_layer, DLSCL_ADMINISTRATIVE );

	     if (!((gdesc.blitting_flags & DSBLIT_BLEND_ALPHACHANNEL) &&
	           (gdesc.blitting_flags & DSBLIT_BLEND_COLORALPHA  )))
	     {
	          layer_config.flags = DLCONF_BUFFERMODE;
	          layer_config.buffermode = DLBM_BACKSYSTEM;

	          DFB_layer->SetConfiguration( DFB_layer, &layer_config );
	     }

	     DFB_layer->GetConfiguration( DFB_layer, &layer_config );
	     DFB_layer->EnableCursor ( DFB_layer, 1 );

		
		/*create window*/
		if (USE_DWINDOW)
	     {


	          desc.flags = ( DWDESC_POSX | DWDESC_POSY |
	                         DWDESC_WIDTH | DWDESC_HEIGHT );


	          desc.caps = DWCAPS_ALPHACHANNEL;
	          desc.flags |= DWDESC_CAPS;

	          desc.posx   = 0;
	          desc.posy   = 0;
	          desc.width  = 1024;
	          desc.height = 768;


	          DFBCHECK( DFB_layer->CreateWindow( DFB_layer, &desc, &dwindow ) );
	          dwindow->GetSurface( dwindow, &dwindow_surface );
	          dwindow_surface->SetColor( dwindow_surface, 0x00, 0x00, 0x00, 0x00 );
    	      dwindow_surface->FillRectangle( dwindow_surface, 0, 0, desc.width, desc.height );	          

	          dwindow->SetOpacity( dwindow, 0xff );

//	          dwindow->CreateEventBuffer( dwindow, &buffer );
			
	//          dwindow_surface->Flip( dwindow_surface, NULL, 0 );
	          DFBCHECK(dwindow_surface->GetSize( dwindow_surface, &xres, &yres ));	    
			  dwindow_surface->GetPixelFormat(dwindow_surface, &format_of_the_surface);
			  bits_per_pixel = DFB_BITS_PER_PIXEL( format_of_the_surface);
			  //bytes_per_line = DFB_BYTES_PER_LINE(format_of_the_surface, xres);
	     }

	}

#if 0
	/* locate and open framebuffer, get info*/
	if(!(env = getenv("FRAMEBUFFER")))
		env = "/dev/fb0";
	fb = open(env, O_RDWR);
	if(fb < 0) {
		EPRINTF("Error opening %s: %m. Check kernel config\n", env);
		return NULL;
	}

	if(ioctl(fb, FBIOGET_FSCREENINFO, &fb_fix) == -1 ||
		ioctl(fb, FBIOGET_VSCREENINFO, &fb_var) == -1) {
			EPRINTF("Error reading screen info: %m\n");
			goto fail;
	}

	/* setup screen device from framebuffer info*/
	type = fb_fix.type;
	visual = fb_fix.visual;
#endif
	psd->portrait = MWPORTRAIT_NONE;

	psd->xres = psd->xvirtres = xres;
	psd->yres = psd->yvirtres = yres;

	psd->planes = 1;

	psd->bpp = bits_per_pixel;
	psd->ncolors = (psd->bpp >= 24)? (1 << 24): (1 << psd->bpp);

	/* set linelen to byte length, possibly converted later*/
//	psd->linelen = bytes_per_line;

	if (USE_DWINDOW)
	{	
		dwindow_surface->Lock( dwindow_surface, DSLF_WRITE , &psd->addr, &psd->linelen);
		dwindow_surface->Unlock( dwindow_surface );//release the buffer

		//?
//		psd->linelen = 1024;
printf("scr_dfb linelen = %d, scr_dfb addr = 0x%x\n", psd->linelen, psd->addr);
	}
	else
	{
		psd->addr = 0;
		psd->linelen = bits_per_pixel * 1024;
	}

	
	psd->size = 0;		/* force subdriver init of size*/


	psd->flags = PSF_SCREEN | PSF_HAVEBLIT;
	if (psd->bpp == 16)
		psd->flags |= PSF_HAVEOP_COPY;

	/* set pixel format*/

//	if(visual == FB_VISUAL_TRUECOLOR || visual == FB_VISUAL_DIRECTCOLOR) {
		switch(psd->bpp) {
		case 8:
//			psd->pixtype = MWPF_TRUECOLOR332;
			psd->pixtype = MWPF_PALETTE;
			break;
		case 16:			
			psd->pixtype = MWPF_TRUECOLOR555;			
			break;
		case 24:
			psd->pixtype = MWPF_TRUECOLOR888;
			break;
		case 32:
//			psd->pixtype = MWPF_TRUECOLOR0888;

			/* Check if we have alpha */
//			if (fb_var.transp.length == 8)
				psd->pixtype = MWPF_TRUECOLOR8888;

			break;
		default:
			EPRINTF(
			"Unsupported %d color (%d bpp) truecolor framebuffer\n",
				psd->ncolors, psd->bpp);
			goto fail;
		}
//	} 
//	else 
//		psd->pixtype = MWPF_PALETTE;

	/*DPRINTF("%dx%dx%d linelen %d type %d visual %d bpp %d\n", psd->xres,
	 	psd->yres, psd->ncolors, psd->linelen, type, visual,
		psd->bpp);*/

	/* select a framebuffer subdriver based on planes and bpp*/
	subdriver = select_fb_subdriver(psd);
	if (!subdriver) {
		EPRINTF("No driver for screen bpp %d\n", psd->bpp);
		goto fail;
	}

	/*
	 * set and initialize subdriver into screen driver
	 * psd->size is calculated by subdriver init
	 */
	if(!set_subdriver(psd, subdriver, TRUE)) {
		EPRINTF("Driver initialize failed bpp %d\n", psd->bpp);
		goto fail;
	}

	/* remember original subdriver for portrait mode switching*/
	pdrivers[0] = psd->orgsubdriver = subdriver;

//	psd->addr = mmap(NULL, psd->size, PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);

	status = 2;


	return psd;	/* success*/

fail:
//	close(fb);
	return NULL;
}

/* close framebuffer*/
static void
dfb_close(PSD psd)
{
	/* if not opened, return*/
	if(status != 2)
		return;
	status = 1;
#if 0
  	/* reset hw palette*/
	ioctl_setpalette(0, 16, saved_red, saved_green, saved_blue);
  
	/* unmap framebuffer*/
	munmap(psd->addr, psd->size);
  

	/* close framebuffer*/
	close(fb);
#endif
	
	/*clean up and close off dfb stuff*/
	if (USE_DWINDOW) {
//		buffer->Release( buffer );
		dwindow_surface->Release( dwindow_surface );
		dwindow->Release( dwindow );
	}
	DFB_layer->Release( DFB_layer );
	dfb->Release( dfb );
	printf("released DFB\n");

	
}

static void
dfb_setportrait(PSD psd, int portraitmode)
{
	psd->portrait = portraitmode;

	/* swap x and y in left or right portrait modes*/
	if (portraitmode & (MWPORTRAIT_LEFT|MWPORTRAIT_RIGHT)) {
		/* swap x, y*/
		psd->xvirtres = psd->yres;
		psd->yvirtres = psd->xres;
	} else {
		/* normal x, y*/
		psd->xvirtres = psd->xres;
		psd->yvirtres = psd->yres;
	}
	/* assign portrait subdriver which calls original subdriver*/
	if (portraitmode == MWPORTRAIT_DOWN)
		portraitmode = 3;	/* change bitpos to index*/
	set_subdriver(psd, pdrivers[portraitmode], FALSE);
}

/* setup directcolor palette - required for ATI cards*/
static void
set_directcolor_palette(PSD psd)
{
	int i;
	short r[256];

	/* 16bpp uses 32 palette entries*/
	if(psd->bpp == 16) {
		for(i=0; i<32; ++i)
			r[i] = i<<11;
		ioctl_setpalette(0, 32, r, r, r);
	} else {
		/* 32bpp uses 256 entries*/
		for(i=0; i<256; ++i)
			r[i] = i<<8;
		ioctl_setpalette(0, 256, r, r, r);
	}
}

static int fade = 100;

/* convert Microwindows palette to framebuffer format and set it*/
static void
dfb_setpalette(PSD psd,int first, int count, MWPALENTRY *palette)
{
	int 	i;
	unsigned short 	red[256];
	unsigned short 	green[256];
	unsigned short 	blue[256];

	if (count > 256)
		count = 256;

	/* convert palette to framebuffer format*/
	for(i=0; i < count; i++) {
		MWPALENTRY *p = &palette[i];

		/* grayscale computation:
		 * red[i] = green[i] = blue[i] =
		 *	(p->r * 77 + p->g * 151 + p->b * 28);
		 */
		red[i] = (p->r * fade / 100) << 8;
		green[i] = (p->g * fade / 100) << 8;
		blue[i] = (p->b * fade / 100) << 8;
	}
	ioctl_setpalette(first, count, red, green, blue);
}

/* get framebuffer palette*/
void
ioctl_getpalette(int start, int len, short *red, short *green, short *blue)
{

	struct fb_cmap cmap;

	cmap.start = start;
	cmap.len = len;
	cmap.red = red;
	cmap.green = green;
	cmap.blue = blue;
	cmap.transp = NULL;
//	ioctl(fb, FBIOGETCMAP, &cmap);

}

/* set framebuffer palette*/
void
ioctl_setpalette(int start, int len, short *red, short *green, short *blue)
{

	struct fb_cmap cmap;

	cmap.start = start;
	cmap.len = len;
	cmap.red = red;
	cmap.green = green;
	cmap.blue = blue;
	cmap.transp = NULL;

//	ioctl(fb, FBIOPUTCMAP, &cmap);

}

/* experimental palette animation*/
void
setfadelevel(PSD psd, int f)
{
	int 		i;
	unsigned short 	r[256], g[256], b[256];
	extern MWPALENTRY gr_palette[256];

	if(psd->pixtype != MWPF_PALETTE)
		return;

	fade = f;
	if(fade > 100)
		fade = 100;
	for(i=0; i<256; ++i) {
		r[i] = (gr_palette[i].r * fade / 100) << 8;
		g[i] = (gr_palette[i].g * fade / 100) << 8;
		b[i] = (gr_palette[i].b * fade / 100) << 8;
	}
	ioctl_setpalette(0, 256, r, g, b);
}

static void
gen_getscreeninfo(PSD psd,PMWSCREENINFO psi)
{
	psi->rows = psd->yvirtres;
	psi->cols = psd->xvirtres;
	psi->planes = psd->planes;
	psi->bpp = psd->bpp;
	psi->ncolors = psd->ncolors;
	psi->fonts = NUMBER_FONTS;
	psi->portrait = psd->portrait;
	psi->fbdriver = TRUE;	/* running fb driver, can direct map*/

	psi->pixtype = psd->pixtype;
	switch (psd->pixtype) {
	case MWPF_TRUECOLOR8888:
	case MWPF_TRUECOLOR0888:
	case MWPF_TRUECOLOR888:
		psi->rmask 	= 0xff0000;
		psi->gmask 	= 0x00ff00;
		psi->bmask	= 0x0000ff;
		break;
	case MWPF_TRUECOLOR565:
		psi->rmask 	= 0xf800;
		psi->gmask 	= 0x07e0;
		psi->bmask	= 0x001f;
		break;
	case MWPF_TRUECOLOR555:
		psi->rmask 	= 0x7c00;
		psi->gmask 	= 0x03e0;
		psi->bmask	= 0x001f;
		break;
	case MWPF_TRUECOLOR332:
		psi->rmask 	= 0xe0;
		psi->gmask 	= 0x1c;
		psi->bmask	= 0x03;
		break;
	case MWPF_PALETTE:
	default:
		psi->rmask 	= 0xff;
		psi->gmask 	= 0xff;
		psi->bmask	= 0xff;
		break;
	}

	if(psd->yvirtres > 480) {
		/* SVGA 800x600*/
		psi->xdpcm = 33;	/* assumes screen width of 24 cm*/
		psi->ydpcm = 33;	/* assumes screen height of 18 cm*/
	} else if(psd->yvirtres > 350) {
		/* VGA 640x480*/
		psi->xdpcm = 27;	/* assumes screen width of 24 cm*/
		psi->ydpcm = 27;	/* assumes screen height of 18 cm*/
        } else if(psd->yvirtres <= 240) {
		/* half VGA 640x240 */
		psi->xdpcm = 14;        /* assumes screen width of 24 cm*/ 
		psi->ydpcm =  5;
	} else {
		/* EGA 640x350*/
		psi->xdpcm = 27;	/* assumes screen width of 24 cm*/
		psi->ydpcm = 19;	/* assumes screen height of 18 cm*/
	}
}




void DFB_DrawOn(HWND hwnd, HDC hdc)
{
#if 0
	/*directfb -- swap out the address to draw surface*/
	hwnd->DFB_windowSurface->Lock( hwnd->DFB_windowSurface, DSLF_WRITE , &hdc->psd->addr, &hdc->psd->linelen);
	if (hdc->psd->bpp > 8)					
	{
		hdc->psd->linelen = hdc->psd->linelen / (hdc->psd->bpp / 8);
	}		
//	printf("locked surface, addr = 0x%x, linelen = %d, scrdev.addr = 0x%x\n", hdc->psd->addr, hdc->psd->linelen, scrdev.addr);
#endif
	hwnd->DFB_windowSurface->Lock( hwnd->DFB_windowSurface, DSLF_WRITE , &hdc->psd->addr, &hdc->psd->linelen);
	hwnd->DFB_SurfaceLocked = TRUE;
	hdc->psd->addr = hwnd->DFB_addr;
	hdc->psd->linelen = hwnd->DFB_linelen;
//printf("locking hwnd = 0x%x\n", hwnd);

}



void DFB_DrawOff(HWND hwnd, HDC hdc)
{
	extern HWND	rootwp;
#if 0		
	extern IDirectFBSurface *dwindow_surface;
	/*directfb -- swap out the address to draw surface*/
	hwnd->DFB_windowSurface->Unlock( hwnd->DFB_windowSurface);
	hwnd->DFB_windowSurface->Flip(hwnd->DFB_windowSurface, NULL, 0);	

	/* TODO: take this out and set as global variables to assign faster */
	dwindow_surface->Lock( dwindow_surface, DSLF_WRITE , &hdc->psd->addr, &hdc->psd->linelen);
	dwindow_surface->Unlock( dwindow_surface);
	if (hdc->psd->bpp > 8)
	{
		hdc->psd->linelen = hdc->psd->linelen / (hdc->psd->bpp / 8);
	}
#endif

	unsigned char * src = hwnd->DFB_addr;
	int i, j = 0;
	int bytes_per_pixel = 4;
	int pitch = hwnd->DFB_linelen * bytes_per_pixel;//hwnd->winrect.right * bytes_per_pixel;


//TODO: fix this shit... this is dumb. used to convert all pixel colors back to opaque exept the colorkey
#if 0
	for( i = 0; i < hwnd->winrect.bottom; i++ ){ //vert loop 
	   unsigned char *pos= src + i * pitch;//to calc next line position I used the pitch 
	   	for( j = 0; j < pitch; j += bytes_per_pixel )
	   	{//horz loop
	   		BYTE R = *(pos + j + 2);
	   		BYTE G = *(pos + j + 1);
	   		BYTE B = *(pos + j + 0);

	   	if ( hwnd->dwFlags & LWA_COLORKEY && 
	   		GetRValue(hwnd->crKey) == R &&
	   		GetGValue(hwnd->crKey) == G &&
	   		GetBValue(hwnd->crKey) == B)
		   	{
		   		
	//	     	*(pos + j) = 0x0; //b
	//	     	*(pos + j + 1) = 0; //g
	//	     	*(pos + j + 2) = j; //r
		     	*(pos + j + 3) = 0x0;//a
//printf("transparent color = 0x%x\n", hwnd->crKey);
		   	}
	   		else
	   		{
		     	*(pos + j + 3) = 0xff;//a
	   		}
	   		
	     	
	  	}
	}
#endif


	hwnd->DFB_windowSurface->Unlock( hwnd->DFB_windowSurface);
	hwnd->DFB_SurfaceLocked = FALSE;
//	printf("unlocking hwnd = 0x%x\n", hwnd);
/*some color -- TODO: remove this*/ 
//	hwnd->DFB_windowSurface->SetColor( hwnd->DFB_windowSurface, 0xFF, 0x00, 0xFF, 0xFF);
//	hwnd->DFB_windowSurface->FillRectangle( hwnd->DFB_windowSurface, 0, 0, 3, 3);

//	hwnd->DFB_windowSurface->Flip(hwnd->DFB_windowSurface, NULL, 0);	
	hdc->psd->addr = rootwp->DFB_addr;
	hdc->psd->linelen = rootwp->DFB_linelen;
}




