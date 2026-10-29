#include "platform/app.h"
#include <stdbool.h>
#include <stdint.h>

#if PLATFORM_DEVICE
#include <eadk.h>

/* Bornes de la RAM de l'app, données par le script d'édition de liens de l'installateur. */
extern char _data_section_start_ram[], _heap_end[];
static bool home_held;

void app_begin(void) {
  __asm__ volatile("svc 10" ::: "r0", "r1", "r2", "r3", "r12", "memory");
  home_held = true;
}

__attribute__((noinline)) int app_end(void) {
  /* Une touche encore enfoncée (OK sur « Quitter ») atteindrait l'écran d'accueil et relancerait
   * l'app : on attend qu'elle soit relâchée, deux secondes au plus. */
  for (int i = 0; i < 200 && eadk_keyboard_scan(); i++) eadk_timing_msleep(10);
  bool unlock = home_held;
  for (volatile uint32_t *p = (volatile uint32_t *)(void *)_data_section_start_ram;
       p < (volatile uint32_t *)(void *)_heap_end; p++)
    *p = 0;
  if (unlock) __asm__ volatile("svc 13" ::: "r0", "r1", "r2", "r3", "r12", "memory");
  return 0;
}
#else
void app_begin(void) {}
int app_end(void) { return 0; }
#endif
