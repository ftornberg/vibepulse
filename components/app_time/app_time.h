#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "torget_app.h"

/*
 * TID: klocka, pomodoro och timer. Panelens orientering väljer läge
 * (time_core.h). Ingen nätverkstask, ingen ljudväg: appen är tyst, och en
 * timer som går ut medan en annan app visas syns först när TID öppnas igen.
 */
extern const torget_app_t time_app;

#ifndef ESP_PLATFORM
/* Bara bänken: samma vägar som en touch och tickern tar, plus en skjutbar
 * klocka så att QA når KLAR utan att vänta 25 minuter. */
void time_app_qa_refresh(void);
void time_app_qa_advance(int64_t us);
void time_app_qa_tap(void);
void time_app_qa_preset(int idx);
void time_app_qa_reset(void);
void time_app_qa_time_unset(bool unset);
#endif

#endif
