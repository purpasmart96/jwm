/***************************************************************************
 * Menu functions display and handling functions.
 * Copyright (C) 2004 Joe Wingbermuehle
 ***************************************************************************/

#include "jwm.h"
#include "menu.h"
#include "font.h"
#include "client.h"
#include "color.h"
#include "icon.h"
#include "image.h"
#include "main.h"
#include "cursor.h"
#include "key.h"
#include "button.h"
#include "event.h"
#include "theme.h"

#define ICON_SPACER 4

typedef enum {
	MENU_NOSELECTION = 0,
	MENU_LEAVE       = 1,
	MENU_SUBSELECT   = 2
} MenuSelectionType;

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
static void SetPosition(MenuType *tp, int index);

static int northOffset = 0;
static int southOffset = 0;
static int eastOffset = 0;
static int westOffset = 0;

static char *runCommand = NULL;

int menuShown = 0;

/***************************************************************************
 ***************************************************************************/
void InitializeMenu(MenuType *menu) {

	MenuItemType *np;
	int index, temp;
	int hasSubmenu;
	int userHeight;
	int north, south, east, west;

	northOffset = parts[PART_MENU_T].height;
	southOffset = parts[PART_MENU_B].height;
	eastOffset = parts[PART_MENU_R].width;
	westOffset = parts[PART_MENU_L].width;

	menu->itemCount = 0;

	GetButtonOffsets(&north, &south, &east, &west);

	/* Compute the width and item height. */
	hasSubmenu = 0;
	userHeight = menu->itemHeight;
	if(userHeight < 0) {
		userHeight = 0;
	}
	if(userHeight == 0) {
		menu->itemHeight = GetStringHeight(FONT_MENU) + north + south;
	}
	menu->width = 0;
	for(np = menu->items; np; np = np->next) {

		/* Load the icon (if there is one). */
		if(np->iconName) {
			np->icon = LoadNamedIcon(np->iconName);
			if(np->icon) {
				if(userHeight == 0) {
					temp = np->icon->image->height + north + south;
					if(menu->itemHeight < temp) {
						menu->itemHeight = temp;
					}
				}
			}
		} else {
			np->icon = NULL;
		}

		/* Determine the width of this item. */
		if(np->icon && np->name) {
			temp = menu->itemHeight + GetStringWidth(FONT_MENU, np->name);
			temp += ICON_SPACER;
		} else if(np->icon) {
			temp = menu->itemHeight;
		} else if(np->name) {
			temp = GetStringWidth(FONT_MENU, np->name);
		} else {
			temp = 0;
		}
		if(np->submenu) {
			hasSubmenu = parts[PART_SUBMENU].width;
		}
		if(temp > menu->width) {
			menu->width = temp;
		}

		++menu->itemCount;
	}
	menu->width += hasSubmenu + eastOffset + westOffset + east + west;
	if(userHeight) {
		menu->itemHeight = userHeight + north + south;
	}

	menu->parent = NULL;
	menu->parentOffset = 0;

	menu->offsets = Allocate(sizeof(int) * menu->itemCount);

	/* Compute the height. */
	menu->height = northOffset;
	if(menu->label) {
		menu->height += menu->itemHeight;
	}
	index = 0;
	for(np = menu->items; np; np = np->next) {
		menu->offsets[index++] = menu->height;
		if(np->name || np->command || np->submenu) {
			menu->height += menu->itemHeight;
		} else {
			menu->height += parts[PART_MENU_S].height;
		}
		if(np->submenu) {
			InitializeMenu(np->submenu);
		}
	}
	menu->height += southOffset;

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
	int hadPress = 0;

	for(;;) {

		WaitForEvent(&event);

		switch(event.type) {
		case Expose:
			RedrawMenuTree(menu);
			break;
		case ButtonPress:
			hadPress = 1;
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
				|| (!hadMotion && !hadPress)) {
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
	if(y + menu->height > rootHeight) {
		y = rootHeight - menu->height;
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

	DrawThemeBackground(PART_MENU, COLOR_MENU_BG, menu->window, menu->gc,
		0, 0, menu->width, menu->height, 0);

	DrawThemeOutline(PART_MENU, menu->window, menu->gc,
		0, 0, menu->width, menu->height, 0);

	ApplyThemeShape(PART_MENU, menu->window, menu->width, menu->height);

	menu->wasCovered = 0;

	if(menu->label) {
		DrawMenuItem(menu, NULL, -1);
	}

	x = 0;
	for(np = menu->items; np; np = np->next) {
		DrawMenuItem(menu, np, x);
		++x;
	}

}

/***************************************************************************
 ***************************************************************************/
MenuSelectionType UpdateMotion(MenuType *menu, XEvent *event) {

	MenuItemType *ip;
	MenuType *tp;
	Window subwindow;
	int x, y;

	if(event->type == MotionNotify) {

		DiscardMotionEvents(event, menu->window);

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
			SetPosition(tp, y);
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
			SetPosition(tp, y);
		}

		return MENU_NOSELECTION;

	} else {
		Debug("invalid event type in menu.c:UpdateMotion");
		return MENU_SUBSELECT;
	}

	/* Update the selection on the current menu */
	if(x > 0 && y > 0 && x < menu->width && y < menu->height) {
		menu->currentIndex = GetMenuIndex(menu, y);
	} else if(menu->parent && subwindow != menu->parent->window) {

		/* Leave if over a menu window. */
		for(tp = menu->parent->parent; tp; tp = tp->parent) {
			if(tp->window == subwindow) {
				return MENU_LEAVE;
			}
		}
		menu->currentIndex = -1;

	} else {

		/* Leave if over the parent, but not on this selection. */
		tp = menu->parent;
		if(tp && subwindow == tp->window) {
			if(y < menu->parentOffset
				|| y > tp->itemHeight + menu->parentOffset) {
				return MENU_LEAVE;
			}
		}

		menu->currentIndex = -1;

	}

	/* Move the menu if needed. */
	if(menu->height > rootHeight && menu->currentIndex >= 0) {

		/* If near the top, shift down. */
		if(y + menu->y <= 0) {
			if(menu->currentIndex > 0) {
				--menu->currentIndex;
				SetPosition(menu, menu->currentIndex);
			}
		}

		/* If near the bottom, shift up. */
		if(y + menu->y + menu->itemHeight / 2 >= rootHeight) {
			if(menu->currentIndex + 1 < menu->itemCount) {
				++menu->currentIndex;
				SetPosition(menu, menu->currentIndex);
			}
		}

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

	ButtonData button;
	MenuItemType *ip;
	int north, south, east, west;
	int x, y;

	/* Clear the old selection. */
	ip = GetMenuItem(menu, menu->lastIndex);
	DrawMenuItem(menu, ip, menu->lastIndex);

	/* Highlight the new selection. */
	ip = GetMenuItem(menu, menu->currentIndex);
	if(ip) {

		if(!ip->name && !ip->submenu && !ip->command) {
			return;
		}

		ResetButton(&button, menu->window, menu->gc);
		button.font = FONT_MENU;
		button.width = menu->width - eastOffset - westOffset;
		button.height = menu->itemHeight;
		button.alignment = ALIGN_LEFT;
		button.icon = ip->icon;
		button.x = westOffset;
		button.y = menu->offsets[menu->currentIndex];
		button.type = BUTTON_ACTIVE;
		button.text = ip->name;

		DrawButton(&button);

		if(ip->submenu && parts[PART_SUBMENU].image) {

			GetButtonOffsets(&north, &south, &east, &west);

			x = menu->width - parts[PART_SUBMENU].width - east;
			y = menu->offsets[menu->currentIndex];
			y += menu->itemHeight / 2 - parts[PART_SUBMENU].height / 2;

			if(parts[PART_SUBMENU].image->shape != None) {
				JXSetClipOrigin(display, menu->gc, x, y);
				JXSetClipMask(display, menu->gc,
					parts[PART_SUBMENU].image->shape);
			}
			JXCopyArea(display, parts[PART_SUBMENU].image->image,
				menu->window, menu->gc, 0, parts[PART_SUBMENU].height,
				parts[PART_SUBMENU].width, parts[PART_SUBMENU].height,
				x, y);
			if(parts[PART_SUBMENU].image->shape != None) {
				JXSetClipMask(display, menu->gc, None);
			}

		}
	}

}

/***************************************************************************
 ***************************************************************************/
void DrawMenuItem(MenuType *menu, MenuItemType *item, int index) {

	ButtonData button;
	int north, south, east, west;
	int x, y, amount;

	Assert(menu);

	if(!item) {
		if(index == -1 && menu->label) {

			ResetButton(&button, menu->window, menu->gc);
			button.type = BUTTON_NONE;
			button.color = COLOR_MENU_FG;
			button.background = COLOR_MENU_BG;
			button.fillPart = &parts[PART_MENU_F];
			button.font = FONT_MENU;
			button.width = menu->width - eastOffset - westOffset;
			button.height = menu->itemHeight;
			button.x = westOffset;
			button.y = northOffset;
			button.text = menu->label;

			DrawButton(&button);

		}
		return;
	}

	if(item->name) {

		ResetButton(&button, menu->window, menu->gc);
		button.type = BUTTON_NONE;
		button.x = westOffset;
		button.y = menu->offsets[index];
		button.width = menu->width - eastOffset - westOffset;
		button.height = menu->itemHeight;
		button.color = COLOR_MENU_FG;
		button.background = COLOR_MENU_BG;
		button.fillPart = &parts[PART_MENU_F];
		button.icon = item->icon;
		button.font = FONT_MENU;
		button.text = item->name;

		DrawButton(&button);

	} else if(!item->command && !item->submenu) {

		if(parts[PART_MENU_S].image) {
			for(x = westOffset; x < menu->width - eastOffset;) {
				amount = parts[PART_MENU_S].width;
				if(x + amount > menu->width - eastOffset) {
					amount = menu->width - eastOffset - x;
				}
				JXCopyArea(display, parts[PART_MENU_S].image->image,
					menu->window, menu->gc, 0, 0,
					amount, parts[PART_MENU_S].height,
					x, menu->offsets[index]);
				x += amount;
			}
		}

	}

	if(item->submenu && parts[PART_SUBMENU].image) {

		GetButtonOffsets(&north, &south, &east, &west);

		x = menu->width - parts[PART_SUBMENU].width - east;
		y = menu->offsets[index];
		y += menu->itemHeight / 2 - parts[PART_SUBMENU].height / 2;

		if(parts[PART_SUBMENU].image->shape != None) {
			JXSetClipOrigin(display, menu->gc, x, y);
			JXSetClipMask(display, menu->gc,
				parts[PART_SUBMENU].image->shape);
		}
		JXCopyArea(display, parts[PART_SUBMENU].image->image,
			menu->window, menu->gc, 0, 0,
			parts[PART_SUBMENU].width, parts[PART_SUBMENU].height,
			x, y);
		if(parts[PART_SUBMENU].image->shape != None) {
			JXSetClipMask(display, menu->gc, None);
		}

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

/***************************************************************************
 ***************************************************************************/
void SetPosition(MenuType *tp, int index) {

	int y;
	int updated;

	y = tp->offsets[index] + tp->itemHeight / 2;

	if(tp->height > rootHeight) {

		updated = 0;
		while(y + tp->y < tp->itemHeight / 2) {
			tp->y += tp->itemHeight;
			updated = tp->itemHeight;
		}
		while(y + tp->y >= rootHeight) {
			tp->y -= tp->itemHeight;
			updated = -tp->itemHeight;
		}
		if(updated) {
			JXMoveWindow(display, tp->window, tp->x, tp->y);
			y += updated;
		}

	}

	/* We need to do this twice so the event gets registered
	 * on the submenu if one exists. */
	SetMousePosition(tp->window, 6, y);
	SetMousePosition(tp->window, 6, y);

}


