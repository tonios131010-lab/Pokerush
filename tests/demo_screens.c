/* Version PC : montre directement les écrans hors combat et un combat avec Méga-Évolution, pour les
 * captures (SIM_SCRIPT) sans avoir à gagner les combats. Compilé à la place de src/main.c. */
#include <eadk.h>
#include <stdlib.h>
#include <string.h>
#include "core/rng.h"
#include "data.h"
#include "game/battle.h"
#include "game/fight.h"
#include "game/screens.h"
#include "game/trainer.h"
#include "game/meta.h"
#include "core/dex.h"
#include "platform/app.h"

const char eadk_app_name[] = "PokeRush";
const uint32_t eadk_api_level = 0;

int main(void) {
  app_begin();
  bool quit = false;
  uint32_t rng = 1234;
  Team team = {0};
  uint16_t sp[6] = {SPECIES_CHARIZARD, SPECIES_GARDEVOIR, SPECIES_GARCHOMP, SPECIES_LUCARIO, SPECIES_FROSMOTH, SPECIES_CERULEDGE};
  for (int i = 0; i < 6; i++) team.mon[team.count++] = trainer_build(sp[i], 0, 9, true, &rng);
  team.mon[0].item = species_stone(SPECIES_CHARIZARD);
  team.mon[1].shiny = 1;
  Pokemon offers[5];
  uint16_t offer_sp[5] = {SPECIES_RAYQUAZA, SPECIES_DRAGAPULT, SPECIES_GHOLDENGO, SPECIES_SALAMENCE, SPECIES_CLAUDE_CODE};
  for (int i = 0; i < 5; i++) offers[i] = trainer_build(offer_sp[i], 0, 7, false, &rng);
  offers[1].shiny = 1;
  int cursor = 1;
  const char *demo = getenv("DEMO");
  dex_load();
  if (demo && strchr(demo, 'E')) lang = 1; /* les écrans en anglais */
  if (demo && strchr(demo, 'r')) {
    static Trainer next;
    int cat = 0;
    trainer_make(&next, 21, &team, &rng);
    screen_reward(offers, 5, &team, &next, 20, &cat, &quit);
  }
  if (demo && strchr(demo, 'a')) screen_achievements(&quit);
  if (demo && strchr(demo, 's')) screen_settings(&quit);
  if (demo && strchr(demo, 'h')) {
    Run r = {0};
    RunStats st = {1234, 56, 7, 2, 1, 0xC0FFEE42};
    r.battle = 100, r.team = team;
    hall_of_fame(&r, &st, "HALL OF FAME", &quit);
  }
  if (demo && strchr(demo, 'e')) ending(&team, &quit);
  if (!demo || strchr(demo, 'd')) {
    static Trainer next;
    trainer_make(&next, 21, &team, &rng);
    screen_draft(offers, 5, &team, &next, &cursor, &quit);
  }
  if (!demo || strchr(demo, 't')) screen_team("Votre équipe", &team, &quit);
  if (!demo || strchr(demo, 'm')) {
    const SpeciesInfo *s = &species_info[SPECIES_GARCHOMP];
    uint16_t ids[128];
    for (int i = 0; i < s->learn_cnt; i++) ids[i] = species_learnset(s, (uint8_t)i);
    screen_moves("Apprendre quelle attaque ?", ids, s->learn_cnt, &quit);
  }
  if (demo && strchr(demo, 'v')) { /* duo : l'écran avant le combat, puis le combat à 2 contre 2 */
    static Battle b;
    static Trainer t;
    trainer_make(&t, 60, &team, &rng);
    uint8_t pick[TEAM_MAX];
    int n = screen_preview(&team, &t, pick, &quit);
    if (n) {
      Team mine = team, theirs;
      mine.count = (uint8_t)n;
      for (int k = 0; k < n; k++) mine.mon[k] = team.mon[pick[k]];
      trainer_bring(&t, &mine, 4, &theirs);
      battle_start_ex(&b, &mine, &theirs, t.name, 5, true, t.mega, true);
      b.foe_skill = t.skill;
      fight_run(&b, &t, 60);
    }
  }
  if (demo && strchr(demo, 'f')) { /* formes à objet (Zacian, Giratina, Arceus) et animation de face */
    static Battle b;
    static Trainer t;
    Team me = {0};
    me.mon[me.count++] = trainer_build(SPECIES_ZACIAN_HERO, 0, 9, true, &rng);
    me.mon[me.count++] = trainer_build(SPECIES_KYOGRE, 0, 9, true, &rng);
    trainer_make(&t, 50, &me, &rng);
    t.team.count = 0;
    t.team.mon[t.team.count++] = trainer_build(SPECIES_GIRATINA_ALTERED, 0, 9, true, &rng);
    t.team.mon[t.team.count++] = trainer_build(SPECIES_ARCEUS_NORMAL, 0, 9, true, &rng);
    battle_start(&b, &me, &t.team, t.name, 7, false, false);
    b.foe_skill = t.skill;
    fight_run(&b, &t, 50);
  }
  if (!demo || strchr(demo, 'b')) {
    static Battle b;
    static Trainer t;
    trainer_make(&t, 96, &team, &rng);
    battle_start(&b, &team, &t.team, t.name, 99, true, t.mega);
    b.foe_skill = t.skill;
    fight_run(&b, &t, 96);
  }
  return app_end();
}
