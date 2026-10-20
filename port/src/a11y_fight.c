/*
 * Accessibility: the fight's alerts. Where the rival is (distance and height), the stage's edge, health, and the
 * cues a11y_game.c already had (a transformation or the Ultimate Blast within reach), each said by a sound
 * (gs/a11y_tones.c, gs/a11y.c), by the screen reader, by both or not at all, as the player sets it in the settings
 * window's Accessibility tab (gs/a11y_ui.cpp). All of them are off by default except the two cues, which sound as
 * before; the gauges asked for with the touchpad (or F3 / F4) are two beeps by default, and can be the spoken
 * gauges again.
 *
 *   where     asked for (Select once in a fight, F5): a sonar ping from the rival's direction, as the stick goes
 *             (the fighter's camera yaw, + 0x4A0: "on the right" is "push right"), muffled behind, higher above,
 *             and the words, "Rival ahead, to the right, above, close". Nothing while the rival is hidden or the
 *             player blinded. The log has the angles and, at each fight's end, the sign test of that reading.
 *   distance  a tick that repeats faster as the rival is nearer, higher when the rival is above and lower when
 *             below (Street Fighter 6's distance sound, with the height of a fight in the air); two ticks while
 *             the game itself counts the rival as close (fighter flag 0x13, BtlCharApi_IsClose). By voice: near,
 *             middle distance or far, and above or below, when that changes.
 *   edge      what the game shows as the wall of the stage (src/battle/eft_g.c, EftBound_BuildWall): from
 *             param->range away from the stage's cylinder a hum, faster as the fighter nears it; the ceiling when
 *             the fighter is held under it (flag 0x15, BtlMove_ClampToStage); on the World Tournament stages,
 *             being out over the grass, where landing loses (src/battle/btl_char_coll_b.c, flag 7). By voice,
 *             when it starts.
 *   health    two beeps as each player's health falls under three quarters, a half and a quarter, louder the less
 *             is left; under a quarter, the player's own every three seconds, as the low-health alarm of the
 *             Pokémon games. By voice, the percent.
 *
 * Only the local player's distance and edge are told (online: that player's side; else player 1); health is told
 * for both, in the middle: the local player's beeps clean, the rival's with a buzz (gs/a11y_tones.c). The stereo is
 * kept for where a sound is: it used to be player 1 on the left and player 2 on the right, which a player took for
 * where the two were.
 *
 * It only reads, by the game's own read-only calls and fields; nothing of the game is written or drawn. a11y_game.c
 * calls it once per blank that is not run again after a rollback, and only while a fight is on with no menu over it.
 * Built soft-float: only integers go to gs/.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif

#define A11Y_INTERRUPT 1 /* gs/a11y.c */
extern const char *PortA11y_Tr(const char *label) __attribute__((format_arg(1)));
#define tr(label) PortA11y_Tr(label)
extern void PortA11y_Say(const char *text, int flags);
extern void PortA11y_Note(const char *text);
extern void PortA11y_Cue(int who, int kind);                      /* gs/a11y_tones.c: who 0 yours, 1 the rival's */
extern void PortA11y_Tone(int kind, int who, int pitch, int loud);  /* (in the middle) */
extern void PortA11y_ToneAt(int kind, int az, int pitch, int loud); /* (from a direction, hundredths of a degree) */
extern int Port_Setting(const char *name, int def);               /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern int Port_NetActive(void), Port_NetMe(void);                 /* gs/net.c */

/* The game's read-only calls (src/battle). The object id of a side's active fighter is the side. */
typedef struct { float x, y, z, w; } A11yVec;
/* (BtlCharApi_GetPos is not used: it writes into the caller's vector, which would have to be in the game's memory) */
extern A11yVec GAME_PTR BtlChar_GetPos(void GAME_PTR chr); /* src/battle/btl_char_get.c: the fighter's position */
extern float BtlCharApi_GetRadius(int objId);
extern int BtlCharApi_IsClose(int objId); /* fighter flag 0x13: the rival within the game's close range */
extern int BtlCharApi_GetHp(int objId), BtlCharApi_GetHpMax(int objId);
extern int BtlCharApi_GetChara(int objId), BtlCharApi_GetActiveMember(int objId);
extern int BtlCharApi_GetKi(int objId), BtlSide_IsPoweredUp(int side); /* src/battle/btl_capi_b.c, btl_side.c (only read) */
extern void GAME_PTR BtlChar_FindBySide(int side);
extern int BtlChar_TestFlag(void GAME_PTR chr, unsigned n);
/* src/battle/btl_char_api.c: the fighter's follow camera's yaw (ChrCam.yaw, fighter + 0x4A0), the direction the
   stick's "up" goes (include/battle/btl_char_cam.h: heading = atan2(stickX, -stickY) + yaw). Returns a float: this
   file is built as the game is, soft-float, so it can take one from the game. */
extern float BtlCharApi_GetCamUnk4A0(int objId);
extern int BtlCharApi_IsLockedOn(int objId); /* fighter flag 5: locked on to the rival */
extern int BtlCharApi_IsHidden(int objId);   /* the object is not drawn (a vanish) */
extern int BtlChar_GetFrame(void);           /* the fight's frame (two blanks each) */
extern int BtlStage_IsReady(void);
extern int Battle_GetStage(void);

/* include/battle/stg_a.h: BtlStage and StgBounds (+Y is down: `top` is the smallest y). */
typedef struct {
    float radius, top, bottom;
} A11yStgBounds;
typedef struct {
    char unk0[0x40];
    A11yStgBounds GAME_PTR bounds; /* 0x40 */
} A11yStage;
_Static_assert(offsetof(A11yStage, bounds) == 0x40, "BtlStage");
extern A11yStage GAME_PTR gBtlStage;
/* include/battle/eft_g.h: EftBound and EftBoundParam, the stage wall's drawing. */
typedef struct {
    float range;     /* 0x00: how far from the wall it starts to show */
    float height, unk8;
    float radiusAdd; /* 0x0C: the wall's cylinder is the fighters' limit plus this */
} A11yBoundParam;
typedef struct {
    char unk0[0x218];
    A11yBoundParam GAME_PTR param; /* 0x218 */
} A11yBound;
_Static_assert(offsetof(A11yBound, param) == 0x218, "EftBound");
extern A11yBound GAME_PTR gEftBound;

#define CHR_AT_TOP 0x15   /* held under the stage's ceiling (BtlMove_ClampToStage) */
#define CHR_BLINDED 0x93  /* blinded (a Solar Flare): the lock-on is gone while it lasts (btl_char_action.c) */
#define STAGE_TOURNAMENT_A 4 /* the World Tournament stages, with the ring (BtlChar_IsStage4Or27) */
#define STAGE_TOURNAMENT_B 27
#define RING_HALF 185.0f  /* inferred: btl_char_move.c keeps a fighter in this square there (under flag 2) */
#define INNER_MARGIN 100.0f /* BtlStage_GetInnerRadius: the stage radius less this */
#define MIDDLE 400.0f     /* inferred, to be tuned in a fight: "middle distance" up to this, "far" beyond */
#define HIGH 60.0f        /* inferred: about three fighters' height, "above" / "below" from here */

/* gs/a11y_tones.c */
enum { TONE_DISTANCE, TONE_DISTANCE_CLOSE, TONE_EDGE_WALL, TONE_EDGE_TOP, TONE_EDGE_FLOOR, TONE_RING, TONE_HEALTH, TONE_KI, TONE_WHERE };
#define WHO_YOURS 0
#define WHO_RIVAL 1

/* The alerts and how each is told (a bit each: 1 a sound, 2 the screen reader). */
enum { ALERT_DISTANCE, ALERT_EDGE, ALERT_HEALTH, ALERT_POWER, ALERT_GAUGES, ALERT_MOMENTS, ALERT_NAMES, ALERT_INTRO, ALERTS };
#define BY_SOUND 1
#define BY_VOICE 2
/* (ALERT_MOMENTS: health down to two bars and to one, ki full, max power, told by themselves; ALERT_NAMES: the text
   the game shows in a fight, the techniques' names, said by the reader (a11y_game.c, shown_text); ALERT_INTRO: who
   against whom and where, said while the fight's opening plays (a11y_game.c, fight_intro): both 0 or BY_VOICE) */
static const char *const kKeys[ALERTS] = {"a11y_alert_distance", "a11y_alert_edge", "a11y_alert_health", "a11y_alert_power",
                                          "a11y_gauges", "a11y_alert_moments", "a11y_technique_names", "a11y_fight_intro"};
static const int kDefaults[ALERTS] = {0, 0, 0, BY_SOUND, BY_SOUND, BY_VOICE, BY_VOICE, BY_VOICE};
static int sMode[ALERTS] = {-1, -1, -1, -1, -1, -1, -1, -1};

static int mode(int alert) {
    if (sMode[alert] < 0) {
        int v = Port_Setting(kKeys[alert], kDefaults[alert]), lo = alert == ALERT_GAUGES ? 1 : 0;
        sMode[alert] = v < lo || v > 3 ? kDefaults[alert] : v; /* (the gauges asked for are always told some way) */
    }
    return sMode[alert];
}

/* For the settings window (gs/a11y_ui.cpp): how an alert is told, 0..3 (the gauges: 1..3). */
int PortA11y_AlertMode(int alert) {
    return alert >= 0 && alert < ALERTS ? mode(alert) : 0;
}

/* a11y_game.c, shown_text: 1 when the text the game shows in a fight (the techniques' names) is said. */
int PortA11y_TechniqueNames(void) {
    return mode(ALERT_NAMES) != 0;
}

/* a11y_game.c, fight_intro: 1 when the fight is described while its opening plays. */
int PortA11y_IntroDescription(void) {
    return mode(ALERT_INTRO) != 0;
}

void PortA11y_SetAlertMode(int alert, int value) {
    if (alert >= 0 && alert < ALERTS) {
        sMode[alert] = value < (alert == ALERT_GAUGES ? 1 : 0) ? (alert == ALERT_GAUGES ? 1 : 0) : value > 3 ? 3 : value;
        Port_SettingSave(kKeys[alert], sMode[alert]);
    }
}

static int sBlank; /* blanks of fights told so far */

/* The side whose alerts these are: online the side this copy plays; else player 1's. */
static int local_side(void) {
    return Port_NetActive() ? Port_NetMe() & 1 : 0;
}

/* Whose a side's sound is, for its timbre: the local player's, or the rival's. */
static int who_of(int side) {
    return side == local_side() ? WHO_YOURS : WHO_RIVAL;
}

/* How loud the health beeps are for a side when health falls: from 70 % at full health to all at none (under 70 %
   they were lost in a fight; the loudness says little anyway, the beeps say "health"). */
static int health_loud(int hp, int max) {
    int pct = max > 0 ? hp * 100 / max : 100;
    return 70 + 30 * (100 - (pct < 0 ? 0 : pct > 100 ? 100 : pct)) / 100;
}

/* The gauges asked for (a11y_game.c, fight_gauges): the beeps when they go by sound, as loud as the glossary plays
   them (they were 6 dB softer at full health, under the fight's sound); returns how they are told, so that the
   caller says them when that includes the voice. */
int PortA11y_GaugesQuery(int side) {
    extern int PortA11y_CueVolume(void); /* gs/a11y.c */
    int m = mode(ALERT_GAUGES);

    if (m == BY_SOUND && PortA11y_CueVolume() <= 0) {
        m = BY_VOICE; /* (the cues turned off: the beeps would be silent, and a question gets an answer) */
    }

    if (m & BY_SOUND) {
        PortA11y_Tone(TONE_HEALTH, who_of(side), 0, 100);
    }
    {
        char note[96];
        snprintf(note, sizeof(note), "gauges asked: player %d (%s), told by %s", side + 1,
                 who_of(side) == WHO_YOURS ? "yours" : "the rival's", m == BY_SOUND ? "sound" : m == BY_VOICE ? "voice" : "sound and voice");
        PortA11y_Note(note);
    }
    return m;
}

/* A transformation or the Ultimate Blast within reach (a11y_game.c, fight_cues): kind 0 with `count`
   transformations, kind 1. */
void PortA11y_PowerCue(int side, int kind, int count) {
    char text[128];
    int m = mode(ALERT_POWER);

    if (m & BY_SOUND) {
        PortA11y_Cue(who_of(side), kind);
    }
    if (m & BY_VOICE) {
        if (kind == 0) {
            snprintf(text, sizeof(text), count > 1 ? tr("Player %d can take %d transformations") : tr("Player %d can transform"),
                     side + 1, count);
        } else {
            snprintf(text, sizeof(text), tr("Player %d can fire the Ultimate Blast"), side + 1);
        }
        PortA11y_Say(text, 0);
    }
}

/* ---- where the rival is ---- */

#define RAD_DEG 57.2957795f
#define BEHIND 100   /* degrees from ahead: behind you beyond this */
#define SIDE_FREE 15 /* within this of ahead (or of behind), neither left nor right is said */
#define LEVEL 20     /* degrees of elevation: above or below beyond this */

/* Where the rival is from `me`, in whole degrees and units: az from ahead (the stick's up: the camera's yaw), + to
   the right, +-180 behind; el + above; d the distance. 0 when there is no such fight. The game's +Y is down. */
static int rel(int me, int *az, int *el, int *d) {
    void GAME_PTR chr = BtlChar_FindBySide(me);
    void GAME_PTR foe = BtlChar_FindBySide(me ^ 1);
    A11yVec a, b;
    float dx, dy, dz, h, ang;

    if (chr == NULL || foe == NULL) {
        return 0;
    }
    a = *BtlChar_GetPos(chr);
    b = *BtlChar_GetPos(foe);
    dx = b.x - a.x;
    dz = b.z - a.z;
    dy = a.y - b.y;
    h = sqrtf(dx * dx + dz * dz);
    /* the game's own angle to the rival (btl_char_flag_opp.c, BtlOpp_GetYawToTarget) less the stick's reference */
    ang = (atan2f(dx, dz) - BtlCharApi_GetCamUnk4A0(me)) * RAD_DEG;
    while (ang > 180.0f) {
        ang -= 360.0f;
    }
    while (ang < -180.0f) {
        ang += 360.0f;
    }
    *az = (int)(ang < 0.0f ? ang - 0.5f : ang + 0.5f); /* (rounded here: the C library's lrintf is the host's) */
    ang = atan2f(dy, h) * RAD_DEG;
    *el = (int)(ang < 0.0f ? ang - 0.5f : ang + 0.5f);
    *d = (int)sqrtf(h * h + dy * dy);
    return 1;
}

/* The words for a direction, in English for the log and in the language spoken, joined by commas. */
static void where_words(int az, int el, int d, int close, char *en, char *said, int cap) {
    const char *part[4], *word[4];
    int n = 0, i, a = az < 0 ? -az : az;
    size_t le = 0, ls = 0;

    part[n] = a > BEHIND ? "behind" : "ahead";
    word[n++] = a > BEHIND ? tr("behind") : tr("ahead");
    if (a > SIDE_FREE && a < 180 - SIDE_FREE) {
        part[n] = az > 0 ? "to the right" : "to the left";
        word[n++] = az > 0 ? tr("to the right") : tr("to the left");
    }
    if (el >= LEVEL || el <= -LEVEL) {
        part[n] = el > 0 ? "above" : "below";
        word[n++] = el > 0 ? tr("above") : tr("below");
    }
    part[n] = close ? "close" : d < (int)MIDDLE ? "at middle distance" : "far";
    word[n++] = close ? tr("close") : d < (int)MIDDLE ? tr("at middle distance") : tr("far");
    en[0] = said[0] = '\0';
    for (i = 0; i < n; i++) {
        le += (size_t)snprintf(en + le, le < (size_t)cap ? (size_t)cap - le : 0, "%s%s", i > 0 ? ", " : "", part[i]);
        ls += (size_t)snprintf(said + ls, ls < (size_t)cap ? (size_t)cap - ls : 0, "%s%s", i > 0 ? ", " : "", word[i]);
    }
}

/* Select once in a fight (a11y.c): where the rival is, a sonar ping from there (from the left or the right as the
   stick goes: "it sounds on the right" is "push right"; muffled behind; higher above, lower below; softer far away)
   and the words, "Rival ahead, to the right, above, close". Only what a player who sees has: nothing while the
   rival is hidden (a vanish) or you are blinded. */
void PortA11y_WhereQuery(void) {
    int me = local_side(), az, el, d, close, pitch, loud;
    char en[128], words[256], text[320], note[320];
    void GAME_PTR chr = BtlChar_FindBySide(me);

    if (chr == NULL || !rel(me, &az, &el, &d)) {
        return;
    }
    close = BtlCharApi_IsClose(me) != 0;
    if (BtlChar_TestFlag(chr, CHR_BLINDED)) {
        PortA11y_Say(tr("You are blinded: the rival is not seen"), A11Y_INTERRUPT);
        snprintf(note, sizeof(note), "where asked: az %+d, el %+d, d %d, lock %d; said: blinded", az, el, d, BtlCharApi_IsLockedOn(me) != 0);
        PortA11y_Note(note);
        return;
    }
    if (BtlCharApi_IsHidden(me ^ 1)) {
        PortA11y_Say(tr("Rival out of sight"), A11Y_INTERRUPT);
        snprintf(note, sizeof(note), "where asked: az %+d, el %+d, d %d, lock %d; said: hidden", az, el, d, BtlCharApi_IsLockedOn(me) != 0);
        PortA11y_Note(note);
        return;
    }
    /* the pitch by the elevation: an octave at 60 degrees, nothing within 8; the loudness by the distance: all at 150
       units and nearer, a little over half at 1500 and beyond */
    pitch = el > 8 ? (el - 8) * 12 / 52 : el < -8 ? (el + 8) * 12 / 52 : 0;
    pitch = pitch > 12 ? 12 : pitch < -12 ? -12 : pitch;
    loud = d <= 150 ? 100 : d >= 1500 ? 55 : 100 - (d - 150) * 45 / 1350;
    PortA11y_ToneAt(TONE_WHERE, az * 100, pitch, loud);
    where_words(az, el, d, close, en, words, (int)sizeof(words));
    snprintf(text, sizeof(text), tr("Rival %s"), words);
    PortA11y_Say(text, A11Y_INTERRUPT);
    snprintf(note, sizeof(note), "where asked: az %+d, el %+d, d %d, lock %d; said: Rival %s", az, el, d, BtlCharApi_IsLockedOn(me) != 0, en);
    PortA11y_Note(note);
}

/* The sign test of the direction (the audit's, 2026-10-17): while locked on, the fighter's camera turns towards the
   rival a quarter of the way each frame (btl_char_cam_modes.c, ChrCam_UpdateLockOnYaw), so the angle from the
   stick's reference to the rival stays small. With the yaw or the sign read wrong it would not. Once per fight frame
   while locked on, |az| goes into a count; at the end of a fight its median goes to the log. */
static int sSignCount[181], sSignTotal;

static void sign_sample(int me) {
    static int frame = -1;
    int az, el, d, f = BtlChar_GetFrame();

    if (f == frame || !BtlCharApi_IsLockedOn(me) || BtlCharApi_IsHidden(me ^ 1) || !rel(me, &az, &el, &d)) {
        return;
    }
    frame = f;
    sSignCount[az < 0 ? -az : az]++;
    sSignTotal++;
}

static void sign_report(void) {
    char note[128];
    int i, sum = 0;

    if (sSignTotal < 30) {
        return;
    }
    for (i = 0; i <= 180 && sum * 2 < sSignTotal; i++) {
        sum += sSignCount[i];
    }
    snprintf(note, sizeof(note), "where: sign test: median |az| %d over %d frames locked on (camera yaw, fighter + 0x4A0)", i - 1, sSignTotal);
    PortA11y_Note(note);
    memset(sSignCount, 0, sizeof(sSignCount));
    sSignTotal = 0;
}

/* ---- distance and height ---- */

static void distance(int me, A11yVec *a, A11yVec *b) {
    static int next, zone = -1, heard = -1, held;
    float dx = b->x - a->x, dy = a->y - b->y, dz = b->z - a->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
    int m = mode(ALERT_DISTANCE), close = BtlCharApi_IsClose(me) != 0, gap, pitch, now;
    float x;
    char text[128];

    if (m & BY_SOUND) {
        /* 80 ms apart close by to 1.2 s far away; half an octave up or down at three fighters' height, an octave at most */
        gap = (int)((80.0f + 1120.0f * d / (d + MIDDLE)) * 60.0f / 1000.0f);
        gap = gap < (close ? 9 : 5) ? (close ? 9 : 5) : gap; /* (a tick lasts 60 ms, two 130: never one over another) */
        if (next - sBlank > gap) {
            next = sBlank + gap; /* (nearer now: the next tick comes sooner) */
        }
        if (sBlank >= next) {
            x = 12.0f * dy / (fabsf(dy) + HIGH);
            pitch = (int)(x < 0.0f ? x - 0.5f : x + 0.5f); /* (rounded here: the C library's lrintf is the host's, SSE) */
            PortA11y_Tone(close ? TONE_DISTANCE_CLOSE : TONE_DISTANCE, WHO_YOURS, pitch, 100);
            next = sBlank + gap;
        }
    }
    if (m & BY_VOICE) {
        now = (close ? 0 : d < MIDDLE ? 1 : 2) * 3 + (dy > HIGH ? 1 : dy < -HIGH ? 2 : 0);
        if (now != zone) {
            zone = now;
            held = 0;
        } else if (++held == 30 && now != heard) { /* (half a second the same: not every step at a border) */
            static const char *const kZones[] = {"Rival close", "Rival at middle distance", "Rival far"};
            static const char *const kHeights[] = {"", ", above", ", below"};
            heard = now;
            snprintf(text, sizeof(text), "%s%s", tr(kZones[now / 3]), now % 3 ? tr(kHeights[now % 3]) : "");
            PortA11y_Say(text, 0);
        }
    }
    if (sBlank % 120 == 0) {
        snprintf(text, sizeof(text), "distance %d, height %d%s", (int)d, (int)dy, close ? ", close" : "");
        PortA11y_Note(text);
    }
}

/* ---- the stage's edge ---- */

static void edge(int me, void GAME_PTR chr, A11yVec *a) {
    static int next, said = -1;
    A11yBoundParam GAME_PTR param = gEftBound != NULL ? gEftBound->param : NULL;
    float range = param != NULL && param->range > 0.0f ? param->range : 300.0f;
    float wall, margin, r = sqrtf(a->x * a->x + a->z * a->z);
    int m = mode(ALERT_EDGE), stage = Battle_GetStage(), kind = -1, gap = 0, now;
    char text[96];

    if (!BtlStage_IsReady() || gBtlStage->bounds == NULL) {
        return;
    }
    wall = gBtlStage->bounds->radius - INNER_MARGIN + (param != NULL ? param->radiusAdd : 0.0f);
    margin = wall - r - BtlCharApi_GetRadius(me);
    if ((stage == STAGE_TOURNAMENT_A || stage == STAGE_TOURNAMENT_B) && (fabsf(a->x) > RING_HALF || fabsf(a->z) > RING_HALF)) {
        kind = TONE_RING;
        gap = 60;
    } else if (BtlChar_TestFlag(chr, CHR_AT_TOP)) {
        kind = TONE_EDGE_TOP;
        gap = 30;
    } else if (margin < range) {
        kind = TONE_EDGE_WALL; /* every half second at the edge of the range down to every tenth at the wall */
        gap = 6 + (int)(24.0f * (margin < 0.0f ? 0.0f : margin) / range);
    }
    if ((m & BY_SOUND) && kind >= 0 && sBlank >= next) {
        PortA11y_Tone(kind, WHO_YOURS, 0, 100);
        next = sBlank + gap;
    }
    now = kind;
    if (now != said) {
        said = now;
        if ((m & BY_VOICE) && now >= 0) {
            PortA11y_Say(tr(now == TONE_RING ? "Out of the ring" : now == TONE_EDGE_TOP ? "Ceiling of the stage" : "Edge of the stage"), 0);
        }
        snprintf(text, sizeof(text), "edge: %s (wall %d away)", now < 0 ? "none" : now == TONE_RING ? "out of the ring" :
                 now == TONE_EDGE_TOP ? "ceiling" : "wall", (int)margin);
        PortA11y_Note(text);
    }
}

/* ---- health ---- */

static void health(int me) {
    static int level[2] = {-1, -1}, who[2], next;
    int side, hp, max, pct, now, m = mode(ALERT_HEALTH), id;
    char text[128];

    for (side = 0; side < 2; side++) {
        hp = BtlCharApi_GetHp(side);
        max = BtlCharApi_GetHpMax(side);
        if (max <= 0) {
            continue;
        }
        pct = hp * 100 / max;
        now = pct > 75 ? 3 : pct > 50 ? 2 : pct > 25 ? 1 : 0;
        id = (BtlCharApi_GetActiveMember(side) << 16) | (BtlCharApi_GetChara(side) + 1);
        if (id != who[side]) { /* another fighter (a team's next member, a transformation, the next fight) */
            who[side] = id;
            level[side] = now;
            continue;
        }
        if (now < level[side]) {
            if (m & BY_SOUND) {
                PortA11y_Tone(TONE_HEALTH, who_of(side), 0, health_loud(hp, max));
            }
            if (m & BY_VOICE) {
                if (now == 0) {
                    snprintf(text, sizeof(text), tr("Player %d: health in danger, %d percent"), side + 1, pct < 0 ? 0 : pct);
                } else {
                    snprintf(text, sizeof(text), tr("Player %d: health %d percent"), side + 1, pct);
                }
                PortA11y_Say(text, 0);
            }
            if (side == me) {
                next = sBlank + 180;
            }
        }
        level[side] = now;
        if (side == me && now == 0 && hp > 0 && (m & BY_SOUND) && sBlank >= next) {
            PortA11y_Tone(TONE_HEALTH, who_of(side), 0, health_loud(hp, max)); /* the alarm, every three seconds */
            next = sBlank + 180;
        }
    }
}

/* ---- important moments, told by themselves ---- */

#define HP_BAR 10000          /* health per bar (docs/systems/hud.md; a11y_game.c GAUGE_HP_BAR) */
#define KI_BAR 20000          /* ki per bar, five bars (a11y_game.c GAUGE_KI_BAR) */
#define KI_FULL (5 * KI_BAR)

/* For each player: health down to two bars and then to one, ki full (said again only after it fell under four bars),
   max power. A new fighter (a team's next member, a transformation, the next fight) is taken as it is, unsaid. */
static void moments(void) {
    static int bars[2], kiFull[2], power[2], who[2];
    int side, hp, ki, now, full, up, id, m = mode(ALERT_MOMENTS);
    char text[128];

    for (side = 0; side < 2; side++) {
        if (BtlCharApi_GetHpMax(side) <= 0) {
            continue;
        }
        hp = BtlCharApi_GetHp(side);
        ki = BtlCharApi_GetKi(side);
        now = hp <= 0 ? 0 : hp <= HP_BAR ? 1 : hp <= 2 * HP_BAR ? 2 : 3;
        full = ki >= KI_FULL ? 1 : ki < KI_FULL - KI_BAR ? 0 : kiFull[side];
        up = BtlSide_IsPoweredUp(side) != 0;
        id = (BtlCharApi_GetActiveMember(side) << 16) | (BtlCharApi_GetChara(side) + 1);
        if (id != who[side]) {
            who[side] = id;
            bars[side] = now;
            kiFull[side] = full;
            power[side] = up;
            continue;
        }
        if (now < bars[side] && now >= 1) {
            if (m & BY_SOUND) {
                PortA11y_Tone(TONE_HEALTH, who_of(side), 0, health_loud(hp, BtlCharApi_GetHpMax(side)));
            }
            if (m & BY_VOICE) {
                snprintf(text, sizeof(text), now == 2 ? tr("Player %d: 2 bars of health left") : tr("Player %d: 1 bar of health left"),
                         side + 1);
                PortA11y_Say(text, 0);
            }
        }
        if (full && !kiFull[side]) {
            if (m & BY_SOUND) {
                PortA11y_Tone(TONE_KI, who_of(side), 0, 100);
            }
            if (m & BY_VOICE) {
                snprintf(text, sizeof(text), tr("Player %d: ki full"), side + 1);
                PortA11y_Say(text, 0);
            }
        }
        if (up && !power[side]) {
            if (m & BY_SOUND) {
                PortA11y_Tone(TONE_KI, who_of(side), 5, 100); /* (the ki's notes a fourth higher: C7, E7, G7) */
            }
            if (m & BY_VOICE) {
                snprintf(text, sizeof(text), tr("Player %d: max power"), side + 1);
                PortA11y_Say(text, 0);
            }
        }
        bars[side] = now;
        kiFull[side] = full;
        power[side] = up;
    }
}

/* Once per blank from a11y_game.c: `fight` while a fight is on with no menu over it. */
void PortA11y_FightAlerts(int fight) {
    static int was;
    A11yVec a, b;
    void GAME_PTR chr;
    void GAME_PTR foe;
    int me;

    if (!fight || BtlChar_FindBySide(0) == NULL || BtlChar_FindBySide(1) == NULL) {
        if (was) {
            sign_report();
        }
        was = 0;
        return;
    }
    if (!was) {
        char note[160];
        was = 1;
        sBlank += 600; /* (whatever was waiting from the last fight is due) */
        snprintf(note, sizeof(note), "fight alerts: distance %d, edge %d, health %d, moments %d, power %d (0 off, 1 sound, 2 voice, 3 both)",
                 mode(ALERT_DISTANCE), mode(ALERT_EDGE), mode(ALERT_HEALTH), mode(ALERT_MOMENTS), mode(ALERT_POWER));
        PortA11y_Note(note);
    }
    sBlank++;
    me = local_side();
    chr = BtlChar_FindBySide(me);
    foe = BtlChar_FindBySide(me ^ 1);
    sign_sample(me);
    if ((mode(ALERT_DISTANCE) | mode(ALERT_EDGE)) != 0) {
        a = *BtlChar_GetPos(chr);
        b = *BtlChar_GetPos(foe);
        if (mode(ALERT_DISTANCE)) {
            distance(me, &a, &b);
        }
        if (mode(ALERT_EDGE)) {
            edge(me, chr, &a);
        }
    }
    if (mode(ALERT_HEALTH)) {
        health(me);
    }
    if (mode(ALERT_MOMENTS)) {
        moments();
    }
}
