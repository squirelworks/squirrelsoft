/* libs/sqgui: SDL2 window back end (original code).
 * A streaming ARGB8888 texture the size of the window holds the canvas;
 * sqg_window_present() uploads it. */
#include "sqgui.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

struct sqg_window {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t *px;
    int w, h;
    int down;
    int rawkey;
    sqg_canvas cv;
};

static int resize(sqg_window *s, int w, int h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    uint32_t *px = malloc((size_t)w * (size_t)h * sizeof *px);
    SDL_Texture *tex = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!px || !tex) {
        free(px);
        if (tex)
            SDL_DestroyTexture(tex);
        return -1;
    }
    free(s->px);
    if (s->tex)
        SDL_DestroyTexture(s->tex);
    s->px = px;
    s->tex = tex;
    s->w = w;
    s->h = h;
    memset(px, 0, (size_t)w * (size_t)h * sizeof *px);
    sqg_canvas_init(&s->cv, px, w, h, w);
    return 0;
}

sqg_window *sqg_window_open(const char *title, int w, int h)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return NULL;
    sqg_window *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                              SDL_WINDOW_RESIZABLE);
    if (s->win)
        s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (s->win && !s->ren)
        s->ren = SDL_CreateRenderer(s->win, -1, 0);
    if (!s->ren || resize(s, w, h) != 0) {
        sqg_window_close(s);
        return NULL;
    }
    SDL_StartTextInput();
    return s;
}

static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return SQG_KEY_ENTER;
    case SDLK_BACKSPACE: return SQG_KEY_BACKSPACE;
    case SDLK_DELETE: return SQG_KEY_DELETE;
    case SDLK_LEFT: return SQG_KEY_LEFT;
    case SDLK_RIGHT: return SQG_KEY_RIGHT;
    case SDLK_UP: return SQG_KEY_UP;
    case SDLK_DOWN: return SQG_KEY_DOWN;
    case SDLK_HOME: return SQG_KEY_HOME;
    case SDLK_END: return SQG_KEY_END;
    case SDLK_TAB: return SQG_KEY_TAB;
    case SDLK_ESCAPE: return SQG_KEY_ESCAPE;
    default: return SQG_KEY_NONE;
    }
}

int sqg_window_poll(sqg_window *s, sqg_input *in)
{
    int alive = 1;
    in->pressed = in->released = 0;
    in->pressed2 = in->released2 = 0;
    in->ctrl_key = 0;
    in->wheel = 0;
    in->key = 0;
    in->text[0] = 0;
    s->rawkey = 0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            alive = 0;
            break;
        case SDL_MOUSEMOTION:
            in->mx = ev.motion.x;
            in->my = ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                in->mx = ev.button.x;
                in->my = ev.button.y;
                in->down = 1;
                in->pressed = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                in->mx = ev.button.x;
                in->my = ev.button.y;
                in->down2 = 1;
                in->pressed2 = 1;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                in->mx = ev.button.x;
                in->my = ev.button.y;
                in->down = 0;
                in->released = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                in->mx = ev.button.x;
                in->my = ev.button.y;
                in->down2 = 0;
                in->released2 = 1;
            }
            break;
        case SDL_MOUSEWHEEL:
            in->wheel += ev.wheel.y;
            break;
        case SDL_KEYDOWN:
            if (!in->key)
                in->key = map_key(ev.key.keysym.sym);
            if (!s->rawkey)
                s->rawkey = (int)ev.key.keysym.sym;
            if ((ev.key.keysym.mod & KMOD_CTRL) && ev.key.keysym.sym >= SDLK_a &&
                ev.key.keysym.sym <= SDLK_z && !in->ctrl_key)
                in->ctrl_key = 'a' + (int)(ev.key.keysym.sym - SDLK_a);
            break;
        case SDL_TEXTINPUT: {
            size_t n = strlen(in->text), m = strlen(ev.text.text);
            if (n + m < sizeof in->text)
                memcpy(in->text + n, ev.text.text, m + 1);
            break;
        }
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                (ev.window.data1 != s->w || ev.window.data2 != s->h))
                if (resize(s, ev.window.data1, ev.window.data2) != 0)
                    alive = 0;
            break;
        default:
            break;
        }
    }
    SDL_Keymod mod = SDL_GetModState();
    in->shift = (mod & KMOD_SHIFT) != 0;
    in->ctrl = (mod & KMOD_CTRL) != 0;
    return alive;
}

sqg_canvas *sqg_window_canvas(sqg_window *s)
{
    return &s->cv;
}

void sqg_window_present(sqg_window *s)
{
    SDL_UpdateTexture(s->tex, NULL, s->px, s->w * (int)sizeof(uint32_t));
    SDL_RenderClear(s->ren);
    SDL_RenderCopy(s->ren, s->tex, NULL, NULL);
    SDL_RenderPresent(s->ren);
}

void sqg_window_set_title(sqg_window *s, const char *title)
{
    SDL_SetWindowTitle(s->win, title);
}

int sqg_window_rawkey(const sqg_window *s)
{
    return s->rawkey;
}

void sqg_window_close(sqg_window *s)
{
    if (!s)
        return;
    SDL_StopTextInput();
    if (s->tex)
        SDL_DestroyTexture(s->tex);
    if (s->ren)
        SDL_DestroyRenderer(s->ren);
    if (s->win)
        SDL_DestroyWindow(s->win);
    free(s->px);
    free(s);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
