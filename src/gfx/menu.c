#include "gfx/menu.h"
#include "gfx/widgets.h"
#include "platform/input.h"
#include "platform/pc.h"

#define LIST_Y 18
#define FULL_H (SCREEN_H - LIST_Y - FOOTER_H - 2)
#define BELOW_H 37 /* fenêtre de détail sous la liste */

typedef struct {
  const Menu *m;
  int top;
} View;

static int row_height(const Menu *m) {
  if (m->icons) return 19;
  if (m->detail_below || (m->descs && !m->detail && m->n > 5)) return 13; /* lignes serrées : tout tient au-dessus du détail */
  return 16;
}
static int list_width(const Menu *m) {
  return m->detail && !m->detail_below ? (m->list_w ? m->list_w : 104) : SCREEN_W - 4;
}
static bool below(const Menu *m) { return m->detail_below || (m->descs && !m->detail); }
static int list_height(const Menu *m) {
  if (m->descs && !m->detail) { /* la liste se resserre sur ses lignes, l'explication dessous */
    int h = m->n * row_height(m) + 10;
    return h < FULL_H - BELOW_H - 2 ? h : FULL_H - BELOW_H - 2;
  }
  return below(m) ? FULL_H - BELOW_H - 2 : FULL_H;
}
static int visible(const Menu *m) { return (list_height(m) - 10) / row_height(m); }

static void draw(const void *ctx) {
  const View *v = ctx;
  const Menu *m = v->m;
  int lw = list_width(m), rh = row_height(m), lh = list_height(m);
  bool scroll = m->n > visible(m);
  ui_background(m->hue);
  ui_title(m->title);
  ui_window(2, LIST_Y, lw, lh);
  for (int k = 0; k < visible(m) && v->top + k < m->n; k++) {
    int i = v->top + k, y = LIST_Y + 5 + k * rh, x = 16;
    int ty = y + (rh - 16) / 2; /* le texte centré dans la ligne (lettres : lignes 3 à 12 de la police) */
    if (i == m->cursor) { /* ligne choisie, comme le sac, avec un liseré à gauche */
      int w = lw - (scroll ? 18 : 12);
      gfx_fill(8, y + 1, w, rh - 2, RGB565(248, 232, 168));
      gfx_fill(9, y, w - 2, 1, RGB565(248, 232, 168)), gfx_fill(9, y + rh - 1, w - 2, 1, RGB565(248, 232, 168));
      gfx_fill(8, y + 1, 2, rh - 2, RGB565(232, 128, 48));
      gfx_text(&font_normal, 9, ty, "▶", INK_DARK, INK_DARK_SHADOW);
    } else if (k & 1) {
      gfx_fill(8, y + 1, lw - (scroll ? 18 : 12), rh - 2, RGB565(240, 240, 232)); /* une ligne sur deux */
    }
    if (m->icons) {
      ui_icon(m->icons[i], x - 5, y - 9);
      x += 28;
    }
    if (m->row) m->row(m->ctx, i, x, y, lw - (x - 2) - 6);
    else gfx_text(&font_normal, x, ty, m->items[i], INK_DARK, INK_DARK_SHADOW);
  }
  if (scroll) { /* barre de défilement quand la liste dépasse */
    int top = LIST_Y + 6, h = lh - 12, th = h * visible(m) / m->n;
    if (th < 6) th = 6;
    gfx_fill(lw - 6, top, 3, h, RGB565(208, 208, 200));
    gfx_fill(lw - 6, top + (h - th) * v->top / (m->n - visible(m)), 3, th, RGB565(104, 112, 144));
  }
  if (m->detail && m->detail_below) {
    int y = LIST_Y + lh + 2;
    ui_window(2, y, SCREEN_W - 4, BELOW_H);
    m->detail(m->ctx, m->cursor, 10, y + 5, SCREEN_W - 20, BELOW_H - 10);
  } else if (m->descs) {
    int y = LIST_Y + lh + 2, h = SCREEN_H - FOOTER_H - 2 - y;
    ui_window(2, y, SCREEN_W - 4, h);
    ui_wrap_lines(&font_small, 10, y + 2, SCREEN_W - 20, 10, (h - 8) / 10, m->descs[m->cursor], INK_DARK, INK_DARK_SHADOW);
  } else if (m->detail) {
    int x = lw + 4;
    ui_window(x, LIST_Y, SCREEN_W - x - 2, FULL_H);
    m->detail(m->ctx, m->cursor, x + 6, LIST_Y + 5, SCREEN_W - x - 14, FULL_H - 10);
  }
  ui_footer(m->help ? m->help : L("↑↓ choisir   {OK} valider   {BACK} annuler", "↑↓ select   {OK} confirm   {BACK} cancel"));
}

int menu_run(Menu *m, bool *quit) {
  View v = {m, 0};
  if (m->cursor < 0 || m->cursor >= m->n) m->cursor = 0;
  if (m->cursor >= visible(m)) v.top = m->cursor - visible(m) + 1;
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw, &v);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return *quit = true, MENU_BACK;
    if ((k & KEY_BACK) && !m->no_back) return pc_sfx(SFX_BACK), MENU_BACK;
    if ((k & KEY_ALT) && m->alt) return pc_sfx(SFX_SELECT), MENU_ALT;
    if ((k & KEY_EXE) && m->exe) return pc_sfx(SFX_SELECT), MENU_EXE;
    if (k & KEY_OK) return pc_sfx(SFX_SELECT), m->cursor;
    int c = m->cursor + ((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0);
    if (k & KEY_RIGHT) c = m->cursor + visible(m) < m->n ? m->cursor + visible(m) : m->n - 1;
    if (k & KEY_LEFT) c = m->cursor - visible(m) > 0 ? m->cursor - visible(m) : 0;
    if (c < 0) c = m->n - 1; /* la liste boucle */
    if (c >= m->n) c = 0;
    if (c == m->cursor) continue;
    pc_sfx(SFX_CURSOR);
    m->cursor = c;
    if (c < v.top) v.top = c;
    if (c >= v.top + visible(m)) v.top = c - visible(m) + 1;
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw, &v);
  }
}

int menu_list(const char *title, const char *const *items, int n, int hue, bool *quit) {
  Menu m = {.title = title, .items = items, .n = n, .hue = hue};
  return menu_run(&m, quit);
}

int menu_list_desc(const char *title, const char *const *items, const char *const *descs, int n, int hue, bool *quit) {
  Menu m = {.title = title, .items = items, .descs = descs, .n = n, .hue = hue};
  return menu_run(&m, quit);
}

#define BOX_TEXT_W (SCREEN_W - 20)

typedef struct {
  const char *text;
  int choice; /* -1 : simple message */
  bool dim;   /* assombrit l'écran derrière (première image seulement : l'écran logique reste en mémoire) */
  int h;      /* hauteur de la boîte : 2 ou 3 lignes de texte */
} Box;

static void draw_box(const void *ctx) {
  const Box *b = ctx;
  if (b->dim) gfx_dim(0, 0, SCREEN_W, SCREEN_H);
  uint16_t ink, shadow;
  ui_dialog(0, SCREEN_H - b->h, SCREEN_W, b->h, &ink, &shadow);
  ui_wrap_lines(&font_normal, 10, SCREEN_H - b->h + 7, BOX_TEXT_W, 16, 3, b->text, ink, shadow);
  if (b->choice < 0) return;
  int y0 = SCREEN_H - b->h - 44;
  ui_window(SCREEN_W - 56, y0, 54, 42);
  const char *yn[2] = {L("OUI", "YES"), L("NON", "NO")};
  for (int i = 0; i < 2; i++) {
    int y = y0 + 5 + i * 16;
    if (i == b->choice) gfx_text(&font_normal, SCREEN_W - 50, y, "▶", INK_DARK, INK_DARK_SHADOW);
    gfx_text(&font_normal, SCREEN_W - 40, y, yn[i], INK_DARK, INK_DARK_SHADOW);
  }
}

static Box box(const char *text, int choice) {
  int lines = ui_wrap_count(&font_normal, BOX_TEXT_W, text);
  return (Box){text, choice, true, lines >= 3 ? 64 : 48};
}

bool menu_confirm(const char *question, bool *quit) {
  Box b = box(question, 0);
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_box, &b);
  b.dim = false;
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return *quit = true, false;
    if (k & KEY_BACK) return pc_sfx(SFX_BACK), false;
    if (k & KEY_OK) return pc_sfx(SFX_SELECT), b.choice == 0;
    if (k & (KEY_UP | KEY_DOWN)) pc_sfx(SFX_CURSOR), b.choice ^= 1, gfx_render(SCREEN_W - 56, SCREEN_H - b.h - 44, 54, 42, draw_box, &b);
  }
}

void menu_message(const char *text, bool *quit) {
  Box b = box(text, -1);
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_box, &b);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return (void)(*quit = true);
    if (k & (KEY_OK | KEY_BACK)) return pc_sfx(SFX_SELECT);
  }
}
