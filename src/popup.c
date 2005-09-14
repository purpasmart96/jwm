/****************************************************************************
 * Functions for displaying popup windows.
 * Copyright (C) 2004 Joe Wingbermuehle
 ****************************************************************************/

#include "jwm.h"

typedef struct PopupType {
	int isActive;
	int x, y;   /* The coordinates of the upper-left corner of the popup. */
	int mx, my; /* The mouse position when the popup was created. */
	unsigned int width, height;
	char *text;
	Window window;
	GC gc;
} PopupType;

static PopupType popup;
static int popupEnabled = 1;

static void DrawPopup();

/****************************************************************************
 ****************************************************************************/
void InitializePopup() {
}

/****************************************************************************
 ****************************************************************************/
void StartupPopup() {
	popup.isActive = 0;
	popup.text = NULL;
	popup.window = None;
	popup.gc = None;
}

/****************************************************************************
 ****************************************************************************/
void ShutdownPopup() {
	if(popup.text) {
		Release(popup.text);
		popup.text = NULL;
	}
	if(popup.gc != None) {
		XFreeGC(display, popup.gc);
		popup.gc = None;
	}
	if(popup.window != None) {
		XDestroyWindow(display, popup.window);
		popup.window = None;
	}
}

/****************************************************************************
 ****************************************************************************/
void DestroyPopup() {
}

/****************************************************************************
 * Show a popup window.
 * x - The x coordinate of the popup window.
 * y - The y coordinate of the popup window.
 * text - The text to display in the popup.
 ****************************************************************************/
void ShowPopup(int x, int y, const char *text) {

	int screenIndex;
	int len;

	Assert(text);

	if(!popupEnabled) {
		return;
	}

	if(popup.text) {
		Release(popup.text);
		popup.text = NULL;
	}

	len = strlen(text);
	if(!len) {
		return;
	}

	popup.text = Allocate(len + 1);
	strcpy(popup.text, text);

	popup.height = fonts[FONT_POPUP]->ascent + fonts[FONT_POPUP]->descent;
	popup.width = XTextWidth(fonts[FONT_POPUP], popup.text, len);

	popup.height += 1;
	popup.width += 1; 

	screenIndex = GetCurrentScreen(x, y);

	if(popup.width > GetScreenWidth(screenIndex)) {
		popup.width = GetScreenWidth(screenIndex);
	}

	popup.x = x;
	popup.y = y;

	if(popup.width + popup.x >= GetScreenWidth(screenIndex)) {
		popup.x = GetScreenWidth(screenIndex) - popup.width - 2;
	}
	if(popup.height + popup.y >= GetScreenHeight(screenIndex) - trayHeight) {
		popup.y = GetScreenHeight(screenIndex) - trayHeight - popup.height - 2;
	}

	if(popup.window == None) {
		popup.window = JXCreateSimpleWindow(display, rootWindow, popup.x,
			popup.y, popup.width, popup.height, 1, colors[COLOR_POPUP_OUTLINE],
			colors[COLOR_POPUP_BG]);
		popup.gc = JXCreateGC(display, popup.window, 0, NULL);
		JXSelectInput(display, popup.window, ExposureMask);
	} else {
		JXMoveResizeWindow(display, popup.window, popup.x, popup.y,
			popup.width, popup.height);
	}

	GetMousePosition(&popup.mx, &popup.my);

	if(!popup.isActive) {
		JXMapRaised(display, popup.window);
		popup.isActive = 1;
	} else {
		DrawPopup();
	}

}

/****************************************************************************
 ****************************************************************************/
void SetPopupEnabled(int e) {
	popupEnabled = e;
}

/****************************************************************************
 ****************************************************************************/
int ProcessPopupEvent(const XEvent *event) {
	int x, y;

	if(popup.isActive) {

		GetMousePosition(&x, &y);

		if(abs(popup.mx - x) > 2 || abs(popup.my - y) > 2) {
			JXUnmapWindow(display, popup.window);
			popup.isActive = 0;
			return 0;
		}

		switch(event->type) {
		case Expose:
			if(event->xexpose.window == popup.window) {
				DrawPopup();
				return 1;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

/****************************************************************************
 ****************************************************************************/
void DrawPopup() {

	Assert(popup.isActive);

	JXClearWindow(display, popup.window);
	RenderString(popup.window, popup.gc, FONT_POPUP, RAMP_POPUP, 1, 1,
		popup.width, popup.text);

}
