/* winlayered.h*/
/*
 *
 * Layered Windows
 */

#ifdef __cplusplus
extern "C"
{
#endif

BOOL IsLayeredPaintStarted(void);
HDC GetLayeredHDC(void);
LONG SendPaintMessage(HWND hwnd);
void chkUpdateRegion(HWND hWnd);


#ifdef __cplusplus
}
#endif
