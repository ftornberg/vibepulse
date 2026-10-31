#ifndef TORGET_CORNER_PROBE_H
#define TORGET_CORNER_PROBE_H

/*
 * Hörnsonden: ett mätmönster för glasets rundade hörn. I varje hörn ritas
 * kvartsbågar som tangerar båda kanterna, med radie 40, 60, 80, 100 och
 * 120 px och siffran innanför bågen. Ramen (bezeln) är själv en sådan
 * kvartsbåge med okänd radie R: bågar med mindre radie hugger in i ramen
 * nära tangentpunkterna och syns inte hela, bågar med större radie går
 * fria. Den MINSTA radie vars båge syns hel i ett hörn är alltså R för det
 * hörnet — avläses av ägaren på glaset, ett tal per hörn.
 *
 * Sonden lever på topplagret och är ett tillfälligt mätverktyg, inte en
 * produktyta. Kallas under torget_ui_lock().
 */
void torget_corner_probe_show(void);
void torget_corner_probe_hide(void);

#endif
