#include "game/meta.h"
#include <string.h>
#include "assets.h"
#include "core/dex.h"
#include "data.h"
#include "gfx/menu.h"
#include "gfx/widgets.h"
#include "platform/input.h"
#include "platform/pc.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

static const char *num(char *buf, unsigned v) { /* buf : 12 octets */
  char *p = buf + 11;
  *p = 0;
  do *--p = (char)('0' + v % 10); while (v /= 10);
  return p;
}

/* ------------------------------------------------------------------------- personnalisation */

static const uint8_t avatar_pics[10] = {TRAINER_PIC_BRENDAN, TRAINER_PIC_MAY,     TRAINER_PIC_WALLY, TRAINER_PIC_STEVEN,
                                        TRAINER_PIC_WALLACE, TRAINER_PIC_RED,     TRAINER_PIC_LEAF,  TRAINER_PIC_BLUE,
                                        TRAINER_PIC_ARCHIE,  TRAINER_PIC_MAXIE};
static const char *const avatar_names[2][10] = {
    {"BRICE", "FLORA", "TIMMY", "PIERRE ROCHE", "MARC", "RED", "LEAF", "BLUE", "ARTHUR", "MAX"},
    {"BRENDAN", "MAY", "WALLY", "STEVEN", "WALLACE", "RED", "LEAF", "BLUE", "ARCHIE", "MAXIE"}};

enum { REWARD_THEME, REWARD_AVATAR, REWARD_BUBBLE };
static const struct {
  const char *name, *desc, *name_en, *desc_en;
  uint8_t kind, value;
} achievements[ACH_COUNT] = {
    [ACH_FIRST_WIN] = {"Premier pas", "Gagner un combat", "First steps", "Win a battle", REWARD_THEME, 1},
    [ACH_MEGA_KO] = {"Méga-chasseur", "Vaincre un POKéMON méga-évolué", "Mega hunter", "Defeat a Mega Evolved POKéMON",
                     REWARD_THEME, 2},
    [ACH_MEGA] = {"Méga-puissance", "Méga-évoluer", "Mega power", "Mega Evolve", REWARD_AVATAR, 2},
    [ACH_BOSS] = {"Premier badge", "Vaincre le Boss du combat 10", "First badge", "Defeat the Boss of battle 10",
                  REWARD_THEME, 3},
    [ACH_B50] = {"Mi-parcours", "Atteindre le combat 50", "Halfway there", "Reach battle 50", REWARD_AVATAR, 3},
    [ACH_CHAMPION] = {"Maître de Hoenn", "Vaincre le Maître (combat 100)", "Hoenn Champion",
                      "Defeat the Champion (battle 100)", REWARD_AVATAR, 4},
    [ACH_SHINY] = {"Éclat rare", "Obtenir un POKéMON chromatique", "Rare shine", "Get a shiny POKéMON", REWARD_THEME, 6},
    [ACH_RAID] = {"Chasseur de raids", "Vaincre un légendaire géant", "Raid hunter", "Defeat a giant legendary",
                  REWARD_AVATAR, 5},
    [ACH_RESONANCE3] = {"Harmonie", "Entrer en Résonance III", "Harmony", "Reach Resonance III", REWARD_THEME, 4},
    [ACH_DEX100] = {"Collectionneur", "100 espèces au Carnet", "Collector", "100 species in the POKéDEX", REWARD_AVATAR, 6},
    [ACH_NUZLOCKE] = {"Survivant", "Combat 100 en Nuzlocke", "Survivor", "Battle 100 in Nuzlocke", REWARD_THEME, 5},
    [ACH_HARD] = {"Expert", "Combat 100 en Difficile", "Expert", "Battle 100 on Hard", REWARD_AVATAR, 7},
    [ACH_B150] = {"Légende de Kanto", "Atteindre le combat 150", "Kanto legend", "Reach battle 150", REWARD_THEME, 7},
    [ACH_CRITS] = {"Coup de maître", "50 coups critiques en une partie", "Masterstroke", "50 critical hits in one game",
                   REWARD_BUBBLE, 1},
    [ACH_DAMAGE] = {"Dévastateur", "20 000 PV infligés en une partie", "Devastator", "Deal 20,000 HP in one game",
                    REWARD_AVATAR, 8},
    [ACH_FULL_TEAM] = {"Équipe complète", "Avoir 6 POKéMON dans l'équipe", "Full team", "Have 6 POKéMON in your team",
                       REWARD_AVATAR, 9},
    [ACH_CLAUDE] = {"Code secret", "Recruter le POKéMON secret, qui se cache très rarement dans le draft",
                    "Secret code", "Recruit the secret POKéMON, which very rarely hides in the draft", REWARD_THEME, 8},
};
#define ACH_NAME(i) L(achievements[i].name, achievements[i].name_en)
#define ACH_DESC(i) L(achievements[i].desc, achievements[i].desc_en)

static bool has(int id) { return dex.achievements >> id & 1; }

static bool unlocked(uint8_t kind, uint8_t value) {
  if (value == 0 || (kind == REWARD_AVATAR && value == 1)) return true;
  for (int i = 0; i < ACH_COUNT; i++)
    if (has(i) && achievements[i].kind == kind && achievements[i].value == value) return true;
  return false;
}

static const char *reward_name(int id) {
  static char buf[40];
  uint8_t v = achievements[id].value;
  switch (achievements[id].kind) {
    case REWARD_THEME: return strcat(strcpy(buf, L("Thème ", "Theme ")), theme_names[v]);
    case REWARD_AVATAR: return strcat(strcpy(buf, L("Personnage ", "Character ")), avatar_names[lang][v]);
    default: return L("Boîte de dialogue claire", "Light dialog box");
  }
}

uint8_t avatar_pic(void) { return avatar_pics[dex.avatar < 10 ? dex.avatar : 0]; }

void settings_apply(void) {
  ui_theme = dex.theme < THEME_COUNT ? dex.theme : 0;
  ui_bubble = dex.bubble;
  lang = dex.lang & 1;
}

void achieve(int id, bool *quit) {
  if (has(id)) return;
  dex.achievements |= 1u << id;
  dex_save();
  static char text[80];
  strcat(strcat(strcat(strcpy(text, L("SUCCÈS : ", "ACHIEVEMENT: ")), ACH_NAME(id)), "\n"), reward_name(id));
  menu_message(text, quit);
}

static void achievement_row(const void *ctx, int i, int x, int y, int w) {
  (void)ctx, (void)w;
  bool ok = has(i);
  int end = gfx_text(&font_normal, x, y, ACH_NAME(i), ok ? INK_DARK : RGB565(150, 150, 150), INK_DARK_SHADOW);
  if (ok) ui_shiny_star(end + 2, y + 4);
}

static void achievement_detail(const void *ctx, int i, int x, int y, int w, int h) {
  (void)ctx, (void)h;
  int lines = ui_wrap(&font_small, x, y - 3, w, ACH_DESC(i), INK_DARK, INK_DARK_SHADOW);
  int dy = lines > 2 ? 12 * (lines - 2) : 0; /* une description longue repousse la récompense */
  gfx_text(&font_small, x, y + dy + 30, L("Récompense :", "Reward:"), INK_BLUE, INK_DARK_SHADOW);
  ui_wrap(&font_small, x, y + dy + 40, w, reward_name(i), INK_BLUE, INK_DARK_SHADOW);
  if (achievements[i].kind == REWARD_AVATAR) {
    const Image *im = &trainer_pic[avatar_pics[achievements[i].value]];
    gfx_image(im, x + (w - 64) / 2 + im->x, y + 64 + im->y);
  }
  gfx_text(&font_small, x, y + h - 12, has(i) ? L("Obtenu !", "Unlocked!") : L("À obtenir", "Locked"), has(i) ? INK_GREEN : INK_RED, INK_DARK_SHADOW);
}

void screen_achievements(bool *quit) {
  char title[32], n[12];
  int got = 0;
  for (int i = 0; i < ACH_COUNT; i++) got += has(i);
  strcat(strcat(strcpy(title, L("SUCCÈS  ", "ACHIEVEMENTS  ")), num(n, (unsigned)got)), " / ");
  strcat(title, num(n, ACH_COUNT));
  Menu m = {.title = title, .n = ACH_COUNT, .row = achievement_row, .detail = achievement_detail, .hue = 3, .list_w = 120};
  while (menu_run(&m, quit) >= 0 && !*quit) {
  }
}

/* ------------------------------------------------------------------------- paramètres */

enum { SET_LANG, SET_FORMAT, SET_SAVE, SET_ANIM, SET_FAST, SET_CHOICE, SET_THEME, SET_AVATAR, SET_BUBBLE, SET_TUTO, SET_COUNT };

static void settings_detail(const void *ctx, int i, int x, int y, int w, int h) {
  (void)ctx, (void)h;
  const char *const help[SET_COUNT] = {
      L("Langue du jeu : FRANÇAIS ou ENGLISH. / Game language.", "Game language: FRANÇAIS or ENGLISH. / Langue du jeu."),
      L("DUO : 2 contre 2 au format VGC, 4 POKéMON choisis avant chaque combat. SIMPLE : 1 contre 1, toute l'équipe.", "DOUBLE: 2 against 2 in VGC format, 4 POKéMON chosen before each battle. SINGLE: 1 against 1, the whole team."),
      L("Défaite : retour au palier de 10 combats, au combat perdu, ou fin de partie.",
        "After a loss: back to the last 10-battle checkpoint, to the lost battle, or game over."),
      L("Attaques, K.O. et Méga-Évolution animés.", "Animated moves, fainting and Mega Evolution."),
      L("Textes et animations deux fois plus rapides. En combat, {BACK} passe les animations.", "Text and animations twice as fast. In battle, {BACK} skips animations."),
      L("En simple. CHOIX : quand l'adversaire envoie un POKéMON, vous pouvez changer le vôtre. DÉFINI : jamais.", "In singles. SHIFT: when the foe sends out a POKéMON, you may switch yours. SET: never."),
      L("Couleur des menus. Les succès en débloquent d'autres.", "Menu colors. Achievements unlock more."),
      L("Le personnage de l'écran titre et du Hall of Fame.", "The character on the title screen and in the Hall of Fame."),
      L("Boîte de dialogue claire, débloquée par un succès.", "Light dialog box, unlocked by an achievement."),
      L("Revoir les règles propres à PokeRush.", "See PokeRush's own rules again.")};
  const char *text = i < SET_COUNT ? help[i] : pc_setting_help(i - SET_COUNT); /* PC : affichage, son, manette */
  ui_wrap_lines(&font_small, x, y - 2, w - (i == SET_AVATAR ? 40 : 0), 10, 3, text, INK_DARK, INK_DARK_SHADOW);
  if (i == SET_AVATAR) { /* le personnage, en buste */
    const Image *im = &trainer_pic[avatar_pic()];
    gfx_clip_push(x + w - 36, y - 4, 40, 40);
    gfx_image(im, x + w - 48 + im->x, y - 4 + im->y);
    gfx_clip_pop();
  }
}

static uint8_t next_unlocked(uint8_t kind, uint8_t cur, uint8_t count) {
  for (uint8_t k = 1; k <= count; k++) {
    uint8_t v = (uint8_t)((cur + k) % count);
    if (unlocked(kind, v)) return v;
  }
  return cur;
}

void screen_settings(bool *quit) {
#define PC_MAX 24
  static char lines[SET_COUNT + PC_MAX][48];
  const char *items[SET_COUNT + PC_MAX];
  int pcn = pc_setting_count() < PC_MAX ? pc_setting_count() : PC_MAX;
  Menu m = {.title = "PARAMÈTRES", .n = SET_COUNT + pcn, .items = items, .detail = settings_detail, .hue = 1,
            .detail_below = true};
  for (;;) {
    strcat(strcpy(lines[SET_LANG], "Langue / Language : "), lang ? "ENGLISH" : "FRANÇAIS");
    strcat(strcpy(lines[SET_FORMAT], L("Combats : ", "Battles: ")), dex.singles ? L("SIMPLE", "SINGLE") : L("DUO (VGC)", "DOUBLE (VGC)"));
    const char *saves[CHECKPOINT_COUNT] = {L("TOUS LES 10 COMBATS", "EVERY 10 BATTLES"), L("À CHAQUE COMBAT", "EVERY BATTLE"),
                                           L("JAMAIS", "NEVER")};
    strcat(strcpy(lines[SET_SAVE], L("Sauvegarde : ", "Checkpoints: ")), saves[dex.checkpoint % CHECKPOINT_COUNT]);
    strcat(strcpy(lines[SET_ANIM], L("Animations de combat : ", "Battle animations: ")), dex.anim ? L("OUI", "ON") : L("NON", "OFF"));
    strcat(strcpy(lines[SET_FAST], L("Vitesse : ", "Speed: ")), dex.fast ? L("RAPIDE (×2)", "FAST (×2)") : L("NORMALE", "NORMAL"));
    strcat(strcpy(lines[SET_CHOICE], L("Changement : ", "Battle style: ")), dex.choice ? L("CHOIX", "SHIFT") : L("DÉFINI", "SET"));
    strcat(strcpy(lines[SET_THEME], L("Thème : ", "Theme: ")), theme_names[ui_theme]);
    strcat(strcpy(lines[SET_AVATAR], L("Personnage : ", "Character: ")), avatar_names[lang][dex.avatar % 10]);
    strcat(strcpy(lines[SET_BUBBLE], L("Boîte de dialogue : ", "Dialog box: ")), dex.bubble ? L("CLAIRE", "LIGHT") : L("CLASSIQUE", "CLASSIC"));
    strcpy(lines[SET_TUTO], L("Revoir le tutoriel", "Replay the tutorial"));
    for (int i = 0; i < pcn; i++) pc_setting_line(i, lines[SET_COUNT + i], sizeof lines[0]);
    for (int i = 0; i < SET_COUNT + pcn; i++) items[i] = lines[i];
    m.title = L("PARAMÈTRES", "SETTINGS");
    int c = menu_run(&m, quit);
    if (c < 0 || *quit) break;
    m.cursor = c;
    switch (c) {
      case SET_LANG: dex.lang ^= 1; break;
      case SET_FORMAT: dex.singles ^= 1; break;
      case SET_SAVE: dex.checkpoint = (uint8_t)((dex.checkpoint + 1) % CHECKPOINT_COUNT); break;
      case SET_ANIM: dex.anim ^= 1; break;
      case SET_FAST: dex.fast ^= 1; break;
      case SET_CHOICE: dex.choice ^= 1; break;
      case SET_THEME: dex.theme = next_unlocked(REWARD_THEME, dex.theme, THEME_COUNT); break;
      case SET_AVATAR: dex.avatar = next_unlocked(REWARD_AVATAR, dex.avatar, 10); break;
      case SET_BUBBLE:
        if (unlocked(REWARD_BUBBLE, 1)) dex.bubble ^= 1;
        else menu_message(L("Débloqué par le succès\n“Coup de maître”.", "Unlocked by the achievement\n“Masterstroke”."), quit);
        break;
      case SET_TUTO: tutorial(quit); break;
      default: pc_setting_change(c - SET_COUNT); break;
    }
    settings_apply();
  }
  dex_save();
}

/* ------------------------------------------------------------------------- tutoriel */

static const char *const tuto[][4] = {
    {"BIENVENUE !", "Enchaînez les combats contre des dresseurs. Après chaque victoire, choisissez UNE récompense : "
                    "RECRUTER un des POKéMON proposés ou ENTRAÎNER un membre. La partie se sauvegarde (voir PARAMÈTRES).",
     "WELCOME!", "Battle one trainer after another. After each victory, choose ONE reward: RECRUIT one of the offered "
                 "POKéMON or TRAIN a member. Checkpoints: see SETTINGS."},
    {"PRÉPARATION", "Après la récompense, le menu de préparation s'ouvre : objets, ordre de l'équipe, Carnet… "
                    "Appuyez sur {EXE} pour lancer le combat suivant. Pendant le choix, {ALT} ouvre ce menu.",
     "GETTING READY", "After the reward, the preparation menu opens: items, team order, POKéDEX… Press {EXE} to start the "
                      "next battle. While choosing the reward, {ALT} opens this menu."},
    {"LES RÉSONANCES", "Quand 3 POKéMON ou plus de l'équipe partagent un type, il RÉSONNE (niveau I à III). Chaque type a "
                       "son effet : soin pour l'Eau, critiques pour la Roche, brûlures pour le Feu…",
     "RESONANCES", "When 3 or more team members share a type, it RESONATES (level I to III). Each type has its own "
                   "effect: healing for Water, critical hits for Rock, burns for Fire…"},
    {"BOSS ET RAIDS", "Tous les 10 combats, un BOSS à l'équipe et à la stratégie fixes. Parfois, un légendaire GÉANT "
                      "surgit (PV ×4) : vaincu, il rejoint le draft.",
     "BOSSES AND RAIDS", "Every 10 battles, a BOSS with a fixed team and strategy. Sometimes a GIANT legendary appears "
                         "(HP ×4): defeat it and it joins the draft."},
    {"SPÉCIALITÉS", "Chaque dresseur a un type favori. Pendant le draft, le badge du prochain adversaire vous aide "
                    "à préparer l'équipe.",
     "SPECIALTIES", "Each trainer has a favorite type. During the draft, the next opponent's badge helps you prepare "
                    "your team."},
    {"COMBATS EN DUO", "Les combats se jouent à 2 contre 2. Juste avant, vous voyez les 6 POKéMON adverses et "
                       "choisissez les 4 qui combattront : les 2 premiers ouvrent. Séisme et Surf touchent plusieurs "
                       "POKéMON.",
     "DOUBLE BATTLES", "Battles are 2 against 2. Just before, you see the 6 opposing POKéMON and choose the 4 that will "
                       "fight: the first 2 lead. Earthquake and Surf hit several POKéMON."},
    {"MÉGA ET OBJETS", "Le MÉGA-ANNEAU arrive au combat 10 : {ALT} dans le menu des attaques. L'ÉCHO-PRISME compte "
                       "double pour les Résonances.",
     "MEGA AND ITEMS", "The MEGA RING comes at battle 10: {ALT} in the move menu. The ECHO PRISM counts double for "
                       "Resonances."},
    {"CARNET ET SUCCÈS", "Le CARNET garde les POKéMON vus et obtenus : tous les 50, un objet offert au départ. Les "
                         "SUCCÈS débloquent thèmes et personnages.",
     "POKéDEX & GOALS", "The POKéDEX keeps the POKéMON seen and obtained: every 50, a free item at the start. "
                        "ACHIEVEMENTS unlock themes and characters."},
    {"COMMANDES", "{OK} : valider, accélérer le texte. {BACK} : annuler, passer les animations. {ALT} : Méga-Évolution, "
                  "menu du draft. {EXE} : combat suivant. Langue et vitesse : PARAMÈTRES.",
     "CONTROLS", "{OK}: confirm, speed up text. {BACK}: cancel, skip animations. {ALT}: Mega Evolution, draft menu. {EXE}: "
                 "next battle. Language and speed: SETTINGS."},
};

static void draw_tuto(const void *ctx) {
  int p = *(const int *)ctx;
  char title[32], a[12], b[12];
  ui_background(0);
  strcat(strcat(strcat(strcpy(title, L("TUTORIEL  ", "TUTORIAL  ")), num(a, (unsigned)p + 1)), " / "), num(b, (unsigned)COUNT(tuto)));
  ui_title(title);
  ui_window(6, 20, SCREEN_W - 12, 126);
  gfx_text(&font_normal, 14, 23, tuto[p][2 * lang], INK_BLUE, INK_DARK_SHADOW);
  ui_wrap_lines(&font_normal, 14, 39, SCREEN_W - 28, 14, 7, tuto[p][2 * lang + 1], INK_DARK, INK_DARK_SHADOW);
  ui_footer(L("{OK} / → suite   ← précédent   {BACK} passer", "{OK} / → next   ← previous   {BACK} skip"));
}

void tutorial(bool *quit) {
  int p = 0;
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_tuto, &p);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return (void)(*quit = true);
    if (k & KEY_BACK) break;
    int q = p + ((k & (KEY_OK | KEY_RIGHT)) ? 1 : 0) - ((k & KEY_LEFT) && p ? 1 : 0);
    if (q >= COUNT(tuto)) break;
    if (q != p) p = q, gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_tuto, &p);
  }
  dex.tuto = 1;
  dex_save();
}

/* ------------------------------------------------------------------------- graine */

typedef struct {
  uint32_t seed;
  int digit;
} SeedView;

static void draw_seed(const void *ctx) {
  const SeedView *s = ctx;
  ui_background(1);
  ui_title(L("GRAINE DE LA PARTIE", "GAME SEED"));
  ui_window(10, 24, SCREEN_W - 20, 130);
  ui_wrap(&font_small, 18, 34, SCREEN_W - 36,
          L("Même graine, même partie : partagez-la pour retrouver les mêmes combats.", "Same seed, same game: share it to replay the same battles."), INK_DARK, INK_DARK_SHADOW);
  for (int i = 0; i < 8; i++) {
    int x = 30 + i * 20, y = 76;
    char d[2] = {"0123456789ABCDEF"[(s->seed >> (28 - 4 * i)) & 15], 0};
    gfx_fill(x - 2, y - 1, 18, 22, i == s->digit ? RGB565(248, 232, 168) : RGB565(232, 232, 224));
    gfx_text_big(&font_normal, x + 2, y - 6, d, INK_DARK, INK_DARK_SHADOW);
  }
  gfx_text(&font_small, 18, 108, L("Haut / Bas : changer le chiffre", "Up / Down: change the digit"), RGB565(96, 96, 96), INK_DARK_SHADOW);
  gfx_text(&font_small, 18, 120, L("Gauche / Droite : autre chiffre", "Left / Right: other digit"), RGB565(96, 96, 96), INK_DARK_SHADOW);
  gfx_text(&font_small, 18, 132, L("{OK} : valider   {BACK} : annuler", "{OK}: confirm   {BACK}: cancel"), RGB565(96, 96, 96), INK_DARK_SHADOW);
}

bool seed_entry(uint32_t *seed, bool *quit) {
  SeedView s = {*seed, 0};
  gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_seed, &s);
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) return *quit = true, false;
    if (k & KEY_BACK) return false;
    if (k & KEY_OK) return *seed = s.seed ? s.seed : 1, true;
    int shift = 28 - 4 * s.digit;
    uint32_t d = (s.seed >> shift) & 15;
    if (k & KEY_UP) d = (d + 1) & 15;
    if (k & KEY_DOWN) d = (d + 15) & 15;
    s.seed = (s.seed & ~(15u << shift)) | d << shift;
    if (k & KEY_RIGHT) s.digit = (s.digit + 1) % 8;
    if (k & KEY_LEFT) s.digit = (s.digit + 7) % 8;
    if (k) gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_seed, &s);
  }
}

/* ------------------------------------------------------------------------- Hall of Fame */

typedef struct {
  const Run *run;
  const RunStats *stats;
  const char *title;
  int page, frame;
} Fame;

static void draw_fame(const void *ctx) {
  const Fame *f = ctx;
  ui_background(2);
  ui_title(f->page ? L("BILAN DE LA PARTIE", "GAME SUMMARY") : f->title);
  if (!f->page) { /* l'équipe, avec des étincelles */
    const Team *t = &f->run->team;
    for (int i = 0; i < t->count; i++) {
      int x = 4 + (i % 3) * 70, y = 19 + (i / 3) * 70;
      ui_window(x, y, 68, 69);
      gfx_clip_push(x + 3, y + 3, 62, 47); /* le sprite reste dans son cadre, au-dessus du nom */
      ui_pokemon(t->mon[i].species, t->mon[i].shiny, false, x + 2, y - 8);
      gfx_clip_pop();
      gfx_fill(x + 4, y + 50, 60, 1, RGB565(208, 208, 200));
      const char *n = species_names[t->mon[i].species];
      gfx_text(&font_small, x + 34 - text_width(&font_small, n) / 2, y + 52, n, INK_DARK, INK_DARK_SHADOW);
    }
    for (int k = 0; k < 8; k++) {
      int x = (f->frame * 7 + k * 53) % SCREEN_W, y = 18 + (k * 37 + f->frame * 3) % 140;
      gfx_fill(x - 1, y, 3, 1, RGB565(255, 248, 160)), gfx_fill(x, y - 1, 1, 3, RGB565(255, 248, 160));
    }
    return;
  }
  ui_window(4, 18, 136, 140);
  const char *const diff[3] = {L("Facile", "Easy"), "Normal", L("Difficile", "Hard")};
  char v[12], line[48];
  int y = 22;
#define LINE(label, value)                                                          \
  do {                                                                              \
    gfx_text(&font_small, 12, y, label, INK_DARK, INK_DARK_SHADOW);                 \
    gfx_text(&font_small, 130 - text_width(&font_small, value), y, value, INK_BLUE, INK_DARK_SHADOW); \
    y += 12;                                                                        \
  } while (0)
  LINE(L("Combat atteint", "Battle reached"), num(v, f->run->battle));
  LINE(L("Difficulté", "Difficulty"), diff[run_difficulty(f->run->flags) % 3]);
  LINE(L("Mode", "Mode"), f->run->flags & RUN_NUZLOCKE ? L("Nuzlocke", "Nuzlocke") : "Normal");
  LINE(L("POKéMON mis K.O.", "POKéMON knocked out"), num(v, f->stats->kos));
  LINE(L("PV infligés", "HP dealt"), num(v, f->stats->damage));
  LINE(L("Coups critiques", "Critical hits"), num(v, f->stats->crits));
  LINE(L("Méga-Évolutions", "Mega Evolutions"), num(v, f->stats->megas));
  LINE(L("Chromatiques", "Shinies"), num(v, f->stats->shinies));
  for (int i = 0; i < 8; i++) v[i] = "0123456789ABCDEF"[(f->stats->seed >> (28 - 4 * i)) & 15];
  v[8] = 0;
  LINE(L("Graine", "Seed"), v);
  strcat(strcpy(line, L("Record : combat ", "Record: battle ")), num(v, dex.best));
  gfx_text(&font_small, 12, y + 4, line, RGB565(160, 96, 32), INK_DARK_SHADOW);
#undef LINE
  const Image *im = &trainer_pic[avatar_pic()];
  gfx_image(im, 146 + im->x, 60 + im->y);
}

void hall_of_fame(const Run *run, const RunStats *stats, const char *title, bool *quit) {
  Fame f = {run, stats, title, 0, 0};
  while (!*quit && f.page < 2) {
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_fame, &f);
    uint16_t k = input_wait_frame();
    f.frame++;
    if (k & KEY_HOME) *quit = true;
    if (k & (KEY_OK | KEY_BACK)) f.page++;
  }
}

/* ------------------------------------------------------------------------- cinématique de fin */

typedef struct {
  const Team *team;
  int t;
} Ending;

static void draw_ending(const void *ctx) {
  const Ending *e = ctx;
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB565(16, 16, 48));
  for (int k = 0; k < 40; k++) { /* étoiles qui scintillent */
    int x = (k * 97) % SCREEN_W, y = (k * 61) % SCREEN_H;
    if ((k + e->t / 6) % 3) gfx_fill(x, y, 1, 1, RGB565(248, 248, 200));
  }
  int t = e->t;
  /* 1 : FÉLICITATIONS qui descend ; 2 : l'équipe défile ; 3 : générique */
  int ty = t < 40 ? -20 + t : 20;
  int w = text_width(&font_normal, L("FÉLICITATIONS !", "CONGRATULATIONS!")) * 2;
  gfx_text_big(&font_normal, (SCREEN_W - w) / 2, ty, L("FÉLICITATIONS !", "CONGRATULATIONS!"), RGB565(248, 216, 64), RGB565(120, 64, 16));
  if (t >= 40) {
    int n = e->team->count, i = (t - 40) / 50 % (n ? n : 1), local = (t - 40) % 50;
    int x = SCREEN_W - local * 6;
    if (x < (SCREEN_W - 64) / 2) x = (SCREEN_W - 64) / 2;
    ui_pokemon(e->team->mon[i].species, e->team->mon[i].shiny, false, x, 56);
    const char *name = species_names[e->team->mon[i].species];
    if (local > 25) gfx_text(&font_normal, (SCREEN_W - text_width(&font_normal, name)) / 2, 120, name, INK_LIGHT, RGB565(40, 40, 80));
  }
  if (t >= 40 + 50 * e->team->count) {
    const char *credits = L("Vous êtes le nouveau MAÎTRE !\nPokeRush, un jeu de fan\nMerci d'avoir joué !",
                            "You are the new CHAMPION!\nPokeRush, a fan game\nThanks for playing!");
    gfx_fill(0, 118, SCREEN_W, 42, RGB565(16, 16, 48));
    gfx_text(&font_normal, 20, 120, credits, INK_LIGHT, RGB565(40, 40, 80));
  }
}

void ending(const Team *team, bool *quit) {
  Ending e = {team, 0};
  int total = 40 + 50 * team->count + 120;
  for (; e.t < total && !*quit; e.t++) {
    gfx_render(0, 0, SCREEN_W, SCREEN_H, draw_ending, &e);
    uint16_t k = input_wait_frame();
    if (k & KEY_HOME) *quit = true;
    if (k & KEY_BACK) break;
    if (k & KEY_OK) e.t += 10;
  }
}
