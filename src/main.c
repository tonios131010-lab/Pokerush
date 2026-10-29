/* PokeRush : point d'entrée. */
#include <eadk.h>
#include "game/play.h"
#include "platform/app.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "PokeRush";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

int main(void) {
  app_begin();
  play_run((uint32_t)eadk_timing_millis() ^ eadk_random());
  return app_end();
}
