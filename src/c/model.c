#include "model.h"

#include <limits.h>
#include <string.h>

static uint32_t add_sat(uint32_t a, uint32_t b) {
  return UINT32_MAX - a < b ? UINT32_MAX : a + b;
}

static void next_segment(InspectModel *m) {
  if (m->segment == UINT32_MAX) {
    /* Avoid reusing a segment ID still represented in the history. */
    m->sample_count = 0;
    m->segment = 1;
  } else {
    m->segment++;
  }
}

static void event(InspectModel *m, uint32_t now, InspectEventKind kind) {
  if (m->event_count == INSPECT_EVENT_CAPACITY) {
    memmove(m->events, m->events + 1,
            (INSPECT_EVENT_CAPACITY - 1) * sizeof(m->events[0]));
    m->event_count--;
  }
  InspectEvent *e = &m->events[m->event_count++];
  e->timestamp = now;
  e->kind = kind;
  e->battery_percent = m->current.battery_percent;
}

static void sample(InspectModel *m, bool force) {
  if (!force && m->sample_count) {
    const InspectSample *last = &m->samples[m->sample_count - 1];
    if (last->segment == m->segment &&
        last->battery_percent == m->current.battery_percent &&
        last->charging == m->current.charging &&
        last->plugged == m->current.plugged &&
        m->current.timestamp >= last->timestamp &&
        m->current.timestamp - last->timestamp < INSPECT_SAMPLE_INTERVAL_SECONDS) {
      return;
    }
  }
  if (m->sample_count == INSPECT_SAMPLE_CAPACITY) {
    memmove(m->samples, m->samples + 1,
            (INSPECT_SAMPLE_CAPACITY - 1) * sizeof(m->samples[0]));
    m->sample_count--;
  }
  InspectSample *s = &m->samples[m->sample_count++];
  s->timestamp = m->current.timestamp;
  s->segment = m->segment;
  s->battery_percent = m->current.battery_percent;
  s->charging = m->current.charging;
  s->plugged = m->current.plugged;
}

static bool valid(const InspectObservation *o) {
  return o && o->battery_percent <= 100;
}

static void account(InspectModel *m, uint32_t seconds) {
  if (m->current.connected) {
    m->connected_seconds = add_sat(m->connected_seconds, seconds);
  } else {
    m->disconnected_seconds = add_sat(m->disconnected_seconds, seconds);
  }
}

void inspect_model_init(InspectModel *m) {
  memset(m, 0, sizeof(*m));
}

void inspect_model_start(InspectModel *m, const InspectObservation *o) {
  if (!valid(o)) {
    return;
  }
  if (m->active) {
    inspect_model_observe(m, o);
    return;
  }
  bool had_observation = m->has_observation;
  uint32_t previous_time = m->last_observed;
  if (m->event_count &&
      m->events[m->event_count - 1].kind == INSPECT_EVENT_SESSION_STOP) {
    previous_time = m->events[m->event_count - 1].timestamp;
  }
  m->current = *o;
  if (had_observation) {
    m->gap_count = add_sat(m->gap_count, 1);
    event(m, o->timestamp, INSPECT_EVENT_GAP);
    if (o->timestamp < previous_time) {
      m->time_reset_count = add_sat(m->time_reset_count, 1);
      event(m, o->timestamp, INSPECT_EVENT_TIME_RESET);
    }
  } else {
    m->first_observed = o->timestamp;
  }
  m->last_observed = o->timestamp;
  m->has_observation = true;
  m->active = true;
  m->session_count = add_sat(m->session_count, 1);
  next_segment(m);
  event(m, o->timestamp, INSPECT_EVENT_SESSION_START);
  sample(m, true);
}

void inspect_model_observe(InspectModel *m, const InspectObservation *o) {
  if (!valid(o)) {
    return;
  }
  if (!m->active) {
    inspect_model_start(m, o);
    return;
  }
  InspectObservation previous = m->current;
  bool reset = o->timestamp < previous.timestamp;
  uint32_t elapsed = reset ? 0 : o->timestamp - previous.timestamp;
  bool gap = reset || elapsed > INSPECT_MAX_INTERVAL_SECONDS;
  m->current = *o;
  if (gap) {
    m->gap_count = add_sat(m->gap_count, 1);
    event(m, o->timestamp, INSPECT_EVENT_GAP);
    if (reset) {
      m->time_reset_count = add_sat(m->time_reset_count, 1);
      event(m, o->timestamp, INSPECT_EVENT_TIME_RESET);
    }
  } else {
    /* The interval belongs to the state observed at its beginning. */
    m->current = previous;
    account(m, elapsed);
    m->current = *o;
    if (o->connected != previous.connected) {
      if (!o->connected) {
        m->drop_count = add_sat(m->drop_count, 1);
      }
      event(m, o->timestamp, o->connected ? INSPECT_EVENT_CONNECTED
                                        : INSPECT_EVENT_DISCONNECTED);
    }
  }
  bool battery_break = o->battery_percent > previous.battery_percent ||
                       o->charging != previous.charging ||
                       o->plugged != previous.plugged;
  if (gap || battery_break) {
    next_segment(m);
  }
  if (!gap && (o->charging != previous.charging || o->plugged != previous.plugged)) {
    event(m, o->timestamp, (o->charging || o->plugged) ? INSPECT_EVENT_CHARGING
                                                    : INSPECT_EVENT_UNPLUGGED);
  }
  m->last_observed = o->timestamp;
  sample(m, gap || battery_break);
}

void inspect_model_stop(InspectModel *m, uint32_t timestamp) {
  if (!m->active) {
    return;
  }
  if (timestamp >= m->current.timestamp &&
      timestamp - m->current.timestamp <= INSPECT_MAX_INTERVAL_SECONDS) {
    account(m, timestamp - m->current.timestamp);
  } else {
    m->gap_count = add_sat(m->gap_count, 1);
    event(m, timestamp, INSPECT_EVENT_GAP);
    if (timestamp < m->current.timestamp) {
      m->time_reset_count = add_sat(m->time_reset_count, 1);
      event(m, timestamp, INSPECT_EVENT_TIME_RESET);
    }
  }
  event(m, timestamp, INSPECT_EVENT_SESSION_STOP);
  m->active = false;
}

void inspect_model_estimate(const InspectModel *m, uint32_t now, InspectEstimate *e) {
  memset(e, 0, sizeof(*e));
  e->status = INSPECT_ESTIMATE_INSUFFICIENT;
  if (!m->has_observation) {
    return;
  }
  if (!m->active || now < m->last_observed ||
      now - m->last_observed > INSPECT_ESTIMATE_STALE_SECONDS) {
    e->status = INSPECT_ESTIMATE_STALE;
    return;
  }
  if (m->current.charging || m->current.plugged) {
    e->status = INSPECT_ESTIMATE_CHARGING;
    return;
  }
  const InspectSample *first = NULL;
  uint32_t previous_time = 0;
  uint8_t previous_percent = 100;
  for (uint8_t i = 0; i < m->sample_count; ++i) {
    const InspectSample *s = &m->samples[i];
    if (s->segment != m->segment) {
      continue;
    }
    if (s->charging || s->plugged || s->timestamp > m->current.timestamp ||
        (first && (s->timestamp < previous_time || s->battery_percent > previous_percent))) {
      e->status = INSPECT_ESTIMATE_UNSUPPORTED;
      return;
    }
    if (!first) {
      first = s;
    }
    if (e->sample_count == 0 || s->timestamp != previous_time) {
      e->sample_count++;
    }
    previous_time = s->timestamp;
    previous_percent = s->battery_percent;
  }
  if (!first) {
    return;
  }
  if (m->current.timestamp > previous_time) {
    e->sample_count++;
  }
  e->from_timestamp = first->timestamp;
  e->to_timestamp = m->current.timestamp;
  e->span_seconds = e->to_timestamp - e->from_timestamp;
  if (m->current.battery_percent > first->battery_percent) {
    e->status = INSPECT_ESTIMATE_UNSUPPORTED;
    return;
  }
  e->drop_percent = first->battery_percent - m->current.battery_percent;
  if (e->sample_count < 3 || e->span_seconds < 7200 || e->drop_percent < 3) {
    return;
  }
  uint64_t remaining = (uint64_t)m->current.battery_percent * e->span_seconds /
                       e->drop_percent;
  if (remaining > 30u * 24u * 3600u) {
    e->status = INSPECT_ESTIMATE_UNSUPPORTED;
    return;
  }
  e->remaining_seconds = (uint32_t)remaining;
  e->status = INSPECT_ESTIMATE_READY;
}
