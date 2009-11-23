/*
	This file is part of Warzone 2100.
	Copyright (C) 2005-2009  Warzone Resurrection Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file wzapp.cpp
 *  Qt-related functions.
 */

#include <QImageReader>
#include <QBitmap>
#include <QPainter>
#include <QMouseEvent>
#include "wzapp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Get platform defines before checking for them!
#include "frame.h"
#include "file.h"
#include "configfile.h"
#include "lib/ivis_common/piestate.h"
#include "lib/ivis_opengl/screen.h"
#include "wzapp_c.h"
#include "src/main.h"
#include "lib/gamelib/gtime.h"

#ifdef __cplusplus
}
#endif

/* The possible states for keys */
typedef enum _key_state
{
	KEY_UP,
	KEY_PRESSED,
	KEY_DOWN,
	KEY_RELEASED,
	KEY_PRESSRELEASE,	///< When a key goes up and down in a frame
	KEY_DOUBLECLICK,	///< Only used by mouse keys
	KEY_DRAG,		///< Only used by mouse keys
} KEY_STATE;

typedef struct _input_state
{
	KEY_STATE state;	///< Last key/mouse state
	UDWORD lastdown;	///< last key/mouse button down timestamp
} INPUT_STATE;

/// constant for the interval between 2 singleclicks for doubleclick event in ms
#define DOUBLE_CLICK_INTERVAL 250

/** The current state of the keyboard */
static INPUT_STATE aKeyState[KEY_MAXSCAN];

/** How far the mouse has to move to start a drag */
#define DRAG_THRESHOLD	5

/** Which button is being used for a drag */
static MOUSE_KEY_CODE dragKey;

/** The start of a possible drag by the mouse */
static SDWORD dragX, dragY;

/** The current mouse button state */
static INPUT_STATE aMouseState[6];

/** The size of the input buffer */
#define INPUT_MAXSTR	512

/** The input string buffer */
static UDWORD	pInputBuffer[INPUT_MAXSTR];

static UDWORD	*pStartBuffer, *pEndBuffer;
static char	pCharInputBuffer[INPUT_MAXSTR];
static char	*pCharStartBuffer, *pCharEndBuffer;
static char	currentChar;
static QColor fontColor;
static float font_size = 12.f;
static uint16_t mouseXPos = 0, mouseYPos = 0;
static CURSOR lastCursor = CURSOR_ARROW;

unsigned int screenWidth = 0;
unsigned int screenHeight = 0;

#define gl_errors() really_report_gl_errors(__FILE__, __LINE__)
static void really_report_gl_errors (const char *file, int line)
{
	GLenum error = glGetError();

	if (error != GL_NO_ERROR)
	{
		qFatal("Oops, GL error caught: %s %s:%i", gluErrorString(error), file, line);
	}
}

/*!
 * The mainloop.
 * Fetches events, executes appropriate code
 */
void WzMainWindow::tick()
{
	paintGL();

	/* Tell the input system about the start of another frame */
	inputNewFrame();
}

void WzMainWindow::loadCursor(CURSOR cursor, int x, int y, QBuffer &buffer)
{
	buffer.reset();
	QImageReader reader(&buffer, "png");
	if (!reader.canRead())
	{
		debug(LOG_ERROR, "Failed to read cursor image: %s", reader.errorString().toAscii().constData());
	}
	reader.setClipRect(QRect(x, y, 32, 32));
	cursors[cursor] = new QCursor(QPixmap::fromImage(reader.read()));
}

WzMainWindow::WzMainWindow(const QGLFormat &format, QWidget *parent) : QGLWidget(format, parent)
{
	myself = this;
	timer = new QTimer(this);
	tickCount.start();
	connect(timer, SIGNAL(timeout()), this, SLOT(tick()));
	for (int i = 0; i < CURSOR_MAX; cursors[i++] = NULL) ;
	timer->start(0);
	setAutoFillBackground(false);
	setAutoBufferSwap(false);
	setMouseTracking(true);

	// Load coloured image cursors
	UDWORD size;
	char *bytes;
	loadFile("images/intfac5.png", &bytes, &size);
	QByteArray array(bytes, size);
	if (array.size() != size)
	{
		debug(LOG_ERROR, "Bad array"); abort();
	}
	QBuffer buffer(&array, this);
	buffer.open(QIODevice::ReadOnly);
	if (!buffer.isReadable())
	{
		debug(LOG_ERROR, "Bad buffer: %s", buffer.errorString().toAscii().constData());
		abort();
	}
	loadCursor(CURSOR_PICKUP, 96, 160, buffer);
	loadCursor(CURSOR_ATTACK, 192, 128, buffer);
	loadCursor(CURSOR_SELECT, 32, 160, buffer);
	loadCursor(CURSOR_LOCKON, 192, 160, buffer);
	loadCursor(CURSOR_JAM, 224, 128, buffer);
	loadCursor(CURSOR_DEFAULT, 64, 128, buffer);
	loadCursor(CURSOR_BUILD, 96, 128, buffer);
	loadCursor(CURSOR_MOVE, 160, 160, buffer);
	loadCursor(CURSOR_GUARD, 224, 128, buffer);
	loadCursor(CURSOR_EMBARK, 0, 128, buffer);
	loadCursor(CURSOR_BRIDGE, 128, 128, buffer);
	loadCursor(CURSOR_ATTACH, 0, 192, buffer);
	loadCursor(CURSOR_FIX, 0, 160, buffer);
	loadCursor(CURSOR_SEEKREPAIR, 64, 160, buffer);
	loadCursor(CURSOR_NOTPOSSIBLE, 128, 160, buffer);
	loadCursor(CURSOR_DEST, 32, 128, buffer);
	free(bytes);

	// Reused (unused) cursors
	cursors[CURSOR_ARROW] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_MENU] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_BOMB] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_EDGEOFMAP] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_SIGHT] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_TARGET] = new QCursor(Qt::ArrowCursor);
	cursors[CURSOR_UARROW] = new QCursor(Qt::SizeVerCursor);
	cursors[CURSOR_DARROW] = new QCursor(Qt::SizeVerCursor);
	cursors[CURSOR_LARROW] = new QCursor(Qt::SizeHorCursor);
	cursors[CURSOR_RARROW] = new QCursor(Qt::SizeHorCursor);

	// Fonts
	regular.setFamily("DejaVu Sans");
	regular.setPointSize(12);
	bold.setFamily("DejaVu Sans");
	bold.setPointSize(21);
	bold.setBold(true);
}

WzMainWindow::~WzMainWindow()
{
	for (int i = 0; i < CURSOR_MAX; free(cursors[i++])) ;
}

WzMainWindow *WzMainWindow::instance()
{
	assert(myself != NULL);
	return myself;
}

void WzMainWindow::initializeGL()
{
}

void WzMainWindow::resizeGL(int width, int height)
{
	screenWidth = width;
	screenHeight = height;

	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, width, height, 0, 1, -1);

	glMatrixMode(GL_TEXTURE);
	glScalef(1.0f / OLD_TEXTURE_SIZE_FIX, 1.0f / OLD_TEXTURE_SIZE_FIX, 1.0f); // FIXME Scaling texture coords to 256x256!

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glCullFace(GL_FRONT);
	glEnable(GL_CULL_FACE);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void WzMainWindow::paintGL()
{
	mainLoop();
}

void WzMainWindow::setCursor(CURSOR index)
{
	QWidget::setCursor(*cursors[index]);
}

void WzMainWindow::setCursor(QCursor cursor)
{
	QWidget::setCursor(cursor);
}

WzMainWindow *WzMainWindow::myself = NULL;

void WzMainWindow::setFontType(enum iV_fonts FontID)
{
	switch (FontID)
	{
	case font_regular:
		setFont(regular);
		break;
	case font_large:
		setFont(bold);
		break;
	default:
		break;
	}
}

void WzMainWindow::mouseMoveEvent(QMouseEvent *event)
{
	mouseXPos = event->x();
	mouseYPos = event->y();

	if (!mouseDown(MOUSE_MMB))
	{
		/* now see if a drag has started */
		if ((aMouseState[dragKey].state == KEY_PRESSED ||
			 aMouseState[dragKey].state == KEY_DOWN)
			&& (ABSDIF(dragX, mouseXPos) > DRAG_THRESHOLD ||
				ABSDIF(dragY, mouseYPos) > DRAG_THRESHOLD))
		{
			aMouseState[dragKey].state = KEY_DRAG;
		}
	}
}

MOUSE_KEY_CODE WzMainWindow::buttonToIdx(Qt::MouseButton button)
{
	MOUSE_KEY_CODE idx;

	switch (button)
	{
		case Qt::LeftButton	: idx = MOUSE_LMB; break;
		case Qt::RightButton : idx = MOUSE_RMB;	break;
		case Qt::MidButton : idx = MOUSE_MMB; break;
		case Qt::XButton1 : idx = MOUSE_MMB; break;
		case Qt::XButton2 : idx = MOUSE_MMB; break;
		default:
		case Qt::NoButton :	idx = MOUSE_BAD; break;	// strange case
	}

	return idx;
}

// TODO consider using QWidget::mouseDoubleClickEvent() for double-click
void WzMainWindow::mousePressEvent(QMouseEvent *event)
{
	Qt::MouseButtons presses = event->buttons();	// full state info for all buttons
	MOUSE_KEY_CODE idx = buttonToIdx(event->button());			// index of button that caused event

	if (idx == MOUSE_BAD)
	{
		debug(LOG_ERROR, "bad mouse idx");	// FIXME remove
		return; // not recognized mouse button
	}

	if (aMouseState[idx].state == KEY_UP
		|| aMouseState[idx].state == KEY_RELEASED
		|| aMouseState[idx].state == KEY_PRESSRELEASE)
	{
		if (!presses.testFlag(Qt::MidButton)) //skip doubleclick check for wheel
		{
			// whether double click or not
			if (gameTime - aMouseState[idx].lastdown < DOUBLE_CLICK_INTERVAL)
			{
				aMouseState[idx].state = KEY_DOUBLECLICK;
				aMouseState[idx].lastdown = 0;
			}
			else
			{
				aMouseState[idx].state = KEY_PRESSED;
				aMouseState[idx].lastdown = gameTime;
			}
		}
		else	//mouse wheel up/down was used, so notify. FIXME.
		{
			aMouseState[idx].state = KEY_PRESSED;
			aMouseState[idx].lastdown = 0;
		}

		if (idx < 2) // Not the mousewheel
		{
			dragKey = idx;
			dragX = mouseX();
			dragY = mouseY();
		}
	}
}

void WzMainWindow::wheelEvent(QWheelEvent *event)
{
	int direction = event->delta();

	if (direction > 0)
	{
		aMouseState[MOUSE_WUP].state = KEY_PRESSED;
		aMouseState[MOUSE_WUP].lastdown = gameTime;
	}
	else
	{
		aMouseState[MOUSE_WDN].state = KEY_PRESSED;
		aMouseState[MOUSE_WDN].lastdown = gameTime;
	}
}

void WzMainWindow::mouseReleaseEvent(QMouseEvent *event)
{
	MOUSE_KEY_CODE idx = buttonToIdx(event->button());

	if (idx == MOUSE_BAD)
	{
		return; // not recognized mouse button
	}

	if (aMouseState[idx].state == KEY_PRESSED)
	{
		aMouseState[idx].state = KEY_PRESSRELEASE;
	}
	else if (aMouseState[idx].state == KEY_DOWN
			|| aMouseState[idx].state == KEY_DRAG
			|| aMouseState[idx].state == KEY_DOUBLECLICK)
	{
		aMouseState[idx].state = KEY_RELEASED;
	}
}

static unsigned int setKey(int code, bool pressed)
{
	if (pressed)
	{
		if (aKeyState[code].state == KEY_UP ||
			aKeyState[code].state == KEY_RELEASED ||
			aKeyState[code].state == KEY_PRESSRELEASE)
		{
			// whether double key press or not
			aKeyState[code].state = KEY_PRESSED;
			aKeyState[code].lastdown = 0;
		}
	}
	else
	{
		if (aKeyState[code].state == KEY_PRESSED)
		{
			aKeyState[code].state = KEY_PRESSRELEASE;
		}
		else if (aKeyState[code].state == KEY_DOWN)
		{
			aKeyState[code].state = KEY_RELEASED;
		}
	}
	return code;
}

void WzMainWindow::realHandleKeyEvent(QKeyEvent *event, bool pressed)
{
	Qt::KeyboardModifiers mods = event->modifiers();
	unsigned int lastKey;

	if (mods.testFlag(Qt::ControlModifier))
	{
		setKey(KEY_LCTRL, pressed);
	}
	if (mods.testFlag(Qt::ShiftModifier))
	{
		setKey(KEY_LSHIFT, pressed);
		setKey(KEY_RSHIFT, pressed);
	}
	if (mods.testFlag(Qt::AltModifier))
	{
		setKey(KEY_LALT, pressed);
	}

	switch (event->key())
	{
		case Qt::Key_Escape		:	lastKey = setKey(KEY_ESC, pressed); break;
		case Qt::Key_Backspace	:	lastKey = setKey(KEY_BACKSPACE, pressed); break;
		case Qt::Key_1			:	lastKey = setKey(KEY_1, pressed); break;
		case Qt::Key_2			:	lastKey = setKey(KEY_2, pressed); break;
		case Qt::Key_3			:	lastKey = setKey(KEY_3, pressed); break;
		case Qt::Key_4			:	lastKey = setKey(KEY_4, pressed); break;
		case Qt::Key_5			:	lastKey = setKey(KEY_5, pressed); break;
		case Qt::Key_6			:	lastKey = setKey(KEY_6, pressed); break;
		case Qt::Key_7			:	lastKey = setKey(KEY_7, pressed); break;
		case Qt::Key_8			:	lastKey = setKey(KEY_8, pressed); break;
		case Qt::Key_9			:	lastKey = setKey(KEY_9, pressed); break;
		case Qt::Key_0			:	lastKey = setKey(KEY_0, pressed); break;
		case Qt::Key_Minus		:	lastKey = setKey(KEY_MINUS, pressed); break;
		case Qt::Key_Equal		:	lastKey = setKey(KEY_EQUALS, pressed); break;
		case Qt::Key_Tab		:	lastKey = setKey(KEY_TAB, pressed); break;
		case Qt::Key_Q			:	lastKey = setKey(KEY_Q, pressed); break;
		case Qt::Key_W			:	lastKey = setKey(KEY_W, pressed); break;
		case Qt::Key_E			:	lastKey = setKey(KEY_E, pressed); break;
		case Qt::Key_R			:	lastKey = setKey(KEY_R, pressed); break;
		case Qt::Key_T			:	lastKey = setKey(KEY_T, pressed); break;
		case Qt::Key_Y			:	lastKey = setKey(KEY_Y, pressed); break;
		case Qt::Key_U			:	lastKey = setKey(KEY_U, pressed); break;
		case Qt::Key_I			:	lastKey = setKey(KEY_I, pressed); break;
		case Qt::Key_O			:	lastKey = setKey(KEY_O, pressed); break;
		case Qt::Key_P			:	lastKey = setKey(KEY_P, pressed); break;
		case Qt::Key_BracketLeft:	lastKey = setKey(KEY_LBRACE, pressed); break;
		case Qt::Key_BracketRight:	lastKey = setKey(KEY_RBRACE, pressed); break;
		case Qt::Key_Return		:	lastKey = setKey(KEY_RETURN, pressed); break;
		case Qt::Key_A			:	lastKey = setKey(KEY_A, pressed); break;
		case Qt::Key_S			:	lastKey = setKey(KEY_S, pressed); break;
		case Qt::Key_D			:	lastKey = setKey(KEY_D, pressed); break;
		case Qt::Key_F			:	lastKey = setKey(KEY_F, pressed); break;
		case Qt::Key_G			:	lastKey = setKey(KEY_G, pressed); break;
		case Qt::Key_H			:	lastKey = setKey(KEY_H, pressed); break;
		case Qt::Key_J			:	lastKey = setKey(KEY_J, pressed); break;
		case Qt::Key_K			:	lastKey = setKey(KEY_K, pressed); break;
		case Qt::Key_L			:	lastKey = setKey(KEY_L, pressed); break;
		case Qt::Key_Semicolon	:	lastKey = setKey(KEY_SEMICOLON, pressed); break;
		case Qt::Key_QuoteDbl	:	lastKey = setKey(KEY_QUOTE, pressed); break;		// ?
		case Qt::Key_Apostrophe	:	lastKey = setKey(KEY_BACKQUOTE, pressed); break;	// ?
		case Qt::Key_Backslash	:	lastKey = setKey(KEY_BACKSLASH, pressed); break;
		case Qt::Key_Z			:	lastKey = setKey(KEY_Z, pressed); break;
		case Qt::Key_X			:	lastKey = setKey(KEY_X, pressed); break;
		case Qt::Key_C			:	lastKey = setKey(KEY_C, pressed); break;
		case Qt::Key_V			:	lastKey = setKey(KEY_V, pressed); break;
		case Qt::Key_B			:	lastKey = setKey(KEY_B, pressed); break;
		case Qt::Key_N			:	lastKey = setKey(KEY_N, pressed); break;
		case Qt::Key_M			:	lastKey = setKey(KEY_M, pressed); break;
		case Qt::Key_Colon		:	lastKey = setKey(KEY_COMMA, pressed); break;
		case Qt::Key_Period		:	lastKey = setKey(KEY_FULLSTOP, pressed); break;
		case Qt::Key_Slash		:	lastKey = setKey(KEY_FORWARDSLASH, pressed); break;
		case Qt::Key_Asterisk	:	lastKey = setKey(KEY_KP_STAR, pressed); break;
		case Qt::Key_Space		:	lastKey = setKey(KEY_SPACE, pressed); break;
		case Qt::Key_CapsLock	:	lastKey = setKey(KEY_CAPSLOCK, pressed); break;
		case Qt::Key_F1			:	lastKey = setKey(KEY_F1, pressed); break;
		case Qt::Key_F2			:	lastKey = setKey(KEY_F2, pressed); break;
		case Qt::Key_F3			:	lastKey = setKey(KEY_F3, pressed); break;
		case Qt::Key_F4			:	lastKey = setKey(KEY_F4, pressed); break;
		case Qt::Key_F5			:	lastKey = setKey(KEY_F5, pressed); break;
		case Qt::Key_F6			:	lastKey = setKey(KEY_F6, pressed); break;
		case Qt::Key_F7			:	lastKey = setKey(KEY_F7, pressed); break;
		case Qt::Key_F8			:	lastKey = setKey(KEY_F8, pressed); break;
		case Qt::Key_F9			:	lastKey = setKey(KEY_F9, pressed); break;
		case Qt::Key_F10		:	lastKey = setKey(KEY_F10, pressed); break;
		case Qt::Key_NumLock	:	lastKey = setKey(KEY_NUMLOCK, pressed); break;
		case Qt::Key_ScrollLock	:	lastKey = setKey(KEY_SCROLLLOCK, pressed); break;
	/*	case Qt::Key_			:	lastKey = setKey(KEY_KP_0, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_1, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_2, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_3, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_4, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_5, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_6, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_7, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_8, pressed); break;
		case Qt::Key_			:	lastKey = setKey(KEY_KP_9, pressed); break; */
		case Qt::Key_Plus		:	lastKey = setKey(KEY_KP_PLUS, pressed); break;
	//	case Qt::Key_			:	lastKey = setKey(KEY_KP_FULLSTOP, pressed); break;
		case Qt::Key_F11		:	lastKey = setKey(KEY_F11, pressed); break;
		case Qt::Key_F12		:	lastKey = setKey(KEY_F12, pressed); break;
	//	case Qt::Key_			:	lastKey = setKey(KEY_KP_BACKSLASH, pressed); break;
		case Qt::Key_Home		:	lastKey = setKey(KEY_HOME, pressed); break;
		case Qt::Key_Up			:	lastKey = setKey(KEY_UPARROW, pressed); break;
		case Qt::Key_PageUp		:	lastKey = setKey(KEY_PAGEUP, pressed); break;
		case Qt::Key_Left		:	lastKey = setKey(KEY_LEFTARROW, pressed); break;
		case Qt::Key_Right		:	lastKey = setKey(KEY_RIGHTARROW, pressed); break;
		case Qt::Key_End		:	lastKey = setKey(KEY_END, pressed); break;
		case Qt::Key_Down		:	lastKey = setKey(KEY_DOWNARROW, pressed); break;
		case Qt::Key_PageDown	:	lastKey = setKey(KEY_PAGEDOWN, pressed); break;
		case Qt::Key_Insert		:	lastKey = setKey(KEY_INSERT, pressed); break;
		case Qt::Key_Delete		:	lastKey = setKey(KEY_DELETE, pressed); break;
		case Qt::Key_Enter		:	lastKey = setKey(KEY_KPENTER, pressed); break;
		default:
			lastKey = 0;
			event->ignore();
			break;
	}

	if (pressed)
	{
		inputAddBuffer(lastKey, event->text().unicode()->toAscii(), 1);
	}
}

void WzMainWindow::keyReleaseEvent(QKeyEvent *event)
{
	realHandleKeyEvent(event, false);
}

void WzMainWindow::keyPressEvent(QKeyEvent *event)
{
	realHandleKeyEvent(event, true);
}

void WzMainWindow::close()
{
	qApp->quit();
}


/************************************/
/***                              ***/
/***          C interface         ***/
/***                              ***/
/************************************/

int wzInit(int argc, char *argv[], int fsaa, bool vsync, int w, int h)
{
	QApplication app(argc, argv);

	// Setting up OpenGL
	QGLFormat format;
	format.setDoubleBuffer(true);
	format.setAlpha(true);
	if (vsync)
	{
		format.setSwapInterval(1);
	}
	if (fsaa)
	{
		format.setSampleBuffers(true);
		format.setSamples(fsaa);
	}
	WzMainWindow mainwindow(format);
	mainwindow.setMinimumSize(w, h);
	mainwindow.setMaximumSize(w, h);
	mainwindow.show();

	debug(LOG_MAIN, "Final initialization");
	if (finalInitialization() != 0)
	{
		debug(LOG_ERROR, "Failed to carry out final initialization.");
		return -1;
	}

	debug(LOG_MAIN, "Entering main loop");
	app.exec();

	debug(LOG_MAIN, "Shutting down Warzone 2100");
	return EXIT_SUCCESS;
}

int wzQuit()
{
	WzMainWindow::instance()->close();
}

void wzScreenFlip()
{
	WzMainWindow::instance()->swapBuffers();
}

int wzGetTicks()
{
	return WzMainWindow::instance()->ticks();
}

/****************************************/
/***     Mouse and keyboard support   ***/
/****************************************/

void pie_ShowMouse(bool visible)
{
	if (!visible)
	{
		WzMainWindow::instance()->setCursor(QCursor(Qt::BlankCursor));
	}
	else
	{
		WzMainWindow::instance()->setCursor(lastCursor);
	}
}

void wzSetCursor(CURSOR index)
{
	WzMainWindow::instance()->setCursor(index);
	lastCursor = index;
}

void wzCreateCursor(CURSOR index, uint8_t *data, uint8_t *mask, int w, int h, int hot_x, int hot_y)
{
	// TODO REMOVE
}

void wzGrabMouse()
{
	WzMainWindow::instance()->grabMouse();
}

void wzReleaseMouse()
{
	WzMainWindow::instance()->releaseMouse();
}

bool wzActiveWindow()
{
	return WzMainWindow::instance()->underMouse();
}

uint16_t mouseX()
{
	return mouseXPos;
}

uint16_t mouseY()
{
	return mouseYPos;
}

void SetMousePos(uint16_t x, uint16_t y)
{
	static int mousewarp = -1;

	if (mousewarp == -1)
	{
		int val;

		mousewarp = 1;
		if (getWarzoneKeyNumeric("nomousewarp", &val))
		{
			mousewarp = !val;
		}
	}
	if (mousewarp)
	{
		WzMainWindow::instance()->cursor().setPos(x, y);
	}
}

/* This returns true if the mouse key is currently depressed */
BOOL mouseDown(MOUSE_KEY_CODE code)
{
	return (aMouseState[code].state != KEY_UP);
}

/* This returns true if the mouse key was double clicked */
BOOL mouseDClicked(MOUSE_KEY_CODE code)
{
	return (aMouseState[code].state == KEY_DOUBLECLICK);
}

/* This returns true if the mouse key went from being up to being down this frame */
BOOL mousePressed(MOUSE_KEY_CODE code)
{
	return ((aMouseState[code].state == KEY_PRESSED) ||
			(aMouseState[code].state == KEY_DOUBLECLICK) ||
			(aMouseState[code].state == KEY_PRESSRELEASE));
}

/* This returns true if the mouse key went from being down to being up this frame */
BOOL mouseReleased(MOUSE_KEY_CODE code)
{
	return ((aMouseState[code].state == KEY_RELEASED) ||
			(aMouseState[code].state == KEY_DOUBLECLICK) ||
			(aMouseState[code].state == KEY_PRESSRELEASE));
}

/* Check for a mouse drag, return the drag start coords if dragging */
BOOL mouseDrag(MOUSE_KEY_CODE code, UDWORD *px, UDWORD *py)
{
	if (aMouseState[code].state == KEY_DRAG)
	{
		*px = dragX;
		*py = dragY;
		return true;
	}

	return false;
}

void keyScanToString(KEY_CODE code, char *ascii, UDWORD maxStringSize)
{
#if 0
	if(keyCodeToSDLKey(code) == KEY_MAXSCAN)
	{
		strcpy(ascii,"???");
		return;
	}
	ASSERT( keyCodeToSDLKey(code) < KEY_MAXSCAN, "Invalid key code: %d", code );
	snprintf(ascii, maxStringSize, "%s", SDL_GetKeyName(keyCodeToSDLKey(code)));
#endif
}

/* Initialise the input module */
void inputInitialise(void)
{
	unsigned int i;

	for (i = 0; i < KEY_MAXSCAN; i++)
	{
		aKeyState[i].state = KEY_UP;
	}

	for (i = 0; i < 6; i++)
	{
		aMouseState[i].state = KEY_UP;
	}

	pStartBuffer = pInputBuffer;
	pEndBuffer = pInputBuffer;
	pCharStartBuffer = pCharInputBuffer;
	pCharEndBuffer = pCharInputBuffer;

	dragX = screenWidth / 2;
	dragY = screenHeight / 2;
	dragKey = MOUSE_LMB;
}

/* add count copies of the characater code to the input buffer */
void inputAddBuffer(UDWORD code, char char_code, UDWORD count)
{
	UDWORD	*pNext;
	char	*pCharNext;

	/* Calculate what pEndBuffer will be set to next */
	pNext = pEndBuffer + 1;
	pCharNext = pCharEndBuffer + 1;
	if (pNext >= pInputBuffer + INPUT_MAXSTR)
	{
		pNext = pInputBuffer;
		pCharNext = pCharInputBuffer;
	}

	while (pNext != pStartBuffer && count > 0)
	{
		/* Store the character */
		*pEndBuffer = code;
		*pCharEndBuffer = char_code;
		pEndBuffer = pNext;
		pCharEndBuffer = pCharNext;
		count -= 1;

		/* Calculate what pEndBuffer will be set to next */
		pNext = pEndBuffer + 1;
		pCharNext = pCharEndBuffer + 1;
		if (pNext >= pInputBuffer + INPUT_MAXSTR)
		{
			pNext = pInputBuffer;
			pCharNext = pCharInputBuffer;
		}
	}
}

/* Clear the input buffer */
void inputClearBuffer(void)
{
	pStartBuffer = pInputBuffer;
	pEndBuffer = pInputBuffer;
	pCharStartBuffer = pCharInputBuffer;
	pCharEndBuffer = pCharInputBuffer;
}

/* Return the next key press or 0 if no key in the buffer.
 * The key returned will have been remaped to the correct ascii code for the
 * windows key map.
 * All key presses are buffered up (including windows auto repeat).
 */
UDWORD inputGetKey(void)
{
	UDWORD	retVal;

	if (pStartBuffer != pEndBuffer)
	{
		retVal = *pStartBuffer;
		currentChar = *pCharStartBuffer;
		pStartBuffer += 1;
		pCharStartBuffer += 1;

		if (pStartBuffer >= pInputBuffer + INPUT_MAXSTR)
		{
			pStartBuffer = pInputBuffer;
			pCharStartBuffer = pCharInputBuffer;
		}
	}
	else
	{
		retVal = 0;
	}

	return retVal;
}

char inputGetCharKey(void)
{
	return currentChar;
}

/*!
 * This is called once a frame so that the system can tell
 * whether a key was pressed this turn or held down from the last frame.
 */
void inputNewFrame(void)
{
	unsigned int i;

	/* Do the keyboard */
	for (i = 0; i < KEY_MAXSCAN; i++)
	{
		if (aKeyState[i].state == KEY_PRESSED)
		{
			aKeyState[i].state = KEY_DOWN;
		}
		else if (aKeyState[i].state == KEY_RELEASED ||
				 aKeyState[i].state == KEY_PRESSRELEASE)
		{
			aKeyState[i].state = KEY_UP;
		}
	}

	/* Do the mouse */
	for (i = 0; i < 6; i++)
	{
		if (aMouseState[i].state == KEY_PRESSED)
		{
			aMouseState[i].state = KEY_DOWN;
		}
		else if (aMouseState[i].state == KEY_RELEASED
				 || aMouseState[i].state == KEY_DOUBLECLICK
				 || aMouseState[i].state == KEY_PRESSRELEASE)
		{
			aMouseState[i].state = KEY_UP;
		}
	}
}

/*!
 * Release all keys (and buttons) when we loose focus
 */
// FIXME This seems to be totally ignored! (Try switching focus while the dragbox is open)
void inputLooseFocus(void)
{
	unsigned int i;

	/* Lost the window focus, have to take this as a global key up */
	for(i = 0; i < KEY_MAXSCAN; i++)
	{
		aKeyState[i].state = KEY_RELEASED;
	}
	for (i = 0; i < 6; i++)
	{
		aMouseState[i].state = KEY_RELEASED;
	}
}

/* This returns true if the key is currently depressed */
BOOL keyDown(KEY_CODE code)
{
	return (aKeyState[code].state != KEY_UP);
}

/* This returns true if the key went from being up to being down this frame */
BOOL keyPressed(KEY_CODE code)
{
	return ((aKeyState[code].state == KEY_PRESSED) || (aKeyState[code].state == KEY_PRESSRELEASE));
}

/* This returns true if the key went from being down to being up this frame */
BOOL keyReleased(KEY_CODE code)
{
	return ((aKeyState[code].state == KEY_RELEASED) || (aKeyState[code].state == KEY_PRESSRELEASE));
}


/**************************/
/***     Font support   ***/
/**************************/

void iV_SetFont(enum iV_fonts FontID)
{
	WzMainWindow::instance()->setFontType(FontID);
}

void iV_TextInit()
{
}

void iV_TextShutdown()
{
}

void iV_font(const char *fontName, const char *fontFace, const char *fontFaceBold)
{
	// TODO
}

unsigned int iV_GetTextWidth(const char* string)
{
	return WzMainWindow::instance()->fontMetrics().width(string, -1);
}

unsigned int iV_GetCountedTextWidth(const char* string, size_t string_length)
{
	return WzMainWindow::instance()->fontMetrics().width(string, string_length);
}

unsigned int iV_GetTextHeight(const char* string)
{
//	printf("iV_GetTextBelowBase=%d\n", (int)WzMainWindow::instance()->fontMetrics().boundingRect(string).height());
	return 9;//WzMainWindow::instance()->fontMetrics().boundingRect(string).height(); // 9
}

unsigned int iV_GetCharWidth(uint32_t charCode)
{
	return WzMainWindow::instance()->fontMetrics().width(QChar(charCode));
}

int iV_GetTextLineSize()
{
	return WzMainWindow::instance()->fontMetrics().lineSpacing();
}

int iV_GetTextAboveBase(void)
{
	return -WzMainWindow::instance()->fontMetrics().ascent();
}

int iV_GetTextBelowBase(void)
{
	return -WzMainWindow::instance()->fontMetrics().descent(); // -4
}

void iV_SetTextColour(PIELIGHT colour)
{
	fontColor = QColor(colour.byte.r, colour.byte.g, colour.byte.b, colour.byte.a);
}

// FIXME - loses texture in-game, offset hack is bad, rotated text appears in slightly wrong position
void iV_DrawTextRotated(const char* string, float XPos, float YPos, float rotation)
{
//	GLint matrix_mode = 0;
//	glPushAttrib(GL_ALL_ATTRIB_BITS);

	if (rotation != 0.f)
	{
		rotation = 360.f - rotation;
	}

	pie_SetTexturePage(TEXPAGE_FONT);
//	glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);

	QPainter *painter = new QPainter(WzMainWindow::instance()->context()->device());
	painter->translate(XPos, YPos);
	painter->rotate(-rotation);
	painter->setPen(fontColor);
	painter->drawText(0, iV_GetTextHeight(string), string);
//	painter->end();
	delete painter;

//	glMatrixMode(matrix_mode);
//	glPopAttrib();
}

void iV_SetTextSize(float size)
{
	// TODO
	font_size = size;
}
