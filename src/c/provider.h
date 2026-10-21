#pragma once
#include <pebble.h>
#include "model.h"

typedef struct {
  InspectObservation observation;
  bool pebblekit_connected;
  uint32_t heap_used;
  uint32_t heap_free;
  uint32_t heap_peak;
  WatchInfoModel model;
  WatchInfoVersion firmware;
} InspectSnapshot;

void inspect_provider_read(InspectSnapshot *snapshot);
const char *inspect_provider_model_name(WatchInfoModel model);
