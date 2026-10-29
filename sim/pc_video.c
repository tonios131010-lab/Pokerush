/* Version PC : la fenêtre (taille au choix, plein écran), l'image de l'écran logique agrandie (nette à
 * l'échelle entière ou étirée, filtres), le clavier et la manette XInput (vibrations), les noms des
 * touches affichés par le jeu, les paramètres du PC et leur fichier. */
#ifdef POKERUSH_PC
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "game/pokemon.h"
#include "gfx/display.h"
#include "gfx/menu.h"
#include "pc_host.h"
#include "platform/input.h"
#include "platform/pc.h"

#define LW SCREEN_W
#define LH SCREEN_H

/* ------------------------------------------------------------------------- paramètres */

typedef struct {
  int fullscreen; /* 0 fenêtre, 1 plein écran */
  int smooth;     /* 0 échelle entière (nette), 1 remplit l'écran */
  int scale;      /* fenêtre : ×3 à ×8 */
  int filter;     /* 0 aucun, 1 lignes, 2 grille LCD */
  int prompts;    /* noms des boutons : 0 auto, 1 clavier, 2 manette */
  int rumble;     /* vibrations de la manette */
  int music, sfx, cries; /* volumes, 0 à 100 */
} Config;

static Config cfg = {0, 0, 4, 0, 0, 1, 70, 80, 80};

void pc_save_path(char *out, size_t size, const char *name) {
  const char *base = getenv("APPDATA");
  char dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%s\\PokeRush", base ? base : ".");
  CreateDirectoryA(dir, NULL);
  snprintf(out, size, "%s\\%s", dir, name);
}

void pc_data_path(char *out, size_t size, const char *name) {
  char exe[MAX_PATH];
  DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
  while (n && exe[n - 1] != '\\' && exe[n - 1] != '/') n--;
  exe[n] = 0;
  snprintf(out, size, "%sdata\\%s", exe, name);
}

static void config_load(void) {
  char path[MAX_PATH], line[128];
  pc_save_path(path, sizeof path, "pokerush_pc.ini");
  FILE *f = fopen(path, "r");
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    int v;
#define READ(name) \
  if (sscanf(line, #name "=%d", &v) == 1) cfg.name = v;
    READ(fullscreen) READ(smooth) READ(scale) READ(filter) READ(prompts) READ(rumble) READ(music) READ(sfx) READ(cries)
#undef READ
  }
  fclose(f);
  if (cfg.scale < 3 || cfg.scale > 8) cfg.scale = 4;
}

static void config_save(void) {
  char path[MAX_PATH];
  pc_save_path(path, sizeof path, "pokerush_pc.ini");
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "fullscreen=%d\nsmooth=%d\nscale=%d\nfilter=%d\nprompts=%d\nrumble=%d\nmusic=%d\nsfx=%d\ncries=%d\n",
          cfg.fullscreen, cfg.smooth, cfg.scale, cfg.filter, cfg.prompts, cfg.rumble, cfg.music, cfg.sfx, cfg.cries);
  fclose(f);
}

/* ------------------------------------------------------------------------- fenêtre */

static bool active, closing;
static DWORD closing_since;
static HWND win;
static uint16_t logical[LW * LH]; /* la copie de l'écran logique du jeu */
static bool dirty = true;
static uint32_t *scaled;
static int scaled_k;
static RECT windowed; /* la fenêtre avant le plein écran */

static void client_size(int scale, int *w, int *h) {
  RECT r = {0, 0, LW * scale, LH * scale};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  *w = r.right - r.left, *h = r.bottom - r.top;
}

static void set_fullscreen(bool on) {
  if (!win) return;
  if (on) {
    GetWindowRect(win, &windowed);
    MONITORINFO mi = {0};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(win, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongA(win, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(win, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  } else {
    SetWindowLongA(win, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    if (windowed.right > windowed.left)
      SetWindowPos(win, NULL, windowed.left, windowed.top, windowed.right - windowed.left, windowed.bottom - windowed.top,
                   SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    else
      SetWindowPos(win, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  }
  dirty = true;
  InvalidateRect(win, NULL, TRUE);
}

static void resize_window(void) {
  if (!win || cfg.fullscreen) return;
  int w, h;
  client_size(cfg.scale, &w, &h);
  SetWindowPos(win, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
}

static inline uint32_t rgb(uint16_t c) {
  uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

static inline uint32_t darken(uint32_t c, int num, int den) {
  return ((c >> 16 & 255) * num / den) << 16 | ((c >> 8 & 255) * num / den) << 8 | (c & 255) * num / den;
}

/* L'écran logique agrandi k fois au plus proche voisin, avec le filtre choisi. */
static void upscale(int k) {
  if (k != scaled_k) {
    free(scaled);
    scaled = malloc((size_t)LW * k * LH * k * 4);
    scaled_k = k;
  }
  int W = LW * k;
  for (int y = 0; y < LH; y++)
    for (int x = 0; x < LW; x++) {
      uint32_t c = rgb(logical[y * LW + x]);
      for (int j = 0; j < k; j++) {
        uint32_t *row = scaled + (size_t)(y * k + j) * W + x * k;
        for (int i = 0; i < k; i++) {
          uint32_t v = c;
          if (k >= 3 && cfg.filter == 1 && j == k - 1) v = darken(c, 5, 8); /* lignes */
          if (k >= 3 && cfg.filter == 2 && (j == k - 1 || i == k - 1)) v = darken(c, 3, 4); /* grille LCD */
          row[i] = v;
        }
      }
    }
}

static void paint(HDC dc) {
  RECT r;
  GetClientRect(win, &r);
  int cw = r.right, ch = r.bottom;
  if (cw <= 0 || ch <= 0) return;
  double s = (double)cw / LW < (double)ch / LH ? (double)cw / LW : (double)ch / LH;
  int k, dw, dh;
  if (cfg.smooth) {
    dw = (int)(LW * s), dh = (int)(LH * s), k = (int)ceil(s);
  } else {
    k = (int)s;
    dw = LW * k, dh = LH * k;
  }
  if (k < 1) k = 1, dw = LW, dh = LH;
  upscale(k);
  int x0 = (cw - dw) / 2, y0 = (ch - dh) / 2;
  HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RECT bars[4] = {{0, 0, cw, y0}, {0, y0 + dh, cw, ch}, {0, y0, x0, y0 + dh}, {x0 + dw, y0, cw, y0 + dh}};
  for (int i = 0; i < 4; i++) FillRect(dc, &bars[i], black);
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof bmi);
  bmi.bmiHeader = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), LW * k, -LH * k, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
  bool exact = dw == LW * k && dh == LH * k;
  SetStretchBltMode(dc, exact ? COLORONCOLOR : HALFTONE);
  if (!exact) SetBrushOrgEx(dc, 0, 0, NULL);
  StretchDIBits(dc, x0, y0, dw, dh, 0, 0, LW * k, LH * k, scaled, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

/* ------------------------------------------------------------------------- clavier et manette */

static bool vk_down[256], sc_down[256]; /* touches virtuelles et codes de position */
static int last_device;                   /* 0 clavier, 1 manette : pour les noms des boutons */

/* manette XInput, chargée à la demande (xinput1_4.dll, ou une plus ancienne) */
typedef struct {
  WORD buttons;
  BYTE left_trigger, right_trigger;
  SHORT lx, ly, rx, ry;
} XPad;
typedef struct {
  DWORD packet;
  XPad pad;
} XState;
typedef struct {
  WORD left, right;
} XVibration;
static DWORD(WINAPI *xget)(DWORD, XState *);
static DWORD(WINAPI *xset)(DWORD, XVibration *);
static int pad = -1;
static DWORD pad_scan, rumble_until;
static WORD pad_prev;

static void xinput_load(void) {
  const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
  for (int i = 0; i < 3 && !xget; i++) {
    HMODULE m = LoadLibraryA(dlls[i]);
    if (!m) continue;
    xget = (DWORD(WINAPI *)(DWORD, XState *))(void *)GetProcAddress(m, "XInputGetState");
    xset = (DWORD(WINAPI *)(DWORD, XVibration *))(void *)GetProcAddress(m, "XInputSetState");
  }
}

enum { PAD_UP = 0x1, PAD_DOWN = 0x2, PAD_LEFT = 0x4, PAD_RIGHT = 0x8, PAD_START = 0x10, PAD_BACK = 0x20,
       PAD_LB = 0x100, PAD_RB = 0x200, PAD_A = 0x1000, PAD_B = 0x2000, PAD_X = 0x4000, PAD_Y = 0x8000 };

static WORD pad_buttons(void) {
  if (!xget) return 0;
  XState st;
  if (pad < 0) { /* chercher une manette, pas trop souvent (l'appel est lent sans manette) */
    if (GetTickCount() - pad_scan < 1500) return 0;
    pad_scan = GetTickCount();
    for (int i = 0; i < 4 && pad < 0; i++)
      if (xget((DWORD)i, &st) == ERROR_SUCCESS) pad = i;
    if (pad < 0) return 0;
  }
  if (xget((DWORD)pad, &st) != ERROR_SUCCESS) return pad = -1, 0;
  WORD b = st.pad.buttons;
  const int dead = 16000; /* le stick gauche vaut la croix */
  if (st.pad.lx < -dead) b |= PAD_LEFT;
  if (st.pad.lx > dead) b |= PAD_RIGHT;
  if (st.pad.ly > dead) b |= PAD_UP;
  if (st.pad.ly < -dead) b |= PAD_DOWN;
  if (st.pad.left_trigger > 128 || st.pad.right_trigger > 128) b |= PAD_RB;
  return b;
}

static bool sc(int code) { return sc_down[code]; }

uint64_t pc_keys(void) {
  if (closing) return 1ull << eadk_key_home; /* la croix de la fenêtre : on quitte proprement */
  if (GetForegroundWindow() != win) return 0;
  WORD p = pad_buttons();
  if (p & ~pad_prev) last_device = 1;
  pad_prev = p;
  uint64_t k = 0;
#define KEY(cond, key) \
  if (cond) k |= 1ull << (key);
  /* positions QWERTY (WASD, Z X C) : les mêmes touches sur un clavier AZERTY (ZQSD, W X C) */
  KEY(vk_down[VK_LEFT] || sc(0x1E) || (p & PAD_LEFT), eadk_key_left)
  KEY(vk_down[VK_RIGHT] || sc(0x20) || (p & PAD_RIGHT), eadk_key_right)
  KEY(vk_down[VK_UP] || sc(0x11) || (p & PAD_UP), eadk_key_up)
  KEY(vk_down[VK_DOWN] || sc(0x1F) || (p & PAD_DOWN), eadk_key_down)
  KEY(vk_down[VK_RETURN] || sc(0x2C) || (p & PAD_A), eadk_key_ok)
  KEY(vk_down[VK_ESCAPE] || vk_down[VK_BACK] || sc(0x2D) || (p & (PAD_B | PAD_BACK)), eadk_key_back)
  KEY(vk_down[VK_SHIFT] || sc(0x2E) || (p & (PAD_X | PAD_Y | PAD_LB | PAD_RB)), eadk_key_shift)
  KEY(vk_down[VK_SPACE] || (p & PAD_START), eadk_key_exe)
#undef KEY
  return k;
}

const char *pc_key_label(uint16_t key) {
  if (!active) return NULL;
  int device = cfg.prompts == 1 ? 0 : cfg.prompts == 2 ? 1 : last_device;
  if (device == 1) {
    switch (key) {
      case KEY_OK: return "A";
      case KEY_BACK: return "B";
      case KEY_ALT: return "Y";
      case KEY_EXE: return "START";
    }
  } else {
    switch (key) {
      case KEY_OK: return L("ENTRÉE", "ENTER");
      case KEY_BACK: return L("ÉCHAP", "ESC");
      case KEY_ALT: return L("MAJ", "SHIFT");
      case KEY_EXE: return L("ESPACE", "SPACE");
    }
  }
  return NULL;
}

void pc_rumble(uint8_t strength, uint16_t ms) {
  if (!active || !cfg.rumble || !xset || pad < 0) return;
  XVibration v = {(WORD)(strength * 257), (WORD)(strength * 200)};
  xset((DWORD)pad, &v);
  rumble_until = GetTickCount() + ms;
}

/* ------------------------------------------------------------------------- messages de Windows */

static LRESULT CALLBACK window_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      paint(dc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: dirty = true; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
      if (wp == VK_F11 || (wp == VK_RETURN && (lp & (1 << 29)))) { /* F11, Alt+Entrée : plein écran */
        if (!(lp & (1 << 30))) cfg.fullscreen ^= 1, set_fullscreen(cfg.fullscreen), config_save();
        return 0;
      }
      if (msg == WM_SYSKEYDOWN && wp == VK_F4) break; /* Alt+F4 */
      vk_down[wp & 255] = true, sc_down[(lp >> 16) & 255] = true, last_device = 0;
      return 0;
    case WM_SYSKEYUP:
    case WM_KEYUP:
      vk_down[wp & 255] = false, sc_down[(lp >> 16) & 255] = false;
      return 0;
    case WM_SYSCHAR: return 0; /* pas de bip pour Alt+touche */
    case WM_KILLFOCUS: memset(vk_down, 0, sizeof vk_down), memset(sc_down, 0, sizeof sc_down); return 0;
    case WM_SETCURSOR:
      if (cfg.fullscreen && LOWORD(lp) == HTCLIENT) return SetCursor(NULL), TRUE;
      break;
    case WM_GETMINMAXINFO: {
      MINMAXINFO *mm = (MINMAXINFO *)lp;
      int w, hh;
      client_size(1, &w, &hh);
      mm->ptMinTrackSize.x = w, mm->ptMinTrackSize.y = hh;
      return 0;
    }
    case WM_CLOSE:
      closing = true, closing_since = GetTickCount();
      return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

bool pc_open(void) {
  if (active) return true;
  if (getenv("SIM_SCRIPT")) return false; /* tests et captures : pas de fenêtre */
  active = true;
  config_load();
  timeBeginPeriod(1);
  SetProcessDPIAware();
  xinput_load();
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
  wc.lpszClassName = "PokeRush";
  RegisterClassA(&wc);
  int w, h;
  /* la fenêtre ne dépasse pas l'écran */
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  while (cfg.scale > 3 && (LW * cfg.scale > sw - 40 || LH * cfg.scale > sh - 100)) cfg.scale--;
  client_size(cfg.scale, &w, &h);
  win = CreateWindowA("PokeRush", "PokeRush", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL,
                      wc.hInstance, NULL);
  if (cfg.fullscreen) set_fullscreen(true);
  pc_audio_init();
  pc_audio_volumes(cfg.music, cfg.sfx, cfg.cries);
  return true;
}

bool pc_present(const uint16_t *fb, int x, int y, int w, int h) {
  if (!pc_open()) return false;
  for (int j = y; j < y + h; j++) memcpy(logical + j * LW + x, fb + j * LW + x, (size_t)w * 2);
  dirty = true; /* affiché à la prochaine image (pc_pump) */
  return true;
}

void pc_pump(void) {
  if (!active) return;
  MSG m;
  while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageA(&m);
  }
  if (dirty && win) {
    HDC dc = GetDC(win);
    paint(dc);
    ReleaseDC(win, dc);
    dirty = false;
  }
  if (rumble_until && (int)(GetTickCount() - rumble_until) >= 0) {
    XVibration v = {0, 0};
    if (xset && pad >= 0) xset((DWORD)pad, &v);
    rumble_until = 0;
  }
  if (closing && GetTickCount() - closing_since > 3000) ExitProcess(0); /* le jeu n'a pas su quitter */
}

void pc_sleep(uint32_t ms) {
  static LARGE_INTEGER freq, next;
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq), QueryPerformanceCounter(&next);
  pc_pump();
  /* une cadence régulière : 16 ms par image, rattrapée si une image a pris du retard */
  next.QuadPart += freq.QuadPart * ms / 1000;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (now.QuadPart > next.QuadPart + freq.QuadPart / 10) next = now; /* trop en retard : on repart */
  while (now.QuadPart < next.QuadPart) {
    LONGLONG left = (next.QuadPart - now.QuadPart) * 1000 / freq.QuadPart;
    Sleep(left > 1 ? (DWORD)(left - 1) : 0);
    QueryPerformanceCounter(&now);
  }
}

bool pc_has_quit(void) { return pc_open(); }

/* ------------------------------------------------------------------------- paramètres du PC */

enum { PS_FULLSCREEN, PS_SMOOTH, PS_SCALE, PS_FILTER, PS_PROMPTS, PS_RUMBLE, PS_MUSIC, PS_SFX, PS_CRIES, PS_CONTROLS, PS_COUNT };

int pc_setting_count(void) { return pc_open() ? PS_COUNT : 0; }

static const char *percent(int v) {
  static char s[8];
  snprintf(s, sizeof s, "%d %%", v);
  return v ? s : L("COUPÉ", "OFF");
}

void pc_setting_line(int i, char *out, int size) {
  const char *yes = L("OUI", "ON"), *no = L("NON", "OFF");
  switch (i) {
    case PS_FULLSCREEN: snprintf(out, size, L("Plein écran : %s", "Fullscreen: %s"), cfg.fullscreen ? yes : no); break;
    case PS_SMOOTH:
      snprintf(out, size, L("Image : %s", "Picture: %s"),
               cfg.smooth ? L("ÉTIRÉE", "STRETCHED") : L("NETTE (×entier)", "SHARP (×integer)"));
      break;
    case PS_SCALE: snprintf(out, size, L("Taille de la fenêtre : ×%d", "Window size: ×%d"), cfg.scale); break;
    case PS_FILTER: {
      const char *f[3] = {L("AUCUN", "NONE"), L("LIGNES", "SCANLINES"), L("GRILLE LCD", "LCD GRID")};
      snprintf(out, size, L("Filtre : %s", "Filter: %s"), f[cfg.filter % 3]);
      break;
    }
    case PS_PROMPTS: {
      const char *p[3] = {"AUTO", L("CLAVIER", "KEYBOARD"), L("MANETTE", "GAMEPAD")};
      snprintf(out, size, L("Boutons affichés : %s", "Button prompts: %s"), p[cfg.prompts % 3]);
      break;
    }
    case PS_RUMBLE: snprintf(out, size, L("Vibrations : %s", "Rumble: %s"), cfg.rumble ? yes : no); break;
    case PS_MUSIC: snprintf(out, size, L("Musique : %s", "Music: %s"), percent(cfg.music)); break;
    case PS_SFX: snprintf(out, size, L("Bruitages : %s", "Sound effects: %s"), percent(cfg.sfx)); break;
    case PS_CRIES: snprintf(out, size, L("Cris des POKéMON : %s", "POKéMON cries: %s"), percent(cfg.cries)); break;
    case PS_CONTROLS: snprintf(out, size, "%s", L("Commandes du clavier et de la manette", "Keyboard and gamepad controls")); break;
  }
}

const char *pc_setting_help(int i) {
  switch (i) {
    case PS_FULLSCREEN: return L("PC : tout l'écran, ou une fenêtre. Aussi avec F11 ou Alt+Entrée, à tout moment.",
                                 "PC: full screen or a window. Also F11 or Alt+Enter, at any time.");
    case PS_SMOOTH: return L("NETTE : pixels parfaits, bandes noires autour. ÉTIRÉE : remplit l'écran, un peu lissée.",
                             "SHARP: perfect pixels, black bars around. STRETCHED: fills the screen, slightly smoothed.");
    case PS_SCALE: return L("La taille de la fenêtre, en multiple de l'écran de la GBA (214×160). Elle se redimensionne aussi à la souris.",
                            "Window size, as a multiple of the GBA-sized screen (214×160). You can also resize it with the mouse.");
    case PS_FILTER: return L("Un effet d'écran rétro : lignes d'un vieux téléviseur, ou grille d'un écran LCD.",
                             "A retro screen effect: old TV scanlines, or an LCD pixel grid.");
    case PS_PROMPTS: return L("Les touches nommées à l'écran. AUTO : celles du dernier appareil utilisé.",
                              "Which buttons the game shows. AUTO: those of the last device used.");
    case PS_RUMBLE: return L("La manette vibre quand vos POKéMON sont touchés, mis K.O. ou méga-évoluent.",
                             "The gamepad rumbles when your POKéMON are hit, faint or Mega Evolve.");
    case PS_MUSIC: return L("Les musiques d'Émeraude (combats, Arènes, Conseil 4, Maître, Hall of Fame…).",
                            "Emerald's music (battles, Gyms, Elite Four, Champion, Hall of Fame…).");
    case PS_SFX: return L("Menus, coups, Méga-Évolution, chromatiques.", "Menus, hits, Mega Evolution, shinies.");
    case PS_CRIES: return L("Le cri de chaque POKéMON quand il entre en combat ou tombe K.O.",
                            "Each POKéMON's cry when it enters battle or faints.");
    case PS_CONTROLS: return L("Voir toutes les touches.", "See all the controls.");
  }
  return "";
}

static int next_volume(int v) { return v >= 100 ? 0 : v + 10; }

void pc_setting_change(int i) {
  switch (i) {
    case PS_FULLSCREEN: cfg.fullscreen ^= 1, set_fullscreen(cfg.fullscreen); break;
    case PS_SMOOTH: cfg.smooth ^= 1; break;
    case PS_SCALE: cfg.scale = cfg.scale >= 8 ? 3 : cfg.scale + 1, resize_window(); break;
    case PS_FILTER: cfg.filter = (cfg.filter + 1) % 3; break;
    case PS_PROMPTS: cfg.prompts = (cfg.prompts + 1) % 3; break;
    case PS_RUMBLE:
      cfg.rumble ^= 1;
      if (cfg.rumble) pc_rumble(180, 250);
      break;
    case PS_MUSIC: cfg.music = next_volume(cfg.music); break;
    case PS_SFX: cfg.sfx = next_volume(cfg.sfx); break;
    case PS_CRIES: cfg.cries = next_volume(cfg.cries); break;
    case PS_CONTROLS: {
      bool quit = false;
      menu_message(L("Flèches ou ZQSD : bouger. ENTRÉE : valider. ÉCHAP : retour. MAJ : Méga, menu. ESPACE : combat "
                     "suivant. F11 : plein écran.",
                     "Arrows or WASD: move. ENTER: confirm. ESC: back. SHIFT: Mega, menu. SPACE: next battle. F11: "
                     "full screen."),
                   &quit);
      menu_message(L("Manette : croix ou stick pour bouger, A valider, B retour, X / Y / gâchettes : Méga et menu, "
                     "START : combat suivant.",
                     "Gamepad: D-pad or stick to move, A confirm, B back, X / Y / triggers: Mega and menu, START: next "
                     "battle."),
                   &quit);
      break;
    }
  }
  pc_audio_volumes(cfg.music, cfg.sfx, cfg.cries);
  if (i == PS_SFX) pc_sfx(SFX_SELECT);
  if (i == PS_CRIES) pc_cry(25); /* un cri pour essayer le volume */
  dirty = true;
  config_save();
}
#endif
