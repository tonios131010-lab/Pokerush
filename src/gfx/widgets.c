#include "gfx/widgets.h"
#include <string.h>
#include "game/battle.h"

/* Couleurs officielles des types, dans l'ordre TYPE_*. */
static const uint16_t type_color[NUM_TYPES] = {
    RGB565(168, 168, 120), RGB565(192, 48, 40),  RGB565(168, 144, 240), RGB565(160, 64, 160), RGB565(224, 192, 104),
    RGB565(184, 160, 56),  RGB565(168, 184, 32), RGB565(112, 88, 152),  RGB565(184, 184, 208), RGB565(104, 160, 144),
    RGB565(240, 128, 48),  RGB565(104, 144, 240), RGB565(120, 200, 80), RGB565(248, 208, 48),  RGB565(248, 88, 136),
    RGB565(152, 216, 216), RGB565(112, 56, 248), RGB565(112, 88, 72),   RGB565(238, 153, 172),
};
static const char *const type_short[2][NUM_TYPES] = {
    {"NORMAL", "COMBAT", "VOL", "POISON", "SOL", "ROCHE", "INSECT", "SPECTR", "ACIER", "???", "FEU", "EAU", "PLANTE",
     "ÉLECTR", "PSY", "GLACE", "DRAGON", "TÉNÈB", "FÉE"},
    {"NORMAL", "FIGHT", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST", "STEEL", "???", "FIRE", "WATER", "GRASS",
     "ELECTR", "PSYCHC", "ICE", "DRAGON", "DARK", "FAIRY"}};

static uint16_t darker(uint16_t c) {
  unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (uint16_t)((r * 5 / 8) << 11 | (g * 5 / 8) << 5 | (b * 5 / 8));
}

/* Pastille arrondie de couleur c avec un texte blanc. */
static int pill(int x, int y, const char *label, uint16_t c) {
  int w = text_width(&font_small, label) + 5;
  uint16_t edge = darker(c);
  gfx_fill(x + 1, y, w - 2, BADGE_H, edge);
  gfx_fill(x, y + 1, w, BADGE_H - 2, edge);
  gfx_fill(x + 1, y + 1, w - 2, BADGE_H - 2, c);
  gfx_text(&font_small, x + 3, y - 3, label, INK_LIGHT, edge);
  return w;
}

uint16_t ui_type_color(uint8_t type) { return type_color[type]; }

int ui_pill(int x, int y, const char *label, uint16_t color) { return pill(x, y, label, color); }

uint16_t ui_pale(uint16_t c) {
  unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (uint16_t)(((r + 62) / 3) << 11 | ((g + 126) / 3) << 5 | ((b + 62) / 3));
}

void ui_frame(int x, int y, int w, int h, uint16_t c) {
  gfx_fill(x, y, w, 1, c), gfx_fill(x, y + h - 1, w, 1, c);
  gfx_fill(x, y, 1, h, c), gfx_fill(x + w - 1, y, 1, h, c);
}

void ui_type_dot(int x, int y, uint8_t type) {
  uint16_t c = type_color[type], edge = darker(c);
  gfx_fill(x, y + 1, 5, 5, edge);
  gfx_fill(x + 1, y, 3, 7, edge);
  gfx_fill(x + 1, y + 1, 3, 5, c);
}

int ui_type_badge_width(uint8_t type) { return text_width(&font_small, type_short[lang][type]) + 5; }
int ui_type_badge(int x, int y, uint8_t type) { return pill(x, y, type_short[lang][type], type_color[type]); }

int ui_type_mini(int x, int y, uint8_t type) {
  static const char *const abbr[2][NUM_TYPES] = {
      {"NOR", "COM", "VOL", "POI", "SOL", "ROC", "INS", "SPE", "ACI", "???", "FEU", "EAU", "PLA", "ÉLE", "PSY", "GLA",
       "DRA", "TÉN", "FÉE"},
      {"NOR", "FIG", "FLY", "POI", "GRO", "ROC", "BUG", "GHO", "STE", "???", "FIR", "WAT", "GRA", "ELE", "PSY", "ICE",
       "DRA", "DAR", "FAI"}};
  return pill(x, y, abbr[lang][type], type_color[type]);
}

int ui_eff_pill(int x, int y, uint8_t eff) {
  static const char *const label[2][6] = {{"x0", "x0,25", "x0,5", "x1", "x2", "x4"}, {"x0", "x0.25", "x0.5", "x1", "x2", "x4"}};
  static const uint16_t color[6] = {RGB565(112, 112, 120), RGB565(184, 56, 40), RGB565(216, 104, 40),
                                    RGB565(120, 128, 152), RGB565(48, 160, 64), RGB565(24, 128, 48)};
  int k = !eff ? 0 : eff <= 4 ? 1 : eff <= 8 ? 2 : eff <= 16 ? 3 : eff <= 32 ? 4 : 5;
  return pill(x, y, label[lang][k], color[k]);
}

int ui_types(int x, int y, uint16_t species) {
  const SpeciesInfo *s = &species_info[species];
  int w = ui_type_badge(x, y, (uint8_t)s->type1);
  if (s->type2 != s->type1) w += 2 + ui_type_badge(x + w + 2, y, (uint8_t)s->type2);
  return w;
}

int ui_status_badge(int x, int y, uint8_t status) {
  static const char *const labels[2][7] = {{"", "SOM", "PSN", "BRÛ", "GEL", "PAR", "TOX"},
                                           {"", "SLP", "PSN", "BRN", "FRZ", "PAR", "TOX"}};
  const char *const *label = labels[lang];
  static const uint16_t color[] = {0,
                                   RGB565(140, 140, 140),
                                   RGB565(160, 64, 160),
                                   RGB565(232, 112, 48),
                                   RGB565(104, 184, 216),
                                   RGB565(200, 168, 24),
                                   RGB565(120, 48, 136)};
  if (!status || status > STATUS_TOXIC) return 0;
  return pill(x, y, label[status], color[status]);
}

void ui_hpbar(int x, int y, uint16_t hp, uint16_t max) {
  static const uint16_t colors[3][2] = {
      {RGB565(90, 213, 131), RGB565(115, 255, 172)}, /* plus de la moitié */
      {RGB565(205, 172, 8), RGB565(255, 230, 57)},   /* plus d'un cinquième */
      {RGB565(172, 65, 74), RGB565(255, 90, 57)},    /* moins */
  };
  gfx_image(&img_hpbar, x, y);
  int fill = max ? hp * HPBAR_FILL_W / max : 0;
  if (hp && !fill) fill = 1;
  int level = hp * 2 > max ? 0 : hp * 5 > max ? 1 : 2;
  gfx_fill(x + HPBAR_FILL_X, y + 2, fill, 1, colors[level][0]);
  gfx_fill(x + HPBAR_FILL_X, y + 3, fill, 1, colors[level][1]);
}

void ui_pokemon(uint16_t species, bool shiny, bool back, int x, int y) {
  const Image *im = back ? &pokemon_back[species] : &pokemon_front[species];
  const uint16_t *pal = &image_palettes[(im->pal + (shiny ? 1 : 0)) * 16];
  gfx_image_pal(im, x + im->x, y + im->y, pal);
}

void ui_pokemon_frame2(uint16_t species, bool shiny, int x, int y) {
  const Image *im = &pokemon_front[species];
  gfx_image_frame2(im, &pokemon_front2[species], x + im->x, y + im->y, &image_palettes[(im->pal + (shiny ? 1 : 0)) * 16]);
}

void ui_icon(uint16_t species, int x, int y) {
  const Image *im = &pokemon_icon[species];
  gfx_image(im, x + im->x, y + im->y);
}

void ui_window(int x, int y, int w, int h) { gfx_nine(&nine_window, x, y, w, h); }

uint8_t ui_theme, ui_bubble;
const char *const theme_names_fr[THEME_COUNT] = {"CLASSIQUE", "OCÉAN", "BRAISE", "FORÊT", "NUIT",
                                                 "OR",        "ROSE",  "CRISTAL", "TERRE CUITE"};
const char *const theme_names_en[THEME_COUNT] = {"CLASSIC", "OCEAN", "EMBER", "FOREST", "NIGHT",
                                                 "GOLD",    "PINK",  "CRYSTAL", "TERRACOTTA"};
static const uint16_t theme_tones[THEME_COUNT][3] = {
    {0, 0, RGB565(72, 80, 112)},
    {RGB565(144, 184, 232), RGB565(128, 168, 216), RGB565(40, 72, 144)},
    {RGB565(240, 176, 136), RGB565(224, 160, 120), RGB565(152, 56, 40)},
    {RGB565(160, 208, 144), RGB565(144, 192, 128), RGB565(48, 104, 56)},
    {RGB565(96, 96, 136), RGB565(80, 80, 120), RGB565(24, 24, 56)},
    {RGB565(240, 216, 128), RGB565(224, 200, 112), RGB565(136, 104, 24)},
    {RGB565(240, 184, 208), RGB565(224, 168, 192), RGB565(160, 64, 112)},
    {RGB565(200, 232, 240), RGB565(184, 216, 232), RGB565(64, 136, 160)},
    {RGB565(244, 214, 196), RGB565(234, 198, 178), RGB565(190, 96, 64)}, /* terre cuite (succès « Code secret ») */
};

void ui_dialog(int x, int y, int w, int h, uint16_t *ink, uint16_t *shadow) {
  if (ui_bubble) {
    ui_window(x, y, w, h);
    *ink = INK_DARK, *shadow = INK_DARK_SHADOW;
  } else {
    gfx_nine(&nine_msgbox, x, y, w, h);
    *ink = RGB565(248, 248, 248), *shadow = RGB565(104, 104, 120);
  }
}

void ui_background(int hue) {
  static const uint16_t tones[4][2] = {
      {RGB565(160, 208, 200), RGB565(144, 192, 184)}, /* vert d'eau */
      {RGB565(176, 192, 232), RGB565(160, 176, 216)}, /* bleu */
      {RGB565(232, 192, 160), RGB565(216, 176, 144)}, /* orangé */
      {RGB565(208, 184, 224), RGB565(192, 168, 208)}, /* mauve */
  };
  const uint16_t *t = ui_theme ? theme_tones[ui_theme] : tones[hue & 3];
  for (int y = 0; y < SCREEN_H; y++)
    for (int x0 = -((y / 2) % 16); x0 < SCREEN_W; x0 += 16) {
      gfx_fill(x0, y, 8, 1, t[0]);
      gfx_fill(x0 + 8, y, 8, 1, t[1]);
    }
}

static uint16_t lighter(uint16_t c) {
  unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (uint16_t)((r + (31 - r) / 3) << 11 | (g + (63 - g) / 3) << 5 | (b + (31 - b) / 3));
}

/* Petite Poké Ball de 9×9 (décor des bandeaux). */
void ui_ball(int x, int y) {
  static const char *const ball[9] = {"..#####..", ".#rrrrr#.", "#rrrrrrr#", "#rrr#rrr#", "####w####",
                                      "#www#www#", "#wwwwwww#", ".#wwwww#.", "..#####.."};
  for (int j = 0; j < 9; j++)
    for (int i = 0; i < 9; i++) {
      char c = ball[j][i];
      if (c != '.')
        gfx_fill(x + i, y + j, 1, 1, c == '#' ? RGB565(40, 40, 56) : c == 'r' ? RGB565(232, 72, 56) : RGB565(248, 248, 248));
    }
}

void ui_title(const char *title) {
  uint16_t c = theme_tones[ui_theme][2];
  gfx_fill(0, 0, SCREEN_W, 16, c);
  gfx_fill(0, 0, SCREEN_W, 1, lighter(c));
  gfx_fill(0, 15, SCREEN_W, 1, darker(c));
  gfx_fill(0, 16, SCREEN_W, 1, RGB565(232, 184, 64)); /* liseré doré */
  gfx_fill(0, 17, SCREEN_W, 1, RGB565(40, 40, 64));
  ui_ball(4, 4);
  gfx_text(&font_normal, 17, 0, title, INK_LIGHT, RGB565(40, 40, 64)); /* (les accents des majuscules : lignes 0 à 2) */
}

void ui_footer(const char *help) {
  int y = SCREEN_H - FOOTER_H;
  gfx_fill(0, y, SCREEN_W, FOOTER_H, RGB565(48, 52, 72));
  gfx_fill(0, y, SCREEN_W, 1, RGB565(232, 184, 64));
  gfx_text(&font_small, (SCREEN_W - text_width(&font_small, help)) / 2, y - 2, help, INK_LIGHT, RGB565(24, 24, 40));
}

int ui_wrap(const Font *f, int x, int y, int width, const char *s, uint16_t fg, uint16_t shadow) {
  return ui_wrap_lines(f, x, y, width, f->line, 99, s, fg, shadow);
}

static int wrap(const Font *f, int x, int y, int width, int step, int max, const char *s, uint16_t fg, uint16_t shadow,
                bool draw);

int ui_wrap_lines(const Font *f, int x, int y, int width, int step, int max, const char *s, uint16_t fg, uint16_t shadow) {
  return wrap(f, x, y, width, step, max, s, fg, shadow, true);
}

int ui_wrap_count(const Font *f, int width, const char *s) { return wrap(f, 0, 0, width, 0, 99, s, 0, 0, false); }

static int wrap(const Font *f, int x, int y, int width, int step, int max, const char *s, uint16_t fg, uint16_t shadow,
                bool draw) {
  char line[64];
  int lines = 0;
  while (*s) {
    /* le plus long début de s (coupé à une espace) qui tient dans la largeur */
    int n = 0, cut = 0;
    while (s[n] && s[n] != '\n' && n < (int)sizeof line - 1) {
      memcpy(line, s, (size_t)n + 1);
      line[n + 1] = 0;
      if (text_width(f, line) > width) break;
      n++;
      if (s[n] == ' ' || s[n] == 0 || s[n] == '\n') cut = n;
    }
    if (!cut) cut = n ? n : 1;
    memcpy(line, s, (size_t)cut);
    line[cut] = 0;
    if (lines == max - 1 && s[cut] && strlen(line) + 3 < sizeof line) { /* plus de place : « … » */
      while (cut > 0 && text_width(f, line) + text_width(f, "…") > width) line[--cut] = 0;
      strcat(line, "…");
      if (draw) gfx_text(f, x, y + lines * step, line, fg, shadow);
      return lines + 1;
    }
    if (draw) gfx_text(f, x, y + lines * step, line, fg, shadow);
    lines++;
    s += cut;
    while (*s == ' ' || *s == '\n') s++;
  }
  return lines;
}

void ui_shiny_star(int x, int y) {
  static const char *const star[7] = {"...#...", "...#...", "..###..", "#######", "..###..", ".#...#.", "#.....#"};
  for (int j = 0; j < 7; j++)
    for (int i = 0; i < 7; i++)
      if (star[j][i] == '#') gfx_fill(x + i, y + j, 1, 1, j == 3 ? RGB565(248, 200, 40) : RGB565(232, 160, 24));
}

void ui_gauge(int x, int y, int w, int value, int max, uint16_t color) {
  int fill = max ? value * w / max : 0;
  if (fill > w) fill = w;
  gfx_fill(x, y, w, 3, RGB565(200, 200, 192));
  gfx_fill(x, y, fill, 3, color);
}
