// This Source Code Form is subject to the terms of the MIT License.
// If a copy of the MIT License was not distributed with this file,
// You can obtain one at https://spdx.org/licenses/MIT.html.

#include "app.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "web.h"
#include "keymap.h"
#include "hid/utils.h"

struct MTY_App {
	struct window_common cmn;
	MTY_Hash *hotkey;
	MTY_Hash *deduper;
	MTY_EventFunc event_func;
	MTY_AppFunc app_func;
	MTY_DetachState detach;
	MTY_ControllerEvent cevt[4];
	void *opaque;
	bool kb_grab;
	double pos_x;
	double pos_y;
	double screen_width;
	double screen_height;
	double width;
	double height;
	bool focus;
	bool fullscreen;
	bool visible;
	double scale;
	bool relative;
};


// Window properties

__attribute__((export_name("mty_window_update_position")))
void mty_window_update_position(MTY_App *ctx, double x, double y)
{
	ctx->pos_x = x;
	ctx->pos_y = y;
}

__attribute__((export_name("mty_window_update_screen")))
void mty_window_update_screen(MTY_App *ctx, double width, double height)
{
	ctx->screen_width = width;
	ctx->screen_height = height;
}

__attribute__((export_name("mty_window_update_size")))
void mty_window_update_size(MTY_App *ctx, double width, double height)
{
	ctx->width = width;
	ctx->height = height;
}

__attribute__((export_name("mty_window_update_focus")))
void mty_window_update_focus(MTY_App *ctx, bool focus)
{
	ctx->focus = focus;
}

__attribute__((export_name("mty_window_update_fullscreen")))
void mty_window_update_fullscreen(MTY_App *ctx, bool fullscreen)
{
	ctx->fullscreen = fullscreen;
}

__attribute__((export_name("mty_window_update_visibility")))
void mty_window_update_visibility(MTY_App *ctx, bool visible)
{
	ctx->visible = visible;
}

__attribute__((export_name("mty_window_update_pixel_ratio")))
void mty_window_update_pixel_ratio(MTY_App *ctx, double ratio)
{
	ctx->scale = ratio;
}

__attribute__((export_name("mty_window_update_relative_mouse")))
void mty_window_update_relative_mouse(MTY_App *ctx, bool relative)
{
	ctx->relative = relative;
}


// Window events

__attribute__((export_name("mty_window_motion")))
void mty_window_motion(MTY_App *ctx, bool relative, int32_t x, int32_t y)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_MOTION;
	evt.motion.relative = relative;
	evt.motion.x = x;
	evt.motion.y = y;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_size")))
void mty_window_size(MTY_App *ctx)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_SIZE;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_move")))
void mty_window_move(MTY_App *ctx)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_MOVE;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_button")))
void mty_window_button(MTY_App *ctx, bool pressed, int32_t button, int32_t x, int32_t y)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_BUTTON;
	evt.button.pressed = pressed;
	evt.button.button =
		button == 0 ? MTY_BUTTON_LEFT :
		button == 1 ? MTY_BUTTON_MIDDLE :
		button == 2 ? MTY_BUTTON_RIGHT :
		button == 3 ? MTY_BUTTON_X1 :
		button == 4 ? MTY_BUTTON_X2 :
		MTY_BUTTON_NONE;

	evt.button.x = x;
	evt.button.y = y;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_scroll")))
void mty_window_scroll(MTY_App *ctx, int32_t x, int32_t y)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_SCROLL;
	evt.scroll.x = x;
	evt.scroll.y = -y;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_keyboard")))
void mty_window_keyboard(MTY_App *ctx, bool pressed, MTY_Key key, uint32_t text, uint32_t mods)
{
	MTY_Event evt = {0};

	if (text > 0) {
		evt.type = MTY_EVENT_TEXT;

		for (uint8_t x = 0; x < 4; x++)
			evt.text[x] = text >> x * 8 & 0xFF;

		evt.text[4] = '\0';
		ctx->event_func(&evt, ctx->opaque);
	}

	if (key != 0) {
		evt.type = MTY_EVENT_KEY;
		evt.key.key = key;
		evt.key.pressed = pressed;
		evt.key.mod = web_keymap_mods(mods);

		mty_app_kb_to_hotkey(ctx, &evt, MTY_EVENT_HOTKEY);
		ctx->event_func(&evt, ctx->opaque);
	}
}

__attribute__((export_name("mty_window_focus")))
void mty_window_focus(MTY_App *ctx, bool focus)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_FOCUS;
	evt.focus = focus;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_drop")))
void mty_window_drop(MTY_App *ctx, const char *name, const void *data, size_t size)
{
	MTY_Event evt = {0};
	evt.type = MTY_EVENT_DROP;
	evt.drop.name = name;
	evt.drop.buf = data;
	evt.drop.size = size;

	ctx->event_func(&evt, ctx->opaque);
}

__attribute__((export_name("mty_window_controller")))
void mty_window_controller(MTY_App *ctx, uint32_t id, uint32_t state, uint32_t buttons,
	float lx, float ly, float rx, float ry, float lt, float rt)
{
	#define TESTB(i) \
		((buttons & (i)) == (i))

	MTY_Event evt = {0};
	evt.type = MTY_EVENT_CONTROLLER;

	MTY_ControllerEvent *c = &evt.controller;
	c->type = MTY_CTYPE_DEFAULT;
	c->numAxes = 6;
	c->numButtons = 17;
	c->vid = 0xCDD;
	c->pid = 0xCDD;
	c->id = id;

	c->buttons[MTY_CBUTTON_A] = TESTB(0x0001);
	c->buttons[MTY_CBUTTON_B] = TESTB(0x0002);
	c->buttons[MTY_CBUTTON_X] = TESTB(0x0004);
	c->buttons[MTY_CBUTTON_Y] = TESTB(0x0008);
	c->buttons[MTY_CBUTTON_LEFT_SHOULDER] = TESTB(0x0010);
	c->buttons[MTY_CBUTTON_RIGHT_SHOULDER] = TESTB(0x0020);
	c->buttons[MTY_CBUTTON_BACK] = TESTB(0x0100);
	c->buttons[MTY_CBUTTON_START] = TESTB(0x0200);
	c->buttons[MTY_CBUTTON_LEFT_THUMB] = TESTB(0x0400);
	c->buttons[MTY_CBUTTON_RIGHT_THUMB] = TESTB(0x0800);
	c->buttons[MTY_CBUTTON_DPAD_UP] = TESTB(0x1000);
	c->buttons[MTY_CBUTTON_DPAD_DOWN] = TESTB(0x2000);
	c->buttons[MTY_CBUTTON_DPAD_LEFT] = TESTB(0x4000);
	c->buttons[MTY_CBUTTON_DPAD_RIGHT] = TESTB(0x8000);

	c->axes[MTY_CAXIS_THUMB_LX].value = lx < 0.0f ? lrint(lx * abs(INT16_MIN)) : lrint(lx * INT16_MAX);
	c->axes[MTY_CAXIS_THUMB_LX].usage = 0x30;
	c->axes[MTY_CAXIS_THUMB_LX].min = INT16_MIN;
	c->axes[MTY_CAXIS_THUMB_LX].max = INT16_MAX;

	c->axes[MTY_CAXIS_THUMB_LY].value = ly > 0.0f ? lrint(-ly * abs(INT16_MIN)) : lrint(-ly * INT16_MAX);
	c->axes[MTY_CAXIS_THUMB_LY].usage = 0x31;
	c->axes[MTY_CAXIS_THUMB_LY].min = INT16_MIN;
	c->axes[MTY_CAXIS_THUMB_LY].max = INT16_MAX;

	c->axes[MTY_CAXIS_THUMB_RX].value = rx < 0.0f ? lrint(rx * abs(INT16_MIN)) : lrint(rx * INT16_MAX);
	c->axes[MTY_CAXIS_THUMB_RX].usage = 0x32;
	c->axes[MTY_CAXIS_THUMB_RX].min = INT16_MIN;
	c->axes[MTY_CAXIS_THUMB_RX].max = INT16_MAX;

	c->axes[MTY_CAXIS_THUMB_RY].value = ry > 0.0f ? lrint(-ry * abs(INT16_MIN)) : lrint(-ry * INT16_MAX);
	c->axes[MTY_CAXIS_THUMB_RY].usage = 0x35;
	c->axes[MTY_CAXIS_THUMB_RY].min = INT16_MIN;
	c->axes[MTY_CAXIS_THUMB_RY].max = INT16_MAX;

	c->axes[MTY_CAXIS_TRIGGER_L].value = lrint(lt * UINT8_MAX);
	c->axes[MTY_CAXIS_TRIGGER_L].usage = 0x33;
	c->axes[MTY_CAXIS_TRIGGER_L].min = 0;
	c->axes[MTY_CAXIS_TRIGGER_L].max = UINT8_MAX;

	c->axes[MTY_CAXIS_TRIGGER_R].value = lrint(rt * UINT8_MAX);
	c->axes[MTY_CAXIS_TRIGGER_R].usage = 0x34;
	c->axes[MTY_CAXIS_TRIGGER_R].min = 0;
	c->axes[MTY_CAXIS_TRIGGER_R].max = UINT8_MAX;

	c->buttons[MTY_CBUTTON_LEFT_TRIGGER] = c->axes[MTY_CAXIS_TRIGGER_L].value > 0;
	c->buttons[MTY_CBUTTON_RIGHT_TRIGGER] = c->axes[MTY_CAXIS_TRIGGER_R].value > 0;

	// Connect
	if (state == 1) {
		MTY_Event cevt = evt;
		cevt.type = MTY_EVENT_CONNECT;
		ctx->event_func(&cevt, ctx->opaque);

	// Disconnect
	} else if (state == 2) {
		evt.type = MTY_EVENT_DISCONNECT;
	}

	// Dedupe and fire event
	if (mty_hid_dedupe(ctx->deduper, c) || evt.type != MTY_EVENT_CONTROLLER)
		ctx->event_func(&evt, ctx->opaque);
}


// App / Window

__attribute__((export_name("mty_app_set_keys")))
void mty_app_set_keys(void)
{
	MTY_Hash *h = web_keymap_hash();

	uint64_t i = 0;
	for (const char *key = NULL; MTY_HashGetNextKey(h, &i, &key);) {
		uintptr_t code = (uintptr_t) MTY_HashGet(h, key);

		bool reverse = code & 0x10000;
		code &= 0xFFFF;

		web_set_key(reverse, key, code);
	}

	MTY_HashDestroy(&h, NULL);
}

MTY_App *MTY_AppCreate(MTY_AppFlag flags, MTY_AppFunc appFunc, MTY_EventFunc eventFunc, void *opaque)
{
	MTY_App *ctx = MTY_Alloc(1, sizeof(MTY_App));
	ctx->app_func = appFunc;
	ctx->event_func = eventFunc;
	ctx->opaque = opaque;

	web_set_app(ctx);

	ctx->hotkey = MTY_HashCreate(0);
	ctx->deduper = MTY_HashCreate(0);

	return ctx;
}

void MTY_AppDestroy(MTY_App **app)
{
	if (!app || !*app)
		return;

	MTY_App *ctx = *app;

	MTY_HashDestroy(&ctx->hotkey, NULL);
	MTY_HashDestroy(&ctx->deduper, MTY_Free);

	MTY_Free(ctx);
	*app = NULL;
}

void MTY_AppRun(MTY_App *ctx)
{
	web_run_and_yield(ctx->app_func, ctx->opaque);
}

void MTY_AppSetTimeout(MTY_App *ctx, uint32_t timeout)
{
}

bool MTY_AppIsActive(MTY_App *ctx)
{
	return ctx->focus;
}

void MTY_AppActivate(MTY_App *ctx, bool active)
{
}

void MTY_AppSetTray(MTY_App *ctx, const char *tooltip, const MTY_MenuItem *items, uint32_t len)
{
}

void MTY_AppRemoveTray(MTY_App *ctx)
{
}

void MTY_AppSendNotification(MTY_App *ctx, const char *title, const char *msg)
{
}

char *MTY_AppGetClipboard(MTY_App *ctx)
{
	return web_get_clipboard();
}

void MTY_AppSetClipboard(MTY_App *ctx, const char *text)
{
	web_set_clipboard(text);
}

void MTY_AppStayAwake(MTY_App *ctx, bool enable)
{
	web_wake_lock(enable);
}

MTY_DetachState MTY_AppGetDetachState(MTY_App *ctx)
{
	return ctx->detach;
}

void MTY_AppSetDetachState(MTY_App *ctx, MTY_DetachState state)
{
	ctx->detach = state;
}

bool MTY_AppIsMouseGrabbed(MTY_App *ctx)
{
	return false;
}

void MTY_AppGrabMouse(MTY_App *ctx, bool grab)
{
}

bool MTY_AppGetRelativeMouse(MTY_App *ctx)
{
	return ctx->relative;
}

void MTY_AppSetRelativeMouse(MTY_App *ctx, bool relative)
{
	web_set_pointer_lock(relative);
}

void MTY_AppSetRGBACursor(MTY_App *ctx, const void *image, uint32_t width, uint32_t height,
	uint32_t hotX, uint32_t hotY)
{
	web_set_rgba_cursor(image, width, height, hotX, hotY);
}

void MTY_AppSetPNGCursor(MTY_App *ctx, const void *image, size_t size, uint32_t hotX, uint32_t hotY)
{
	web_set_png_cursor(image, size, hotX, hotY);
}

void MTY_AppUseDefaultCursor(MTY_App *ctx, bool useDefault)
{
	web_use_default_cursor(useDefault);
}

void MTY_AppSetCursor(MTY_App *ctx, MTY_Cursor cursor)
{
	web_use_default_cursor(cursor != MTY_CURSOR_NONE);
}

void MTY_AppShowCursor(MTY_App *ctx, bool show)
{
	web_show_cursor(show);
}

bool MTY_AppCanWarpCursor(MTY_App *ctx)
{
	return false;
}

bool MTY_AppIsKeyboardGrabbed(MTY_App *ctx)
{
	return ctx->kb_grab;
}

bool MTY_AppGrabKeyboard(MTY_App *ctx, bool grab)
{
	ctx->kb_grab = grab;

	web_set_kb_grab(grab);

	return ctx->kb_grab;
}

uint32_t MTY_AppGetHotkey(MTY_App *ctx, MTY_Scope scope, MTY_Mod mod, MTY_Key key)
{
	mod &= 0xFF;

	return (uint32_t) (uintptr_t) MTY_HashGetInt(ctx->hotkey, (mod << 16) | key);
}

void MTY_AppSetHotkey(MTY_App *ctx, MTY_Scope scope, MTY_Mod mod, MTY_Key key, uint32_t id)
{
	mod &= 0xFF;
	MTY_HashSetInt(ctx->hotkey, (mod << 16) | key, (void *) (uintptr_t) id);
}

void MTY_AppRemoveHotkeys(MTY_App *ctx, MTY_Scope scope)
{
	MTY_HashDestroy(&ctx->hotkey, NULL);
	ctx->hotkey = MTY_HashCreate(0);
}

void MTY_AppEnableGlobalHotkeys(MTY_App *ctx, bool enable)
{
}

bool MTY_AppIsSoftKeyboardShowing(MTY_App *ctx)
{
	return false;
}

void MTY_AppShowSoftKeyboard(MTY_App *ctx, bool show)
{
}

MTY_Orientation MTY_AppGetOrientation(MTY_App *ctx)
{
	return MTY_ORIENTATION_USER;
}

void MTY_AppSetOrientation(MTY_App *ctx, MTY_Orientation orientation)
{
}

void MTY_AppRumbleController(MTY_App *ctx, uint32_t id, uint16_t low, uint16_t high)
{
	web_rumble_gamepad(id, (float) low / (float) UINT16_MAX, (float) high / (float) UINT16_MAX);
}

const char *MTY_AppGetControllerDeviceName(MTY_App *ctx, uint32_t id)
{
	return NULL;
}

MTY_CType MTY_AppGetControllerType(MTY_App *ctx, uint32_t id)
{
	return MTY_CTYPE_DEFAULT;
}

void MTY_AppSubmitHIDReport(MTY_App *ctx, uint32_t id, const void *report, size_t size)
{
}

bool MTY_AppIsPenEnabled(MTY_App *ctx)
{
	return false;
}

void MTY_AppEnablePen(MTY_App *ctx, bool enable)
{
}

MTY_InputMode MTY_AppGetInputMode(MTY_App *ctx)
{
	return MTY_INPUT_MODE_UNSPECIFIED;
}

void MTY_AppSetInputMode(MTY_App *ctx, MTY_InputMode mode)
{
}

void MTY_AppSetWMsgFunc(MTY_App *ctx, MTY_WMsgFunc func)
{
}


// Window

MTY_Window MTY_WindowCreate(MTY_App *app, const char *title, const MTY_Frame *frame, MTY_Window index)
{
	MTY_WindowSetTitle(app, 0, title ? title : "MTY_Window");

	return 0;
}

void MTY_WindowDestroy(MTY_App *app, MTY_Window window)
{
}

MTY_Size MTY_WindowGetSize(MTY_App *app, MTY_Window window)
{
	MTY_Size size = {
		.w = app->width,
		.h = app->height,
	};

	return size;
}

MTY_Frame MTY_WindowGetFrame(MTY_App *app, MTY_Window window)
{
	MTY_Frame frame = {
		.size = MTY_WindowGetSize(app, window),
		.x = app->pos_x,
		.y = app->pos_y,
	};

	return frame;
}

void MTY_WindowSetFrame(MTY_App *app, MTY_Window window, const MTY_Frame *frame)
{
}

void MTY_WindowSetMinSize(MTY_App *app, MTY_Window window, uint32_t minWidth, uint32_t minHeight)
{
}

MTY_Size MTY_WindowGetScreenSize(MTY_App *app, MTY_Window window)
{
	MTY_Size size = {
		.w = app->screen_width,
		.h = app->screen_height,
	};

	return size;
}

float MTY_WindowGetScreenScale(MTY_App *app, MTY_Window window)
{
	return app->scale;
}

void MTY_WindowSetTitle(MTY_App *app, MTY_Window window, const char *title)
{
	web_set_title(title);
}

bool MTY_WindowIsVisible(MTY_App *app, MTY_Window window)
{
	return app->visible;
}

bool MTY_WindowIsActive(MTY_App *app, MTY_Window window)
{
	return app->focus;
}

void MTY_WindowActivate(MTY_App *app, MTY_Window window, bool active)
{
}

bool MTY_WindowExists(MTY_App *app, MTY_Window window)
{
	return true;
}

bool MTY_WindowIsFullscreen(MTY_App *app, MTY_Window window)
{
	return app->fullscreen;
}

void MTY_WindowSetFullscreen(MTY_App *app, MTY_Window window, bool fullscreen)
{
	web_set_fullscreen(fullscreen);
}

void MTY_WindowWarpCursor(MTY_App *app, MTY_Window window, uint32_t x, uint32_t y)
{
}

MTY_ContextState MTY_WindowGetContextState(MTY_App *app, MTY_Window window)
{
	return MTY_CONTEXT_STATE_NORMAL;
}

void *MTY_WindowGetNative(MTY_App *app, MTY_Window window)
{
	return app;
}


// App, Window Private

MTY_EventFunc mty_app_get_event_func(MTY_App *app, void **opaque)
{
	*opaque = app->opaque;

	return app->event_func;
}

MTY_Hash *mty_app_get_hotkey_hash(MTY_App *app)
{
	return app->hotkey;
}

struct window_common *mty_window_get_common(MTY_App *app, MTY_Window window)
{
	return &app->cmn;
}


// Misc

MTY_Frame MTY_MakeDefaultFrame(int32_t x, int32_t y, uint32_t w, uint32_t h, float maxHeight)
{
	return (MTY_Frame) {
		.x = x,
		.y = y,
		.size.w = w,
		.size.h = h,
	};
}

void MTY_HotkeyToString(MTY_Mod mod, MTY_Key key, char *str, size_t len)
{
	memset(str, 0, len);

	MTY_Strcat(str, len, (mod & MTY_MOD_WIN) ? "Super+" : "");
	MTY_Strcat(str, len, (mod & MTY_MOD_CTRL) ? "Ctrl+" : "");
	MTY_Strcat(str, len, (mod & MTY_MOD_ALT) ? "Alt+" : "");
	MTY_Strcat(str, len, (mod & MTY_MOD_SHIFT) ? "Shift+" : "");

	char key_str[32];
	if (web_get_key(key, key_str, 32))
		MTY_Strcat(str, len, key_str);
}

void MTY_SetAppID(const char *id)
{
}

void *MTY_GLGetProcAddress(const char *name)
{
	return NULL;
}

void MTY_RunAndYield(MTY_IterFunc iter, void *opaque)
{
	web_run_and_yield(iter, opaque);
}
