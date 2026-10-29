#include "game/screens.h"
#include <string.h>
#include "assets.h"
#include "core/dex.h"
#include "game/meta.h"
#include "data.h"
#include "game/movetext.h"
#include "game/trainer.h"
#include "gfx/menu.h"
#include "gfx/widgets.h"
#include "platform/input.h"

/* Petit texte : la police « small » a 3 lignes vides au-dessus des lettres. */
static int small(int x, int y, const char *s, uint16_t c) { return gfx_text(&font_small, x, y - 3, s, c, INK_DARK_SHADOW); }
static int small_num(int right, int y, unsigned v, uint16_t c) { return gfx_number(&font_small, right, y - 3, v, c, INK_DARK_SHADOW); }

static char *num(char *buf, unsigned v) {
  char d[8], *p = d + sizeof d;
  *--p = 0;
  do *--p = (char)('0' + v % 10); while (v /= 10);
  return strcpy(buf, p);
}

/* ------------------------------------------------------------------------- textes */

const char *item_description(uint8_t item) { return item_descs[item]; }



/* ------------------------------------------------------------------------- morceaux de fiches */

static const uint8_t stat_order[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPA, STAT_SPD, STAT_SPE};
static const char *const stat_labels[2][NUM_STATS] = {{"PV", "ATQ", "DÉF", "VIT", "A.SP", "D.SP"},
                                                      {"HP", "ATK", "DEF", "SPE", "SPA", "SPD"}};

/* Les 6 stats sur 2 lignes de 3 colonnes (col px), en rouge / bleu ce que la nature monte / baisse. */
static void stats_grid(const Pokemon *p, int x, int y, int col) {
  uint8_t up = (uint8_t)(p->nature / 5 + 1), down = (uint8_t)(p->nature % 5 + 1);
  for (int k = 0; k < NUM_STATS; k++) {
    uint8_t s = stat_order[k];
    int cx = x + (k % 3) * col, cy = y + (k / 3) * 9;
    uint16_t c = up != down && s == up ? INK_RED : up != down && s == down ? INK_BLUE : INK_DARK;
    small(cx, cy, stat_labels[lang][s], c);
    small_num(cx + col - 6, cy, pokemon_stat(p, s), INK_DARK);
  }
}

/* Une attaque : pastille de la couleur du type et nom. */
static void move_line(int x, int y, uint16_t move) {
  if (!move) return (void)small(x + 7, y, "-", INK_DARK);
  ui_type_dot(x, y, (uint8_t)move_info[move].type);
  small(x + 7, y, move_names[move], INK_DARK);
}

static bool header_frame2; /* le sprite de la fiche montre la 2e image de son animation */

/* En-tête d'une fiche : sprite à gauche, nom, types, talent à droite. */
static void mon_header(const Pokemon *p, int x, int y, int w, bool desc) {
  uint16_t sp = p->species;
  gfx_clip_push(0, y - 4, x + 62, desc ? 56 : 64); /* draft : le sprite s'arrête au-dessus des stats */
  if (header_frame2) ui_pokemon_frame2(sp, p->shiny, x - 2, y - 4);
  else ui_pokemon(sp, p->shiny, false, x - 2, y - 4);
  gfx_clip_pop();
  int rx = x + 64;
  int nx = gfx_text(&font_normal, rx, y - 4, species_names[sp], INK_DARK, INK_DARK_SHADOW);
  if (p->shiny) ui_shiny_star(nx + 2, y + 1);
  if (species_info[sp].legendary) small(nx + (p->shiny ? 11 : 2), y, L("LÉGENDAIRE", "LEGENDARY"), RGB565(184, 136, 16));
  ui_types(rx, y + 10, sp);
  uint16_t a = pokemon_ability(p);
  int tx = small(rx, y + 22, L("Talent ", "Ability "), INK_DARK);
  small(tx, y + 22, ability_names[a], INK_BLUE);
  if (desc) ui_wrap_lines(&font_small, rx, y + 27, w - 64, 9, 2, ability_descs[a], RGB565(96, 96, 96), INK_DARK_SHADOW);
}

/* ------------------------------------------------------------------------- titre */

typedef struct {
  const Run *saved;
  int cursor;
  int moving; /* emblème qui montre la 2e image de son animation, ou -1 */
} Title;

static const uint16_t mascots[3] = {SPECIES_RAYQUAZA, SPECIES_GARCHOMP, SPECIES_MIRAIDON};

static void draw_title(const void *ctx) {
  const Title *t = ctx;
  ui_background(1);
  /* trois Pokémon emblèmes en entier, debout derrière le logo « POKéRUSH » */
  gfx_fill(0, 0, SCREEN_W, 50, RGB565(120, 168, 224));
  gfx_fill(0, 34, SCREEN_W, 16, RGB565(104, 152, 208));
  for (int i = 0; i < 3; i++) {
    if (i == t->moving) ui_pokemon_frame2(mascots[i], false, 8 + i * 68, 0);
    else ui_pokemon(mascots[i], false, false, 8 + i * 68, 0);
  }
  gfx_fill(0, 50, SCREEN_W, 29, RGB565(40, 48, 96));
  gfx_fill(0, 51, SCREEN_W, 1, RGB565(248, 216, 64)), gfx_fill(0, 77, SCREEN_W, 1, RGB565(248, 216, 64));
  ui_ball(8, 60), ui_ball(SCREEN_W - 17, 60);
  int w = text_width(&font_normal, "POKéRUSH") * 2;
  gfx_text_big(&font_normal, (SCREEN_W - w) / 2, 47, "POKéRUSH", RGB565(248, 216, 64), RGB565(152, 64, 32));
  char sub[48], n[12];
  if (t->saved) strcat(strcpy(sub, L("Partie en cours : combat ", "Game in progress: battle ")), num(n, t->saved->battle));
  else strcpy(sub, L("Roguelike des 9 générations", "A roguelike of all 9 generations"));
  if (dex.best) strcat(strcat(sub, L(" · record ", " · best ")), num(n, dex.best));
  small((SCREEN_W - text_width(&font_small, sub)) / 2, 82, sub, RGB565(48, 48, 96));
  ui_window(8, 90, 138, 68);
  const char *const items[5] = {L("CONTINUER", "CONTINUE"), L("NOUVELLE PARTIE", "NEW GAME"), L("CARNET", "POKéDEX"),
                                L("SUCCÈS", "ACHIEVEMENTS"), L("PARAMÈTRES", "SETTINGS")};
  for (int i = 0; i < 5; i++) {
    int y = 93 + i * 12;
    bool off = i == 0 && !t->saved;
    if (i == t->cursor) {
      gfx_fill(13, y + 1, 126, 12, RGB565(248, 232, 168));
      gfx_fill(13, y + 1, 2, 12, RGB565(232, 128, 48));
      gfx_text(&font_normal, 15, y - 1, "▶", INK_DARK, INK_DARK_SHADOW);
    }
    gfx_text(&font_normal, 25, y - 1, items[i], off ? RGB565(168, 168, 168) : INK_DARK, INK_DARK_SHADOW);
  }
  const Image *av = &trainer_pic[avatar_pic()]; /* le personnage choisi */
  gfx_image(av, 148 + av->x, 94 + av->y);
}

int screen_title(const Run *saved, bool *quit) {
  Title t = {saved, saved ? 0 : 1, -1};
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_title, &t);
  for (int f = 0;; f++) {
    uint16_t k = input_wait_frame();
    /* les emblèmes s'animent chacun leur tour */
    int phase = f % 90, who = (f / 90) % 3;
    int moving = dex.anim && (phase < 10 || (phase >= 16 && phase < 26)) ? who : -1;
    if (moving != t.moving) t.moving = moving, gfx_render(0, 0, SCREEN_W, 50, draw_title, &t);
    if (k & KEY_HOME) return *quit = true, -1;
    if (k & KEY_OK) return t.cursor;
    int c = t.cursor + ((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0);
    if (c < (saved ? 0 : 1) || c > 4 || c == t.cursor) continue;
    t.cursor = c;
    gfx_render(0, 90, 150, 70, draw_title, &t);
  }
}

/* ------------------------------------------------------------------------- carnet */

#define DEX_COLS 8
#define DEX_ROWS 4

static void draw_dex(const void *ctx) {
  int cursor = *(const int *)ctx, page = cursor / (DEX_COLS * DEX_ROWS);
  char title[64], n[12];
  ui_background(3);
  strcat(strcat(strcpy(title, L("CARNET  ", "POKéDEX  ")), num(n, (unsigned)dex_count(dex.owned))), " / ");
  strcat(strcat(title, num(n, NUM_BASE_SPECIES)), L(" obtenus", " caught"));
  ui_title(title);
  for (int k = 0; k < DEX_COLS * DEX_ROWS; k++) {
    int s = page * DEX_COLS * DEX_ROWS + k;
    if (s >= NUM_BASE_SPECIES) break;
    int x = 3 + (k % DEX_COLS) * 26, y = 19 + (k / DEX_COLS) * 21;
    if (s == cursor) gfx_fill(x, y, 26, 21, RGB565(248, 232, 168));
    if (dex_has(dex.owned, (uint16_t)s)) ui_icon((uint16_t)s, x - 3, y - 9);
    else if (dex_has(dex.seen, (uint16_t)s)) {
      const Image *im = &pokemon_icon[s];
      gfx_image_flat(im, x - 3 + im->x, y - 9 + im->y, RGB565(96, 96, 120));
    } else {
      small(x + 10, y + 7, "?", RGB565(120, 120, 140));
    }
    if (dex_has(dex.shiny, (uint16_t)s)) ui_shiny_star(x + 18, y + 1);
  }
  ui_window(2, 105, SCREEN_W - 4, 42);
  bool seen = dex_has(dex.seen, (uint16_t)cursor);
  char line[48];
  strcat(strcat(strcpy(line, L("N. ", "No. ")), species_info[cursor].dex ? num(n, species_info[cursor].dex) : "???"), "  ");
  strcat(line, seen ? species_names[cursor] : "?????");
  gfx_text(&font_normal, 9, 108, line, INK_DARK, INK_DARK_SHADOW);
  if (seen) ui_types(SCREEN_W - 80, 112, (uint16_t)cursor);
  strcat(strcat(strcpy(line, L("Vus ", "Seen ")), num(n, (unsigned)dex_count(dex.seen))), L("  Chromatiques ", "  Shinies "));
  strcat(strcat(line, num(n, (unsigned)dex_count(dex.shiny))), L("  Parties ", "  Games "));
  strcat(line, num(n, dex.runs));
  small(9, 129, line, RGB565(96, 96, 96));
  ui_gauge(9, 140, SCREEN_W - 22, dex_count(dex.owned), NUM_BASE_SPECIES, RGB565(232, 128, 48));
  ui_footer(L("←→↑↓ parcourir   RETOUR quitter", "←→↑↓ browse   BACK exit"));
}

void screen_dex(bool *quit) {
  int cursor = 0;
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_dex, &cursor);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return (void)(*quit = true);
    if (k & (KEY_BACK | KEY_OK)) return;
    int c = cursor + ((k & KEY_RIGHT) ? 1 : 0) - ((k & KEY_LEFT) ? 1 : 0) + ((k & KEY_DOWN) ? DEX_COLS : 0) -
            ((k & KEY_UP) ? DEX_COLS : 0);
    if (c < 0) c += NUM_BASE_SPECIES;
    if (c >= NUM_BASE_SPECIES) c -= NUM_BASE_SPECIES;
    if (c != cursor) cursor = c, gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_dex, &cursor);
  }
}

/* ------------------------------------------------------------------------- starter */

typedef struct {
  int cursor;
  Pokemon mon;
} Starter;

static void draw_starter(const void *ctx) {
  const Starter *s = ctx;
  ui_background(0);
  ui_title(L("CHOISISSEZ VOTRE STARTER", "CHOOSE YOUR STARTER"));
  /* 9 colonnes (générations) × 3 lignes (Plante, Feu, Eau) */
  for (int i = 0; i < STARTER_COUNT; i++) {
    int col = i / 3, row = i % 3, x = 3 + col * 23, y = 18 + row * 22;
    if (i == s->cursor) {
      gfx_fill(x, y, 23, 22, RGB565(248, 232, 168));
      gfx_fill(x, y, 23, 1, RGB565(216, 128, 48)), gfx_fill(x, y + 21, 23, 1, RGB565(216, 128, 48));
      gfx_fill(x, y, 1, 22, RGB565(216, 128, 48)), gfx_fill(x + 22, y, 1, 22, RGB565(216, 128, 48));
    }
    ui_icon(starter_species[i], x - 4, y - 8);
  }
  ui_window(2, 86, SCREEN_W - 4, SCREEN_H - 88 - FOOTER_H);
  mon_header(&s->mon, 8, 91, SCREEN_W - 16, false);
  stats_grid(&s->mon, 72, 124, 44);
  ui_footer(L("←→ génération   ↑↓ type   OK choisir", "←→ generation   ↑↓ type   OK choose"));
  char g[24], n[4];
  strcat(strcpy(g, L("GÉN. ", "GEN. ")), num(n, (unsigned)(s->cursor / 3 + 1)));
  small(SCREEN_W - 12 - text_width(&font_small, g), 92, g, RGB565(120, 120, 120));
}

int screen_starter(bool *quit) {
  Starter s = {0};
  uint32_t r = 1;
  for (;;) {
    s.mon = trainer_build(starter_species[s.cursor], 0, 8, false, &r);
    s.mon.shiny = 0;
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_starter, &s);
    for (;;) {
      uint16_t k = input_wait_frame();
      if (k & KEY_HOME) return *quit = true, -1;
      if (k & KEY_BACK) return -1;
      if (k & KEY_OK) return s.cursor;
      int col = s.cursor / 3, row = s.cursor % 3;
      if (k & KEY_LEFT) col = (col + 8) % 9;
      if (k & KEY_RIGHT) col = (col + 1) % 9;
      if (k & KEY_UP) row = (row + 2) % 3;
      if (k & KEY_DOWN) row = (row + 1) % 3;
      if (col * 3 + row != s.cursor) {
        s.cursor = col * 3 + row;
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------------- draft */

/* Le prochain adversaire, à droite du bandeau : son type favori, un Boss ou un raid. */
static void next_tag(const Trainer *t) {
  if (!t) return;
  char s[40];
  uint16_t c;
  if (t->boss) strcat(strcpy(s, "BOSS "), t->name), c = RGB565(200, 48, 48);
  else if (t->wild) strcpy(s, L("RAID !", "RAID!")), c = RGB565(208, 152, 16);
  else strcat(strcpy(s, L("ENSUITE ", "NEXT ")), type_names[t->type]), c = ui_type_color(t->type);
  ui_pill(SCREEN_W - 3 - ui_pill(-200, -200, s, c), 3, s, c);
}

/* La Résonance que l'offre ferait naître ou monter dans l'équipe (si elle y entre) : « EAU II ». */
static bool resonance_gain(const Team *team, const Pokemon *p, char *out) {
  if (team->count >= TEAM_MAX) return false;
  uint8_t before[NUM_TYPES], after[NUM_TYPES];
  Team t = *team;
  resonance_levels(&t, before);
  t.mon[t.count++] = *p;
  resonance_levels(&t, after);
  static const char *const roman[4] = {"", " I", " II", " III"};
  for (int k = 0; k < NUM_TYPES; k++)
    if (after[k] > before[k]) return strcat(strcpy(out, type_names[k]), roman[after[k]]), true;
  return false;
}

/* --- le choix de la récompense : RECRUTER ou ENTRAÎNER */

typedef struct {
  const Pokemon *offers;
  int n;
  const Team *team;
  const Trainer *next;
  uint16_t won;
  int cursor; /* 0 recruter, 1 entraîner */
} Reward;

#define CARD_Y 31
#define CARD_W 101
#define CARD_H 98

static void reward_card(const Reward *r, int k) {
  static const uint16_t hue[2][2] = {{RGB565(216, 104, 48), RGB565(252, 232, 208)}, {RGB565(56, 104, 184), RGB565(216, 228, 252)}};
  bool on = k == r->cursor;
  int x = 4 + k * (CARD_W + 4), y = CARD_Y;
  ui_window(x, y, CARD_W, CARD_H);
  gfx_fill(x + 3, y + 3, CARD_W - 6, CARD_H - 6, on ? hue[k][1] : RGB565(232, 232, 228));
  gfx_fill(x + 3, y + 3, CARD_W - 6, 15, on ? hue[k][0] : RGB565(160, 160, 168)); /* bandeau de la carte */
  if (on) ui_frame(x + 2, y + 2, CARD_W - 4, CARD_H - 4, RGB565(232, 128, 48)), ui_frame(x + 1, y + 1, CARD_W - 2, CARD_H - 2, RGB565(232, 128, 48));
  const char *name = k ? L("ENTRAÎNER", "TRAIN") : L("RECRUTER", "RECRUIT");
  gfx_text(&font_normal, x + (CARD_W - text_width(&font_normal, name)) / 2, y + 3, name, INK_LIGHT, RGB565(40, 40, 64));
  /* les Pokémon concernés : les offres, ou l'équipe */
  int n = k ? r->team->count : r->n;
  for (int i = 0; i < n; i++) {
    uint16_t sp = k ? r->team->mon[i].species : r->offers[i].species;
    ui_icon(sp, x + 3 + (i % 3) * 32, y + 8 + (i / 3) * 19);
  }
  const char *what;
  if (k) what = L("Changer une attaque, la nature ou le talent d'un membre.", "Change a member's move, nature or ability.");
  else if (r->team->count < TEAM_MAX) what = L("Un des POKéMON proposés rejoint l'équipe.", "One of the offered POKéMON joins your team.");
  else what = L("Équipe complète : la recrue remplace un membre.", "Full team: the recruit replaces a member.");
  ui_wrap_lines(&font_small, x + 6, y + 56, CARD_W - 12, 9, 4, what, on ? INK_DARK : RGB565(128, 128, 128), INK_DARK_SHADOW);
}

static void draw_reward(const void *ctx) {
  const Reward *r = ctx;
  char title[48], n[12];
  ui_background(2);
  strcat(strcpy(title, L("VICTOIRE · COMBAT ", "VICTORY · BATTLE ")), num(n, r->won));
  ui_title(title);
  next_tag(r->next);
  const char *ask = L("Choisissez UNE récompense :", "Choose ONE reward:");
  gfx_text(&font_normal, (SCREEN_W - text_width(&font_normal, ask)) / 2, 15, ask, INK_DARK, INK_DARK_SHADOW);
  for (int k = 0; k < 2; k++) reward_card(r, k);
  ui_footer(L("←→ choisir   OK valider   SHIFT menu", "←→ select   OK confirm   SHIFT menu"));
}

int screen_reward(const Pokemon *offers, int n, const Team *team, const Trainer *next, uint16_t won, int *cursor, bool *quit) {
  Reward r = {offers, n, team, next, won, *cursor};
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_reward, &r);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return *quit = true, -1;
    if (k & KEY_ALT) return *cursor = r.cursor, MENU_ALT;
    if (k & KEY_OK) return *cursor = r.cursor;
    if (k & (KEY_LEFT | KEY_RIGHT)) r.cursor ^= 1, gfx_render(0, CARD_Y, SCREEN_W, CARD_H, draw_reward, &r);
  }
}

/* --- les offres */

typedef struct {
  const Pokemon *offers;
  int n;
  const Team *team;
  const Trainer *next;
  int cursor;
} Draft;

static void draw_draft(const void *ctx) {
  const Draft *d = ctx;
  char title[64], n[12];
  ui_background(2);
  strcat(strcat(strcpy(title, L("RECRUTER · ÉQUIPE ", "RECRUIT · TEAM ")), num(n, d->team->count)), "/6");
  ui_title(title);
  next_tag(d->next);
  /* les offres : une carte teintée du type de chaque Pokémon */
  for (int i = 0; i < d->n; i++) {
    const Pokemon *p = &d->offers[i];
    const SpeciesInfo *s = &species_info[p->species];
    bool on = i == d->cursor;
    int x = 3 + i * 42, y = 18;
    ui_window(x, y, 40, 27);
    gfx_fill(x + 3, y + 3, 34, 21, on ? RGB565(248, 232, 168) : ui_pale(ui_type_color((uint8_t)s->type1)));
    if (on) ui_frame(x + 2, y + 2, 36, 23, RGB565(232, 128, 48));
    ui_icon((uint16_t)p->species, x + 4, y - 5);
    if (p->shiny) ui_shiny_star(x + 30, y + 4);
    if (s->legendary || p->species == SPECIES_CLAUDE_CODE) gfx_fill(x + 5, y + 5, 3, 3, RGB565(232, 176, 16));
    if (!dex_has(dex.owned, (uint16_t)p->species)) gfx_fill(x + 31, y + 18, 4, 4, RGB565(48, 176, 72)); /* jamais obtenu */
  }
  const Pokemon *p = &d->offers[d->cursor];
  int x = 8, y = 51;
  ui_window(2, 46, SCREEN_W - 4, SCREEN_H - 46 - FOOTER_H);
  mon_header(p, x, y, SCREEN_W - 16, true);
  /* sous le sprite, à gauche : jamais obtenu, et le POKéMON secret */
  if (!dex_has(dex.owned, (uint16_t)p->species)) ui_pill(x, y + 38, L("NOUVEAU", "NEW"), RGB565(48, 168, 72));
  if (p->species == SPECIES_CLAUDE_CODE) ui_pill(x, y + 27, L("TRÈS RARE", "VERY RARE"), RGB565(216, 120, 80));
  stats_grid(p, x, y + 50, 66);
  int ix = small(x, y + 67, L("Objet ", "Item "), INK_DARK);
  small(ix, y + 67, p->item ? item_names[p->item] : L("aucun", "none"), INK_BLUE);
  char res[24];
  if (resonance_gain(d->team, p, res)) {
    int rx = small(x + 100, y + 67, L("RÉSONANCE ", "RESONANCE "), INK_GREEN);
    small(rx, y + 67, res, INK_GREEN);
  }
  for (int k = 0; k < MOVES_MAX; k++) move_line(x + (k & 1) * 100, y + 76 + (k >> 1) * 9, pokemon_move(p, (uint8_t)k));
  ui_footer(L("OK recruter   RETOUR revenir   SHIFT menu", "OK recruit   BACK return   SHIFT menu"));
}

int screen_draft(const Pokemon *offers, int n, const Team *team, const Trainer *next, int *cursor, bool *quit) {
  Draft d = {offers, n, team, next, *cursor};
  header_frame2 = false;
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_draft, &d);
  for (int t = 0;; t++) {
    uint16_t k = input_wait_frame();
    /* le Pokémon choisi s'anime de temps en temps, comme à son entrée en combat */
    int phase = t % 150;
    bool f2 = dex.anim && pokemon_front2[d.offers[d.cursor].species].w && ((phase >= 60 && phase < 70) || (phase >= 76 && phase < 86));
    if (f2 != header_frame2) header_frame2 = f2, gfx_render(0, 46, 72, 62, draw_draft, &d);
    if (k & KEY_HOME) return *quit = true, -1;
    if (k & KEY_BACK) return *cursor = d.cursor, MENU_BACK;
    if (k & KEY_ALT) return *cursor = d.cursor, MENU_ALT;
    if (k & KEY_OK) return *cursor = d.cursor;
    int c = d.cursor + ((k & KEY_RIGHT) ? 1 : 0) - ((k & KEY_LEFT) ? 1 : 0);
    c = (c + n) % n;
    if (c != d.cursor) d.cursor = c, t = 0, header_frame2 = false, gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_draft, &d);
  }
}

/* ------------------------------------------------------------------------- avant le combat (duo) */

typedef struct {
  const Team *mine;
  const Trainer *foe;
  uint8_t order[4];
  int n, need, cursor;
} Preview;

static int picked(const Preview *p, int i) {
  for (int k = 0; k < p->n; k++)
    if (p->order[k] == i) return k + 1;
  return 0;
}

#define VS_RED RGB565(136, 36, 44)
#define VS_BLUE RGB565(36, 64, 128)
#define VS_GOLD RGB565(248, 200, 64)

static void draw_preview(const void *ctx) {
  const Preview *p = ctx;
  char line[64], n[12];
  /* en haut, l'adversaire sur fond rouge ; en bas, vous sur fond bleu ; « VS » entre les deux */
  gfx_fill(0, 0, SCREEN_W, 60, VS_RED);
  gfx_fill(0, 60, SCREEN_W, 14, RGB565(24, 24, 40));
  gfx_fill(0, 74, SCREEN_W, SCREEN_H - 74, VS_BLUE);
  gfx_fill(0, 59, SCREEN_W, 1, VS_GOLD), gfx_fill(0, 74, SCREEN_W, 1, VS_GOLD);
  for (int y = 0; y < 60; y += 4) gfx_fill(0, y, SCREEN_W, 1, RGB565(120, 28, 36)); /* rayures */
  for (int y = 76; y < SCREEN_H; y += 4) gfx_fill(0, y, SCREEN_W, 1, RGB565(32, 56, 112));
  ui_ball(4, 3);
  gfx_text(&font_normal, 16, -1, L("AVANT LE COMBAT · DUO", "BEFORE THE BATTLE · DOUBLE"), INK_LIGHT, RGB565(64, 16, 24));
  int nx = gfx_text(&font_normal, 6, 12, p->foe->name, VS_GOLD, RGB565(64, 16, 24));
  if (p->foe->team.count > 4) gfx_text(&font_small, nx + 5, 15, L("en enverra 4", "will send 4"), INK_LIGHT, RGB565(64, 16, 24));
  for (int i = 0; i < p->foe->team.count; i++) {
    int x = 4 + i * 35;
    gfx_fill(x + 2, 31, 30, 24, RGB565(168, 64, 72));
    ui_frame(x + 2, 31, 30, 24, RGB565(200, 112, 112));
    ui_icon((uint16_t)p->foe->team.mon[i].species, x + 1, 25);
  }
  int w = text_width(&font_normal, "VS");
  gfx_text(&font_normal, (SCREEN_W - w) / 2, 58, "VS", VS_GOLD, RGB565(152, 64, 32));
  strcat(strcat(strcpy(line, L("VOTRE ÉQUIPE : ", "YOUR TEAM: ")), num(n, (unsigned)p->n)), " / ");
  strcat(strcat(line, num(n, (unsigned)p->need)), L(" choisis", " chosen"));
  gfx_text(&font_small, 6, 74, line, INK_LIGHT, RGB565(16, 24, 64));
  /* vos Pokémon : deux colonnes ; le numéro d'ordre (1 et 2 en or : ils ouvrent le combat) */
  for (int i = 0; i < p->mine->count; i++) {
    const Pokemon *m = &p->mine->mon[i];
    int x = 3 + (i & 1) * 106, y = 87 + (i >> 1) * 21, k = picked(p, i);
    bool on = i == p->cursor;
    gfx_fill(x, y, 102, 20, k ? RGB565(232, 236, 248) : RGB565(168, 184, 216));
    ui_frame(x, y, 102, 20, on ? RGB565(232, 128, 48) : RGB565(24, 40, 96));
    if (on) ui_frame(x + 1, y + 1, 100, 18, RGB565(232, 128, 48));
    ui_icon((uint16_t)m->species, x - 1, y - 8);
    gfx_text(&font_small, x + 30, y - 2, species_names[m->species], INK_DARK, INK_DARK_SHADOW);
    if (m->item) gfx_text(&font_small, x + 30, y + 6, item_names[m->item], INK_BLUE, INK_DARK_SHADOW);
    if (k) {
      char d[2] = {(char)('0' + k), 0};
      uint16_t c = k <= 2 ? RGB565(216, 160, 24) : RGB565(120, 128, 152);
      gfx_fill(x + 88, y + 5, 11, 11, c), gfx_fill(x + 89, y + 4, 9, 13, c), gfx_fill(x + 87, y + 6, 13, 9, c);
      gfx_text(&font_small, x + 91, y + 2, d, INK_LIGHT, RGB565(64, 48, 16));
    }
  }
  ui_footer(L("OK choisir / retirer   RETOUR annuler", "OK pick / remove   BACK undo"));
}

int screen_preview(const Team *mine, const Trainer *foe, uint8_t order[4], bool *quit) {
  Preview p = {mine, foe, {0}, 0, mine->count < 4 ? mine->count : 4, 0};
  if (mine->count <= 4) /* tout le monde combat : l'ordre de l'équipe, modifiable */
    for (int i = 0; i < mine->count; i++) p.order[p.n++] = (uint8_t)i;
  for (;;) {
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_preview, &p);
    if (p.n == p.need) {
      if (menu_confirm(L("Combattre avec ces\nPOKéMON ? (1 et 2 en tête)", "Battle with these\nPOKéMON? (1 and 2 lead)"), quit)) break;
      if (*quit) return 0;
      gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_preview, &p);
    }
    for (;;) {
      uint16_t k = input_wait_frame();
      if (k & KEY_HOME) return *quit = true, 0;
      int c = p.cursor;
      if (k & KEY_LEFT) c ^= 1;
      if (k & KEY_RIGHT) c ^= 1;
      if (k & KEY_UP) c -= 2;
      if (k & KEY_DOWN) c += 2;
      if (c >= mine->count) c = mine->count - 1;
      if (c < 0) c = 0;
      if (c != p.cursor) {
        p.cursor = c;
        gfx_render(0, 74, SCREEN_W, SCREEN_H - 74, draw_preview, &p);
        continue;
      }
      if ((k & KEY_BACK) && p.n) {
        p.n--;
        break;
      }
      if (k & KEY_OK) {
        int at = picked(&p, p.cursor);
        if (at) { /* retiré : les suivants remontent */
          for (int j = at; j < p.n; j++) p.order[j - 1] = p.order[j];
          p.n--;
        } else if (p.n < p.need) {
          p.order[p.n++] = (uint8_t)p.cursor;
        }
        break;
      }
    }
  }
  for (int k = 0; k < p.n; k++) order[k] = p.order[k];
  return p.n;
}

/* ------------------------------------------------------------------------- équipe, attaques, objets */

static void team_detail(const void *ctx, int i, int x, int y, int w, int h) {
  const Pokemon *p = &((const Team *)ctx)->mon[i];
  (void)h;
  ui_pokemon((uint16_t)p->species, p->shiny, false, x + (w - 64) / 2, y - 4);
  ui_types(x, y + 60, (uint16_t)p->species);
  int ix = small(x, y + 73, L("Objet ", "Item "), INK_DARK);
  small(ix, y + 73, p->item ? item_names[p->item] : L("aucun", "none"), INK_BLUE);
  for (int k = 0; k < MOVES_MAX; k++) move_line(x, y + 85 + k * 10, pokemon_move(p, (uint8_t)k));
}

static void team_row(const void *ctx, int i, int x, int y, int w) {
  const Pokemon *p = &((const Team *)ctx)->mon[i];
  (void)w;
  int nx = gfx_text(&font_normal, x, y + 2, species_names[p->species], INK_DARK, INK_DARK_SHADOW);
  if (p->shiny) ui_shiny_star(nx + 2, y + 8);
}

int screen_team(const char *title, const Team *team, bool *quit) {
  uint16_t icons[TEAM_MAX];
  for (int i = 0; i < team->count; i++) icons[i] = team->mon[i].species;
  Menu m = {.title = title, .n = team->count, .icons = icons, .detail = team_detail, .row = team_row, .ctx = team,
            .hue = 1, .list_w = 110};
  return menu_run(&m, quit);
}

typedef struct {
  const uint16_t *moves;
} MoveList;

static void move_detail(const void *ctx, int i, int x, int y, int w, int h) {
  uint16_t mv = ((const MoveList *)ctx)->moves[i];
  const MoveInfo *m = &move_info[mv];
  char desc[MOVETEXT_BYTES], buf[8];
  (void)h;
  if (!mv) return;
  int bw = ui_type_badge(x, y, (uint8_t)m->type);
  int cat = move_is_status(m) ? 2 : m->special ? 1 : 0;
  gfx_image(&category_icon[cat], x + bw + 3 + category_icon[cat].x, y - 3 + category_icon[cat].y);
  const char *const cat_name[3] = {L("Physique", "Physical"), L("Spéciale", "Special"), L("Statut", "Status")};
  small(x, y + 14, cat_name[cat], INK_DARK);
  int tx = small(x, y + 26, L("Puissance ", "Power "), INK_DARK);
  small(tx, y + 26, m->power > 1 ? num(buf, m->power) : m->power ? "?" : "-", INK_DARK);
  tx = small(x, y + 36, L("Précision ", "Accuracy "), INK_DARK);
  small(tx, y + 36, m->accuracy ? num(buf, m->accuracy) : "-", INK_DARK);
  tx = small(x, y + 46, "PP ", INK_DARK);
  small(tx, y + 46, num(buf, m->pp), INK_DARK);
  if (move_priority(m)) small(x + 40, y + 46, move_priority(m) > 0 ? L("Prioritaire", "Priority") : L("Lente", "Slow"), INK_RED);
  move_description(mv, desc);
  ui_wrap(&font_small, x, y + 56, w, desc, INK_BLUE, INK_DARK_SHADOW);
}

static void move_row(const void *ctx, int i, int x, int y, int w) {
  uint16_t mv = ((const MoveList *)ctx)->moves[i];
  (void)w;
  if (mv) ui_type_dot(x - 2, y + 5, (uint8_t)move_info[mv].type);
  gfx_text(&font_normal, x + 5, y, mv ? move_names[mv] : "-", INK_DARK, INK_DARK_SHADOW);
}

int screen_moves(const char *title, const uint16_t *moves, int n, bool *quit) {
  MoveList l = {moves};
  Menu m = {.title = title, .n = n, .detail = move_detail, .row = move_row, .ctx = &l, .hue = 3, .list_w = 116};
  return menu_run(&m, quit);
}

typedef struct {
  const uint8_t *items;
} ItemList;

static void item_detail(const void *ctx, int i, int x, int y, int w, int h) {
  (void)h;
  ui_wrap(&font_small, x, y, w, item_description(((const ItemList *)ctx)->items[i]), INK_DARK, INK_DARK_SHADOW);
}

static void item_row(const void *ctx, int i, int x, int y, int w) {
  (void)w;
  gfx_text(&font_normal, x, y, item_names[((const ItemList *)ctx)->items[i]], INK_DARK, INK_DARK_SHADOW);
}

int screen_items(const char *title, const uint8_t *items, int n, bool *quit) {
  ItemList l = {items};
  Menu m = {.title = title, .n = n, .detail = item_detail, .row = item_row, .ctx = &l, .hue = 2, .list_w = 116};
  return menu_run(&m, quit);
}

/* ------------------------------------------------------------------------- fiche en combat */

typedef struct {
  Battle *b;
  int pos; /* la position montrée (0..3) */
} Summary;

static void draw_summary(const void *ctx) {
  const Summary *s = ctx;
  Battle *b = s->b;
  const Fighter *f = battle_active(b, s->pos);
  const Volatile *v = battle_vol(b, s->pos);
  bool foe = s->pos & 1;
  uint16_t sp = f->p.species;
  char buf[96];
  ui_background(foe ? 2 : 0);
  strcat(strcpy(buf, foe ? L("ADVERSAIRE : ", "FOE: ") : L("INFOS : ", "INFO: ")), species_names[sp]);
  ui_title(buf);
  /* gauche : sprite, types, statut, talent */
  ui_window(2, 18, 98, SCREEN_H - 20);
  ui_pokemon(sp, f->p.shiny, false, 19, 18);
  int tx = 8;
  ui_type_badge(tx, 84, v->type[0]);
  if (v->type[1] != v->type[0]) ui_type_badge(tx + ui_type_badge_width(v->type[0]) + 2, 84, v->type[1]);
  ui_status_badge(tx, 96, f->status);
  small(tx, 110, ability_names[f->ability], INK_BLUE);
  ui_wrap(&font_small, tx, 117, 86, ability_descs[f->ability], RGB565(96, 96, 96), INK_DARK_SHADOW);
  /* droite : PV, stats et modificateurs, objet, attaques */
  ui_window(102, 18, SCREEN_W - 104, SCREEN_H - 20);
  int x = 108, y = 24;
  ui_hpbar(x, y, f->hp, f->max_hp);
  if (!foe) {
    char hp[16];
    strcat(strcat(num(hp, f->hp), "/"), num(buf, f->max_hp));
    small(x + 70, y, hp, INK_DARK);
  } else {
    strcat(num(buf, (unsigned)(f->hp * 100 / f->max_hp)), " %");
    small(x + 70, y, buf, INK_DARK);
  }
  static const char *const labels[2][7] = {{"ATQ", "DÉF", "VIT", "A.SP", "D.SP", "PRÉC", "ESQ"},
                                          {"ATK", "DEF", "SPE", "SPA", "SPD", "ACC", "EVA"}};
  const char *const *label = labels[lang];
  for (int k = 0; k < 7; k++) {
    int ly = y + 10 + k * 9;
    small(x, ly, label[k], INK_DARK);
    if (k < 5 && !foe) small_num(x + 50, ly, f->stat[k + 1], INK_DARK);
    int st = v->stage[k];
    if (st) {
      buf[0] = st > 0 ? '+' : '-';
      num(buf + 1, (unsigned)(st > 0 ? st : -st));
      small(x + 56, ly, buf, st > 0 ? INK_RED : INK_BLUE);
    }
  }
  int iy = y + 76;
  if (!foe) {
    int ix = small(x, iy, L("Objet ", "Item "), INK_DARK);
    small(ix, iy, f->p.item ? item_names[f->p.item] : L("aucun", "none"), INK_BLUE);
    for (int k = 0; k < MOVES_MAX; k++) {
      uint16_t mv = pokemon_move(&f->p, (uint8_t)k);
      if (!mv) continue;
      int my = iy + 11 + k * 10;
      small(x, my, move_names[mv], INK_DARK);
      char pp[12];
      strcat(strcat(num(pp, f->pp[k]), "/"), num(buf, move_info[mv].pp));
      small(SCREEN_W - 8 - text_width(&font_small, pp), my, pp, f->pp[k] ? INK_DARK : INK_RED);
    }
  } else {
    small(x, iy, L("Gauche / Droite :", "Left / Right:"), RGB565(120, 120, 120));
    small(x, iy + 10, L("un autre POKéMON", "another POKéMON"), RGB565(120, 120, 120));
  }
  /* terrain : Résonances, Distorsion, pièges */
  const Side *sd = &b->side[POS_SIDE(s->pos)];
  static const char *const roman[4] = {"", " I", " II", " III"};
  int rx = x;
  for (int k = 0; k < NUM_TYPES; k++)
    if (sd->resonance[k]) {
      char r[24];
      strcat(strcpy(r, type_names[k]), roman[sd->resonance[k]]);
      rx = small(rx, SCREEN_H - 22, r, INK_GREEN) + 4;
    }
  buf[0] = 0;
  if (sd->stealth_rock) strcat(buf, L("Pierres ", "Rocks "));
  if (sd->spikes) strcat(buf, L("Picots ", "Spikes "));
  if (sd->toxic_spikes) strcat(buf, L("Pics Toxik ", "T.Spikes "));
  if (sd->tailwind) strcat(buf, L("Vent ", "Tailwind "));
  if (b->trick_room) strcat(buf, L("Distorsion ", "Trick Room "));
  const char *const terrain[5] = {"", L("Ch. Électrik", "Electric T."), L("Ch. Herbu", "Grassy T."),
                                 L("Ch. Brumeux", "Misty T."), L("Ch. Psychique", "Psychic T.")};
  strcat(buf, terrain[b->terrain]);
  if (buf[0]) small(x, SCREEN_H - 12, buf, RGB565(160, 96, 32));
}

void screen_summary(Battle *b, bool *quit) {
  Summary s = {b, SIDE_PLAYER};
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_summary, &s);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return (void)(*quit = true);
    if (k & (KEY_OK | KEY_BACK)) return;
    if (!(k & (KEY_LEFT | KEY_RIGHT))) continue;
    /* les Pokémon sur le terrain, l'un après l'autre (0, 1, 2, 3 en duo) */
    int n = battle_positions(b), p = s.pos;
    do p = (p + ((k & KEY_RIGHT) ? 1 : n - 1)) % n;
    while (p != s.pos && !battle_alive(b, p));
    s.pos = p;
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_summary, &s);
  }
}
