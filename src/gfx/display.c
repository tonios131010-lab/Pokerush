#include "gfx/display.h"
#include <eadk.h>

#define LCD_W 320
#define LCD_H 240
#define PAIRS 8 /* paires de lignes logiques par envoi : 24 lignes de l'écran */

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint16_t out[LCD_W * PAIRS * 3];
static int cx0, cy0, cx1, cy1; /* le rectangle en cours de dessin */

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline uint16_t mix(uint16_t a, uint16_t b) { return (uint16_t)(((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1)); }

/* Une ligne logique [x0, x1) (x0 pair) -> ses W pixels d'écran : a, mélange(a, b), b. */
static void expand(const uint16_t *src, uint16_t *dst, int x0, int x1, int W) {
  int n = 0;
  for (int x = x0; x < x1 && n < W; x += 2) {
    uint16_t a = src[x], b = x + 1 < SCREEN_W ? src[x + 1] : a;
    dst[n++] = a;
    if (n < W) dst[n++] = mix(a, b);
    if (n < W) dst[n++] = b;
  }
}

static void present(int x0, int y0, int x1, int y1) {
  int X0 = x0 / 2 * 3, W = imin(x1 / 2 * 3, LCD_W) - X0;
  for (int y = y0; y < y1; y += 2 * PAIRS) {
    int pairs = imin(PAIRS, (y1 - y) / 2);
    for (int p = 0; p < pairs; p++) {
      uint16_t *a = out + 3 * p * W, *b = a + W, *c = b + W;
      expand(fb + (y + 2 * p) * SCREEN_W, a, x0, x1, W);
      expand(fb + (y + 2 * p + 1) * SCREEN_W, c, x0, x1, W);
      for (int i = 0; i < W; i++) b[i] = mix(a[i], c[i]);
    }
    eadk_display_push_rect((eadk_rect_t){(uint16_t)X0, (uint16_t)(y / 2 * 3), (uint16_t)W, (uint16_t)(pairs * 3)}, out);
  }
}

void gfx_render(int x, int y, int w, int h, draw_fn draw, const void *ctx) {
  /* aligné sur les paires de pixels : 2 pixels logiques font 3 pixels d'écran */
  int x0 = imax(x, 0) & ~1, y0 = imax(y, 0) & ~1;
  int x1 = (imin(x + w, SCREEN_W) + 1) & ~1, y1 = (imin(y + h, SCREEN_H) + 1) & ~1;
  if (x0 >= x1 || y0 >= y1) return;
  cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
  draw(ctx);
  present(x0, y0, x1, y1);
}

static int saved_clip[4];

void gfx_clip_push(int x, int y, int w, int h) {
  saved_clip[0] = cx0, saved_clip[1] = cy0, saved_clip[2] = cx1, saved_clip[3] = cy1;
  cx0 = imax(cx0, x), cy0 = imax(cy0, y), cx1 = imin(cx1, x + w), cy1 = imin(cy1, y + h);
  if (cx1 < cx0) cx1 = cx0;
  if (cy1 < cy0) cy1 = cy0;
}

void gfx_clip_pop(void) { cx0 = saved_clip[0], cy0 = saved_clip[1], cx1 = saved_clip[2], cy1 = saved_clip[3]; }

void gfx_fill(int x, int y, int w, int h, uint16_t color) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + w, cx1), y1 = imin(y + h, cy1);
  for (int j = y0; j < y1; j++) {
    uint16_t *d = fb + j * SCREEN_W + x0;
    for (int i = x0; i < x1; i++) *d++ = color;
  }
}

void gfx_dim(int x, int y, int w, int h) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + w, cx1), y1 = imin(y + h, cy1);
  for (int j = y0; j < y1; j++) {
    uint16_t *d = fb + j * SCREEN_W + x0;
    for (int i = x0; i < x1; i++, d++) *d = (uint16_t)((*d & 0xF7DE) >> 1);
  }
}

void gfx_tint(int x, int y, int w, int h, uint16_t color, int amount) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + w, cx1), y1 = imin(y + h, cy1);
  for (int j = y0; j < y1; j++) {
    uint16_t *d = fb + j * SCREEN_W + x0;
    for (int i = x0; i < x1; i++, d++) {
      uint16_t half = mix(*d, color);
      *d = amount <= 0 ? *d : amount == 1 ? mix(*d, half) : amount == 2 ? half : amount == 3 ? mix(half, color) : color;
    }
  }
}

/* Indice du pixel u d'une ligne d'image. */
static inline uint8_t texel(const uint8_t *row, int bpp8, int u) {
  return bpp8 ? row[u] : (uint8_t)((row[u >> 1] >> ((u & 1) << 2)) & 15);
}

/* Dessine le rectangle (x0, y0)-(x1, y1) de l'écran, déjà découpé, avec la ligne j et la colonne i
 * de l'image prises modulo ses dimensions à partir de l'origine (ox, oy). flat : une seule couleur. */
/* ------------------------------------------------------------------------- deflate (RFC 1951)
 * Décodeur canonique minimal (à la puff.c de zlib) pour les sprites compressés par
 * tools/convert_assets.py. Les références arrière peuvent remonter avant d : la 2e image d'une
 * animation est décodée juste après la 1re, qui lui sert de dictionnaire. */

typedef struct {
  uint16_t count[16];  /* nombre de codes de chaque longueur */
  uint16_t symbol[288]; /* symboles rangés par code */
} Tree;

static struct {
  const uint8_t *s;
  unsigned bits, nbits;
  Tree lit, dist;
} inf;

static unsigned bits(unsigned n) {
  while (inf.nbits < n) inf.bits |= (unsigned)*inf.s++ << inf.nbits, inf.nbits += 8;
  unsigned v = inf.bits & ((1u << n) - 1);
  inf.bits >>= n, inf.nbits -= n;
  return v;
}

static void build(Tree *t, const uint8_t *len, int n) {
  uint16_t offs[16];
  for (int i = 0; i < 16; i++) t->count[i] = 0;
  for (int i = 0; i < n; i++) t->count[len[i]]++;
  t->count[0] = 0;
  for (int i = 0, sum = 0; i < 16; i++) offs[i] = (uint16_t)sum, sum += t->count[i];
  for (int i = 0; i < n; i++)
    if (len[i]) t->symbol[offs[len[i]]++] = (uint16_t)i;
}

static int decode(const Tree *t) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= (int)bits(1);
    int count = t->count[len];
    if (code - first < count) return t->symbol[index + code - first];
    index += count, first = (first + count) << 1, code <<= 1;
  }
  return -1;
}

static void inflate(const uint8_t *src, uint8_t *d, const uint8_t *end) {
  static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  uint8_t len[320];
  inf.s = src, inf.bits = inf.nbits = 0;
  for (int last = 0; !last && d < end;) {
    last = (int)bits(1);
    unsigned type = bits(2);
    if (type == 0) { /* bloc stocké */
      inf.bits = inf.nbits = 0;
      unsigned n = inf.s[0] | inf.s[1] << 8;
      inf.s += 4;
      while (n-- && d < end) *d++ = *inf.s++;
      continue;
    }
    if (type == 1) { /* codes fixes */
      int i = 0;
      for (; i < 144; i++) len[i] = 8;
      for (; i < 256; i++) len[i] = 9;
      for (; i < 280; i++) len[i] = 7;
      for (; i < 288; i++) len[i] = 8;
      build(&inf.lit, len, 288);
      for (i = 0; i < 30; i++) len[i] = 5;
      build(&inf.dist, len, 30);
    } else { /* codes dynamiques */
      int nlen = (int)bits(5) + 257, ndist = (int)bits(5) + 1, ncode = (int)bits(4) + 4;
      for (int i = 0; i < 19; i++) len[order[i]] = i < ncode ? (uint8_t)bits(3) : 0;
      build(&inf.lit, len, 19);
      for (int i = 0; i < nlen + ndist;) {
        int sym = decode(&inf.lit), rep = 0;
        uint8_t v = 0;
        if (sym < 16) {
          len[i++] = (uint8_t)sym;
          continue;
        }
        if (sym == 16) v = len[i - 1], rep = 3 + (int)bits(2);
        else if (sym == 17) rep = 3 + (int)bits(3);
        else rep = 11 + (int)bits(7);
        while (rep--) len[i++] = v;
      }
      build(&inf.lit, len, nlen);
      build(&inf.dist, len + nlen, ndist);
    }
    for (;;) {
      int sym = decode(&inf.lit);
      if (sym < 256) {
        if (d < end) *d++ = (uint8_t)sym;
        continue;
      }
      if (sym == 256) break;
      sym -= 257;
      unsigned n = lbase[sym] + bits(lext[sym]);
      int ds = decode(&inf.dist);
      const uint8_t *p = d - (dbase[ds] + bits(dext[ds]));
      while (n-- && d < end) *d++ = *p++;
    }
  }
}

/* Les pixels de l'image ; une image compressée est décodée dans l'un des 3 tampons de 2 Ko, gardés
 * tant qu'on redessine les mêmes (un combat : les deux Pokémon et le dresseur). */
static const uint8_t *pixels(const Image *im) {
  static uint8_t buf[3][32 * 64];
  static const Image *cached[3];
  static uint8_t next;
  if (!im->z) return image_data(im);
  for (int k = 0; k < 3; k++)
    if (cached[k] == im) return buf[k];
  int k = next;
  next = (uint8_t)((next + 1) % 3);
  inflate(image_data(im), buf[k], buf[k] + image_stride(im) * im->h);
  cached[k] = im;
  return buf[k];
}

static void blit(const Image *im, const uint8_t *data, const uint16_t *pal, int flat, uint16_t color, int ox, int oy, int x0,
                 int y0, int x1, int y1) {
  int w = im->w, h = im->h, bpp8 = im->bpp8, key = im->key, stride = image_stride(im);
  for (int j = y0; j < y1; j++) {
    const uint8_t *row = data + ((j - oy) % h) * stride;
    uint16_t *d = fb + j * SCREEN_W + x0;
    for (int i = x0, u = (x0 - ox) % w; i < x1; i++, d++) {
      uint8_t v = texel(row, bpp8, u);
      if (v || !key) *d = flat ? color : pal[v];
      if (++u == w) u = 0;
    }
  }
}

static void image_at(const Image *im, const uint16_t *pal, int flat, uint16_t color, int x, int y) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + im->w, cx1), y1 = imin(y + im->h, cy1);
  if (x0 < x1 && y0 < y1) blit(im, pixels(im), pal, flat, color, x, y, x0, y0, x1, y1);
}

void gfx_image(const Image *im, int x, int y) { image_at(im, image_palette(im), 0, 0, x, y); }
void gfx_image_pal(const Image *im, int x, int y, const uint16_t *pal) { image_at(im, pal, 0, 0, x, y); }
void gfx_image_flat(const Image *im, int x, int y, uint16_t color) { image_at(im, NULL, 1, color, x, y); }

void gfx_image_frame2(const Image *first, const Image *second, int x, int y, const uint16_t *pal) {
  static uint8_t pair[2 * 32 * 64]; /* la 1re image décodée, puis la 2e qui s'appuie sur elle */
  int n = image_stride(first) * first->h;
  if (!second->w || n > 32 * 64) {
    image_at(first, pal, 0, 0, x, y);
    return;
  }
  const uint8_t *a = pixels(first);
  for (int k = 0; k < n; k++) pair[k] = a[k];
  inflate(image_data(second), pair + n, pair + 2 * n);
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + first->w, cx1), y1 = imin(y + first->h, cy1);
  if (x0 < x1 && y0 < y1) blit(first, pair + n, pal, 0, 0, x, y, x0, y0, x1, y1);
}

void gfx_image_rows(const Image *im, int sy, int h, int x, int y) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + im->w, cx1), y1 = imin(y + h, cy1);
  if (x0 < x1 && y0 < y1) blit(im, pixels(im), image_palette(im), 0, 0, x, y - sy, x0, y0, x1, y1);
}

void gfx_image_tiled(const Image *im, int x, int y, int w, int h) {
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + w, cx1), y1 = imin(y + h, cy1);
  if (x0 < x1 && y0 < y1) blit(im, pixels(im), image_palette(im), 0, 0, x, y, x0, y0, x1, y1);
}

void gfx_nine(const Nine *n, int x, int y, int w, int h) {
  int c = n->c;
  if (y >= cy1 || y + h <= cy0) return;
  gfx_fill(x + c, y + c, w - 2 * c, h - 2 * c, n->fill);
  gfx_image_tiled(&n->edge[0], x + c, y, w - 2 * c, c);
  gfx_image_tiled(&n->edge[1], x + c, y + h - c, w - 2 * c, c);
  gfx_image_tiled(&n->edge[2], x, y + c, c, h - 2 * c);
  gfx_image_tiled(&n->edge[3], x + w - c, y + c, c, h - 2 * c);
  gfx_image(&n->corner[0], x, y);
  gfx_image(&n->corner[1], x + w - c, y);
  gfx_image(&n->corner[2], x, y + h - c);
  gfx_image(&n->corner[3], x + w - c, y + h - c);
}

void gfx_backdrop(const Backdrop *bd, int y, int foe_dx, int foe_dy, int player_dx) {
  for (int j = cy0; j < cy1; j++) {
    int r = j - y;
    if (r < 0) r = (BACKDROP_PERIOD - (-r % BACKDROP_PERIOD)) % BACKDROP_PERIOD;
    if (r >= BACKDROP_H) r = BACKDROP_H - 1;
    uint16_t color = bd->rows[r], *d = fb + j * SCREEN_W + cx0;
    for (int i = cx0; i < cx1; i++) *d++ = color;
  }
  gfx_image(&bd->foe, bd->foe.x + foe_dx, y + bd->foe.y + foe_dy);
  gfx_image(&bd->player, bd->player.x + player_dx, y + bd->player.y);
}

/* ------------------------------------------------------------------------- texte */

static uint16_t next_char(const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  if (p[0] < 0x80) {
    *s += 1;
    return p[0];
  }
  if ((p[0] & 0xE0) == 0xC0) {
    *s += 2;
    return (uint16_t)((p[0] & 0x1F) << 6 | (p[1] & 0x3F));
  }
  *s += 3;
  return (uint16_t)((p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F));
}

static const Glyph *find_glyph(const Font *f, uint16_t c) {
  int lo = 0, hi = f->count - 1;
  while (lo <= hi) {
    int m = (lo + hi) >> 1;
    uint16_t g = f->glyphs[m].cp;
    if (g == c) return &f->glyphs[m];
    if (g < c) lo = m + 1;
    else hi = m - 1;
  }
  return c == '?' ? &f->glyphs[0] : find_glyph(f, '?');
}

static void draw_glyph(const Font *f, const Glyph *g, int x, int y, const uint16_t ink[3]) {
  int w = g->w;
  int x0 = imax(x, cx0), y0 = imax(y, cy0), x1 = imin(x + w, cx1), y1 = imin(y + f->height, cy1);
  const uint8_t *bits = f->bits + g->ofs;
  for (int j = y0; j < y1; j++) {
    uint16_t *d = fb + j * SCREEN_W + x0;
    for (int i = x0; i < x1; i++, d++) {
      int k = (j - y) * w + (i - x);
      uint8_t v = (bits[k >> 2] >> ((k & 3) << 1)) & 3;
      if (v) *d = ink[v];
    }
  }
}

int gfx_text_n(const Font *f, int x, int y, const char *s, int n, uint16_t fg, uint16_t shadow) {
  const uint16_t ink[3] = {0, fg, shadow};
  int left = x;
  for (; *s && n != 0; n--) {
    uint16_t c = next_char(&s);
    if (c == '\n') {
      x = left, y += f->line;
    } else if (c == ' ') {
      x += f->space;
    } else {
      const Glyph *g = find_glyph(f, c);
      if (y < cy1 && y + f->height > cy0) draw_glyph(f, g, x, y, ink);
      x += g->w;
    }
  }
  return x;
}

int gfx_text_big(const Font *f, int x, int y, const char *s, uint16_t fg, uint16_t shadow) {
  while (*s) {
    uint16_t c = next_char(&s);
    if (c == ' ') {
      x += 2 * f->space;
      continue;
    }
    const Glyph *g = find_glyph(f, c);
    const uint8_t *bits = f->bits + g->ofs;
    for (int j = 0; j < f->height; j++)
      for (int i = 0; i < g->w; i++) {
        int k = j * g->w + i;
        uint8_t v = (bits[k >> 2] >> ((k & 3) << 1)) & 3;
        if (v) gfx_fill(x + 2 * i, y + 2 * j, 2, 2, v == 1 ? fg : shadow);
      }
    x += 2 * g->w;
  }
  return x;
}

int text_width(const Font *f, const char *s) {
  int x = 0, best = 0;
  while (*s) {
    uint16_t c = next_char(&s);
    if (c == '\n') x = 0;
    else x += c == ' ' ? f->space : find_glyph(f, c)->w;
    best = imax(best, x);
  }
  return best;
}

int text_length(const char *s) {
  int n = 0;
  while (*s) next_char(&s), n++;
  return n;
}

int gfx_number(const Font *f, int right, int y, unsigned v, uint16_t fg, uint16_t shadow) {
  char buf[8], *p = buf + sizeof buf;
  *--p = 0;
  do *--p = (char)('0' + v % 10); while (v /= 10);
  int x = right - text_width(f, p);
  gfx_text(f, x, y, p, fg, shadow);
  return x;
}
