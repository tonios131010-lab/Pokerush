/* L'EADK sur PC, pour voir le jeu sans calculatrice. L'écran est un tableau 320×240.
 *
 * Sans SIM_SCRIPT, sous Windows : une fenêtre (écran agrandi 2 fois) et le clavier du PC.
 *   flèches = flèches, Espace = OK, Entrée = EXE, Retour arrière = Retour, Échap = Maison (quitter),
 *   F12 = capture d'écran (capture_N.ppm dans SIM_OUT).
 *
 * Avec SIM_SCRIPT : pas de fenêtre, le clavier suit le script et des captures sont écrites (PPM).
 *   SIM_SCRIPT="_*5 shot:debut right ok _*40 shot:attaque home" build/sim/pokerush
 *
 * Chaque mot du script vaut pour une lecture du clavier : left up down right ok back home, ou _
 * (rien) ; "*N" le répète N fois ; "shot:nom" écrit SIM_OUT/nom.ppm (SIM_OUT vaut "." par défaut).
 * À la fin du script, la touche Maison est enfoncée puis relâchée en boucle pour quitter (le jeu ne
 * voit que les touches qui viennent d'être enfoncées : un appui pendant une animation serait perdu). */
#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t screen[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static uint64_t clock_ms, keys;
static const char *script;
static int repeat;
static uint64_t token_keys;

static bool dirty; /* l'écran a changé depuis le dernier affichage dans la fenêtre */

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *pixels) {
  for (int j = 0; j < r.height; j++)
    memcpy(&screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x], pixels + j * r.width, r.width * sizeof *pixels);
  dirty = true;
}

void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t color) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x + i] = color;
  dirty = true;
}

void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *pixels) {
  for (int j = 0; j < r.height; j++)
    memcpy(pixels + j * r.width, &screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x], r.width * sizeof *pixels);
}

bool eadk_display_wait_for_vblank(void) { return true; }
/* La police du système n'est pas imitée : le texte part dans la console. */
void eadk_display_draw_string(const char *text, eadk_point_t p, bool large, eadk_color_t fg, eadk_color_t bg) {
  if (*text) printf("[ecran %3d,%3d] %s\n", p.x, p.y, text);
}

/* SIM_OUT/<nom><ext> (SIM_OUT vaut "." par défaut). */
static void out_path(char *path, size_t size, const char *name, int len, const char *ext) {
  const char *dir = getenv("SIM_OUT");
  snprintf(path, size, "%s/%.*s%s", dir ? dir : ".", len, name, ext);
}

static void shot(const char *name, int len) {
  char path[512];
  out_path(path, sizeof path, name, len, ".ppm");
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT);
  for (int i = 0; i < EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT; i++) {
    uint16_t c = screen[i];
    unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                            (unsigned char)((c & 31) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

/* Le mot suivant du script : ses touches et son nombre de répétitions. */
static void next_token(void) {
  static const struct { const char *name; eadk_key_t key; } names[] = {
      {"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
      {"ok", eadk_key_ok}, {"back", eadk_key_back}, {"home", eadk_key_home}, {"shift", eadk_key_shift},
      {"exe", eadk_key_exe}};
  for (;;) {
    while (*script == ' ') script++;
    if (!*script) {
      token_keys = token_keys == 1ull << eadk_key_home ? 0 : 1ull << eadk_key_home, repeat = 1;
      return;
    }
    const char *end = script + strcspn(script, " ");
    if (!strncmp(script, "shot:", 5)) {
      shot(script + 5, (int)(end - script - 5));
      script = end;
      continue;
    }
    size_t len = strcspn(script, "* ");
    token_keys = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
      if (strlen(names[i].name) == len && !strncmp(script, names[i].name, len)) token_keys = 1ull << names[i].key;
    repeat = script[len] == '*' ? atoi(script + len + 1) : 1;
    script = end;
    return;
  }
}

#ifdef _WIN32
/* ------------------------------------------------------------------------- fenêtre Windows */
#include <windows.h>

static HWND window;
static int shots, scale = 2; /* agrandissement entier, au plus proche voisin : pixels nets */

static LRESULT CALLBACK window_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } bmi = {{sizeof(BITMAPINFOHEADER), EADK_SCREEN_WIDTH,
      -EADK_SCREEN_HEIGHT, 1, 16, BI_BITFIELDS, 0, 0, 0, 0, 0}, {0xF800, 0x07E0, 0x001F}};
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, EADK_SCREEN_WIDTH * scale, EADK_SCREEN_HEIGHT * scale, 0, 0, EADK_SCREEN_WIDTH,
                  EADK_SCREEN_HEIGHT, screen, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
    EndPaint(h, &ps);
    return 0;
  }
  if (msg == WM_KEYDOWN && wp == VK_F12) {
    char name[32];
    int n = snprintf(name, sizeof name, "capture_%d", ++shots);
    shot(name, n);
  }
  if (msg == WM_DESTROY) exit(0);
  return DefWindowProcA(h, msg, wp, lp);
}

static bool interactive(void) {
  if (window) return true;
  if (getenv("SIM_SCRIPT")) return false;
  /* Sans cela, Windows agrandit lui-même la fenêtre sur un écran zoomé, et l'image est floue. */
  SetProcessDPIAware();
  HDC sdc = GetDC(NULL);
  int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
  ReleaseDC(NULL, sdc);
  scale = (2 * dpi + 48) / 96;
  if (scale < 2) scale = 2;
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "PokeRush";
  RegisterClassA(&wc);
  RECT r = {0, 0, EADK_SCREEN_WIDTH * scale, EADK_SCREEN_HEIGHT * scale};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  window = CreateWindowA("PokeRush", "PokeRush (version PC)", style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
  return window != NULL;
}

/* Traite les messages de la fenêtre et affiche l'écran s'il a changé. */
static void pump(void) {
  MSG m;
  while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageA(&m);
  }
  if (dirty) {
    InvalidateRect(window, NULL, FALSE);
    UpdateWindow(window);
    dirty = false;
  }
}

static uint64_t pc_keys(void) {
  if (GetForegroundWindow() != window) return 0;
  static const struct { int vk; eadk_key_t key; } map[] = {
      {VK_LEFT, eadk_key_left}, {VK_UP, eadk_key_up}, {VK_DOWN, eadk_key_down}, {VK_RIGHT, eadk_key_right},
      {VK_RETURN, eadk_key_exe}, {VK_SPACE, eadk_key_ok}, {VK_BACK, eadk_key_back}, {VK_ESCAPE, eadk_key_home},
      {VK_SHIFT, eadk_key_shift}, {'M', eadk_key_shift}};
  uint64_t k = 0;
  for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
    if (GetAsyncKeyState(map[i].vk) & 0x8000) k |= 1ull << map[i].key;
  return k;
}
#else
static bool interactive(void) { return false; }
static void pump(void) {}
static uint64_t pc_keys(void) { return 0; }
#endif

void _eadk_keyboard_scan_do_scan(void) {
  if (interactive()) {
    pump();
    keys = pc_keys();
    return;
  }
  if (!script) script = getenv("SIM_SCRIPT") ? getenv("SIM_SCRIPT") : "";
  if (repeat <= 0) next_token();
  repeat--;
  keys = token_keys;
}
uint32_t _eadk_keyboard_scan_low(void) { return (uint32_t)keys; }
uint32_t _eadk_keyboard_scan_high(void) { return (uint32_t)(keys >> 32); }

void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep(us / 1000); }
void eadk_timing_msleep(uint32_t ms) {
  clock_ms += ms;
#ifdef _WIN32
  if (interactive()) {
    pump();
    Sleep(ms);
  }
#endif
}
uint32_t _eadk_timing_millis_low(void) { return (uint32_t)clock_ms; }
uint32_t _eadk_timing_millis_high(void) { return (uint32_t)(clock_ms >> 32); }

/* ------------------------------------------------------------------------- fichiers d'Epsilon
 * Le même format que sur la calculatrice, dans un tampon gardé dans SIM_OUT/storage.bin. Un tampon
 * neuf contient déjà un script (exemple.py), pour que la sauvegarde ne soit pas le seul fichier.
 * Après chaque écriture, les scripts Python sont recopiés en clair dans SIM_OUT/, pour les lire. */
#define SIM_STORAGE 32768
static uint8_t storage[SIM_STORAGE];
static bool storage_loaded;

uint8_t *sim_storage_area(uint32_t *size) {
  if (!storage_loaded) {
    storage_loaded = true;
    char path[512];
    out_path(path, sizeof path, "storage", 7, ".bin");
    FILE *f = fopen(path, "rb");
    if (!f || fread(storage, 1, SIM_STORAGE, f) != SIM_STORAGE) {
      static const char example[] = "exemple.py\0\1print('Bonjour')\n";
      uint16_t n = (uint16_t)(2 + sizeof example);
      memset(storage, 0, SIM_STORAGE);
      storage[0] = (uint8_t)n, storage[1] = (uint8_t)(n >> 8);
      memcpy(storage + 2, example, sizeof example);
    }
    if (f) fclose(f);
  }
  *size = SIM_STORAGE;
  return storage;
}

void sim_storage_commit(void) {
  char path[512];
  out_path(path, sizeof path, "storage", 7, ".bin");
  FILE *f = fopen(path, "wb");
  if (f) fwrite(storage, 1, SIM_STORAGE, f), fclose(f);
  for (uint32_t p = 0; p + 2 <= SIM_STORAGE;) {
    uint16_t n = (uint16_t)(storage[p] | storage[p + 1] << 8);
    if (!n) break;
    const char *name = (const char *)storage + p + 2;
    size_t nlen = strlen(name);
    if (nlen > 3 && !strcmp(name + nlen - 3, ".py")) {
      const uint8_t *content = storage + p + 2 + nlen + 1;
      size_t clen = n - 2 - nlen - 1;
      out_path(path, sizeof path, name, (int)nlen, "");
      if ((f = fopen(path, "wb"))) {
        /* sans l'octet d'état du début ni le zéro de la fin */
        fwrite(content + 1, 1, clen > 2 ? clen - 2 : 0, f);
        fclose(f);
      }
    }
    p += n;
  }
}

uint32_t eadk_random(void) {
  static uint32_t s = 2463534242u;
  s ^= s << 13, s ^= s >> 17, s ^= s << 5;
  return s;
}

bool eadk_usb_is_plugged(void) { return true; }
void eadk_backlight_set_brightness(uint8_t b) {}
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 100; }
float eadk_battery_voltage(void) { return 4.0f; }
const char *eadk_external_data;
size_t eadk_external_data_size;
