/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"
#include "common/utils/assertions.h"
#include "isac_dump.h"

#define ISAC_DUMP_SLOTS 8

typedef struct {
  isac_record_header_t hdr;
  cf_t *payload;
  size_t cap;
  bool full;
} isac_slot_t;

struct isac_dump_s {
  FILE *f;
  char *path;
  double min_interval_ms;
  uint32_t max_records;
  uint32_t index; // records offered
  uint32_t written, dropped;
  double last_ms; // time of the last accepted record
  isac_slot_t slots[ISAC_DUMP_SLOTS];
  int head, tail; // producer writes at head, writer thread reads at tail
  bool stop;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  pthread_t thread;
};

static void *isac_writer(void *arg)
{
  isac_dump_t *d = arg;
  pthread_mutex_lock(&d->mutex);
  while (true) {
    while (!d->slots[d->tail].full && !d->stop)
      pthread_cond_wait(&d->cond, &d->mutex);
    if (!d->slots[d->tail].full)
      break; // stopping, and nothing left
    isac_slot_t *s = &d->slots[d->tail];
    pthread_mutex_unlock(&d->mutex);
    const bool ok = fwrite(&s->hdr, sizeof(s->hdr), 1, d->f) == 1 && fwrite(s->payload, s->hdr.payload_bytes, 1, d->f) == 1;
    fflush(d->f);
    pthread_mutex_lock(&d->mutex);
    if (!ok)
      LOG_E(PHY, "ISAC dump: write to %s failed: %s\n", d->path, strerror(errno));
    else
      d->written++;
    s->full = false;
    d->tail = (d->tail + 1) % ISAC_DUMP_SLOTS;
  }
  pthread_mutex_unlock(&d->mutex);
  return NULL;
}

isac_dump_t *isac_dump_init(void)
{
  if (!config_get_if())
    return NULL;
  char *path = NULL;
  double min_interval_ms = 50;
  uint32_t max_records = 0;
  paramdef_t params[] = {
      {"dump_file", "file receiving the float SRS channel estimates\n", 0, .strptr = &path, .defstrval = NULL, TYPE_STRING, 0, NULL},
      {"min_interval_ms", "shortest time between two records\n", 0, .dblptr = &min_interval_ms, .defdblval = 50, TYPE_DOUBLE, 0, NULL},
      {"max_records", "stop after this many records, 0 for no limit\n", 0, .uptr = &max_records, .defuintval = 0, TYPE_UINT, 0, NULL},
  };
  config_get(config_get_if(), params, sizeofArray(params), "isac");
  if (!path || !path[0])
    return NULL;
  isac_dump_t *d = calloc_or_fail(1, sizeof(*d));
  d->path = strdup(path);
  d->f = fopen(path, "wb");
  AssertFatal(d->f, "ISAC dump: cannot open %s: %s\n", path, strerror(errno));
  d->min_interval_ms = min_interval_ms;
  d->max_records = max_records;
  d->last_ms = -1e30;
  pthread_mutex_init(&d->mutex, NULL);
  pthread_cond_init(&d->cond, NULL);
  AssertFatal(pthread_create(&d->thread, NULL, isac_writer, d) == 0, "ISAC dump: cannot start the writer thread\n");
  LOG_I(PHY, "ISAC dump of the SRS channel estimates to %s, one record every %.1f ms at most\n", path, min_interval_ms);
  return d;
}

void isac_dump_record(isac_dump_t *d, const isac_record_header_t *hdr, const cf_t *h)
{
  if (!d)
    return;
  // thin out on the simulated (radio) time, not the wall clock: rfsim runs slower than real time
  const double t_ms = hdr->slot_timestamp / hdr->fs_hz * 1e3;
  if (t_ms - d->last_ms < d->min_interval_ms)
    return;
  if (d->max_records && d->index >= d->max_records)
    return;
  d->last_ms = t_ms;
  pthread_mutex_lock(&d->mutex);
  isac_slot_t *s = &d->slots[d->head];
  const uint32_t index = d->index++;
  if (s->full) {
    d->dropped++;
    pthread_mutex_unlock(&d->mutex);
    LOG_W(PHY, "ISAC dump: writer behind, record %u dropped (%u so far)\n", index, d->dropped);
    return;
  }
  pthread_mutex_unlock(&d->mutex);
  // the slot is ours until marked full
  if (s->cap < hdr->payload_bytes) {
    free(s->payload);
    s->payload = malloc_or_fail(hdr->payload_bytes);
    s->cap = hdr->payload_bytes;
  }
  s->hdr = *hdr;
  memcpy(s->hdr.magic, ISAC_RECORD_MAGIC, 4);
  s->hdr.version = ISAC_RECORD_VERSION;
  s->hdr.header_bytes = sizeof(isac_record_header_t);
  s->hdr.record_index = index;
  memcpy(s->payload, h, hdr->payload_bytes);
  pthread_mutex_lock(&d->mutex);
  s->full = true;
  d->head = (d->head + 1) % ISAC_DUMP_SLOTS;
  pthread_cond_signal(&d->cond);
  pthread_mutex_unlock(&d->mutex);
}

void isac_dump_end(isac_dump_t *d)
{
  if (!d)
    return;
  pthread_mutex_lock(&d->mutex);
  d->stop = true;
  pthread_cond_signal(&d->cond);
  pthread_mutex_unlock(&d->mutex);
  pthread_join(d->thread, NULL);
  fclose(d->f);
  LOG_I(PHY, "ISAC dump: %u records written to %s, %u dropped\n", d->written, d->path, d->dropped);
  for (int i = 0; i < ISAC_DUMP_SLOTS; i++)
    free(d->slots[i].payload);
  pthread_mutex_destroy(&d->mutex);
  pthread_cond_destroy(&d->cond);
  free(d->path);
  free(d);
}
