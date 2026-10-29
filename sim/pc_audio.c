/* Version PC : le son. Les musiques d'Émeraude (fichiers MIDI de l'expansion, avec leurs boucles) jouées
 * par le synthétiseur General MIDI de Windows ; les cris des Pokémon (WAV de l'expansion) et des
 * bruitages calculés ici, mélangés dans un flux waveOut. Les fichiers sont dans data\ à côté de
 * l'exécutable (tools/build_pc.py les y copie) ; s'ils manquent, le jeu reste muet. */
#ifdef POKERUSH_PC
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_host.h"
#include "platform/pc.h"

static bool audio_on;
static int vol_music = 70, vol_sfx = 80, vol_cries = 80;

/* ------------------------------------------------------------------------- mélange (cris, bruitages) */

#define RATE 22050
#define BUF 1024 /* échantillons par tampon : 46 ms */
#define NBUF 4
#define VOICES 8

typedef struct {
  const int16_t *pcm;
  int len, pos;
  bool cry;
} Voice;

static HWAVEOUT wave_out;
static WAVEHDR headers[NBUF];
static int16_t buffers[NBUF][BUF];
static HANDLE wave_event;
static Voice voices[VOICES];
static CRITICAL_SECTION lock;

static void mix(int16_t *out) {
  static int32_t acc[BUF];
  memset(acc, 0, sizeof acc);
  EnterCriticalSection(&lock);
  for (int v = 0; v < VOICES; v++) {
    Voice *vo = &voices[v];
    if (!vo->pcm) continue;
    int vol = vo->cry ? vol_cries : vol_sfx;
    for (int i = 0; i < BUF && vo->pos < vo->len; i++) acc[i] += vo->pcm[vo->pos++] * vol / 100;
    if (vo->pos >= vo->len) vo->pcm = NULL;
  }
  LeaveCriticalSection(&lock);
  for (int i = 0; i < BUF; i++) out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
}

static DWORD WINAPI mixer_thread(LPVOID unused) {
  for (;;) {
    WaitForSingleObject(wave_event, 100);
    for (int b = 0; b < NBUF; b++)
      if (headers[b].dwFlags & WHDR_DONE) {
        mix(buffers[b]);
        headers[b].dwFlags &= ~(DWORD)WHDR_DONE;
        waveOutWrite(wave_out, &headers[b], sizeof headers[b]);
      }
  }
  return 0;
}

static void play(const int16_t *pcm, int len, bool cry) {
  if (!audio_on || !pcm || len <= 0) return;
  EnterCriticalSection(&lock);
  int slot = -1;
  for (int v = 0; v < VOICES; v++) {
    if (cry && voices[v].pcm && voices[v].cry) slot = v; /* un seul cri à la fois : le nouveau remplace */
    if (slot < 0 && !voices[v].pcm) slot = v;
  }
  if (slot < 0) slot = 0;
  voices[slot] = (Voice){pcm, len, 0, cry};
  LeaveCriticalSection(&lock);
}

/* ------------------------------------------------------------------------- bruitages calculés */

static int16_t *sfx_pcm[SFX_COUNT];
static int sfx_len[SFX_COUNT];
static uint32_t noise_state = 0x1234567;

/* Un son ajouté à buf à partir de l'échantillon at : carré ou bruit, fréquence f0 -> f1, enveloppe qui
 * décroît. */
static void tone(int16_t *buf, int at, int ms, double f0, double f1, double vol, bool noise) {
  int n = RATE * ms / 1000;
  double phase = 0;
  for (int i = 0; i < n; i++) {
    double t = (double)i / n, f = f0 + (f1 - f0) * t, env = (1 - t) * (1 - t);
    double s;
    if (noise) {
      noise_state ^= noise_state << 13, noise_state ^= noise_state >> 17, noise_state ^= noise_state << 5;
      s = (double)(int32_t)noise_state / 2147483648.0;
    } else {
      phase += f / RATE;
      s = phase - floor(phase) < 0.5 ? 1 : -1;
    }
    int v = buf[at + i] + (int)(s * env * vol * 32767);
    buf[at + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
}

static int16_t *sfx_new(int id, int ms) {
  sfx_len[id] = RATE * ms / 1000;
  return sfx_pcm[id] = calloc((size_t)sfx_len[id], sizeof(int16_t));
}

static void sfx_build(void) {
  int16_t *b;
  b = sfx_new(SFX_CURSOR, 30), tone(b, 0, 25, 1700, 1700, 0.18, false);
  b = sfx_new(SFX_SELECT, 100), tone(b, 0, 45, 1250, 1250, 0.2, false), tone(b, RATE * 45 / 1000, 50, 1900, 1900, 0.2, false);
  b = sfx_new(SFX_BACK, 100), tone(b, 0, 45, 950, 950, 0.2, false), tone(b, RATE * 45 / 1000, 50, 620, 620, 0.2, false);
  b = sfx_new(SFX_HIT, 160), tone(b, 0, 150, 0, 0, 0.45, true), tone(b, 0, 90, 180, 60, 0.35, false);
  b = sfx_new(SFX_HIT_SUPER, 300), tone(b, 0, 180, 0, 0, 0.55, true), tone(b, 0, 120, 220, 50, 0.45, false),
  tone(b, RATE * 110 / 1000, 180, 0, 0, 0.45, true);
  b = sfx_new(SFX_HIT_WEAK, 90), tone(b, 0, 80, 0, 0, 0.25, true);
  b = sfx_new(SFX_MEGA, 900);
  for (int k = 0; k < 6; k++) tone(b, RATE * k * 120 / 1000, 160, 400 + k * 220, 520 + k * 260, 0.16, false);
  tone(b, RATE * 600 / 1000, 290, 0, 0, 0.2, true);
  b = sfx_new(SFX_SHINY, 260);
  for (int k = 0; k < 3; k++) tone(b, RATE * k * 70 / 1000, 90, 2600 + k * 500, 2600 + k * 500, 0.12, false);
}

void pc_sfx(int id) {
  if (audio_on && id >= 0 && id < SFX_COUNT && vol_sfx) play(sfx_pcm[id], sfx_len[id], false);
}

/* ------------------------------------------------------------------------- cris */

#define MAX_SPECIES 1024
static char *cry_name[MAX_SPECIES];
static int16_t *cry_pcm[MAX_SPECIES];
static int cry_len[MAX_SPECIES];

static void cries_load_names(void) {
  char path[MAX_PATH], line[64];
  pc_data_path(path, sizeof path, "cries.txt");
  FILE *f = fopen(path, "r");
  if (!f) return;
  for (int i = 0; i < MAX_SPECIES && fgets(line, sizeof line, f); i++) {
    line[strcspn(line, "\r\n")] = 0;
    if (line[0] && strcmp(line, "-")) cry_name[i] = _strdup(line);
  }
  fclose(f);
}

/* Un WAV PCM 8 ou 16 bits mono, ramené à RATE par interpolation linéaire. */
static int16_t *load_wav(const char *path, int *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)size);
  if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) return fclose(f), free(d), NULL;
  fclose(f);
  int rate = 0, bits = 0, channels = 1;
  const uint8_t *data = NULL;
  uint32_t data_len = 0;
  for (long p = 12; p + 8 <= size;) {
    uint32_t n = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
    if (!memcmp(d + p, "fmt ", 4)) {
      channels = d[p + 10] | d[p + 11] << 8;
      rate = d[p + 12] | d[p + 13] << 8 | d[p + 14] << 16 | d[p + 15] << 24;
      bits = d[p + 22] | d[p + 23] << 8;
    } else if (!memcmp(d + p, "data", 4)) {
      data = d + p + 8, data_len = n;
      if (p + 8 + (long)n > size) data_len = (uint32_t)(size - p - 8);
    }
    p += 8 + n + (n & 1);
  }
  if (!data || !rate || (bits != 8 && bits != 16)) return free(d), NULL;
  int frames = (int)(data_len / (uint32_t)(bits / 8 * channels));
  int len = (int)((int64_t)frames * RATE / rate);
  int16_t *pcm = malloc((size_t)len * sizeof(int16_t));
  for (int i = 0; i < len; i++) {
    double src = (double)i * rate / RATE;
    int a = (int)src;
    double t = src - a;
    int b = a + 1 < frames ? a + 1 : a;
#define SAMPLE(k) (bits == 8 ? (data[(k) * channels] - 128) * 256 : (int16_t)(data[(k) * channels * 2] | data[(k) * channels * 2 + 1] << 8))
    pcm[i] = (int16_t)(SAMPLE(a) * (1 - t) + SAMPLE(b) * t);
#undef SAMPLE
  }
  free(d);
  *out_len = len;
  return pcm;
}

void pc_cry(uint16_t species) {
  if (!audio_on || !vol_cries || species >= MAX_SPECIES || !cry_name[species]) return;
  if (!cry_pcm[species]) {
    char path[MAX_PATH], name[96];
    snprintf(name, sizeof name, "cries\\%s.wav", cry_name[species]);
    pc_data_path(path, sizeof path, name);
    cry_pcm[species] = load_wav(path, &cry_len[species]);
  }
  play(cry_pcm[species], cry_len[species], true);
}

/* ------------------------------------------------------------------------- musique (MIDI) */

/* Chaque musique, et une autre si le fichier manque. */
static const char *const songs[MUSIC_COUNT][2] = {
    [MUSIC_TITLE] = {"mus_title"},
    [MUSIC_DRAFT] = {"mus_poke_center"},
    [MUSIC_TRAINER] = {"mus_vs_trainer"},
    [MUSIC_GYM] = {"mus_vs_gym_leader"},
    [MUSIC_RIVAL] = {"mus_vs_rival"},
    [MUSIC_ELITE] = {"mus_vs_elite_four"},
    [MUSIC_CHAMPION] = {"mus_vs_champion"},
    [MUSIC_LEGEND] = {"mus_vs_kyogre_groudon", "mus_vs_regi"},
    [MUSIC_KANTO_GYM] = {"mus_rg_vs_gym_leader", "mus_vs_gym_leader"},
    [MUSIC_VICTORY_TRAINER] = {"mus_victory_trainer"},
    [MUSIC_VICTORY_GYM] = {"mus_victory_gym_leader"},
    [MUSIC_VICTORY_LEAGUE] = {"mus_victory_league"},
    [MUSIC_HALL_OF_FAME] = {"mus_hall_of_fame"},
    [MUSIC_ENDING] = {"mus_credits"},
};

typedef struct {
  double us;       /* moment de l'événement */
  uint32_t tick;
  uint8_t kind;    /* 0 message, 1 début de boucle, 2 fin de boucle */
  uint8_t st, a, b;
} Event;

static Event *events;
static int nevents, loop_start, loop_end;
static char kinds[128];  /* instrument : 'D' batterie, 'N' bruit (GBA), '.' mélodique */
static int song_volume = 100;
static HMIDIOUT midi;
static volatile LONG want_song = MUSIC_NONE;
static int cur_song = -1;
static uint8_t ch_volume[16], ch_kind[16];

static uint32_t read_var(const uint8_t *d, long *p, long end) {
  uint32_t v = 0;
  while (*p < end) {
    uint8_t c = d[(*p)++];
    v = v << 7 | (c & 127);
    if (!(c & 128)) break;
  }
  return v;
}

static int event_cmp(const void *x, const void *y) {
  const Event *a = x, *b = y;
  if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
  return a->kind == 2 ? -1 : b->kind == 2 ? 1 : 0; /* la fin de boucle avant les notes du même instant */
}

typedef struct {
  uint32_t tick, tempo;
} Tempo;

static bool song_load(const char *name) {
  char path[MAX_PATH], file[96];
  free(events), events = NULL, nevents = 0;
  snprintf(file, sizeof file, "music\\%s.map", name);
  pc_data_path(path, sizeof path, file);
  FILE *f = fopen(path, "r");
  memset(kinds, '.', sizeof kinds), song_volume = 100;
  if (f) {
    char line[160];
    if (fgets(line, sizeof line, f)) sscanf(line, "volume=%d", &song_volume);
    if (fgets(line, sizeof line, f)) memcpy(kinds, line, strlen(line) >= 128 ? 128 : strlen(line));
    fclose(f);
  }
  snprintf(file, sizeof file, "music\\%s.mid", name);
  pc_data_path(path, sizeof path, file);
  if (!(f = fopen(path, "rb"))) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)size);
  if (!d || fread(d, 1, (size_t)size, f) != (size_t)size || size < 14 || memcmp(d, "MThd", 4)) return fclose(f), free(d), false;
  fclose(f);
  int ntracks = d[10] << 8 | d[11], division = d[12] << 8 | d[13];
  if (division <= 0 || division & 0x8000) division = 24;
  int cap = 4096;
  events = malloc(sizeof(Event) * (size_t)cap);
  Tempo tempos[256];
  int ntempo = 0;
  long p = 8 + (d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7]);
  for (int t = 0; t < ntracks && p + 8 <= size; t++) {
    long len = d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7], q = p + 8, end = p + 8 + len;
    if (end > size) end = size;
    uint32_t tick = 0;
    uint8_t status = 0;
    while (q < end) {
      tick += read_var(d, &q, end);
      if (d[q] & 0x80) status = d[q++];
      if (status == 0xFF) {
        uint8_t type = d[q++];
        uint32_t n = read_var(d, &q, end);
        if (type == 0x51 && n == 3 && ntempo < 256) tempos[ntempo++] = (Tempo){tick, (uint32_t)(d[q] << 16 | d[q + 1] << 8 | d[q + 2])};
        if ((type == 0x06 || type == 0x01) && n == 1 && (d[q] == '[' || d[q] == ']')) {
          if (nevents == cap) events = realloc(events, sizeof(Event) * (size_t)(cap *= 2));
          events[nevents++] = (Event){0, tick, (uint8_t)(d[q] == '[' ? 1 : 2), 0, 0, 0};
        }
        if (type == 0x2F) break;
        q += n;
      } else if (status == 0xF0 || status == 0xF7) {
        q += read_var(d, &q, end);
      } else {
        uint8_t kind = status & 0xF0, a = d[q++], b = 0;
        if (kind != 0xC0 && kind != 0xD0) b = d[q++];
        if (nevents == cap) events = realloc(events, sizeof(Event) * (size_t)(cap *= 2));
        events[nevents++] = (Event){0, tick, 0, status, a, b};
      }
    }
    p = end;
  }
  free(d);
  qsort(events, (size_t)nevents, sizeof(Event), event_cmp);
  /* les temps, d'après les changements de tempo */
  uint32_t tempo = 500000, last = 0;
  double us = 0;
  int ti = 0;
  for (int i = 0; i < nevents; i++) {
    while (ti < ntempo && tempos[ti].tick <= events[i].tick) {
      us += (double)(tempos[ti].tick - last) * tempo / division;
      last = tempos[ti].tick, tempo = tempos[ti].tempo, ti++;
    }
    events[i].us = us + (double)(events[i].tick - last) * tempo / division;
  }
  loop_start = 0, loop_end = nevents;
  for (int i = 0; i < nevents; i++) {
    if (events[i].kind == 1) loop_start = i;
    if (events[i].kind == 2) loop_end = i;
  }
  return true;
}

/* Un canal du MIDI d'origine -> celui du synthétiseur : la batterie et le bruit de la GBA vont sur le
 * canal 10 (percussions General MIDI). */
static int out_channel(int ch) { return ch_kind[ch] != '.' ? 9 : ch == 9 ? 15 : ch; }

static void midi_send(int st, int a, int b) { midiOutShortMsg(midi, (DWORD)(st | a << 8 | b << 16)); }

static void channel_volume(int ch) {
  int v = ch_volume[ch] * song_volume / 127 * vol_music / 100;
  midi_send(0xB0 | out_channel(ch), 7, v > 127 ? 127 : v);
}

static void all_off(void) {
  for (int c = 0; c < 16; c++) midi_send(0xB0 | c, 123, 0), midi_send(0xB0 | c, 120, 0);
}

static void song_reset(void) {
  for (int c = 0; c < 16; c++) {
    midi_send(0xB0 | c, 121, 0);
    ch_volume[c] = 100, ch_kind[c] = '.';
    channel_volume(c);
  }
}

static void midi_event(const Event *e) {
  int ch = e->st & 15, kind = e->st & 0xF0;
  switch (kind) {
    case 0xC0:
      ch_kind[ch] = (uint8_t)kinds[e->a & 127];
      if (ch_kind[ch] == '.') midi_send(0xC0 | out_channel(ch), e->a, 0);
      channel_volume(ch);
      break;
    case 0x80:
    case 0x90: {
      int note = e->a;
      if (ch_kind[ch] == 'N') note = note < 60 ? 42 : 46; /* bruit de la GBA : charleston */
      midi_send(kind | out_channel(ch), note, e->b);
      break;
    }
    case 0xB0:
      if (e->a == 7) ch_volume[ch] = e->b, channel_volume(ch);
      else if (e->a == 10 || e->a == 11 || e->a == 64) midi_send(0xB0 | out_channel(ch), e->a, e->b);
      else if (e->a == 1) midi_send(0xB0 | out_channel(ch), 1, e->b / 2);
      break;
    case 0xE0:
      if (ch_kind[ch] == '.') midi_send(0xE0 | out_channel(ch), e->a, e->b);
      break;
  }
}

static DWORD WINAPI music_thread(LPVOID unused) {
  LARGE_INTEGER freq, start, now;
  QueryPerformanceFrequency(&freq);
  int idx = 0;
  double offset = 0; /* temps du morceau au moment start */
  int applied_volume = vol_music;
  for (;;) {
    int want = (int)want_song;
    if (want != cur_song) {
      all_off();
      cur_song = want;
      nevents = 0;
      if (want > MUSIC_NONE && want < MUSIC_COUNT)
        for (int k = 0; k < 2 && songs[want][k] && !song_load(songs[want][k]); k++) {
        }
      song_reset();
      idx = 0, offset = 0;
      QueryPerformanceCounter(&start);
    }
    if (applied_volume != vol_music) {
      applied_volume = vol_music;
      for (int c = 0; c < 16; c++) channel_volume(c);
    }
    if (!nevents || !vol_music) {
      if (!vol_music && nevents) all_off();
      Sleep(20);
      QueryPerformanceCounter(&start), offset = idx < nevents ? events[idx].us : 0;
      continue;
    }
    QueryPerformanceCounter(&now);
    double t = offset + (double)(now.QuadPart - start.QuadPart) * 1e6 / freq.QuadPart;
    while (idx < nevents && events[idx].us <= t) {
      if (idx >= loop_end) break;
      if (events[idx].kind == 0) midi_event(&events[idx]);
      idx++;
    }
    if (idx >= loop_end) { /* la boucle : on revient au marqueur de début */
      double end_us = loop_end < nevents ? events[loop_end].us : events[nevents - 1].us;
      if (t >= end_us) {
        all_off();
        idx = loop_start;
        offset = events[loop_start].us + (t - end_us);
        QueryPerformanceCounter(&start);
      }
    }
    Sleep(1);
  }
  return 0;
}

void pc_music(int song) {
  if (audio_on) InterlockedExchange(&want_song, song);
}

/* ------------------------------------------------------------------------- démarrage */

static void audio_quit(void) {
  if (midi) all_off(), midiOutReset(midi), midiOutClose(midi);
  if (wave_out) waveOutReset(wave_out);
}

void pc_audio_init(void) {
  if (audio_on) return;
  InitializeCriticalSection(&lock);
  sfx_build();
  cries_load_names();
  WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0};
  wave_event = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (waveOutOpen(&wave_out, WAVE_MAPPER, &fmt, (DWORD_PTR)wave_event, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR) {
    for (int b = 0; b < NBUF; b++) {
      headers[b] = (WAVEHDR){(LPSTR)buffers[b], sizeof buffers[b], 0, 0, 0, 0, NULL, 0};
      waveOutPrepareHeader(wave_out, &headers[b], sizeof headers[b]);
      waveOutWrite(wave_out, &headers[b], sizeof headers[b]);
    }
    CreateThread(NULL, 0, mixer_thread, NULL, 0, NULL);
  }
  if (midiOutOpen(&midi, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
    HANDLE t = CreateThread(NULL, 0, music_thread, NULL, 0, NULL);
    if (t) SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL); /* un rythme régulier */
  }
  atexit(audio_quit);
  audio_on = true;
}

void pc_audio_volumes(int music, int sfx, int cries) { vol_music = music, vol_sfx = sfx, vol_cries = cries; }
#endif
