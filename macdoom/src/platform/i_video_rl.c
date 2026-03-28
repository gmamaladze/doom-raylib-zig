// DOOM macOS Port - Video & Input (Raylib)
//
// Replaces the X11-based i_video.c with Raylib for:
// - Window creation and management
// - 8-bit indexed framebuffer -> RGBA texture blitting
// - Keyboard and mouse input
//

#include <stdlib.h>
#include <string.h>

// DOOM headers MUST come before raylib.h because raylib includes
// <stdbool.h> which redefines true/false, and both DOOM and Raylib
// define KEY_* constants that would conflict.
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "v_video.h"
#include "m_argv.h"

// Save DOOM's key values as literal constants before raylib.h redefines them.
// DOOM key codes from doomdef.h (values are different from Raylib's).
enum {
    DOOMKEY_RIGHTARROW  = 0xae,
    DOOMKEY_LEFTARROW   = 0xac,
    DOOMKEY_UPARROW     = 0xad,
    DOOMKEY_DOWNARROW   = 0xaf,
    DOOMKEY_ESCAPE      = 27,
    DOOMKEY_ENTER       = 13,
    DOOMKEY_TAB         = 9,
    DOOMKEY_F1          = (0x80+0x3b),
    DOOMKEY_F2          = (0x80+0x3c),
    DOOMKEY_F3          = (0x80+0x3d),
    DOOMKEY_F4          = (0x80+0x3e),
    DOOMKEY_F5          = (0x80+0x3f),
    DOOMKEY_F6          = (0x80+0x40),
    DOOMKEY_F7          = (0x80+0x41),
    DOOMKEY_F8          = (0x80+0x42),
    DOOMKEY_F9          = (0x80+0x43),
    DOOMKEY_F10         = (0x80+0x44),
    DOOMKEY_F11         = (0x80+0x57),
    DOOMKEY_F12         = (0x80+0x58),
    DOOMKEY_BACKSPACE   = 127,
    DOOMKEY_PAUSE       = 0xff,
    DOOMKEY_EQUALS      = 0x3d,
    DOOMKEY_MINUS       = 0x2d,
    DOOMKEY_RSHIFT      = (0x80+0x36),
    DOOMKEY_RCTRL       = (0x80+0x1d),
    DOOMKEY_RALT        = (0x80+0x38),
};

// Undefine DOOM's KEY_* macros so they don't conflict with Raylib's enum
#undef KEY_RIGHTARROW
#undef KEY_LEFTARROW
#undef KEY_UPARROW
#undef KEY_DOWNARROW
#undef KEY_ESCAPE
#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_PAUSE
#undef KEY_EQUALS
#undef KEY_MINUS
#undef KEY_RSHIFT
#undef KEY_RCTRL
#undef KEY_RALT
#undef KEY_LALT

// Undefine true/false for stdbool.h compatibility
#undef true
#undef false

#include "raylib.h"

// The framebuffer texture and RGBA conversion buffer
static Texture2D    fb_texture;
static unsigned int rgba_buffer[SCREENWIDTH * SCREENHEIGHT];
static unsigned int palette_rgba[256];
static int          window_multiply = 3;
static int          grabmouse = 1;
static int          initialized = 0;

// Track key states for detecting press/release edges
static int          prev_keys[512];

// Tic counter for input polling
static int          tic_count = 0;

// Externs for direct game state manipulation (bypasses event system for movement)
#define NUMKEYS_LOCAL 256
extern boolean gamekeydown[NUMKEYS_LOCAL];
extern int key_up, key_down, key_left, key_right, key_fire, key_use, key_speed, key_strafe;

// Translate Raylib key codes to DOOM key codes
// WASD maps to arrow key codes so they work in BOTH menus and gameplay.
static int TranslateKey(int rlkey)
{
    switch (rlkey)
    {
        // Arrow keys AND WASD → DOOM arrow codes (works everywhere)
        case KEY_RIGHT:
        case KEY_D:             return DOOMKEY_RIGHTARROW;
        case KEY_LEFT:
        case KEY_A:             return DOOMKEY_LEFTARROW;
        case KEY_UP:
        case KEY_W:             return DOOMKEY_UPARROW;
        case KEY_DOWN:
        case KEY_S:             return DOOMKEY_DOWNARROW;

        case KEY_ESCAPE:        return DOOMKEY_ESCAPE;
        case KEY_ENTER:         return DOOMKEY_ENTER;
        case KEY_TAB:           return DOOMKEY_TAB;
        case KEY_BACKSPACE:     return DOOMKEY_BACKSPACE;
        case KEY_PAUSE:         return DOOMKEY_PAUSE;

        case KEY_EQUAL:         return DOOMKEY_EQUALS;
        case KEY_MINUS:         return DOOMKEY_MINUS;

        case KEY_RIGHT_SHIFT:
        case KEY_LEFT_SHIFT:    return DOOMKEY_RSHIFT;
        case KEY_RIGHT_CONTROL:
        case KEY_LEFT_CONTROL:  return DOOMKEY_RCTRL;
        case KEY_RIGHT_ALT:
        case KEY_LEFT_ALT:      return DOOMKEY_RALT;

        // F → Fire (maps to Ctrl, the default fire key)
        case KEY_F:             return DOOMKEY_RCTRL;

        case KEY_F1:            return DOOMKEY_F1;
        case KEY_F2:            return DOOMKEY_F2;
        case KEY_F3:            return DOOMKEY_F3;
        case KEY_F4:            return DOOMKEY_F4;
        case KEY_F5:            return DOOMKEY_F5;
        case KEY_F6:            return DOOMKEY_F6;
        case KEY_F7:            return DOOMKEY_F7;
        case KEY_F8:            return DOOMKEY_F8;
        case KEY_F9:            return DOOMKEY_F9;
        case KEY_F10:           return DOOMKEY_F10;
        case KEY_F11:           return DOOMKEY_F11;
        case KEY_F12:           return DOOMKEY_F12;

        case KEY_SPACE:         return ' ';
        case KEY_COMMA:         return ',';
        case KEY_PERIOD:        return '.';

        // Remaining letter keys: lowercase ASCII for chat, etc.
        default:
            if (rlkey >= KEY_A && rlkey <= KEY_Z)
                return rlkey + 32; // to lowercase
            if (rlkey >= KEY_ZERO && rlkey <= KEY_NINE)
                return rlkey; // ASCII digits
            return 0;
    }
}

void I_InitGraphics(void)
{
    // Check for scale flags
    if (M_CheckParm("-2"))
        window_multiply = 2;
    if (M_CheckParm("-3"))
        window_multiply = 3;
    if (M_CheckParm("-4"))
        window_multiply = 4;

    int w = SCREENWIDTH * window_multiply;
    int h = SCREENHEIGHT * window_multiply;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(w, h, "DOOM");
    SetTargetFPS(TICRATE);

    // Create framebuffer texture (320x200, RGBA)
    Image img = GenImageColor(SCREENWIDTH, SCREENHEIGHT, BLACK);
    fb_texture = LoadTextureFromImage(img);
    UnloadImage(img);

    // Nearest-neighbor scaling for crispy pixels
    SetTextureFilter(fb_texture, TEXTURE_FILTER_POINT);

    // Grab mouse for FPS-style input
    if (grabmouse)
        DisableCursor();

    memset(prev_keys, 0, sizeof(prev_keys));

    initialized = 1;
}

void I_ShutdownGraphics(void)
{
    if (initialized)
    {
        UnloadTexture(fb_texture);
        CloseWindow();
        initialized = 0;
    }
}

void I_StartFrame(void)
{
    // Nothing needed - Raylib handles frame sync internally
}

void I_StartTic(void)
{
    if (!initialized) return;

    tic_count++;

    // Poll Raylib input events. Must call this for IsKeyDown() to work.
    // Safe to call multiple times per frame — just re-processes the OS event queue.
    PollInputEvents();

    // Handle window close
    if (WindowShouldClose())
        I_Quit();

    event_t ev;

    // --- Direct gamekeydown manipulation ---
    // DOOM's event queue system has timing issues with Raylib: events posted
    // in I_StartTic get consumed in D_ProcessEvents (called right after),
    // and keyup events can cancel keydown events before G_BuildTiccmd reads them.
    // Instead, we directly set gamekeydown[] from Raylib's key state.
    {
        // Movement (WASD + arrows)
        gamekeydown[key_up]    = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
        gamekeydown[key_down]  = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
        gamekeydown[key_left]  = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
        gamekeydown[key_right] = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);

        // Actions
        gamekeydown[key_fire]  = IsKeyDown(KEY_F) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        gamekeydown[key_use]   = IsKeyDown(KEY_SPACE);
        gamekeydown[key_speed] = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        gamekeydown[key_strafe]= IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    }

    // --- Event-based input for menu/UI keys ---
    {
        static const struct { int rlkey; int doomkey; } menu_keys[] = {
            { KEY_ESCAPE,        DOOMKEY_ESCAPE },
            { KEY_ENTER,         DOOMKEY_ENTER },
            { KEY_UP,            DOOMKEY_UPARROW },
            { KEY_DOWN,          DOOMKEY_DOWNARROW },
            { KEY_LEFT,          DOOMKEY_LEFTARROW },
            { KEY_RIGHT,         DOOMKEY_RIGHTARROW },
            { KEY_W,             DOOMKEY_UPARROW },
            { KEY_S,             DOOMKEY_DOWNARROW },
            { KEY_A,             DOOMKEY_LEFTARROW },
            { KEY_D,             DOOMKEY_RIGHTARROW },
            { KEY_BACKSPACE,     DOOMKEY_BACKSPACE },
            { KEY_TAB,           DOOMKEY_TAB },
            { KEY_F1,            DOOMKEY_F1 },
            { KEY_F2,            DOOMKEY_F2 },
            { KEY_F3,            DOOMKEY_F3 },
            { KEY_F4,            DOOMKEY_F4 },
            { KEY_F5,            DOOMKEY_F5 },
            { KEY_F6,            DOOMKEY_F6 },
            { KEY_F7,            DOOMKEY_F7 },
            { KEY_F8,            DOOMKEY_F8 },
            { KEY_F9,            DOOMKEY_F9 },
            { KEY_F10,           DOOMKEY_F10 },
            { KEY_F11,           DOOMKEY_F11 },
            { KEY_F12,           DOOMKEY_F12 },
            { KEY_EQUAL,         DOOMKEY_EQUALS },
            { KEY_MINUS,         DOOMKEY_MINUS },
            { KEY_PAUSE,         DOOMKEY_PAUSE },
            { KEY_SPACE,         ' ' },
            { KEY_Y,             'y' },
            { KEY_N,             'n' },
            { 0, 0 }
        };

        for (int i = 0; menu_keys[i].rlkey != 0; i++)
        {
            int rlkey = menu_keys[i].rlkey;
            int is_down = IsKeyDown(rlkey);
            int was_down = prev_keys[rlkey];

            if (is_down && !was_down)
            {
                ev.type = ev_keydown;
                ev.data1 = menu_keys[i].doomkey;
                ev.data2 = 0;
                ev.data3 = 0;
                D_PostEvent(&ev);
            }
            else if (!is_down && was_down)
            {
                ev.type = ev_keyup;
                ev.data1 = menu_keys[i].doomkey;
                ev.data2 = 0;
                ev.data3 = 0;
                D_PostEvent(&ev);
            }

            prev_keys[rlkey] = is_down;
        }

        // Number keys for weapon switching
        for (int k = KEY_ONE; k <= KEY_NINE; k++) {
            int is_down = IsKeyDown(k);
            int was_down = prev_keys[k];
            if (is_down && !was_down) {
                ev.type = ev_keydown;
                ev.data1 = k; // ASCII digits
                ev.data2 = 0;
                ev.data3 = 0;
                D_PostEvent(&ev);
            } else if (!is_down && was_down) {
                ev.type = ev_keyup;
                ev.data1 = k;
                ev.data2 = 0;
                ev.data3 = 0;
                D_PostEvent(&ev);
            }
            prev_keys[k] = is_down;
        }
    }

    // --- Mouse input ---
    {
        int buttons = 0;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))   buttons |= 1;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))  buttons |= 2;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) buttons |= 4;

        Vector2 delta = GetMouseDelta();

        if (buttons || delta.x != 0.0f || delta.y != 0.0f)
        {
            ev.type = ev_mouse;
            ev.data1 = buttons;
            ev.data2 = (int)(delta.x * 5);
            ev.data3 = (int)(-delta.y * 5);
            D_PostEvent(&ev);
        }
    }

}

void I_SetPalette(byte* palette)
{
    // Convert 768-byte RGB palette to RGBA lookup table
    for (int i = 0; i < 256; i++)
    {
        unsigned int r = gammatable[usegamma][palette[0]];
        unsigned int g = gammatable[usegamma][palette[1]];
        unsigned int b = gammatable[usegamma][palette[2]];

        // Raylib PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 expects 0xAABBGGRR
        palette_rgba[i] = r | (g << 8) | (b << 16) | (0xFFu << 24);
        palette += 3;
    }
}

void I_UpdateNoBlit(void)
{
    // Nothing needed
}

static int frame_count = 0;

void I_FinishUpdate(void)
{
    if (!initialized) return;

    frame_count++;


    // Update sound - called here because SNDSERV is defined in doomdef.h
    // which prevents d_main.c's game loop from calling these
    I_UpdateSound();
    I_SubmitSound();

    // Convert 8-bit indexed framebuffer to RGBA
    byte* src = screens[0];
    for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
    {
        rgba_buffer[i] = palette_rgba[src[i]];
    }

    // Upload to GPU texture
    UpdateTexture(fb_texture, rgba_buffer);

    // Draw the texture scaled to fill the window
    BeginDrawing();
    ClearBackground(BLACK);

    Rectangle src_rect = { 0, 0, SCREENWIDTH, SCREENHEIGHT };
    Rectangle dst_rect = { 0, 0, GetScreenWidth(), GetScreenHeight() };

    DrawTexturePro(fb_texture, src_rect, dst_rect,
                   (Vector2){0, 0}, 0.0f, WHITE);

    EndDrawing();
}

void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
