#ifndef TORGET_TIME_CORE_H
#define TORGET_TIME_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../platform/audio_cue.h"

/*
 * TID: den rena kärnan. Ingen LVGL, ingen systemklocka — allt tar `now_us`
 * (enhetens monotona mikrosekunder) som argument, så samma kod går att testa
 * på värden. Timrarna lagrar en deadline och räknar `kvar = deadline - nu`;
 * inget behöver ticka för att förbli korrekt, och en lång frånvaro (appen
 * dold, panelen vänd) ger rätt svar vid nästa tick.
 */

/* ---- läge ur rotationen ------------------------------------------------- */

typedef enum {
  TG_TIME_MODE_CLOCK = 0,
  TG_TIME_MODE_POMODORO,
  TG_TIME_MODE_MEETING,
} tg_time_mode;

/* Kvartsvarv från boot som auto-rotationen mäter (torget_orientation()).
 * UPPMÄTT på ägarens 2.16-panel 2026-09-26: knapparna uppåt ger 0, åt vänster
 * 1, nedåt 2 och åt höger 3. Ägaren vill ha klockan med knapparna åt höger
 * (panelens viloläge, laddsladden åt rätt håll), möteskostnaden med knapparna
 * uppåt (där timern satt fram till 2026-11-02) och pomodoro med knapparna åt vänster. Rotation 2 (knappkanten nedåt, där
 * bilden är förvrängd i alla appar) har inget eget läge och behåller det
 * senaste. */
#ifndef TG_TIME_ROT_CLOCK
#define TG_TIME_ROT_CLOCK 3
#endif
#ifndef TG_TIME_ROT_POMODORO
#define TG_TIME_ROT_POMODORO 1
#endif
#ifndef TG_TIME_ROT_MEETING
#define TG_TIME_ROT_MEETING 0
#endif

/* Läget för en rotation. Allt utanför de tre kända (2, -1 utan IMU, 4, ...)
 * ger `last` — aldrig ett hopp till ett annat läge. */
tg_time_mode tg_time_mode_for(int rot, tg_time_mode last);

/* ---- en deadline-timer -------------------------------------------------- */

typedef enum {
  TG_TIMER_IDLE = 0,
  TG_TIMER_RUNNING,
  TG_TIMER_PAUSED,
  TG_TIMER_DONE,
} tg_timer_state;

typedef struct {
  tg_timer_state state;
  int64_t deadline_us;   /* RUNNING: monoton sluttid */
  int64_t remaining_us;  /* PAUSED: det som var kvar */
  int64_t total_us;      /* den startade längden */
} tg_timer;

void tg_timer_init(tg_timer *t);
/* Bara från IDLE och med duration > 0; annars false och ingen ändring. */
bool tg_timer_start(tg_timer *t, int64_t now_us, int64_t duration_us);
/* RUNNING -> DONE när now_us >= deadline. Billig att kalla ofta. */
void tg_timer_tick(tg_timer *t, int64_t now_us);
/* Som tg_timer_tick, men svarar true om DEN HÄR tickningen flyttade RUNNING ->
 * DONE. Ett tryck som landar just efter en deadline ska visa KLAR först och
 * aldrig kvittera eller avbryta en markering användaren inte hunnit se. */
bool tg_timer_tick_expired(tg_timer *t, int64_t now_us);
/* RUNNING <-> PAUSED. Tickar först: en körning som redan gått ut blir DONE,
 * aldrig PAUSED med negativ tid. IDLE och DONE ändras inte. */
void tg_timer_toggle(tg_timer *t, int64_t now_us);
void tg_timer_cancel(tg_timer *t); /* alla lägen -> IDLE */
/* Aldrig negativ. IDLE och DONE ger 0. */
int64_t tg_timer_remaining_us(const tg_timer *t, int64_t now_us);

/* ---- pomodoro ----------------------------------------------------------- */

/* 25 fokus / 5 paus / 25 / 5 / 25 / 5 / 25 / 15 lång paus, därefter om. */
#define TG_POMO_STEPS 8

typedef enum {
  TG_POMO_FOCUS = 0,
  TG_POMO_SHORT_BREAK,
  TG_POMO_LONG_BREAK,
} tg_pomo_phase;

typedef struct {
  tg_timer timer;
  int step; /* 0..TG_POMO_STEPS-1 */
} tg_pomo;

void tg_pomo_init(tg_pomo *p);
tg_pomo_phase tg_pomo_phase_of(const tg_pomo *p);
int64_t tg_pomo_phase_us(const tg_pomo *p);
/* IDLE: starta fasen. RUNNING/PAUSED: växla. DONE: kvittera och gå till nästa
 * fas som väntar i IDLE (ingen automatstart — appen är tyst i steg 1). */
void tg_pomo_tap(tg_pomo *p, int64_t now_us);
void tg_pomo_reset(tg_pomo *p); /* -> steg 0, IDLE */

/* ---- möteskostnad ------------------------------------------------------- */

/* Ersatte timern med förval (20/40/50) 2026-11-02. Räknar UPPÅT: förfluten
 * tid gånger deltagare gånger timkostnad per person. Samma deadline-tanke som
 * timrarna: RUNNING lagrar när mötet (omräknat) började, så inget behöver
 * ticka för att förbli korrekt. Antal och timkostnad ändras bara i vila — ett
 * möte som pågår räknar med det det startade med. */
#define TG_MEETING_PEOPLE_MIN 1
#define TG_MEETING_PEOPLE_MAX 99
#define TG_MEETING_PEOPLE_DEFAULT 6
#define TG_MEETING_RATE_MIN 100
#define TG_MEETING_RATE_MAX 5000
#define TG_MEETING_RATE_STEP 100
#define TG_MEETING_RATE_DEFAULT 900
/* Tio timmar: därefter står mötet pausat. Håller den största summan
 * (99 x 5 000 kr/h x 10 h = 4 950 000) inom sju siffror, som ryms i ringen. */
#define TG_MEETING_MAX_US (10LL * 3600LL * 1000000LL)

typedef enum {
  TG_MEETING_IDLE = 0,
  TG_MEETING_RUNNING,
  TG_MEETING_PAUSED,
} tg_meeting_state;

typedef struct {
  tg_meeting_state state;
  int people;          /* TG_MEETING_PEOPLE_MIN..MAX */
  int rate;            /* kr per person och timme, i steg om RATE_STEP */
  int64_t start_us;    /* RUNNING: nu - förfluten tid */
  int64_t elapsed_us;  /* PAUSED: förfluten tid */
} tg_meeting;

/* Värden utanför området klipps; rate rundas till närmaste steg. */
void tg_meeting_init(tg_meeting *m, int people, int rate);
/* Bara i vila; svarar true om värdet ändrades (klipps vid gränserna). */
bool tg_meeting_step_people(tg_meeting *m, int delta);
bool tg_meeting_step_rate(tg_meeting *m, int delta_steps);
/* RUNNING som nått TG_MEETING_MAX_US -> PAUSED vid maxtiden. */
void tg_meeting_tick(tg_meeting *m, int64_t now_us);
/* IDLE -> RUNNING, RUNNING <-> PAUSED. Tickar först. */
void tg_meeting_tap(tg_meeting *m, int64_t now_us);
void tg_meeting_reset(tg_meeting *m); /* -> IDLE, behåller antal och kostnad */
/* 0..TG_MEETING_MAX_US; 0 i vila. */
int64_t tg_meeting_elapsed_us(const tg_meeting *m, int64_t now_us);
int64_t tg_meeting_hourly_kr(const tg_meeting *m);
/* Hela kronor, avrundat NEDÅT: aldrig en krona som inte hunnit kosta. */
int64_t tg_meeting_cost_kr(const tg_meeting *m, int64_t now_us);
/* Ringen: ett varv per timme, andel av den pågående timmen i promille.
 * -1 i vila. */
int tg_meeting_ring(const tg_meeting *m, int64_t now_us);

/* Kronor med mellanslag var tredje siffra: "107 917". Negativt ger "0". */
void tg_meeting_kr_text(int64_t kr, char *out, size_t cap);
/* Förfluten tid: "MM:SS" under en timme, annars "H:MM:SS". Sekunder rundas
 * NEDÅT (tiden räknas upp och ska aldrig ligga före). */
void tg_meeting_elapsed_text(int64_t elapsed_us, char *out, size_t cap);

/* Tickar pomodoron och svarar TG_TIME_EXPIRED_POMODORO om DEN HÄR tickningen
 * flyttade den från RUNNING till DONE (0 annars). Den ENDA tickvägen i appen,
 * så att en signal varken missas eller spelas två gånger. NULL-säker.
 * Möteskostnaden har ingen slutpunkt och låter aldrig. */
#define TG_TIME_EXPIRED_POMODORO 1
int tg_time_advance(tg_pomo *p, int64_t now_us);

/* Signalen för det tg_time_advance just svarade; -1 när inget gick ut. En
 * paus som är slut låter annorlunda än en fokusfas. Kallas FÖRE
 * kvitteringen: p står då kvar på fasen som tog slut. */
int tg_time_cue_for(int expired, const tg_pomo *p);

/* ---- text --------------------------------------------------------------- */

/* Samma regel som main.c: ett år före 2026 är en osatt klocka. */
#define TG_CLOCK_VALID_EPOCH_S 1767225600LL
bool tg_time_clock_valid(int64_t epoch_s);

/* Platshållaren för en okänd tid. Stora siffrefonten plex_num_118 saknar ASCII-
 * bindestreck (0x2D) men bär tankstreck U+2013 (och kolon): en "--:--" klarar
 * värdtestet och ritas som tomma rutor på glaset. Ingen kommentar på define-raden
 * (vakten i test_time_app_wiring.py läser den). */
#define TG_TIME_NO_VALUE "\xE2\x80\x93\xE2\x80\x93:\xE2\x80\x93\xE2\x80\x93"

/* "HH:MM", eller TG_TIME_NO_VALUE när klockan är ogiltig eller siffrorna utanför
 * området — aldrig "00:00" som gissning. */
void tg_time_clock_text(bool valid, int hour, int minute, char *out, size_t cap);

/* "MM:SS" med sekunder rundade UPP, så en gående timer aldrig visar 00:00
 * medan tid återstår. Negativt ger 00:00, över 99:59 klipps till 99:59. */
void tg_time_mmss_text(int64_t remaining_us, char *out, size_t cap);

/* ---- ringen (specens "Ring"-avsnitt) ------------------------------------- */

/* En ringbåge i promille av varvet, medurs från klockan 12: start..end.
 * end == -1 döljer ringen. start == end är en tom ring. */
typedef struct {
  int start;
  int end;
} tg_ring_arc;

/* Sekundringen på klockan rör sig ALLTID medurs (ägaren 2026-09-26): udda
 * minuter växer den fyllda delen från 12, jämna minuter växer den tomma, så
 * ringen varken hoppar från full till tom eller går baklänges. En kant flyttar
 * exakt 1/60 varv per sekund. Dold (end -1) vid ogiltig klocka eller
 * minut/sekund utanför 0..59. */
tg_ring_arc tg_ring_seconds(bool valid, int minute, int second);

/* Kvarvarande andel av en pomodorofas i promille, avrundad UPP så att en gående
 * timer aldrig visar en tom ring. -1 utom när timern går eller är pausad. */
int tg_ring_remaining(const tg_timer *t, int64_t now_us);

#endif
