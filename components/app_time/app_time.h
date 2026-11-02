#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "torget_app.h"

/*
 * TID: klocka, pomodoro och möteskostnad. Panelens orientering väljer läge
 * (time_core.h). Ingen nätverkstask. Ljud bara när en pomodorofas tar slut
 * (TK_TID_SOUND), också medan en annan app visas.
 */
extern const torget_app_t time_app;

#ifndef ESP_PLATFORM
/* Bara bänken: samma vägar som en touch och tickern tar, plus en skjutbar
 * klocka så att QA når KLAR utan att vänta 25 minuter. */
void time_app_qa_refresh(void);
void time_app_qa_advance(int64_t us);
void time_app_qa_tap(void);
/* Samma väg som ett tryck på en stegare: which 0 = deltagare, 1 = kr/h. */
void time_app_qa_step(int which, int dir);
void time_app_qa_reset(void);
void time_app_qa_time_unset(bool unset);
/* Lås klockans väggtid (h < 0 släpper låset) så att klockbilden blir
 * reproducerbar i dokumentationen. */
void time_app_qa_clock(int hour, int minute, int second);
/* Samma väg som ett tryck på högtalarsymbolen. */
void time_app_qa_speaker(void);
#endif

#endif
