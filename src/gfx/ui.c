#include "gfx/ui.h"
#include <string.h>
#include "assets.h"
#include "game/pokemon.h"
#include "gfx/display.h"
#include "gfx/widgets.h"

/* Disposition : celle de la GBA (240×160), les éléments de droite rapprochés de 26 px. */
#define FOE_DX -26 /* plateforme adverse */
#define PLAYER_DX -4
#define FOE_X 117 /* cadre de 64×64 du Pokémon adverse */
#define FOE_Y 10 /* (4 px plus bas que la GBA : place pour le compteur de combats) */
#define FOE_DY 4
#define PLAYER_X 26

/* Le cadre de PV d'Émeraude (le même des deux côtés : pas de barre d'expérience, le jeu n'a pas de
 * niveaux), agrandi de GROW lignes à partir de la ligne SPLIT pour y ranger les types. */
#define GROW 26
#define SPLIT 10
#define BOX_W 100
#define BOX_H (28 + GROW)
#define FOE_BOX_X 3
#define FOE_BOX_Y 2
#define PLAYER_BOX_X (SCREEN_W - BOX_W - 3)
#define PLAYER_BOX_Y (SCENE_H - BOX_H - 3)
#define ROW4 37 /* 4e ligne des cadres : Poké Balls ou PV, et statut */

#define PANEL_Y (FOE_BOX_Y + BOX_H + 1) /* fenêtre de détail du menu des attaques */
#define PANEL_W 108

/* Duo : cadres compacts, ceux de l'adversaire en haut à gauche, les siens en bas à droite. */
#define MINI_W 112
#define MINI_H 26

#define BOX_INK RGB565(64, 64, 64)
#define BOX_SHADOW RGB565(216, 208, 176)

static const char *const menu_items[2][4] = {{"ATTAQUE", "POKéMON", "INFOS", "FUITE"}, {"FIGHT", "POKéMON", "INFO", "RUN"}};

static void stretched(const Image *im, int x, int y) {
  gfx_image_rows(im, 0, SPLIT, x, y);
  for (int k = 0; k < GROW; k++) gfx_image_rows(im, SPLIT, 1, x, y + SPLIT + k);
  gfx_image_rows(im, SPLIT, im->h - SPLIT, x, y + SPLIT + GROW);
}

/* Nom (et symbole Méga ou étoile chromatique), types, statut, barre de PV. */
static void box_contents(const BattleSide *s, int x, int y, int bar_x, int name_x) {
  int nx = gfx_text(&font_normal, x + name_x, y + 3, s->name, BOX_INK, BOX_SHADOW);
  if (species_info[s->species].is_mega) gfx_image(&mega_icon[0], nx + 1, y + 8);
  else if (s->shiny) ui_shiny_star(nx + 1, y + 8);
  ui_types(x + name_x, y + 18, s->species);
  ui_hpbar(x + bar_x, y + 30, s->hp, s->max_hp);
  ui_status_badge(x + 6, y + ROW4, s->status);
}

/* L'adversaire : ses Poké Balls (sombres si K.O.) sur la dernière ligne, à droite du statut. */
static void draw_hpbox_foe(const BattleView *v) {
  int x = FOE_BOX_X, y = FOE_BOX_Y;
  stretched(&img_hpbox_foe, x, y);
  box_contents(&v->mon[1], x, y, 21, 6);
  for (int i = 0; i < v->foe_count; i++) {
    const Image *b = &ball_icon[i < v->foe_alive ? 0 : 2];
    gfx_image(b, x + 86 - (v->foe_count - i) * 9 + b->x, y + ROW4 + 1 + b->y);
  }
}

/* Le joueur voit en plus ses PV, à droite de ses types. */
static void draw_hpbox_player(const BattleView *v) {
  int x = PLAYER_BOX_X, y = PLAYER_BOX_Y;
  stretched(&img_hpbox_foe, x, y);
  box_contents(&v->mon[0], x, y, 21, 6);
  int ty = y + ROW4 - 3; /* (la police small : lettres sur les lignes 4 à 11) */
  int slash = gfx_number(&font_small, x + 86, ty, v->mon[0].max_hp, BOX_INK, BOX_SHADOW) - text_width(&font_small, "/");
  gfx_text(&font_small, slash, ty, "/", BOX_INK, BOX_SHADOW);
  gfx_number(&font_small, slash, ty, v->mon[0].hp, BOX_INK, BOX_SHADOW);
}

static void draw_trainer(const BattleView *v) {
  const Image *im = &trainer_pic[v->trainer];
  gfx_image(im, FOE_X + im->x, FOE_Y + im->y);
}

/* Le coin haut gauche du cadre de 64×64 du sprite de la position p. */
static void spot(const BattleView *v, int p, const Image *im, int *x, int *y) {
  static const int8_t dx[VIEW_POS] = {-20, 22, 22, -22}, dy[VIEW_POS] = {0, -3, 3, 6};
  bool foe = p & 1;
  *x = (foe ? FOE_X : PLAYER_X) + (v->doubles ? dx[p] : 0);
  *y = (foe ? FOE_Y : SCENE_H + 2 - im->y - im->h) + (v->doubles ? dy[p] : 0);
}

/* Le milieu du sprite de la position p (effets d'attaque). */
static void center(const BattleView *v, int p, int *x, int *y) {
  const BattleSide *b = &v->mon[p];
  const Image *im = p & 1 ? &pokemon_front[b->species] : &pokemon_back[b->species];
  spot(v, p, im, x, y);
  *x += 32, *y += p & 1 ? 34 : im->y + im->h / 2;
}

/* Duo : cadre compact (nom, statut, barre de PV, et les PV chez le joueur). */
static void mini_box_at(int p, int *x, int *y) {
  bool foe = p & 1;
  *x = foe ? 3 : SCREEN_W - MINI_W - 2;
  *y = foe ? 2 + (p >> 1) * (MINI_H + 2) : SCENE_H - 2 * (MINI_H + 2) + (p >> 1) * (MINI_H + 2);
}

/* Ligne 1 : nom, puis les types en abrégé à droite ; ligne 2 : barre de PV, statut, PV (chez le
 * joueur) ou, pendant le choix d'une attaque, son efficacité sur ce Pokémon. */
static void draw_mini_box(const BattleView *v, int p) {
  const BattleSide *s = &v->mon[p];
  const SpeciesInfo *sp = &species_info[s->species];
  int x, y;
  mini_box_at(p, &x, &y);
  ui_window(x, y, MINI_W, MINI_H);
  if (p == v->choosing) ui_frame(x + 2, y + 2, MINI_W - 4, MINI_H - 4, RGB565(232, 128, 48));
  if (p == v->aim) ui_frame(x + 2, y + 2, MINI_W - 4, MINI_H - 4, RGB565(216, 48, 32));
  int nx = gfx_text(&font_small, x + 6, y - 1, s->name, BOX_INK, BOX_SHADOW);
  if (sp->is_mega) gfx_image(&mega_icon[0], nx + 1, y + 3);
  else if (s->shiny) ui_shiny_star(nx + 1, y + 3);
  int tx = x + MINI_W - 6;
  if (sp->type2 != sp->type1) tx -= ui_type_mini(-100, -100, (uint8_t)sp->type2) + 2, ui_type_mini(tx + 2, y + 2, (uint8_t)sp->type2);
  tx -= ui_type_mini(-100, -100, (uint8_t)sp->type1);
  ui_type_mini(tx, y + 2, (uint8_t)sp->type1);
  ui_hpbar(x + 5, y + 15, s->hp, s->max_hp);
  int right = x + MINI_W - 6;
  if (v->tag[p] != 255) {
    right -= ui_eff_pill(-100, -100, v->tag[p]);
    ui_eff_pill(right, y + 13, v->tag[p]);
  } else if (!(p & 1)) {
    right = gfx_number(&font_small, right, y + 10, s->hp, BOX_INK, BOX_SHADOW);
  }
  if (s->status) ui_status_badge(x + 74, y + 13, s->status);
}

static void draw_sparkle(int cx, int cy, int frame) {
  static const int8_t pos[6][2] = {{-24, -18}, {20, -22}, {26, 6}, {-20, 14}, {0, -28}, {4, 20}};
  for (int k = 0; k < 6; k++) {
    if ((frame + k) % 3 == 0) continue;
    int r = 1 + (frame + k) % 3;
    int x = cx + pos[k][0] * (12 - frame % 12) / 12, y = cy + pos[k][1] * (12 - frame % 12) / 12;
    gfx_fill(x - r, y, 2 * r + 1, 1, RGB565(255, 255, 200));
    gfx_fill(x, y - r, 1, 2 * r + 1, RGB565(255, 255, 200));
  }
}

/* Anneaux colorés de la Méga-Évolution, autour de (cx, cy). */
static void draw_rings(int cx, int cy, int r0) {
  static const uint16_t rainbow[6] = {RGB565(248, 80, 80),  RGB565(248, 176, 48), RGB565(248, 240, 80),
                                      RGB565(96, 224, 96),   RGB565(80, 160, 248), RGB565(200, 112, 248)};
  for (int k = 0; k < 3; k++) {
    int r = (r0 + k * 12) % 40 + 4;
    uint16_t c = rainbow[(r0 / 3 + k) % 6];
    for (int x = 0, y = r, d = 3 - 2 * r; x <= y; x++) { /* cercle de Bresenham */
      static const int8_t sx[8] = {1, -1, 1, -1, 1, -1, 1, -1}, sy[8] = {1, 1, -1, -1, 1, 1, -1, -1};
      for (int q = 0; q < 8; q++) {
        int px = q < 4 ? x : y, py = q < 4 ? y : x;
        gfx_fill(cx + sx[q] * px, cy + sy[q] * py, 1, 1, c);
      }
      if (d < 0) d += 4 * x + 6;
      else d += 4 * (x - y) + 10, y--;
    }
  }
}

/* Effet d'une attaque : boule de la couleur du type qui file vers la cible, impact, étincelles. */
static void draw_fx(const BattleView *v) {
  int t = v->fx_t, fx, fy, tx, ty;
  center(v, v->fx_side, &fx, &fy);
  center(v, v->fx_to, &tx, &ty);
  uint16_t c = v->fx_color, light = RGB565(255, 255, 255);
  if (v->fx_kind == FX_ORB) {
    int x = fx + (tx - fx) * t / 16, y = fy + (ty - fy) * t / 16;
    gfx_fill(x - 4, y - 2, 9, 5, c), gfx_fill(x - 2, y - 4, 5, 9, c), gfx_fill(x - 1, y - 1, 3, 3, light);
  } else if (v->fx_kind == FX_IMPACT) {
    int r = 3 + t / 2;
    for (int k = -r; k <= r; k += 2) gfx_fill(tx + k, ty + k, 2, 2, (k + t) & 2 ? c : light), gfx_fill(tx + k, ty - k, 2, 2, c);
  } else {
    for (int k = 0; k < 6; k++) {
      int x = fx - 20 + (k * 13 + t * 3) % 40, y = fy - 24 + (k * 17 + t * 5) % 40;
      gfx_fill(x - 1, y, 3, 1, c), gfx_fill(x, y - 1, 1, 3, c);
    }
  }
}

/* Fenêtre de détail de l'attaque sous le curseur. */
static void draw_move_panel(const BattleView *v) {
  const MoveSlot *m = &v->moves[v->cursor];
  int x = 0, y = PANEL_Y;
  ui_window(x, y, PANEL_W, SCENE_H - PANEL_Y);
  if (!m->name) return;
  x += 7, y += 4;
  int w = ui_type_badge(x, y, m->type);
  gfx_image(&category_icon[m->category], x + w + 3 + category_icon[m->category].x, y - 3 + category_icon[m->category].y);
  /* PP, en orange puis en rouge quand ils s'épuisent */
  uint16_t ink = !m->pp ? INK_RED : m->pp * 4 <= m->max_pp ? RGB565(224, 128, 8) : INK_DARK;
  int right = PANEL_W - 8;
  int slash = gfx_number(&font_small, right, y - 3, m->max_pp, INK_DARK, INK_DARK_SHADOW) - text_width(&font_small, "/");
  gfx_text(&font_small, slash, y - 3, "/", INK_DARK, INK_DARK_SHADOW);
  int px = gfx_number(&font_small, slash, y - 3, m->pp, ink, INK_DARK_SHADOW);
  gfx_text(&font_small, px - text_width(&font_small, "PP "), y - 3, "PP ", INK_DARK, INK_DARK_SHADOW);
  y += 11;
  if (m->category != CATEGORY_STATUS) {
    int tx = gfx_text(&font_small, x, y - 3, L("PUISS. ", "POWER "), INK_DARK, INK_DARK_SHADOW);
    if (m->power > 1) gfx_number(&font_small, tx + text_width(&font_small, "000"), y - 3, m->power, INK_DARK, INK_DARK_SHADOW);
    else gfx_text(&font_small, tx, y - 3, "?", INK_DARK, INK_DARK_SHADOW);
  }
  int ax = gfx_text(&font_small, x + 52, y - 3, L("PRÉC. ", "ACC. "), INK_DARK, INK_DARK_SHADOW);
  if (m->accuracy) gfx_number(&font_small, ax + text_width(&font_small, "000"), y - 3, m->accuracy, INK_DARK, INK_DARK_SHADOW);
  else gfx_text(&font_small, ax, y - 3, "-", INK_DARK, INK_DARK_SHADOW);
  y += 8;
  if (!v->doubles && m->eff != 255 && m->category != CATEGORY_STATUS) { /* (duo : sur le cadre de chaque cible) */
    const char *t = !m->eff ? L("Sans effet", "No effect") : m->eff > 16 ? L("Super efficace", "Super effective")
                    : m->eff < 16 ? L("Peu efficace", "Not very effective") : L("Efficace", "Effective");
    uint16_t c = !m->eff ? RGB565(120, 120, 120) : m->eff > 16 ? INK_GREEN : m->eff < 16 ? INK_RED : INK_DARK;
    gfx_text(&font_small, x, y - 3, t, c, INK_DARK_SHADOW);
  }
  if (v->mega_ready) { /* Méga-Évolution : shift l'allume */
    const char *mega = L("MÉGA", "MEGA");
    int mx = PANEL_W - 8 - text_width(&font_small, mega);
    gfx_image(&mega_icon[0], mx - 10, y);
    gfx_text(&font_small, mx, y - 3, mega, v->mega_on ? RGB565(216, 64, 32) : RGB565(160, 160, 160), INK_DARK_SHADOW);
  }
  y += 9;
  if (m->desc) ui_wrap_lines(&font_small, x, y - 3, PANEL_W - 14, 8, 2, m->desc, INK_BLUE, INK_DARK_SHADOW);
}

static void draw_dialog_box(const BattleView *v) {
  if (v->moves_menu) {
    gfx_nine(&nine_menubox, 0, DIALOG_Y, SCREEN_W, DIALOG_H);
    for (int i = 0; i < 4; i++) {
      int x = 18 + (i & 1) * 98, y = DIALOG_Y + 7 + (i >> 1) * 16;
      if (i == v->cursor) gfx_text(&font_normal, x - 10, y, "▶", INK_DARK, INK_DARK_SHADOW);
      gfx_text(&font_normal, x, y, v->moves[i].name ? v->moves[i].name : "-", INK_DARK, INK_DARK_SHADOW);
    }
    return;
  }
  int w = v->menu ? SCREEN_W - 118 : SCREEN_W;
  uint16_t ink, shadow;
  ui_dialog(0, DIALOG_Y, w, DIALOG_H, &ink, &shadow);
  if (v->text) gfx_text_n(&font_normal, 10, DIALOG_Y + 7, v->text, v->shown, ink, shadow);
  if (!v->menu) return;
  int mx = SCREEN_W - 118;
  gfx_nine(&nine_menubox, mx, DIALOG_Y, 118, DIALOG_H);
  for (int i = 0; i < 4; i++) {
    int x = mx + 15 + (i & 1) * 53, y = DIALOG_Y + 7 + (i >> 1) * 16;
    if (i == v->cursor) gfx_text(&font_normal, x - 10, y, "▶", INK_DARK, INK_DARK_SHADOW);
    gfx_text(&font_normal, x, y, menu_items[lang][i], INK_DARK, INK_DARK_SHADOW);
  }
}

static void draw_scene(const void *ctx) {
  const BattleView *v = ctx;
  gfx_backdrop(&backdrops[v->backdrop], 0, FOE_DX, FOE_DY, PLAYER_DX);
  if (v->terrain) { /* Champs : le sol prend leur couleur */
    static const uint16_t tint[5] = {0, RGB565(248, 224, 64), RGB565(72, 200, 72), RGB565(248, 160, 216), RGB565(176, 96, 232)};
    gfx_tint(0, 40, SCREEN_W, SCENE_H - 40, tint[v->terrain], 1);
  }
  if (v->show_trainer) draw_trainer(v);
  /* un Pokémon pas encore envoyé ou K.O. (0 PV) n'a ni sprite ni cadre de PV, comme sur GBA ; les plus
   * éloignés d'abord */
  static const uint8_t back_to_front[VIEW_POS] = {1, 3, 2, 0};
  for (int k = 0; k < VIEW_POS; k++) {
    int s = back_to_front[k];
    if (!v->doubles && s >= 2) continue;
    const BattleSide *b = &v->mon[s];
    bool foe = s & 1;
    if ((!b->hp && !v->cut[s]) || v->hide[s] || (foe && v->show_trainer)) continue;
    const Image *im = foe ? &pokemon_front[b->species] : &pokemon_back[b->species];
    int x, y;
    spot(v, s, im, &x, &y);
    x += v->dx[s], y += v->dy[s];
    if (v->cut[s]) gfx_clip_push(0, 0, SCREEN_W, y + im->y + im->h); /* K.O. : il s'enfonce */
    if (v->silhouette[s]) gfx_image_flat(im, x + im->x, y + v->cut[s] + im->y, RGB565(255, 255, 255));
    else if (foe && v->frame2[s]) ui_pokemon_frame2(b->species, b->shiny, x, y + v->cut[s]);
    else ui_pokemon(b->species, b->shiny, !foe, x, y + v->cut[s]);
    if (v->cut[s]) gfx_clip_pop();
    if (v->ring[s]) draw_rings(x + 32, y + 36, v->ring[s]);
    if (s == v->aim) { /* la cible visée : une flèche qui la désigne */
      int ax = x + 29, ay = y + im->y - 8 < 0 ? 0 : y + im->y - 8;
      for (int j = 0; j < 5; j++) gfx_fill(ax + j, ay + j, 9 - 2 * j, 1, RGB565(216, 48, 32));
    }
  }
  if (v->fx_kind) draw_fx(v);
  for (int s = 0; s < VIEW_POS; s++)
    if (v->sparkle[s] && !((s & 1) && v->show_trainer)) {
      int cx, cy;
      center(v, s, &cx, &cy);
      draw_sparkle(cx, cy, v->sparkle[s]);
    }
  if (v->flash) gfx_tint(0, 0, SCREEN_W, SCENE_H, RGB565(255, 255, 255), v->flash);
  if (v->doubles) {
    for (int s = 0; s < VIEW_POS; s++)
      if (v->mon[s].hp && !((s & 1) && v->show_trainer)) draw_mini_box(v, s);
    if (!v->show_trainer) /* les Poké Balls de l'adversaire, sous le compteur */
      for (int i = 0; i < v->foe_count; i++) {
        const Image *b = &ball_icon[i < v->foe_alive ? 0 : 2];
        gfx_image(b, SCREEN_W - 4 - (v->foe_count - i) * 9 + b->x, 11 + b->y);
      }
  } else {
    if (v->mon[1].hp && !v->show_trainer) draw_hpbox_foe(v);
    if (v->mon[0].hp) draw_hpbox_player(v);
  }
  if (v->number) { /* numéro du combat, en haut à droite */
    char n[16], d[8], *p = d + sizeof d;
    strcpy(n, L("COMBAT ", "BATTLE "));
    unsigned x = v->number;
    *--p = 0;
    do *--p = (char)('0' + x % 10); while (x /= 10);
    strcat(n, p);
    int w = text_width(&font_small, n) + 7, x0 = SCREEN_W - w - 2;
    gfx_fill(x0 + 1, 0, w - 2, 9, RGB565(48, 56, 88));
    gfx_fill(x0, 0, w, 8, RGB565(48, 56, 88));
    gfx_text(&font_small, x0 + 4, -4, n, INK_LIGHT, RGB565(16, 16, 40));
  }
  if (v->moves_menu) draw_move_panel(v);
  draw_dialog_box(v);
}

void ui_draw_battle(const BattleView *v) { gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_scene, v); }
void ui_draw_scene(const BattleView *v) { gfx_render(0, 0, SCREEN_W, SCENE_H, draw_scene, v); }

void ui_draw_hpbox(const BattleView *v, int pos) {
  if (v->doubles) {
    int x, y;
    mini_box_at(pos, &x, &y);
    gfx_render(x, y, MINI_W, MINI_H, draw_scene, v);
  } else if (!(pos & 1)) {
    gfx_render(PLAYER_BOX_X, PLAYER_BOX_Y, img_hpbox_foe.w, img_hpbox_foe.h + GROW, draw_scene, v);
  } else {
    gfx_render(FOE_BOX_X, FOE_BOX_Y, img_hpbox_foe.w, img_hpbox_foe.h + GROW, draw_scene, v);
  }
}

void ui_draw_dialog(const BattleView *v) { gfx_render(0, PANEL_Y, SCREEN_W, SCREEN_H - PANEL_Y, draw_scene, v); }
void ui_draw_text(const BattleView *v) { gfx_render(0, DIALOG_Y, SCREEN_W, DIALOG_H, draw_scene, v); }
