#include "platform/input.h"
#include <eadk.h>

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
  primed = true;
  held = now;
  return pressed;
}

uint16_t input_wait_frame(void) {
  eadk_timing_msleep(16);
  return input_poll();
}
