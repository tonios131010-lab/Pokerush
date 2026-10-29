#include "platform/input.h"
#include <eadk.h>
#include "game/pokemon.h"
#include "platform/pc.h"

#define ARROWS (KEY_LEFT | KEY_UP | KEY_DOWN | KEY_RIGHT)
#define REPEAT_DELAY 20 /* images avant qu'une flèche maintenue se répète */
#define REPEAT_EVERY 5

static uint16_t held;

uint16_t input_poll(void) {
  eadk_keyboard_state_t s = eadk_keyboard_scan();
  bool exe = eadk_keyboard_key_down(s, eadk_key_exe);
  uint16_t now = (uint16_t)(eadk_keyboard_key_down(s, eadk_key_left) * KEY_LEFT |
                            eadk_keyboard_key_down(s, eadk_key_up) * KEY_UP |
                            eadk_keyboard_key_down(s, eadk_key_down) * KEY_DOWN |
                            eadk_keyboard_key_down(s, eadk_key_right) * KEY_RIGHT |
                            (eadk_keyboard_key_down(s, eadk_key_ok) || exe) * KEY_OK | exe * KEY_EXE |
                            eadk_keyboard_key_down(s, eadk_key_back) * KEY_BACK |
                            eadk_keyboard_key_down(s, eadk_key_home) * KEY_HOME |
                            (eadk_keyboard_key_down(s, eadk_key_shift) || eadk_keyboard_key_down(s, eadk_key_alpha)) * KEY_ALT);
  /* Première lecture : les touches déjà enfoncées (OK ou EXE qui vient de lancer l'app) ne comptent
   * pas, sinon elles choisiraient tout de suite la première ligne du premier menu. */
  static bool primed;
  uint16_t pressed = primed ? now & (uint16_t)~held : 0;
  /* une flèche maintenue se répète, pour parcourir vite les listes */
  static int hold_frames;
  if ((now & ARROWS) && (now & ARROWS) == (held & ARROWS)) {
    if (++hold_frames >= REPEAT_DELAY && (hold_frames - REPEAT_DELAY) % REPEAT_EVERY == 0) pressed |= now & ARROWS;
  } else {
    hold_frames = 0;
  }
  primed = true;
  held = now;
  return pressed;
}

uint16_t input_wait_frame(void) {
  eadk_timing_msleep(16);
  return input_poll();
}

const char *key_label(uint16_t key) {
  const char *pc = pc_key_label(key);
  if (pc) return pc;
  switch (key) {
    case KEY_OK: return "OK";
    case KEY_BACK: return L("RETOUR", "BACK");
    case KEY_ALT: return "SHIFT";
    case KEY_EXE: return "EXE";
    default: return "?";
  }
}
