#ifndef INSPECT_MODEL_H
#define INSPECT_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INSPECT_SAMPLE_CAPACITY 48
#define INSPECT_EVENT_CAPACITY 24
#define INSPECT_MAX_INTERVAL_SECONDS 300u
#define INSPECT_SAMPLE_INTERVAL_SECONDS 900u
#define INSPECT_ESTIMATE_STALE_SECONDS INSPECT_MAX_INTERVAL_SECONDS

typedef struct {
  uint32_t timestamp;
  uint8_t battery_percent;
  bool charging;
  bool plugged;
  bool connected;
} InspectObservation;

typedef struct {
  uint32_t timestamp;
  uint32_t segment;
  uint8_t battery_percent;
  bool charging;
  bool plugged;
} InspectSample;

typedef enum {
  INSPECT_EVENT_SESSION_START = 1,
  INSPECT_EVENT_SESSION_STOP,
  INSPECT_EVENT_CONNECTED,
  INSPECT_EVENT_DISCONNECTED,
  INSPECT_EVENT_GAP,
  INSPECT_EVENT_TIME_RESET,
  INSPECT_EVENT_CHARGING,
  INSPECT_EVENT_UNPLUGGED,
  INSPECT_EVENT_UNKNOWN_END
} InspectEventKind;

typedef struct {
  uint32_t timestamp;
  InspectEventKind kind;
  uint8_t battery_percent;
} InspectEvent;

typedef struct {
  bool active;
  bool has_observation;
  InspectObservation current;
  uint32_t first_observed;
  uint32_t last_observed;
  uint32_t connected_seconds;
  uint32_t disconnected_seconds;
  uint32_t drop_count;
  uint32_t session_count;
  uint32_t gap_count;
  uint32_t time_reset_count;
  uint32_t segment;
  uint8_t sample_count;
  uint8_t event_count;
  /* Oldest first. Bounded histories deliberately overwrite oldest evidence. */
  InspectSample samples[INSPECT_SAMPLE_CAPACITY];
  InspectEvent events[INSPECT_EVENT_CAPACITY];
} InspectModel;

typedef enum {
  INSPECT_ESTIMATE_INSUFFICIENT = 0,
  INSPECT_ESTIMATE_READY,
  INSPECT_ESTIMATE_CHARGING,
  INSPECT_ESTIMATE_STALE,
  INSPECT_ESTIMATE_UNSUPPORTED
} InspectEstimateStatus;

typedef struct {
  InspectEstimateStatus status;
  uint32_t remaining_seconds;
  uint32_t span_seconds;
  uint32_t from_timestamp;
  uint32_t to_timestamp;
  uint8_t sample_count;
  uint8_t drop_percent;
} InspectEstimate;

void inspect_model_init(InspectModel *model);
/* Start never infers transitions or elapsed durations from an earlier session. */
void inspect_model_start(InspectModel *model, const InspectObservation *observation);
void inspect_model_observe(InspectModel *model, const InspectObservation *observation);
void inspect_model_stop(InspectModel *model, uint32_t timestamp);
void inspect_model_estimate(const InspectModel *model, uint32_t now,
                            InspectEstimate *estimate);

#endif
