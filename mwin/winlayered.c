/*
 *
 * Implementation of Layered Windows
 *
 * Original author: Allan Shin
 */
#include "windows.h"
#include "wintern.h"
#include "device.h"
#include <string.h>


#define DEBUG_LAYERED_WINDOWS	0

#if DEBUG_LAYERED_WINDOWS
#define DLW_PRINTF(str, args...)	printf(str, ##args)
#else
#define DLW_PRINTF(str, args...)
#endif

typedef struct tagLWPAINTSTRUCT {
	HDC		hdc;
	HDC		hdcMem;
	RECT	clientRect;
	RECT	wndRect;
} LWPAINTSTRUCT;

/*static variables used to maintain consistent WIN32 API*/
static HWND currentTargetHWnd = NULL;
static HDC hdcToPaint = NULL;


static MWBITMAPOBJ default_bitmap = {
	{OBJ_BITMAP, TRUE}, 1, 1, 1, 1, 1, 1
};

extern HWND rootwp;
extern int mwpaintSerial;
extern BOOL mwforceNCpaint;
extern void SetAlphaValue(int);

/*
 * Forward Delclarations
 */
static void ShowImmediateChildWindows(HWND);
static void HideImmediateChildWindows(HWND);
static BOOL isLayeredParentWindow(HWND hWnd);

void BitBltMergeLayers(HDC hdcDest, int nWidth, int nHeight, HDC hdcSrc, HWND hWnd)
{
	if (hdcSrc && hdcDest)
	{
		if (hWnd->exstyle & WS_EX_LAYERED)
		{
			DWORD alphaValue;

			POINT ptDelta = {0,0};		
			ptDelta.x = hWnd->winrect.left - hdcDest->pt.x;
			ptDelta.y = hWnd->winrect.top - hdcDest->pt.y;
			
			/*transparency enabled using color key crKey*/
			if (hWnd->dwFlags & LWA_COLORKEY)
			{
				/* mask the color key's most significant byte to 0*/
				COLORREF ColorKey = hWnd->crKey & ~MWROP_EXTENSION;

				if (hWnd->dwFlags & LWA_ALPHA)
				{
					SetAlphaValue(hWnd->bAlpha);
					BitBlt(hdcDest, ptDelta.x, ptDelta.y, nWidth, nHeight, hdcSrc, 0, 0, MWROP_BLENDTRANS | ColorKey);
				} 
				else
				{
					BitBlt(hdcDest, ptDelta.x, ptDelta.y, nWidth, nHeight, hdcSrc, 0, 0, MWROP_SRCTRANSCOPY | ColorKey);
				}				
			}
			else
			{
				alphaValue = (hWnd->dwFlags & LWA_ALPHA) ? (MWROP_BLENDCONSTANT | hWnd->bAlpha) : SRCCOPY;

				if (isLayeredParentWindow(hWnd))
				{
					HRGN hrgn, tempHrgn;
					RECT clientRect, tempRect;
					POINT clientOrigin;
					HWND wp = NULL;						
					
					GetClientRect(currentTargetHWnd, &clientRect);		
					hrgn = CreateRectRgnIndirect(&clientRect);

					/*store screen coordinates of client window for offset*/
					clientOrigin.x = currentTargetHWnd->winrect.left;
					clientOrigin.y = currentTargetHWnd->winrect.top;
					
					/* start with entire client area for clip region,
					 * we then cut out regions occupied by child windows*/
					SelectClipRgn(hdcDest, hrgn);

					/* traverse immediate children to build clip rectangles.
					 * First we call show immediate children to enable
					 * clipping on children for one hide level, check the
					 * clipping to add subtract from clientrect, then disable
					 * back clipping before returning to recursive call
					 */
					ShowImmediateChildWindows(hWnd);
					
					for (wp = hWnd->children; wp; wp=wp->siblings)
					{
						if (wp->unmapcount == 0)
						{
							tempRect.left = wp->winrect.left - clientOrigin.x;
							tempRect.right = wp->winrect.right - clientOrigin.x;
							tempRect.top = wp->winrect.top - clientOrigin.y;
							tempRect.bottom = wp->winrect.bottom - clientOrigin.y;
							
							tempHrgn = CreateRectRgnIndirect(&tempRect);
							ExtSelectClipRgn(hdcDest, tempHrgn, RGN_DIFF);
							DeleteObject(tempHrgn);
						}
					}
					HideImmediateChildWindows(hWnd);

					BitBlt(hdcDest, ptDelta.x, ptDelta.y, nWidth, nHeight, hdcSrc, 0, 0, alphaValue);
	
					SelectClipRgn(hdcDest, NULL);
					DeleteObject(hrgn);
					
				}	
				else
				{
					BitBlt(hdcDest, ptDelta.x, ptDelta.y, nWidth, nHeight, hdcSrc, 0, 0, alphaValue);				
				}
			}
		}
		else
		{
			BitBlt(hdcDest, 0, 0, nWidth, nHeight, hdcSrc, 0, 0, SRCCOPY);		
		}
	}
}



inline static HWND Parent(HWND hWnd)
{
	return hWnd->parent;
}

inline static HWND Children(HWND hWnd)
{
	return hWnd->children;
}

inline static HWND NextSibling(HWND hWnd)
{
	return hWnd->siblings;
}

inline static BOOL isNodeVisited(HWND hWnd)
{
	return (hWnd->paintSerial == mwpaintSerial);
}

inline static void setNodeVisited(HWND hWnd)
{
	hWnd->paintSerial = mwpaintSerial;
}


/* Enable clipping to this window */
static void ShowThisWindow(HWND hWnd)
{
	if (hWnd == rootwp)
		return;

	--hWnd->unmapcount;
}

/* Disable clipping to this window */
static void HideThisWindow(HWND hWnd)
{
	if (hWnd == rootwp)
		return;
	++hWnd->unmapcount;

}

/* Disable clipping for all levels down (children)*/
static void HideChildWindows(HWND hWnd)
{
	HWND wp = NULL;

	wp = Children(hWnd);
	if (wp != NULL)
	{
		++wp->unmapcount;
		HideChildWindows(wp);
	}
	
	wp = NextSibling(hWnd);
	if (wp != NULL)
	{
		++wp->unmapcount;
		HideChildWindows(wp);
	}
}

/* Shows all levels down (children) */
static void ShowChildWindows(HWND hWnd)
{
	HWND wp = NULL;

	wp = Children(hWnd);
	if (wp != NULL)
	{
		--wp->unmapcount;
		ShowChildWindows(wp);
	}
	
	wp = NextSibling(hWnd);
	if (wp != NULL)
	{
		--wp->unmapcount;
		ShowChildWindows(wp);
	}
	
}

/* Disable the clipping from overlapped windows 
 * by hiding all windows except for the current
 * window, leaving unobstructed rendering 
 */
static void DisableClippingOverlapWindows(HWND hWnd)
{
	HideChildWindows(rootwp);
	ShowThisWindow(hWnd);
}

/* Restore the clipping from overlapped windows 
 * by restoring visibility to all windows.
 */
static void EnableClippingOverlapWindows(HWND hWnd)
{
	HideThisWindow(hWnd);
	ShowChildWindows(rootwp);
}

/* Check if 2 rectangles overlap eachother*/
BOOL CheckOverlapRect(RECT *Rect1, RECT *Rect2)
{
	MWCOORD	minx1;
	MWCOORD	miny1;
	MWCOORD	maxx1;
	MWCOORD	maxy1;
	MWCOORD	minx2;
	MWCOORD	miny2;
	MWCOORD	maxx2;
	MWCOORD	maxy2;

	minx1 = Rect1->left;
	miny1 = Rect1->top;
	maxx1 = Rect1->right - 1;
	maxy1 = Rect1->bottom - 1;
	
	minx2 = Rect2->left;
	miny2 = Rect2->top;
	maxx2 = Rect2->right - 1;
	maxy2 = Rect2->bottom - 1;
	
	if (minx1 > maxx2 || minx2 > maxx1 || miny1 > maxy2 || miny2 > maxy1)
		return FALSE;
		
	return TRUE;
}


static BOOL isLayeredParentWindow(HWND hWnd)
{
	if (hWnd == rootwp)
		return FALSE;
		
	if (hWnd->children)
		return TRUE;
		
	return FALSE;
}


/*Shows one level down children*/
static void ShowImmediateChildWindows(HWND hWnd)
{
	for (hWnd = Children(hWnd); hWnd; hWnd=NextSibling(hWnd))
	{
		--hWnd->unmapcount;
	}
}

/*Hides one level down children*/
static void HideImmediateChildWindows(HWND hWnd)
{
	for (hWnd = Children(hWnd); hWnd; hWnd=NextSibling(hWnd))
	{
		++hWnd->unmapcount;
	}
}


/* Print hwnd tree's unmapcount starting at hWnd */
#if 0
void PrintUnmapcount(HWND hWnd)
{
	HWND wp = NULL;
	
	printf("hWnd = %x, unmapcount = %d, shouldPaint = %d, %s\n", hWnd, hWnd->unmapcount, hWnd->shouldPaint, hWnd->pClass->lpszClassName);

	wp = NextSibling(hWnd);
	if (wp != NULL)
	{
		PrintUnmapcount(wp);
	}
	
	wp = Children(hWnd);
	if (wp != NULL)
	{
		PrintUnmapcount(wp);
	}
}
#endif


static BOOL isWindowOpaque(HWND hWnd)
{
	return !((hWnd->dwFlags & LWA_COLORKEY) || (hWnd->dwFlags & LWA_ALPHA));
}	

static void ResetPaintFlag(void)
{
	HWND wp;
	for (wp=listwp; wp; wp=wp->next)
	{
		wp->shouldPaint = FALSE;
	}
}

BOOL IsLayeredPaintStarted(void)
{
	return (currentTargetHWnd ? TRUE : FALSE);
}


static BOOL IsRectAInRectB(RECT *RectA, RECT *RectB)
{
	return ((RectA->left >= RectB->left) && (RectA->top >= RectB->top) && 
		(RectA->right <= RectB->right) && (RectA->bottom <= RectB->bottom));
}

static void MarkPaintWindows(HWND hwnd, BOOL shouldPaint, LWPAINTSTRUCT *ps)
{
	HWND wp;

	setNodeVisited(hwnd);
	
	if (shouldPaint)
	{
		if (CheckOverlapRect(&hwnd->winrect, &ps->wndRect))
		{
			hwnd->shouldPaint = TRUE;
		}
	}
	else
	{
		hwnd->shouldPaint = FALSE;
	}
	
	wp = Parent(hwnd);
	if (wp)
	{
		if (!isNodeVisited(wp))	
		{
			if (isWindowOpaque(hwnd))
				MarkPaintWindows(wp, FALSE, ps);
			else
				MarkPaintWindows(wp, shouldPaint, ps);
		}
	}
	
	wp = NextSibling(hwnd);
	if (wp)
	{
		if (!isNodeVisited(wp))	
		{
			if (isWindowOpaque(hwnd) && IsRectAInRectB(&ps->wndRect, &hwnd->winrect))
				MarkPaintWindows(wp, FALSE, ps);
			else
				MarkPaintWindows(wp, shouldPaint, ps);
		}
	}

	wp = Children(hwnd);
	if (wp)
	{
		if (!isNodeVisited(wp))
		{
			if (hwnd != rootwp)
				MarkPaintWindows(wp, hwnd->shouldPaint, ps);
			else
				MarkPaintWindows(wp, TRUE, ps);
		}
	}
}

static void BeginGlobalPaint(HWND hwnd, LWPAINTSTRUCT *ps)
{
		DLW_PRINTF("-- Begin Paint of hwnd=0x%x, %s\n", hwnd, hwnd->pClass->lpszClassName);
		POINT ptDest = {0,0};

		ps->hdc = GetDCEx(hwnd, NULL, DCX_DEFAULTCLIP | DCX_EXCLUDEUPDATE);	
		
		/*create memory dc to pass around for all window rendering*/
		ps->hdcMem = CreateCompatibleDC(ps->hdc);
		HBITMAP hbmpOrg, hbmMem; 
		GetClientRect(hwnd, &ps->clientRect);
		hbmMem = CreateCompatibleBitmap(ps->hdc, ps->clientRect.right, ps->clientRect.bottom);
		hbmpOrg = SelectObject(ps->hdcMem, hbmMem);


		/*save the target hWnd's client offset, so blit will know delta shift*/
		ClientToScreen(hwnd, &ptDest);
		ps->hdcMem->pt.x = ptDest.x;
		ps->hdcMem->pt.y = ptDest.y;
		
		ps->wndRect = hwnd->winrect;

		currentTargetHWnd = hwnd;
	
		ResetPaintFlag();
		MarkPaintWindows(hwnd, TRUE, ps);

		mwpaintSerial++;
		HideChildWindows(rootwp);	
}

static void EndGlobalPaint(HWND hwnd, LWPAINTSTRUCT *ps)
{
		DLW_PRINTF("-- End Paint of hwnd=0x%x, %s\n", hwnd, hwnd->pClass->lpszClassName);
		
		/*disable clipping so blit can freely paint area*/
		DisableClippingOverlapWindows(hwnd);

		/*blit final memory dc bitmap to screen*/
		BitBlt(ps->hdc, 0, 0, ps->clientRect.right, ps->clientRect.bottom, ps->hdcMem, 0, 0, SRCCOPY);	

		/*enable back clipping entirely*/
		EnableClippingOverlapWindows(hwnd);

		/*cleanup temporary memory dc related stuff*/
		DeleteObject(SelectObject(ps->hdcMem, (HGDIOBJ)&default_bitmap));
		DeleteDC(ps->hdcMem);
		ReleaseDC(hwnd, ps->hdc);
		currentTargetHWnd = NULL;
	
		/*done blitting, allow other sendmessage WM_PAINT requests*/
		hwnd->gotPaintMsg = PAINT_PAINTED;
}

static void PaintWindowNow(HWND hwnd, LWPAINTSTRUCT *ps)
{
		ShowThisWindow(hwnd);

		if (!hwnd->shouldPaint)
			return;

		if (!IsWindowVisible(hwnd))
			return;

		hdcToPaint = CreateCompatibleDC(ps->hdcMem);
		HBITMAP hbmMem; 
		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		hbmMem = CreateCompatibleBitmap(ps->hdcMem, clientRect.right, clientRect.bottom);
		SelectObject(hdcToPaint, hbmMem);
		
		if (hwnd->exstyle & WS_EX_TRANSPARENT)
		{

			POINT ptDelta;
	
			/* copy from currentTargetHDC bitmap with an offset to the
			 * newly created source bitmap at 0,0.  The offset is where this
			 * window starts relative to the target window. */

			ptDelta.x = hwnd->winrect.left - ps->hdcMem->pt.x;
			ptDelta.y = hwnd->winrect.top - ps->hdcMem->pt.y;
	
			BitBlt(hdcToPaint, 0, 0, clientRect.right, clientRect.bottom, 
				ps->hdcMem, ptDelta.x, ptDelta.y, SRCCOPY);	
		}

		DLW_PRINTF("Painting hwnd=0x%x, %s\n", hwnd, hwnd->pClass->lpszClassName);		

		SendMessage(hwnd, WM_PAINT, 0, 0);

		if (currentTargetHWnd != hwnd && 
				hwnd->gotPaintMsg != PAINT_PAINTED && 
				IsRectAInRectB(&hwnd->winrect, &ps->wndRect))
		{
			DLW_PRINTF("Cancelling future Paint on hwnd=0x%x, %s\n", hwnd, hwnd->pClass->lpszClassName);
			hwnd->gotPaintMsg = PAINT_PAINTED;
		}

		BitBltMergeLayers(ps->hdcMem, hwnd->clirect.right, 
				hwnd->clirect.bottom, hdcToPaint, hwnd);	

		/* cleanup */
		DeleteObject(SelectObject(hdcToPaint, (HGDIOBJ)&default_bitmap));
		DeleteDC(hdcToPaint);
		hdcToPaint = NULL;	
}



static void PaintWindow(HWND hwnd, LWPAINTSTRUCT *ps)
{
	HWND wp;
	
	wp = NextSibling(hwnd);
	if (wp)
	{
		PaintWindow(wp, ps);
	}
	
	PaintWindowNow(hwnd, ps);
	
	wp = Children(hwnd);
	if (wp)
	{
		PaintWindow(wp, ps);
	}
}

static void GlobalPaintWindow(HWND hwnd)
{
	LWPAINTSTRUCT ps;
	BeginGlobalPaint(hwnd, &ps);
	PaintWindow(rootwp, &ps);
	EndGlobalPaint(hwnd, &ps);
}

/* Get the HDC required for painting by BeginPaint() call*/
HDC GetLayeredHDC(void)
{
	return hdcToPaint;
}

LONG SendPaintMessage(HWND hwnd)
{
#if USE_OFFSCREEN_BUFFER
	/*ashin 01/26/06: added for offscreen buffer update.
	Add window rect if it is the current one being painted.
	Need to check this, because of layered window's 
	recurrsive painting will always paint lower layers like root, 
	and we don't want to add root to update region.*/
	MwAddUpdateRectToScreenBuffer(&hwnd->winrect);
#endif	

//directfb
#if 0
	if (!IsLayeredPaintStarted() && (hwnd->exstyle & WS_EX_LAYERED))
	{
		GlobalPaintWindow(hwnd);
		return 0;
	}
#endif
	return SendMessage(hwnd, WM_PAINT, 0, 0);
}

/* clear update region for layered windows repainting.
 * Since layered windows is recursive, you cannot clear this
 * region after the window is painted, since other windows
 * may depend on repainting this region too.  Instead, enqueue
 * a message to clear region until all painting is complete
 */
void chkUpdateRegion(HWND hWnd)
{
	if (hWnd->gotClearRegionMsg == REGION_NEEDSCLEAR)
	{
		hWnd->gotClearRegionMsg = REGION_CLEARED;
		GdSetRectRegion(hWnd->update, 0, 0, 0, 0);
	}	
}
