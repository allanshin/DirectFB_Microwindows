#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "device.h"
#include "windows.h"
#include <directfb.h>

/* macro for a safe call to DirectFB functions */
#define DFBCHECK(x...) \
     {                                                                \                                                   
          if (x != DFB_OK) {                                        \
               fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
               DirectFBErrorFatal( #x, x );                         \
          }                                                           \
     }

MWBOOL    dfb_GetTextInfo(PMWFONT pfont, PMWFONTINFO pfontinfo);
void    dfb_GetTextSize(PMWFONT pfont, const void * text, int cc, 
			MWTEXTFLAGS flags, MWCOORD *pwidth, MWCOORD *pheight,
			MWCOORD *pbase);
void	dfb_DestroyFont(PMWFONT pfont);
void	dfb_DrawText(PMWFONT pfont, PSD hdc, MWCOORD x, MWCOORD y,
		     const void *str, int count, MWTEXTFLAGS flags);


static MWFONTPROCS dfb_procs = {
  MWTF_UC16,
  dfb_GetTextInfo,
  dfb_GetTextSize,
  NULL,
  dfb_DestroyFont,
  dfb_DrawText, 
  NULL,
  NULL,
  NULL,
  NULL
};

MWBOOL    dfb_GetTextInfo(PMWFONT pfont, PMWFONTINFO pfontinfo)
{
  int i;
  int width, height;
  DFBRectangle r;
  IDirectFBFont * font = (IDirectFBFont *)pfont->pGfnFont;
  if (!pfont)
    return FALSE;

	font->GetGlyphExtents(font, 'W', &r, NULL);
	font->GetMaxAdvance (font, &width);
	height = r.h;

	pfontinfo->maxwidth = width;
	pfontinfo->height = height;
	pfontinfo->baseline = height - 2;
	pfontinfo->descent = height - pfontinfo->baseline;
	pfontinfo->maxascent = pfontinfo->baseline;
	pfontinfo->maxdescent = pfontinfo->descent;
	pfontinfo->linespacing = height;
	pfontinfo->firstchar = 0;
	pfontinfo->lastchar = 0;
	pfontinfo->fixed = TRUE;

	for (i = 0; i < 256; i++)
		pfontinfo->widths[i] = width / 2;
	return TRUE;
    
}


void    dfb_GetTextSize(PMWFONT pfont, const void * text, int cc, 
			MWTEXTFLAGS flags, MWCOORD *pwidth, MWCOORD *pheight,
			MWCOORD *pbase)
{
  MWFONTINFO fontinfo;
  IDirectFBFont * font = (IDirectFBFont *)pfont->pGfnFont;
//  dfb_GetTextInfo(pfont, &fontinfo);
  //*pwidth = fontinfo.maxwidth;
  if ((flags & MWTF_PACKMASK) == MWTF_ASCII)
  {
  		DFBRectangle ret_logical_rect;   		
		font->GetStringExtents (font, text, cc, &ret_logical_rect, NULL);  
		if (pwidth)
			*pwidth = ret_logical_rect.w;
		if (pheight)			
			*pheight = ret_logical_rect.h;
		if (pbase)
			*pbase = 0;		
  }
  else
  {
  		int maxHeight = 0;
  		DFBRectangle r;
  		int advance = 0;
  		int totalAdvance = 0;
  		WORD * pGlyph = text;
  		int i = 0;
  		for (i = 0; i < cc; i++)
  		{	
			font->GetGlyphExtents(font, pGlyph[i], &r, &advance);
			if (maxHeight < r.h)
				maxHeight = r.h;
			totalAdvance += advance;
  		}		
  		if (pwidth)  			
	  		*pwidth = totalAdvance;
  		if (pheight)
	  		*pheight = maxHeight;
  		if (pbase)
	  		*pbase = 0;

  }      

}


PMWFONT dfb_CreateFont(const char *name, MWCOORD height, int attr)
{
	extern IDirectFB	*dfb;
	IDirectFBFont		*font;
    int fontheight;
    DFBFontDescription desc;
   	char			fontname[128];
	char *			p;

  	PMWFONT pfont = malloc(sizeof(MWFONT));
  	pfont->fontprocs = &dfb_procs;
	desc.flags = DFDESC_HEIGHT;
  	desc.height = height;

	/* check for pathname prefix*/
	if (strchr(name, '/') != NULL)
		strcpy(fontname, name);
	else {
		strcpy(fontname, FREETYPE_FONT_DIR);
		strcat(fontname, "/");
		strcat(fontname, name);
	}

	/* check for extension*/
	if ((p = strrchr(fontname, '.')) == NULL ||
	    strcmp(p, ".ttf") != 0) {
		strcat(fontname, ".ttf");
	}

	DFBCHECK(dfb->CreateFont( dfb, fontname, &desc, &pfont->pGfnFont ));
  	pfont->fontrotation = 0;
  	pfont->fontattr = 0;
	font = (IDirectFBFont *)pfont->pGfnFont;
  	font->GetHeight( font, &pfont->fontsize);

  return pfont;
}

void	dfb_DestroyFont(PMWFONT pfont)
{
	if (pfont)
	{
		IDirectFBFont * font = (IDirectFBFont *)pfont->pGfnFont;
		font->Release (font);
	}
}

void	dfb_DrawText(PMWFONT pfont, PSD hDC, MWCOORD x, MWCOORD y,
			const void *str, int count, MWTEXTFLAGS flags)
{
  	HDC hdc = (HDC)hDC;
  	int alignment = 0;
	IDirectFBFont * font = (IDirectFBFont *)pfont->pGfnFont;
	DWORD dwFlags = 0;
	int shouldDrawTextBg = 0;
	

	/* adjust the alignment flags */
	if (hdc->textalign & TA_RIGHT) 
	{
		alignment |= DSTF_RIGHT;
	}
	else if (hdc->textalign & TA_CENTER)
	{
		alignment |= DSTF_CENTER;
	}
	else 
	{
		alignment |= DSTF_LEFT;
	}
	
	if (hdc->textalign & TA_BASELINE) 
	{
		alignment |= DSTF_BOTTOM;
	}
	else if (hdc->textalign & TA_BOTTOM)
	{
		 alignment |= DSTF_BOTTOM;
	}
	else 
	{
		alignment |= DSTF_TOP;
	}

	if (hdc->hwnd->DFB_SurfaceLocked)
	{
		hdc->hwnd->DFB_windowSurface->Unlock( hdc->hwnd->DFB_windowSurface);
	}
	DFBCHECK(hdc->hwnd->DFB_windowSurface->SetFont( hdc->hwnd->DFB_windowSurface, font ));

	/* AShin: this is to make the text look nice.  If the background is transparent, then we need
	 * to render the text on the background color that is the same as the text color, 
	 * except that it is 100% transparent.  Therefore, there is no loss in color of the text, 
	 * and the rendered output is only a set of varying alpha with the same text color throughout.
	 * The results may be very slight, but this is to pay attention to small details.
	 */		 
	GetLayeredWindowAttributes(hdc->hwnd, NULL, NULL, &dwFlags);
	if (dwFlags & LWA_COLORKEY)
	{
		int height;
		int width;			
		dfb_GetTextSize(pfont, str, count, flags, &width, &height, NULL);
		hdc->hwnd->DFB_windowSurface->SetColor( hdc->hwnd->DFB_windowSurface, 
			GetRValue(hdc->textcolor), GetGValue(hdc->textcolor), GetBValue(hdc->textcolor), 0x00 );									
		hdc->hwnd->DFB_windowSurface->FillRectangle( hdc->hwnd->DFB_windowSurface, x, y, width, height);
	}

	//TODO: change alpha value to GetAValue()
	hdc->hwnd->DFB_windowSurface->SetColor( hdc->hwnd->DFB_windowSurface, 
			GetRValue(hdc->textcolor), GetGValue(hdc->textcolor), GetBValue(hdc->textcolor), 0xFF );

	if ((flags & MWTF_PACKMASK) == MWTF_ASCII)
	{
		hdc->hwnd->DFB_windowSurface->DrawString( hdc->hwnd->DFB_windowSurface, str, count, x, y, alignment);			
	}
	else
	{	
		int i = 0;
		WORD *pGlyph = (WORD *)str;
		for (i = 0; i < count; i++)
		{
			int advance;
			DFBRectangle r;	
				
			font->GetGlyphExtents(font, pGlyph[i], &r, &advance);				
			hdc->hwnd->DFB_windowSurface->DrawGlyph( hdc->hwnd->DFB_windowSurface, pGlyph[i], x, y, alignment);
			x += advance;
		}		
	}

	if (hdc->hwnd->DFB_SurfaceLocked)
	{
   		hdc->hwnd->DFB_windowSurface->Lock( hdc->hwnd->DFB_windowSurface, DSLF_WRITE , NULL, NULL);
	}
}



