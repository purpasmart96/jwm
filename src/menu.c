/***************************************************************************
 * Menu functions display and handling functions.
 * Copyright (C) 2004 Joe Wingbermuehle
 ***************************************************************************/

#include "jwm.h"

#define BASE_ICON_OFFSET 3

typedef enum {
	MENU_NOSELECTION = 0,
	MENU_LEAVE       = 1,
	MENU_SUBSELECT   = 2
} MenuSelectionType;

/* Submenu arrow, 4 x 7 pixels */
static char menu_bitmap[] = {
	0x01, 0x03, 0x07, 0x0F, 0x07, 0x03, 0x01
};

static int ShowSubmenu(MenuType *menu, MenuType *parent, int x, int y);

static void CreateMenu(MenuType *menu, int x, int y);
static void HideMenu(MenuType *menu);
static void DrawMenu(MenuType *menu);
static void RedrawMenuTree(MenuType *menu);

static int MenuLoop(MenuType *menu);
static MenuSelectionType UpdateMotion(MenuType *menu, XEvent *event);

static void UpdateMenu(MenuType *menu);
static void DrawMenuItem(MenuType *menu, MenuItemType *item, int index);
static MenuItemType *GetMenuItem(MenuType *menu, int index);
static int GetNextMenuIndex(MenuType *menu);
static int GetPreviousMenuIndex(MenuType *menu);
static int GetMenuIndex(MenuType *menu, int index);

static char *runCommand = NULL;

int menuShown = 0;

/***************************************************************************
 ***************************************************************************/
void InitializeMenu(MenuType *menu) {
	MenuItemType *np;
	int index, temp;
	int hasSubmenu;

	menu->textOffset = 0;
	menu->itemCount = 0;

	/* Compute the max size needed */
	menu->itemHeight = fonts[FONT_MENU]->ascent + fonts[FONT_MENU]->descent;
	for(np = menu->items; np; np = np->next) {
		if(np->iconName) {
			np->icon = LoadNamedIcon(np->iconName, 0);
			if(np->icon) {
				if(menu->itemHeight < np->icon->height) {
					menu->itemHeight = np->icon->height;
				}
				if(menu->textOffset < np->icon->width + 4) {
					menu->textOffset = np->icon->width + 4;
				}
			}
		} else {
			np->icon = NULL;
		}
		++menu->itemCount;
	}
	menu->itemHeight += 7;

	menu->width = 5;
	menu->parent = NULL;
	menu->parentOffset = 0;

	menu->height = 1;
	if(menu->label) {
		menu->height += menu->itemHeight;
	}

	menu->offsets = Allocate(sizeof(int) * menu->itemCount);

	hasSubmenu = 0;
	index = 0;
	for(np = menu->items; np; np = np->next) {
		menu->offsets[index++] = menu->height;
		if(np->name || np->command || np->submenu) {
			menu->height += menu->itemHeight;
		} else {
			menu->height += 5;
		}
		if(np->name) {
			temp = JXTextWidth(fonts[FONT_MENU], np->name, strlen(np->name));
			if(temp > menu->width) {
				menu->width = temp;
			}
		}
		if(np->submenu) {
			hasSubmenu = 7;
			InitializeMenu(np->submenu);
		}
	}
	menu->height += 2;
	menu->width += 11 + hasSubmenu + menu->textOffset;

}

/***************************************************************************
 ***************************************************************************/
void ShowMenu(MenuType *menu, RunMenuCommandType runner, int x, int y) {
	int mouseStatus, keyboardStatus;

	mouseStatus = GrabMouseForMenu();
	keyboardStatus = JXGrabKeyboard(display, rootWindow, False,
		GrabModeAsync, GrabModeAsync, CurrentTime);
	if(!mouseStatus && keyboardStatus != GrabSuccess) {
		return;
	}

	ShowSubmenu(menu, NULL, x, y);

	JXUngrabPointer(display, CurrentTime);
	JXUngrabKeyboard(display, CurrentTime);
	RefocusClient();

	if(runCommand) {
		(runner)(runCommand);
		runCommand = NULL;
	}

}

/***************************************************************************
 ***************************************************************************/
void DestroyMenu(MenuType *menu) {
	MenuItemType *np;

	if(menu) {
		while(menu->items) {
			np = menu->items->next;
			if(menu->items->name) {
				Release(menu->items->name);
			}
			if(menu->items->command) {
				Release(menu->items->command);
			}
			if(menu->items->iconName) {
				Release(menu->items->iconName);
			}
			if(menu->items->submenu) {
				DestroyMenu(menu->items->submenu);
			}
			Release(menu->items);
			menu->items = np;
		}
		if(menu->label) {
			Release(menu->label);
		}
		if(menu->offsets) {
			Release(menu->offsets);
		}
		Release(menu);
		menu = NULL;
	}
}

/***************************************************************************
 ***************************************************************************/
int ShowSubmenu(MenuType *menu, MenuType *parent, int x, int y) {
	int status;

	CreateMenu(menu, x, y);
	menu->parent = parent;

	++menuShown;
	status = MenuLoop(menu);
	--menuShown;

	HideMenu(menu);

	return status;
}

/***************************************************************************
 * Returns 0 if no selection was made or 1 if a selection was made.
 ***************************************************************************/
int MenuLoop(MenuType *menu) {
	XEvent event;
	MenuItemType *ip;
	int count;
	int hadMotion = 0;

	for(;;) {

		WaitForEvent(&event);

		switch(event.type) {
		case Expose:
			RedrawMenuTree(menu);
			break;
		case ButtonPress:
			if(event.xbutton.button != Button4
				&& event.xbutton.button != Button5) {
				break;		
			}
		case KeyPress:
		case MotionNotify:
			hadMotion = 1;
			switch(UpdateMotion(menu, &event)) {
			case MENU_NOSELECTION: /* no selection */
				break;
			case MENU_LEAVE: /* mouse left the menu */
				JXPutBackEvent(display, &event);
				return 0;
			case MENU_SUBSELECT: /* selection made */
				return 1;
			}
			break;
		case ButtonRelease:
			if(event.xbutton.button == Button4
				|| event.xbutton.button == Button5
				|| !hadMotion) {
				break;
			}
			if(menu->currentIndex >= 0) {
				count = 0;
				for(ip = menu->items; ip; ip = ip->next) {
					if(count == menu->currentIndex && ip->command) {
						runCommand = ip->command;
						break;
					}
					++count;
				}
			}
			return 1;
		default:
			break;
		}

	}
}

/***************************************************************************
 ***************************************************************************/
void CreateMenu(MenuType *menu, int x, int y) {
	int temp;

	menu->lastIndex = -1;
	menu->currentIndex = -1;

	if(x + menu->width > rootWidth) {
		x = rootWidth - menu->width;
		if(menu->parent) {
			menu->parent->wasCovered = 1;
		}
	}
	temp = y;
	if(y + menu->height > rootHeight - trayHeight) {
		y = rootHeight - trayHeight - menu->height;
	}
	if(y < 0) {
		y = 0;
	}

	menu->x = x;
	menu->y = y;
	menu->parentOffset = temp - y;

	menu->window = JXCreateSimpleWindow(display, rootWindow, x, y,
		menu->width, menu->height, 0, 0, colors[COLOR_MENU_BG]);
	JXMapRaised(display, menu->window);
	JXSelectInput(display, menu->window, ExposureMask);

	JXFlush(display);

	JXSetInputFocus(display, menu->window, RevertToPointerRoot, CurrentTime);
	menu->gc = JXCreateGC(display, menu->window, 0, NULL);
	JXSetFont(display, menu->gc, fonts[FONT_MENU]->fid);

	DrawMenu(menu);

}

/***************************************************************************
 ***************************************************************************/
void HideMenu(MenuType *menu) {
	JXFreeGC(display, menu->gc);
	JXDestroyWindow(display, menu->window);
}

/***************************************************************************
 ***************************************************************************/
void RedrawMenuTree(MenuType *menu) {

	if(menu->parent) {
		RedrawMenuTree(menu->parent);
	}

	DrawMenu(menu);
	UpdateMenu(menu);

}

/***************************************************************************
 ***************************************************************************/
void DrawMenu(MenuType *menu) {
	MenuItemType *np;
	int x;

	menu->wasCovered = 0;

	if(menu->label) {
		DrawMenuItem(menu, NULL, -1);
	}

	x = 0;
	for(np = menu->items; np; np = np->next) {
		DrawMenuItem(menu, np, x);
		++x;
	}

	JXSetForeground(display, menu->gc, colors[COLOR_MENU_UP]);
	JXDrawLine(display, menu->window, menu->gc,
		0, 0, menu->width - 1, 0);
	JXDrawLine(display, menu->window, menu->gc,
		0, 1, menu->width - 2, 1);
	JXDrawLine(display, menu->window, menu->gc,
		0, 2, 0, menu->height - 1);
	JXDrawLine(display, menu->window, menu->gc,
		1, 2, 1, menu->height - 2);

	JXSetForeground(display, menu->gc, colors[COLOR_MENU_DOWN]);
	JXDrawLine(display, menu->window, menu->gc,
		1, menu->height - 1, menu->width - 1, menu->height - 1);
	JXDrawLine(display, menu->window, menu->gc,
		2, menu->height - 2, menu->width - 1, menu->height - 2);
	JXDrawLine(display, menu->window, menu->gc,
		menu->width - 1, 1, menu->width - 1, menu->height - 3);
	JXDrawLine(display, menu->window, menu->gc,
		menu->width - 2, 2, menu->width - 2, menu->height - 3);

}

/***************************************************************************
 ***************************************************************************/
MenuSelectionType UpdateMotion(MenuType *menu, XEvent *event) {

	MenuItemType *ip;
	MenuType *tp;
	Window subwindow;
	int x, y;

	if(event->type == MotionNotify) {

		while(JXCheckTypedEvent(display, MotionNotify, event));

		x = event->xmotion.x - menu->x;
		y = event->xmotion.y - menu->y;
		subwindow = event->xmotion.subwindow;

	} else if(event->type == ButtonPress) {

		if(menu->currentIndex >= 0 || !menu->parent) {
			tp = menu;
		} else {
			tp = menu->parent;
		}

		y = -1;
		if(event->xbutton.button == Button4) {
			y = GetPreviousMenuIndex(tp);
		} else if(event->xbutton.button == Button5) {
			y = GetNextMenuIndex(tp);
		}

		if(y >= 0) {
			y = tp->offsets[y] + menu->itemHeight / 2;

			/* We need to do this twice so the event gets registered
			 * on the submenu if one exists. */
			JXWarpPointer(display, None, tp->window, 0, 0, 0, 0, 6, y);
			JXWarpPointer(display, None, tp->window, 0, 0, 0, 0, 6, y);
		}

		return MENU_NOSELECTION;

	} else if(event->type == KeyPress) {

		if(menu->currentIndex >= 0 || !menu->parent) {
			tp = menu;
		} else {
			tp = menu->parent;
		}

		y = -1;
		switch(GetKey(&event->xkey) & 0xFF) {
		case KEY_UP:
			y = GetPreviousMenuIndex(tp);
			break;
		case KEY_DOWN:
			y = GetNextMenuIndex(tp);
			break;
		case KEY_RIGHT:
			tp = menu;
			y = 0;
			break;
		case KEY_LEFT:
			if(tp->parent) {
				tp = tp->parent;
				if(tp->currentIndex >= 0) {
					y = tp->currentIndex;
				} else {
					y = 0;
				}
			}
			break;
		case KEY_ESC:
			return MENU_SUBSELECT;
		case KEY_ENTER:
			if(tp->currentIndex >= 0) {
				x = 0;
				for(ip = tp->items; ip; ip = ip->next) {
					if(x == tp->currentIndex && ip->command) {
						runCommand = ip->command;
						break;
					}
					++x;
				}
			}
			return MENU_SUBSELECT;
		default:
			break;
		}

		if(y >= 0) {
			y = tp->offsets[y] + menu->itemHeight / 2;

			/* We need to do this twice so the event gets registered
			 * on the submenu if one exists. */
			JXWarpPointer(display, None, tp->window, 0, 0, 0, 0, 6, y);
			JXWarpPointer(display, None, tp->window, 0, 0, 0, 0, 6, y);
		}

		return MENU_NOSELECTION;

	} else {
		Debug("invalid event type in menu.c:UpdateMotion");
		return MENU_SUBSELECT;
	}

	/* Update the selection on the current menu */
	if(x > 0 && y > 0 && x < menu->width && y < menu->height) {
		menu->currentIndex = GetMenuIndex(menu, y);
	} else {
		for(tp = menu->parent; tp; tp = tp->parent) {
			if(subwindow == tp->window) {
				if(y < menu->parentOffset
					|| y > tp->itemHeight + menu->parentOffset) {
					return MENU_LEAVE;
				}
			}
		}
		menu->currentIndex = -1;
	}
	if(menu->lastIndex != menu->currentIndex) {
		UpdateMenu(menu);
		menu->lastIndex = menu->currentIndex;
	}

	/* If the selected item is a submenu, show it. */
	ip = GetMenuItem(menu, menu->currentIndex);
	if(ip && ip->submenu) {
		if(ShowSubmenu(ip->submenu, menu, menu->x + menu->width,
			menu->y + menu->offsets[menu->currentIndex])) {

			/* Item selected; destroy the menu tree. */
			return MENU_SUBSELECT;

		} else {

			/* No selection made; redraw. */
			if(menu->wasCovered) {
				DrawMenu(menu);
			}
			UpdateMenu(menu);

		}
	}

	return MENU_NOSELECTION;

}

/***************************************************************************
 ***************************************************************************/
void UpdateMenu(MenuType *menu) {
	Pixmap pixmap;
	MenuItemType *ip;

	/* Clear the old selection. */
	ip = GetMenuItem(menu, menu->lastIndex);
	DrawMenuItem(menu, ip, menu->lastIndex);

	/* Highlight the new selection. */
	ip = GetMenuItem(menu, menu->currentIndex);
	if(ip) {
		if(!ip->name && !ip->submenu && !ip->command) {
			return;
		}
		SetButtonDrawable(menu->window, menu->gc);
		SetButtonFont(FONT_MENU);
		SetButtonSize(menu->width - 5, menu->itemHeight - 2);
		SetButtonTextOffset(menu->textOffset);
		SetButtonAlignment(ALIGN_LEFT);
		DrawButton(2, menu->offsets[menu->currentIndex] + 1,
			BUTTON_MENU_ACTIVE, ip->name);

		if(ip->icon) {
			PutIcon(ip->icon, menu->window, menu->gc,
				menu->textOffset / 2 - ip->icon->width / 2 + BASE_ICON_OFFSET,
				menu->offsets[menu->currentIndex]
				+ menu->itemHeight / 2 - ip->icon->height / 2);
		}

		if(ip->submenu) {
			pixmap = JXCreatePixmapFromBitmapData(display, menu->window,
				menu_bitmap, 4, 7, colors[COLOR_MENU_ACTIVE_FG],
				colors[COLOR_MENU_ACTIVE_BG], rootDepth);
			JXCopyArea(display, pixmap, menu->window, menu->gc, 0, 0, 4, 7,
				menu->width - 9,
				menu->offsets[menu->currentIndex] + menu->itemHeight / 2 - 4);
			JXFreePixmap(display, pixmap);

		}
	}

}

/***************************************************************************
 ***************************************************************************/
void DrawMenuItem(MenuType *menu, MenuItemType *item, int index) {

	Pixmap pixmap;
	int xoffset;
	int yoffset;

	Assert(menu);

	if(!item) {
		if(index == -1 && menu->label) {
			JXSetForeground(display, menu->gc, colors[COLOR_MENU_BG]);
			JXFillRectangle(display, menu->window, menu->gc,
				2, 2, menu->width - 4, menu->itemHeight - 1);
			xoffset = menu->width / 2;
			xoffset -= JXTextWidth(fonts[FONT_MENU], menu->label,
				strlen(menu->label)) / 2;
			yoffset = 1 + menu->itemHeight / 2;
			yoffset -= (fonts[FONT_MENU]->ascent + fonts[FONT_MENU]->descent) / 2;
			RenderString(menu->window, menu->gc, FONT_MENU, RAMP_MENU,
				xoffset, yoffset, rootWidth, menu->label);
		}
		return;
	}

	if(item->name) {

		xoffset = 5 + menu->textOffset;

		JXSetForeground(display, menu->gc, colors[COLOR_MENU_BG]);
		JXFillRectangle(display, menu->window, menu->gc,
			2, menu->offsets[index] + 1, menu->width - 4, menu->itemHeight - 1);

		if(item->icon) {
			PutIcon(item->icon, menu->window, menu->gc,
				menu->textOffset / 2 - item->icon->width / 2 + BASE_ICON_OFFSET,
				menu->offsets[index] + menu->itemHeight / 2
				- item->icon->height / 2);
		}

		RenderString(menu->window, menu->gc, FONT_MENU, RAMP_MENU,
			xoffset, menu->offsets[index] + menu->itemHeight / 2
			- (fonts[FONT_MENU]->ascent + fonts[FONT_MENU]->descent) / 2,
			rootWidth, item->name);

	} else if(!item->command && !item->submenu) {
		JXSetForeground(display, menu->gc, colors[COLOR_MENU_DOWN]);
		JXDrawLine(display, menu->window, menu->gc, 4,
			menu->offsets[index] + 2, menu->width - 6,
			menu->offsets[index] + 2);
		JXSetForeground(display, menu->gc, colors[COLOR_MENU_UP]);
		JXDrawLine(display, menu->window, menu->gc, 4,
			menu->offsets[index] + 3, menu->width - 6,
			menu->offsets[index] + 3);
	}

	if(item->submenu) {
		pixmap = JXCreatePixmapFromBitmapData(display, menu->window,
			menu_bitmap, 4, 7, colors[COLOR_MENU_FG],
			colors[COLOR_MENU_BG], rootDepth);
		JXCopyArea(display, pixmap, menu->window, menu->gc, 0, 0, 4, 7,
			menu->width - 9, menu->offsets[index] + menu->itemHeight / 2 - 4);
		JXFreePixmap(display, pixmap);

	}

}

/***************************************************************************
 ***************************************************************************/
int GetNextMenuIndex(MenuType *menu) {
	MenuItemType *item;
	int x;

	for(x = menu->currentIndex + 1; x < menu->itemCount; x++) {
		item = GetMenuItem(menu, x);
		if(item->name || item->command || item->submenu) {
			return x;
		}
	}

	return 0;

}

/***************************************************************************
 ***************************************************************************/
int GetPreviousMenuIndex(MenuType *menu) {
	MenuItemType *item;
	int x;

	for(x = menu->currentIndex - 1; x >= 0; x--) {
		item = GetMenuItem(menu, x);
		if(item->name || item->command || item->submenu) {
			return x;
		}
	}

	return menu->itemCount - 1;
}

/***************************************************************************
 ***************************************************************************/
int GetMenuIndex(MenuType *menu, int y) {
	int x;

	if(y < menu->offsets[0]) {
		return -1;
	}
	for(x = 0; x < menu->itemCount - 1; x++) {
		if(y >= menu->offsets[x] && y < menu->offsets[x + 1]) {
			return x;
		}
	}
	return x;

}

/***************************************************************************
 ***************************************************************************/
MenuItemType *GetMenuItem(MenuType *menu, int index) {
	MenuItemType *ip;

	if(index >= 0) {
		for(ip = menu->items; ip; ip = ip->next) {
			if(!index) {
				return ip;
			}
			--index;
		}
	} else {
		ip = NULL;
	}

	return ip;
}
