#include "game/fight.h"
#include <string.h>
#include "assets.h"
#include "data.h"
#include "game/ai.h"
#include "core/dex.h"
#include "game/movetext.h"
#include "game/play.h"
#include "game/screens.h"
#include "gfx/display.h"
#include "gfx/menu.h"
#include "gfx/ui.h"
#include "gfx/widgets.h"
#include "platform/input.h"
#include "platform/pc.h"

static BattleView view;
static Battle *bt;
static bool quit;
static char prompt[48];

/* ------------------------------------------------------------------------- affichage */

uint8_t fight_feats;
static bool skip; /* Retour pendant les animations : la fin du tour défile sans elles */

static BattleSide *view_side(int s) { return &view.mon[s]; }
static bool animate(void) { return dex.anim && !skip && !quit; }
static int frames(int n) { return dex.fast ? (n + 1) / 2 : n; }

/* Le Pokémon index entre sur le terrain du côté s : son sprite, son nom, ses PV max, son statut. */
static void sync_side(int s, int index) {
  BattleSide *v = view_side(s);
  const Fighter *f = &bt->side[POS_SIDE(s)].team[index];
  v->species = (uint16_t)f->p.species;
  v->shiny = f->p.shiny;
  v->name = species_names[f->p.species];
  v->max_hp = f->max_hp;
  v->status = f->status;
}

static void count_foes(void) {
  const Side *foe = &bt->side[SIDE_FOE];
  view.foe_count = foe->count, view.foe_alive = 0;
  for (int i = 0; i < foe->count; i++) { /* sur le terrain : les PV affichés (le tour se rejoue) */
    int k = foe->active[0] == i ? 0 : foe->slots > 1 && foe->active[1] == i ? 1 : -1;
    view.foe_alive += k >= 0 ? view.mon[1 + 2 * k].hp > 0 : foe->team[i].hp > 0;
  }
}

static bool wait_frame(void) {
  uint16_t k = input_wait_frame();
  if (k & KEY_HOME) quit = true;
  if (k & KEY_BACK) skip = true;
  return (k & (KEY_OK | KEY_BACK)) != 0;
}

/* Le message tel qu'il tient dans la boîte de dialogue : si une ligne est trop large (un nom long, une
 * traduction), le texte est recoupé aux espaces. */
static const char *fit(const char *text) {
  static char out[2][BATTLE_TEXT + 8];
  static int which;
  const int width = SCREEN_W - 20;
  bool wide = false;
  for (const char *p = text; *p;) { /* chaque ligne */
    char line[BATTLE_TEXT + 8];
    int n = 0;
    while (*p && *p != '\n' && n < (int)sizeof line - 1) line[n++] = *p++;
    line[n] = 0;
    wide |= text_width(&font_normal, line) > width;
    if (*p == '\n') p++;
  }
  if (!wide) return text;
  char *o = out[which ^= 1];
  int n = 0, start = 0, last_space = -1;
  for (const char *p = text; *p && n < (int)sizeof out[0] - 1; p++) {
    o[n] = *p == '\n' ? ' ' : *p;
    if (o[n] == ' ') last_space = n;
    o[++n] = 0;
    if (text_width(&font_normal, o + start) > width && last_space > start) {
      o[last_space] = '\n';
      start = last_space + 1;
    }
  }
  return o;
}

/* Message lettre par lettre (OK l'affiche d'un coup), puis une pause qu'OK écourte. */
static void say(const char *text) {
  text = fit(text);
  view.text = text;
  bool panel = view.moves_menu;
  view.menu = view.moves_menu = false;
  if (panel) ui_draw_dialog(&view);
  int n = text_length(text);
  for (view.shown = 0; view.shown < n && !quit && !skip; view.shown += dex.fast ? 4 : 2) {
    ui_draw_text(&view);
    if (wait_frame()) break;
  }
  view.shown = -1;
  ui_draw_text(&view);
  for (int t = 0; t < (skip ? 6 : frames(45)) && !quit; t++)
    if (wait_frame()) break;
}

/* La barre de PV glisse jusqu'à la nouvelle valeur. */
static void slide_hp(int s, uint16_t target) {
  BattleSide *v = view_side(s);
  int step = v->max_hp / (dex.fast ? 16 : 32);
  if (!step) step = 1;
  if (skip) step = v->max_hp;
  while (v->hp != target && !quit) {
    if (v->hp < target) v->hp = (uint16_t)(v->hp + step > target ? target : v->hp + step);
    else v->hp = (uint16_t)(v->hp - step < target || v->hp < step ? target : v->hp - step);
    ui_draw_hpbox(&view, s);
    if (!skip) wait_frame();
  }
}

static void sparkle(int s) {
  pc_sfx(SFX_SHINY);
  for (int t = 24; t > 0 && !quit && !skip; t -= dex.fast ? 2 : 1) {
    view.sparkle[s] = (uint8_t)t;
    ui_draw_scene(&view);
    wait_frame();
  }
  view.sparkle[s] = 0;
}

/* L'attaque part : le lanceur prend son élan, puis l'effet de la couleur du type (boule pour une
 * attaque spéciale, impact pour une physique, étincelles pour une attaque de statut). */
static void move_anim(int s, uint16_t move, int to) {
  if (!animate()) return;
  const MoveInfo *m = &move_info[move];
  int dir = POS_SIDE(s) == SIDE_PLAYER ? 1 : -1;
  for (int t = 0; t < 8 && !quit; t += dex.fast ? 2 : 1) {
    int d = t < 4 ? t : 8 - t;
    view.dx[s] = (int8_t)(dir * d * 2), view.dy[s] = (int8_t)(-dir * d);
    ui_draw_scene(&view);
    wait_frame();
  }
  view.dx[s] = view.dy[s] = 0;
  view.fx_kind = move_is_status(m) ? FX_SPARKS : m->special ? FX_ORB : FX_IMPACT;
  view.fx_side = (uint8_t)s, view.fx_to = (uint8_t)(to < 4 ? to : s ^ 1), view.fx_color = ui_type_color((uint8_t)m->type);
  for (int t = 0; t <= 16 && !quit && !skip; t += dex.fast ? 4 : 2) {
    view.fx_t = (uint8_t)t;
    ui_draw_scene(&view);
    wait_frame();
  }
  view.fx_kind = FX_NONE;
  ui_draw_scene(&view);
}

/* Touché : le sprite clignote. Sur PC, un bruit (plus fort si c'est super efficace) et, si c'est un
 * Pokémon du joueur, la manette vibre. */
static void hit_anim(int s, int sound) {
  pc_sfx(sound);
  if (POS_SIDE(s) == SIDE_PLAYER) pc_rumble(sound == SFX_HIT_SUPER ? 200 : sound == SFX_HIT_WEAK ? 70 : 130, 180);
  if (!animate()) return;
  for (int t = 0; t < 6 && !quit; t++) {
    view.hide[s] = !(t & 1);
    ui_draw_scene(&view);
    for (int k = 0; k < frames(3); k++) wait_frame();
  }
  view.hide[s] = false;
}

/* Animation de face (les deux images de l'expansion), à l'entrée et après un changement de forme. */
static void front_anim(int s) {
  if (!animate() || POS_SIDE(s) != SIDE_FOE || !pokemon_front2[view_side(s)->species].w) return;
  static const uint8_t steps[] = {10, 6, 10};
  for (int k = 0; k < 3 && !quit && !skip; k++) {
    view.frame2[s] = !(k & 1);
    ui_draw_scene(&view);
    for (int t = 0; t < frames(steps[k]) && !quit && !skip; t++) wait_frame();
  }
  view.frame2[s] = false;
  ui_draw_scene(&view);
}

/* K.O. : le sprite s'enfonce et disparaît. */
static void faint_anim(int s) {
  if (!animate()) return;
  for (int c = 4; c <= 64 && !quit && !skip; c += dex.fast ? 12 : 6) {
    view.cut[s] = (uint8_t)c;
    ui_draw_scene(&view);
    wait_frame();
  }
  view.cut[s] = 0;
}

/* Méga-Évolution : anneaux arc-en-ciel, le sprite clignote en blanc entre les deux formes de plus en
 * plus vite, flash, puis la nouvelle forme. */
static void mega_anim(int s, uint16_t form) {
  BattleSide *v = view_side(s);
  uint16_t base = v->species;
  pc_sfx(SFX_MEGA);
  if (POS_SIDE(s) == SIDE_PLAYER) pc_rumble(160, 600);
  if (dex.anim && !skip) {
    for (int t = 0; t < 48 && !quit && !skip; t += dex.fast ? 2 : 1) {
      int period = t < 16 ? 8 : t < 32 ? 4 : 2;
      view.ring[s] = (uint8_t)(1 + t);
      view.silhouette[s] = true;
      v->species = (t / period) & 1 ? form : base;
      ui_draw_scene(&view);
      wait_frame();
    }
  }
  view.ring[s] = 0, view.silhouette[s] = false;
  for (int t = 0; t <= 8 && !quit; t++) {
    view.flash = (uint8_t)(t <= 4 ? t : 8 - t);
    v->species = t < 4 ? base : form;
    ui_draw_scene(&view);
    if (!skip) for (int k = 0; k < frames(3); k++) wait_frame();
  }
  view.flash = 0;
  v->species = form;
  v->name = species_names[form];
  ui_draw_battle(&view);
}

static void play_events(void) {
  skip = false;
  for (int i = 0; i < bt->nevents && !quit; i++) {
    const BattleEvent *e = &bt->events[i];
    BattleSide *v = view_side(e->side);
    switch (e->kind) {
      case EV_TEXT:
        if (POS_SIDE(e->side) == SIDE_PLAYER && strstr(e->text, L("Coup critique", "critical hit"))) run_stats.crits++;
        say(e->text);
        break;
      case EV_MOVE: move_anim(e->side, e->value, e->text[0] - '0'); break;
      case EV_HP:
        if (e->value < v->hp) {
          if (POS_SIDE(e->side) == SIDE_FOE) run_stats.damage += (uint32_t)(v->hp - e->value);
          /* le message qui suit dit si c'est super efficace ou peu efficace */
          int sound = SFX_HIT;
          for (int k = i + 1; k < bt->nevents && k <= i + 3; k++) {
            const char *t = bt->events[k].text;
            if (bt->events[k].kind != EV_TEXT) continue;
            if (strstr(t, L("super efficace", "super effective"))) sound = SFX_HIT_SUPER;
            if (strstr(t, L("pas très", "not very"))) sound = SFX_HIT_WEAK;
          }
          hit_anim(e->side, sound);
        }
        slide_hp(e->side, e->value);
        break;
      case EV_STATUS:
        v->status = (uint8_t)e->value;
        ui_draw_hpbox(&view, e->side);
        break;
      case EV_SWITCH:
        if (POS_SIDE(e->side) == SIDE_FOE) view.show_trainer = false; /* le dresseur laisse la place à son Pokémon */
        sync_side(e->side, e->value);
        /* une forme à objet prise juste après l'entrée : on montre d'abord la forme de base */
        if (species_info[v->species].is_form)
          for (int k = i + 1; k < bt->nevents && bt->events[k].kind != EV_SWITCH; k++)
            if (bt->events[k].kind == EV_FORM && bt->events[k].side == e->side) {
              v->species = (uint16_t)species_info[v->species].mega_base;
              v->name = species_names[v->species];
              break;
            }
        if (POS_SIDE(e->side) == SIDE_FOE) dex_see(v->species);
        pc_cry(v->species);
        if (i + 1 < bt->nevents && bt->events[i + 1].kind == EV_HP && bt->events[i + 1].side == e->side)
          v->hp = bt->events[++i].value;
        count_foes();
        ui_draw_battle(&view);
        if (v->shiny) sparkle(e->side);
        front_anim(e->side);
        break;
      case EV_MEGA:
        run_stats.megas += POS_SIDE(e->side) == SIDE_PLAYER;
        mega_anim(e->side, e->value);
        break;
      case EV_FORM:
        mega_anim(e->side, e->value);
        front_anim(e->side);
        break;
      case EV_TERRAIN:
        view.terrain = (uint8_t)e->value;
        ui_draw_battle(&view);
        break;
      case EV_FAINT:
        pc_cry(v->species);
        if (POS_SIDE(e->side) == SIDE_PLAYER) pc_rumble(255, 400);
        if (POS_SIDE(e->side) == SIDE_FOE) {
          run_stats.kos++;
          if (species_info[v->species].is_mega) fight_feats |= FEAT_MEGA_KO;
        }
        v->hp = 1, faint_anim(e->side);
        v->hp = 0;
        count_foes();
        ui_draw_battle(&view);
        break;
    }
  }
  bt->nevents = 0;
  skip = false;
}

/* ------------------------------------------------------------------------- choix du joueur */

static void ask(int pos) {
  strcat(strcpy(prompt, L("Que doit faire\n", "What will\n")), species_names[battle_active(bt, pos)->p.species]);
  view.text = strcat(prompt, L(" ?", " do?"));
  view.menu = true, view.moves_menu = false;
  view.choosing = (int8_t)(bt->doubles ? pos : -1);
  ui_draw_battle(&view);
}

static uint8_t move_cursor(uint8_t c, uint8_t k) {
  if (k & KEY_LEFT) c &= ~1;
  if (k & KEY_RIGHT) c |= 1;
  if (k & KEY_UP) c &= ~2;
  if (k & KEY_DOWN) c |= 2;
  return c;
}

/* Duo : quel adversaire viser (Gauche / Droite) ; -1 si le joueur revient en arrière. */
static int choose_target(int pos) {
  int a = pos ^ 1;
  if (!battle_alive(bt, a)) a ^= 2;
  if (!battle_alive(bt, a ^ 2)) return a; /* un seul adversaire debout : pas de question */
  static char text[48];
  view.moves_menu = view.menu = false;
  for (;;) {
    view.aim = (int8_t)a;
    strcat(strcat(strcpy(text, L("Viser quel POKéMON ?\n← ", "Target which POKéMON?\n← ")),
                  species_names[battle_active(bt, a)->p.species]),
           " →");
    view.text = text, view.shown = -1;
    ui_draw_battle(&view);
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return quit = true, view.aim = -1, -1;
    if (k & KEY_BACK) return view.aim = -1, -1;
    if (k & KEY_OK) return view.aim = -1, a;
    if (k & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)) a ^= 2;
  }
}

/* Duo : l'efficacité de l'attaque de l'emplacement slot sur chaque Pokémon qu'elle peut toucher, affichée
 * sur leurs cadres (rien pour une attaque de statut). */
static void tag_targets(int pos, uint8_t slot) {
  memset(view.tag, 255, sizeof view.tag);
  uint16_t move = pokemon_move(&battle_active(bt, pos)->p, slot);
  const MoveInfo *m = &move_info[move];
  if (!bt->doubles || !move || move_is_status(m)) return;
  for (int q = 0; q < battle_positions(bt); q++) {
    bool foe = POS_SIDE(q) != POS_SIDE(pos);
    if (q == pos || !battle_alive(bt, q) || (!foe && m->spread != 2)) continue;
    uint8_t eff = 255;
    battle_estimate_at(bt, pos, q, move, 100, &eff);
    view.tag[q] = eff;
  }
}

/* Le menu des attaques ; false si le joueur revient en arrière. mega_free : la Méga-Évolution n'est
 * pas déjà choisie par le partenaire ce tour-ci. */
static bool choose_move(Action *act, int pos, bool mega_free) {
  static char desc[MOVES_MAX][MOVETEXT_BYTES];
  const Fighter *f = battle_active(bt, pos);
  bool any = false;
  int facing = pos ^ 1;
  if (!battle_alive(bt, facing) && bt->doubles) facing ^= 2;
  for (uint8_t k = 0; k < MOVES_MAX; k++) {
    uint16_t move = pokemon_move(&f->p, k);
    const MoveInfo *m = &move_info[move];
    move_description(move, desc[k]);
    if (bt->doubles && move && m->spread)
      strcpy(desc[k], m->spread == 2 ? L("Touche tous les autres POKéMON", "Hits all other POKéMON")
                                     : L("Touche les deux adversaires", "Hits both foes"));
    uint8_t eff = 255;
    if (move && !move_is_status(m)) battle_estimate_at(bt, pos, facing, move, 100, &eff);
    view.moves[k] = (MoveSlot){
        .name = move ? move_names[move] : NULL, .desc = desc[k], .pp = f->pp[k],
        .max_pp = move ? (uint8_t)m->pp : 0, .type = (uint8_t)m->type,
        .category = move_is_status(m) ? CATEGORY_STATUS : m->special ? CATEGORY_SPECIAL : CATEGORY_PHYSICAL,
        .power = (uint8_t)m->power, .accuracy = (uint8_t)m->accuracy, .eff = eff};
    any |= battle_can_move(bt, pos, k);
  }
  view.mega_ready = mega_free && battle_can_mega(bt, pos) >= 0;
  view.mega_on = false;
  if (!any) { /* plus rien d'utilisable : Lutte, choisie par le moteur */
    *act = (Action){ACT_MOVE, 0, 0, false, (uint8_t)facing};
    return true;
  }
  view.moves_menu = true, view.menu = false, view.cursor = 0;
  tag_targets(pos, 0);
  ui_draw_battle(&view);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return quit = true, false;
    if (k & KEY_BACK) {
      memset(view.tag, 255, sizeof view.tag);
      return view.moves_menu = false, false;
    }
    if ((k & KEY_ALT) && view.mega_ready) view.mega_on = !view.mega_on, ui_draw_dialog(&view);
    uint8_t c = move_cursor(view.cursor, k);
    if (c != view.cursor && view.moves[c].name) {
      view.cursor = c;
      tag_targets(pos, c);
      if (bt->doubles) ui_draw_battle(&view); /* les cadres des cibles changent aussi */
      else ui_draw_dialog(&view);
    }
    if (k & KEY_OK) {
      if (battle_can_move(bt, pos, view.cursor)) {
        uint8_t slot = view.cursor;
        bool mega = view.mega_on;
        int target = facing;
        if (bt->doubles && battle_move_targets_foe(pokemon_move(&f->p, slot))) {
          target = choose_target(pos);
          if (target < 0) { /* retour au menu des attaques */
            if (quit) return false;
            view.cursor = slot, view.moves_menu = true, view.text = NULL;
            ui_draw_battle(&view);
            continue;
          }
        }
        *act = (Action){ACT_MOVE, slot, (uint8_t)ai_replacement(bt, pos), mega, (uint8_t)target};
        memset(view.tag, 255, sizeof view.tag);
        view.moves_menu = view.menu = false;
        view.text = NULL;
        ui_draw_battle(&view);
        return true;
      }
      say(!f->pp[view.cursor] ? L("Plus de PP pour\ncette attaque !", "There's no PP left\nfor this move!")
                              : L("Impossible d'utiliser\ncette attaque !", "This move can't\nbe used!"));
      view.moves_menu = true;
      ui_draw_dialog(&view);
    }
  }
}

static char *add_num(char *s, unsigned v) {
  char d[6], *p = d + sizeof d;
  *--p = 0;
  do *--p = (char)('0' + v % 10); while (v /= 10);
  return strcat(s, p);
}

/* Une ligne de l'équipe (19 px) : nom et objet, puis barre de PV, PV et statut. */
static void party_row(const void *ctx, int i, int x, int y, int w) {
  const Fighter *f = &((const Side *)ctx)->team[i];
  char hp[12] = "";
  (void)w;
  int nx = gfx_text(&font_normal, x, y - 2, species_names[f->p.species], f->hp ? INK_DARK : RGB565(160, 160, 160), INK_DARK_SHADOW);
  if (f->p.shiny) ui_shiny_star(nx + 2, y + 3), nx += 9;
  if (f->p.item) gfx_text(&font_small, nx + 4, y - 1, item_names[f->p.item], INK_BLUE, INK_DARK_SHADOW);
  ui_hpbar(x, y + 11, f->hp, f->max_hp);
  add_num(strcat(add_num(hp, f->hp), "/"), f->max_hp);
  int hx = gfx_text(&font_small, x + 70, y + 6, hp, INK_DARK, INK_DARK_SHADOW);
  ui_status_badge(hx + 4, y + 9, f->hp ? f->status : 0);
}

/* L'écran d'équipe pour la position pos. Après un K.O. (replacing), il faut choisir ; sinon Retour
 * annule (-1). taken : un équipier déjà appelé par le partenaire ce tour-ci (ou -1). */
static int party_menu(bool replacing, int pos, int taken) {
  const Side *sd = &bt->side[SIDE_PLAYER];
  uint16_t icons[TEAM_MAX];
  for (int i = 0; i < sd->count; i++) icons[i] = sd->team[i].p.species;
  int here = sd->active[POS_SLOT(pos)];
  Menu m = {.title = replacing ? L("Qui envoyer au combat ?", "Send out which POKéMON?") : L("POKéMON : qui envoyer ?", "POKéMON: send out who?"), .n = sd->count, .icons = icons,
            .row = party_row, .ctx = sd, .hue = 0, .cursor = here};
  if (replacing) /* le curseur sur le premier Pokémon qui peut entrer */
    for (int i = sd->count - 1; i >= 0; i--)
      if (sd->team[i].hp && !battle_on_field(sd, i) && i != taken) m.cursor = i;
  int c;
  for (;;) {
    c = menu_run(&m, &quit);
    if (quit || (c < 0 && !replacing)) break;
    if (c < 0) continue;
    bool ok = c != taken && (replacing ? sd->team[c].hp && !battle_on_field(sd, c) : battle_can_switch(bt, pos, (uint8_t)c));
    if (ok) break;
    menu_message(!sd->team[c].hp ? L("Ce POKéMON est K.O. !", "This POKéMON has fainted!") : battle_on_field(sd, c) || c == taken ? L("Il est déjà au combat !", "It's already in battle!") :
                                                                                                L("Impossible de fuir !", "You can't switch out!"), &quit);
  }
  ui_draw_battle(&view);
  return quit ? -1 : c;
}

/* L'action de la position pos : 1 choisie, 0 abandon (Maison), -1 retour au Pokémon précédent (duo). */
static int choose_action(Action *act, int pos, bool can_back, bool mega_free, int taken) {
  if (battle_forced_move(bt, pos)) {
    *act = (Action){ACT_MOVE, 0, 0, false, (uint8_t)(pos ^ 1)};
    return 1;
  }
  view.cursor = MENU_FIGHT;
  ask(pos);
  while (!quit) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) quit = true;
    if ((k & KEY_BACK) && can_back) return view.choosing = -1, -1;
    uint8_t c = move_cursor(view.cursor, k);
    if (c != view.cursor) view.cursor = c, ui_draw_dialog(&view);
    if (!(k & KEY_OK)) continue;
    uint8_t choice = view.cursor;
    switch (choice) {
      case MENU_FIGHT:
        if (choose_move(act, pos, mega_free)) return view.choosing = -1, 1;
        break;
      case MENU_INFO:
        screen_summary(bt, &quit);
        ui_draw_battle(&view);
        break;
      case MENU_POKEMON: {
        int i = party_menu(false, pos, taken);
        if (i >= 0) {
          *act = (Action){ACT_SWITCH, (uint8_t)i, 0, false, (uint8_t)(pos ^ 1)};
          return view.choosing = -1, 1;
        }
        break;
      }
      case MENU_RUN: say(L("Non ! Impossible de fuir\nun combat de DRESSEUR !", "No! There's no running\nfrom a TRAINER battle!")); break;
    }
    view.cursor = choice;
    ask(pos);
  }
  return 0;
}

/* ------------------------------------------------------------------------- le combat */

uint8_t fight_run(Battle *b, const Trainer *t, uint16_t number) {
  static char line[64];
  bt = b, quit = false, fight_feats = 0;
  memset(&view, 0, sizeof view);
  view.number = number;
  view.backdrop = t->backdrop, view.shown = -1;
  view.show_trainer = !t->wild;
  view.trainer = t->pic;
  view.doubles = b->doubles;
  view.choosing = view.aim = -1;
  memset(view.tag, 255, sizeof view.tag);
  for (int p = 0; p < battle_positions(b); p++) {
    sync_side(p, b->side[POS_SIDE(p)].active[POS_SLOT(p)]);
    view.mon[p].hp = 0; /* rien sur le terrain avant les entrées */
  }
  count_foes();
  view.foe_alive = view.foe_count;
  ui_draw_battle(&view);
  say(add_num(strcpy(line, L("COMBAT ", "BATTLE ")), number));
  play_events();
  while (!b->result && !quit) {
    for (int s = 0; s < battle_positions(b) && !quit; s++) {
      if (!battle_must_replace(b, s)) continue;
      int i = POS_SIDE(s) == SIDE_PLAYER ? party_menu(true, s, -1) : ai_replacement(b, s);
      if (i < 0) break;
      if (s == SIDE_FOE && !b->doubles && dex.choice && battle_active(b, SIDE_PLAYER)->hp) {
        /* mode Choix : l'adversaire annonce son Pokémon, le joueur peut changer le sien */
        static char ask_text[80];
        bool other = false;
        for (uint8_t j = 0; j < b->side[SIDE_PLAYER].count; j++) other |= battle_can_switch(b, SIDE_PLAYER, j);
        strcat(strcat(strcpy(ask_text, L("Il va envoyer ", "They're about to send ")), species_names[b->side[SIDE_FOE].team[i].p.species]), L(".\nChanger de POKéMON ?", ".\nSwitch POKéMON?"));
        if (other && menu_confirm(ask_text, &quit)) {
          int j = party_menu(false, SIDE_PLAYER, -1);
          if (j >= 0) battle_send(b, SIDE_PLAYER, (uint8_t)j), play_events();
        }
        ui_draw_battle(&view);
      }
      battle_send(b, s, (uint8_t)i);
      play_events();
    }
    if (b->result || quit) break;
    Action act[POSITIONS];
    memset(act, 0, sizeof act);
    /* le joueur choisit pour chacun de ses Pokémon debout (Retour : revenir au précédent) */
    int mine[2] = {0, 2}, n = b->doubles ? 2 : 1, k = 0;
    while (k < n && !quit) {
      int p = mine[k];
      if (!battle_alive(b, p)) {
        act[p] = (Action){ACT_MOVE, 0, 0, false, (uint8_t)(p ^ 1)};
        k++;
        continue;
      }
      bool first = k == 0 || !battle_alive(b, mine[0]);
      bool mega_free = first || !act[mine[0]].mega;
      int taken = !first && act[mine[0]].kind == ACT_SWITCH ? act[mine[0]].index : -1;
      int r = choose_action(&act[p], p, !first, mega_free, taken);
      if (r == 0) break;
      k = r < 0 ? 0 : k + 1;
    }
    if (quit || k < n) break;
    for (int p = 1; p < battle_positions(b); p += 2) act[p] = ai_choose(b, p);
    battle_turn_all(b, act);
    play_events();
  }
  if (!quit && b->result == BATTLE_WON && t->wild) {
    pc_music(MUSIC_VICTORY_TRAINER);
    say(L("Le POKéMON légendaire\nrejoint le draft !", "The legendary POKéMON\njoins the draft!"));
  } else if (!quit && b->result == BATTLE_WON) { /* le dresseur revient, vaincu */
    pc_music(number == 100 ? MUSIC_VICTORY_LEAGUE : t->boss ? MUSIC_VICTORY_GYM : MUSIC_VICTORY_TRAINER);
    view.show_trainer = true;
    ui_draw_battle(&view);
    say(strcat(strcat(strcpy(line, L("Vous avez battu\n", "You defeated\n")), t->name), " !"));
  } else if (!quit && b->result) {
    say(L("Vous n'avez plus\nde POKéMON en forme…", "You have no more\nPOKéMON that can fight…"));
  }
  return quit ? BATTLE_ONGOING : b->result;
}
