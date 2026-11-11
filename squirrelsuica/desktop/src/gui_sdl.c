/* sqgui: SDL2 + SDL_ttf window showing balance and history. Original code. */
#include "sqgui.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sqs_gui_available(void) { return 1; }

static const char *font_candidates[] = {
    /* Prefer fonts that cover both Latin and CJK (station names + "Balance ¥…").
     * WenQuanYi / Noto CJK before DroidSansFallback: some Droid builds leave
     * ASCII as .notdef tofu while still drawing kanji. */
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
    "C:\\Windows\\Fonts\\meiryo.ttc",
    "C:\\Windows\\Fonts\\msgothic.ttc",
    "C:\\Windows\\Fonts\\arial.ttf",
    NULL,
};

static TTF_Font *open_font(int px) {
    const char *env = getenv("SQUIRRELSUICA_FONT");
    if (env && *env) {
        TTF_Font *f = TTF_OpenFont(env, px);
        if (f) return f;
        fprintf(stderr, "font: cannot open $SQUIRRELSUICA_FONT=%s: %s\n", env, TTF_GetError());
    }
    for (int i = 0; font_candidates[i]; i++) {
        TTF_Font *f = TTF_OpenFont(font_candidates[i], px);
        if (f) return f;
    }
    fprintf(stderr, "font: no usable TTF found (set SQUIRRELSUICA_FONT)\n");
    return NULL;
}

static SDL_Texture *make_text(SDL_Renderer *ren, TTF_Font *font, const char *utf8, SDL_Color fg) {
    if (!utf8 || !*utf8) utf8 = " ";
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, utf8, fg);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    return t;
}

static void blit(SDL_Renderer *ren, SDL_Texture *t, int x, int y) {
    if (!t) return;
    int w = 0, h = 0;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(ren, t, NULL, &dst);
}

int sqs_gui_show(const SqsView *view, int autoquit_ms) {
    if (!view) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }

    const int W = 720, H = 560;
    char title[128];
    snprintf(title, sizeof title, "SquirrelSuica — %s", view->brand);
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    TTF_Font *font_lg = open_font(28);
    TTF_Font *font_md = open_font(20);
    TTF_Font *font_sm = open_font(16);
    if (!font_lg || !font_md || !font_sm) {
        if (font_lg) TTF_CloseFont(font_lg);
        if (font_md) TTF_CloseFont(font_md);
        if (font_sm) TTF_CloseFont(font_sm);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_Color fg = {0x1a, 0x1a, 0x1a, 0xff};
    SDL_Color muted = {0x55, 0x55, 0x55, 0xff};
    SDL_Color accent = {0x0b, 0x5f, 0xa5, 0xff};
    SDL_Color bg = {0xf4, 0xf6, 0xf8, 0xff};
    SDL_Color card_bg = {0xff, 0xff, 0xff, 0xff};
    SDL_Color line = {0xd8, 0xde, 0xe4, 0xff};

    SDL_Texture *t_brand = make_text(ren, font_md, view->brand, muted);
    SDL_Texture *t_bal = make_text(ren, font_lg, view->balance, accent);
    char meta[160];
    snprintf(meta, sizeof meta, "IDm %s · System %s", view->idm, view->system);
    SDL_Texture *t_meta = make_text(ren, font_sm, meta, muted);
    SDL_Texture *t_hist = make_text(ren, font_md, "History", fg);

    SDL_Texture **row_a = calloc(view->n_rows ? view->n_rows : 1, sizeof *row_a);
    SDL_Texture **row_b = calloc(view->n_rows ? view->n_rows : 1, sizeof *row_b);
    SDL_Texture **row_c = calloc(view->n_rows ? view->n_rows : 1, sizeof *row_c);
    if (!row_a || !row_b || !row_c) {
        free(row_a);
        free(row_b);
        free(row_c);
        SDL_DestroyTexture(t_brand);
        SDL_DestroyTexture(t_bal);
        SDL_DestroyTexture(t_meta);
        SDL_DestroyTexture(t_hist);
        TTF_CloseFont(font_lg);
        TTF_CloseFont(font_md);
        TTF_CloseFont(font_sm);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    if (!view->n_rows) {
        row_a[0] = make_text(ren, font_sm, "(no history rows)", muted);
    } else {
        for (size_t i = 0; i < view->n_rows; i++) {
            const SqsViewRow *r = &view->rows[i];
            char line1[256];
            snprintf(line1, sizeof line1, "%zu. %s  %s  %s  %s", i + 1, r->date, r->kind, r->fare, r->balance);
            row_a[i] = make_text(ren, font_sm, line1, fg);
            if (r->route[0]) row_b[i] = make_text(ren, font_sm, r->route, accent);
            if (r->detail[0]) row_c[i] = make_text(ren, font_sm, r->detail, muted);
        }
    }

    int scroll = 0;
    int win_w = W, win_h = H;
    int running = 1;
    Uint32 started = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) running = 0;
                if (k == SDLK_DOWN || k == SDLK_j) scroll += 24;
                if (k == SDLK_UP || k == SDLK_k) scroll -= 24;
                if (k == SDLK_PAGEDOWN) scroll += 120;
                if (k == SDLK_PAGEUP) scroll -= 120;
                if (k == SDLK_HOME) scroll = 0;
            } else if (ev.type == SDL_MOUSEWHEEL) {
                scroll -= ev.wheel.y * 32;
            } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
            }
        }
        if (autoquit_ms > 0 && (int)(SDL_GetTicks() - started) >= autoquit_ms) running = 0;
        if (scroll < 0) scroll = 0;

        SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, 255);
        SDL_RenderClear(ren);

        /* Header card */
        SDL_Rect header = {16, 16, win_w - 32, 110};
        SDL_SetRenderDrawColor(ren, card_bg.r, card_bg.g, card_bg.b, 255);
        SDL_RenderFillRect(ren, &header);
        SDL_SetRenderDrawColor(ren, line.r, line.g, line.b, 255);
        SDL_RenderDrawRect(ren, &header);
        blit(ren, t_brand, 32, 28);
        blit(ren, t_bal, 32, 56);
        blit(ren, t_meta, 32, 96);

        blit(ren, t_hist, 24, 140);

        int y = 172 - scroll;
        size_t nshow = view->n_rows ? view->n_rows : 1;
        for (size_t i = 0; i < nshow; i++) {
            int block_h = 56;
            if (row_b[i]) block_h += 20;
            if (row_c[i]) block_h += 18;
            if (y + block_h < 160) {
                y += block_h;
                continue;
            }
            if (y > win_h) break;
            SDL_Rect row = {16, y, win_w - 32, block_h - 8};
            SDL_SetRenderDrawColor(ren, card_bg.r, card_bg.g, card_bg.b, 255);
            SDL_RenderFillRect(ren, &row);
            SDL_SetRenderDrawColor(ren, line.r, line.g, line.b, 255);
            SDL_RenderDrawRect(ren, &row);
            blit(ren, row_a[i], 28, y + 8);
            int yy = y + 30;
            if (row_b[i]) {
                blit(ren, row_b[i], 40, yy);
                yy += 20;
            }
            if (row_c[i]) blit(ren, row_c[i], 40, yy);
            y += block_h;
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    for (size_t i = 0; i < (view->n_rows ? view->n_rows : 1); i++) {
        if (row_a[i]) SDL_DestroyTexture(row_a[i]);
        if (row_b[i]) SDL_DestroyTexture(row_b[i]);
        if (row_c[i]) SDL_DestroyTexture(row_c[i]);
    }
    free(row_a);
    free(row_b);
    free(row_c);
    SDL_DestroyTexture(t_brand);
    SDL_DestroyTexture(t_bal);
    SDL_DestroyTexture(t_meta);
    SDL_DestroyTexture(t_hist);
    TTF_CloseFont(font_lg);
    TTF_CloseFont(font_md);
    TTF_CloseFont(font_sm);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
