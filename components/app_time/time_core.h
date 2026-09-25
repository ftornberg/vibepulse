#ifndef TORGET_TIME_CORE_H
#define TORGET_TIME_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
  TG_TIME_MODE_TIMER,
} tg_time_mode;

/* Kvartsvarv från boot som auto-rotationen mäter (torget_orientation()).
 * Värdena är PROVISORISKA tills de mätts på enheten: ett fel visar sig som
 * konstant fel läge och rättas här (eller med -D vid bygget). Rotation 2
 * (knappkanten nedåt) har inget eget läge och behåller det senaste. */
#ifndef TG_TIME_ROT_CLOCK
#define TG_TIME_ROT_CLOCK 0
#endif
#ifndef TG_TIME_ROT_POMODORO
#define TG_TIME_ROT_POMODORO 1
#endif
#ifndef TG_TIME_ROT_TIMER
#define TG_TIME_ROT_TIMER 3
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

/* ---- vanlig timer med förval -------------------------------------------- */

#define TG_COUNTDOWN_PRESETS 3

typedef struct {
  tg_timer timer;
  int preset; /* senast startade förval, 0..TG_COUNTDOWN_PRESETS-1 */
} tg_countdown;

void tg_countdown_init(tg_countdown *c);
/* Förvalen är 20, 40 och 50 minuter; utanför området 0. */
int tg_countdown_preset_minutes(int preset);
/* Bara från IDLE och med ett giltigt förval. */
bool tg_countdown_start(tg_countdown *c, int preset, int64_t now_us);
/* RUNNING/PAUSED: växla. DONE: kvittera -> IDLE. IDLE: ingenting. */
void tg_countdown_tap(tg_countdown *c, int64_t now_us);
void tg_countdown_reset(tg_countdown *c); /* -> IDLE */

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

/* Sekundringen på klockan: aldrig tom medan tiden är giltig ((sekund+1)/60,
 * full vid :59). -1 döljer ringen (ogiltig klocka eller sekund utanför 0..59). */
int tg_ring_seconds(bool valid, int second);

/* Kvarvarande andel av en löpning i promille, avrundad UPP så att en gående
 * timer aldrig visar en tom ring. -1 utom när timern går eller är pausad. */
int tg_ring_remaining(const tg_timer *t, int64_t now_us);

#endif
