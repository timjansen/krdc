/***************************************************************************
                     keycapturedialog2.cpp - KeyCaptureDialog
                             -------------------
    begin                : Wed Dec 25 01:20:22 CET 2002
    copyright            : (C) 2002 by Tim Jansen
                           (C) unknown (whoever wrote kshortcutdialog.cpp)
    email                : tim@tjansen.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

//
// based on key capture code from kdelibs/kdeui/kshortcutdialog.cpp
//

#include "keycapturedialog2.h"
#include <qlabel.h>
#include <qlayout.h>
#include <kdebug.h>

#define XK_XKB_KEYS
#define XK_MISCELLANY
#include <X11/Xlib.h>	// For x11Event()
#include <X11/keysymdef.h> // For XK_...

#ifdef KeyPress
const int XFocusOut = FocusOut;
const int XFocusIn = FocusIn;
const int XKeyPress = KeyPress;
const int XKeyRelease = KeyRelease;
#undef KeyRelease
#undef KeyPress
#undef FocusOut
#undef FocusIn
#endif


KeyCaptureDialog2::KeyCaptureDialog2(QWidget *w, 
				     const char *name, 
				     bool modal) :
	KeyCaptureDialog(w, name, modal) {
}

KeyCaptureDialog2::~KeyCaptureDialog2() {
}


void KeyCaptureDialog2::execute() {
	keyLabel->setText("");
	grabKeyboard();
	exec();
	releaseKeyboard();
}

bool KeyCaptureDialog2::x11Event(XEvent *pEvent)
{
	switch( pEvent->type ) {
		case XKeyPress:
		case XKeyRelease:
			x11EventKeyPress( pEvent );
			return true;
		default:
			break;
	}
	return QWidget::x11Event( pEvent );
}

void KeyCaptureDialog2::x11EventKeyPress( XEvent *pEvent )
{
	// taken from kshortcutdialog.h
	KKeyNative keyNative( pEvent );
	uint keyModX = keyNative.mod(), keySymX = keyNative.sym();
	if ((keySymX == XK_Escape) && !keyModX) {
		accept();
		return;
	}

	switch( keySymX ) {
		// Don't allow setting a modifier key as an accelerator.
		// Also, don't release the focus yet.  We'll wait until
		//  we get a 'normal' key.
		case XK_Shift_L:   case XK_Shift_R:   keyModX = KKeyNative::modX(KKey::SHIFT); break;
		case XK_Control_L: case XK_Control_R: keyModX = KKeyNative::modX(KKey::CTRL); break;
		case XK_Alt_L:     case XK_Alt_R:     keyModX = KKeyNative::modX(KKey::ALT); break;
		// FIXME: check whether the Meta or Super key are for the Win modifier
		case XK_Meta_L:    case XK_Meta_R:
		case XK_Super_L:   case XK_Super_R:   keyModX = KKeyNative::modX(KKey::WIN); break;
		case XK_Hyper_L:   case XK_Hyper_R:
		case XK_Mode_switch:
		case XK_Num_Lock:
		case XK_Caps_Lock:
			break;
		default:
			if( pEvent->type == XKeyPress && keyNative.sym() ) {
				emit keyPressed(keyNative);
				reject();
			}
			return;
	}

	// If we are editing the first key in the sequence,
	//  display modifier keys which are held down
	if( pEvent->type == XKeyPress )
		keyModX |= pEvent->xkey.state;
	else
		keyModX = pEvent->xkey.state & ~keyModX;

	QString keyModStr;
	if( keyModX & KKeyNative::modX(KKey::WIN) )	keyModStr += KKey::modFlagLabel(KKey::WIN) + "+";
	if( keyModX & KKeyNative::modX(KKey::ALT) )	keyModStr += KKey::modFlagLabel(KKey::ALT) + "+";
	if( keyModX & KKeyNative::modX(KKey::CTRL) )	keyModStr += KKey::modFlagLabel(KKey::CTRL) + "+";
	if( keyModX & KKeyNative::modX(KKey::SHIFT) )	keyModStr += KKey::modFlagLabel(KKey::SHIFT) + "+";

	// Display currently selected modifiers, or redisplay old key.
	keyLabel->setText( keyModStr );
}

#include "keycapturedialog2.moc"

