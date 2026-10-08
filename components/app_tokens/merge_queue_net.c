/*
 * Merge queue LAN feed: pull requests that local agent-team orchestrators
 * have reviewed and that wait for a person (tokenserver /api/merge-queue).
 * The tokenserver polls the orchestrators; the panel only reads one flat
 * payload. LAN only — the relays never carry pull request titles.
 */
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "app_tokens.h"
#include "app_tokens_config.h"
#include "merge_queue_parse.h"
#include "poll_backoff_policy.h"
#include "torget.h"
#include "ext_ram.h"
#include "torget_http.h"

#if TK_MERGE_QUEUE

static const char *TAG = "merge-net";

/* No secrets.h change needed: the endpoint lives on the same tokenserver as
 * the quotas, so a configured base URL is enough, and an advertised
 * tokenserver works without any. TK_MERGE_QUEUE_URL overrides both. */
#if defined(TK_MERGE_QUEUE_URL)
#define MERGE_QUEUE_URL TK_MERGE_QUEUE_URL
#elif defined(TK_VIBEPULSE_BASE_URL)
#define MERGE_QUEUE_URL TK_VIBEPULSE_BASE_URL "/api/merge-queue"
#else
#define MERGE_QUEUE_URL NULL
#endif

/* The tokenserver polls the orchestrators once a minute; asking it more
 * often only re-reads the same answer. */
#define MERGE_FETCH_EVERY_MS 60000
/* A tokenserver without the endpoint (older host) answers 404 forever:
 * back off like the other optional feeds instead of asking every minute. */
#define MERGE_FETCH_MAX_MS 600000
/* The tokenserver keeps the whole payload within 3840 bytes (it drops titles
 * first; merge_queue.PUBLIC_PAYLOAD_BYTES), so this holds the fullest queue
 * with every title escaped as \uXXXX. */
#define MERGE_BODY_MAX 4096
/* cJSON's recursion, the HTTP client and the card render (it runs here,
 * under the UI lock) share this. The payload itself is static. */
#define MERGE_TASK_STACK_BYTES 6144

static void merge_queue_net_task(void *arg) {
  (void)arg;
  static EXT_RAM_BSS_ATTR char body[MERGE_BODY_MAX];
  size_t len;

  tk_poll_backoff backoff;
  tk_poll_backoff_init(&backoff, MERGE_FETCH_EVERY_MS, MERGE_FETCH_MAX_MS);

  torget_net_wait();
  /* Clear of quotas (10 s), Max Tracker (15 s) and GitHub (20 s). */
  vTaskDelay(pdMS_TO_TICKS(25000));

  for (;;) {
    static EXT_RAM_BSS_ATTR tk_merge_queue queue;
    bool fetched = torget_http_get_service("/api/merge-queue", MERGE_QUEUE_URL, NULL,
                                           body, sizeof body, &len) &&
                   tk_merge_queue_parse(body, len, &queue);
    if (fetched) {
      torget_ui_lock();
      tokens_apply_merge_queue(&queue);
      torget_ui_unlock();
    }
    /* No orchestrator configured on the host: nothing will change until
     * someone runs `vibepulse_setup.py merge-queue add`, so ask rarely. */
    bool switched_off = fetched && !queue.enabled;
    uint32_t streak_before = backoff.streak;
    if (tk_poll_backoff_note(&backoff, fetched)) {
      if (fetched) {
        ESP_LOGI(TAG, "Merge-kön svarar igen efter %" PRIu32 " missar",
                 streak_before);
      } else {
        ESP_LOGW(TAG, "Merge-kön: %" PRIu32 " missar i rad — hämtar var %"
                      PRIu32 " s (äldre tokenserver utan /api/merge-queue?)",
                 backoff.streak, tk_poll_backoff_delay_ms(&backoff) / 1000);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(switched_off ? MERGE_FETCH_MAX_MS
                                          : tk_poll_backoff_delay_ms(&backoff)));
  }
}

void tokens_merge_queue_net_start(void) {
  if (xTaskCreate(merge_queue_net_task, "merge-queue", MERGE_TASK_STACK_BYTES,
                  NULL, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Merge-kö-tasken kunde inte starta");
  }
}

#else /* !TK_MERGE_QUEUE */

void tokens_merge_queue_net_start(void) {}

#endif
