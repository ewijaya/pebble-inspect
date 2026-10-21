#include "codec.h"

#include <string.h>

typedef struct {
  uint8_t *out;
  const uint8_t *in;
  size_t at;
} Cursor;

static void put8(Cursor *c, uint8_t value) { c->out[c->at++] = value; }
static void put32(Cursor *c, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    put8(c, (uint8_t)(value >> (8 * i)));
  }
}
static uint8_t get8(Cursor *c) { return c->in[c->at++]; }
static uint32_t get32(Cursor *c) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    value |= (uint32_t)get8(c) << (8 * i);
  }
  return value;
}
static uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static uint8_t observation_flags(const InspectObservation *o) {
  return (o->charging ? 1u : 0u) | (o->plugged ? 2u : 0u) |
         (o->connected ? 4u : 0u);
}

static bool valid(const InspectModel *m) {
  if (m->sample_count > INSPECT_SAMPLE_CAPACITY ||
      m->event_count > INSPECT_EVENT_CAPACITY || m->current.battery_percent > 100) {
    return false;
  }
  if (!m->has_observation) {
    return !m->active && !m->sample_count && !m->event_count && !m->segment &&
           !m->first_observed && !m->last_observed && !m->current.timestamp &&
           !m->current.battery_percent && !observation_flags(&m->current) &&
           !m->connected_seconds && !m->disconnected_seconds && !m->drop_count &&
           !m->session_count && !m->gap_count && !m->time_reset_count;
  }
  if (!m->session_count || !m->segment || !m->sample_count || !m->event_count ||
      m->time_reset_count > m->gap_count ||
      m->current.timestamp != m->last_observed) {
    return false;
  }
  for (uint8_t i = 0; i < m->sample_count; ++i) {
    const InspectSample *s = &m->samples[i];
    if (s->battery_percent > 100 || !s->segment || s->segment > m->segment) {
      return false;
    }
    if (i) {
      const InspectSample *p = &m->samples[i - 1];
      if (s->segment < p->segment ||
          (s->segment == p->segment &&
           (s->timestamp < p->timestamp || s->battery_percent > p->battery_percent ||
            s->charging != p->charging || s->plugged != p->plugged))) {
        return false;
      }
    }
  }
  const InspectSample *last = &m->samples[m->sample_count - 1];
  if (last->segment != m->segment || last->timestamp > m->current.timestamp ||
      last->battery_percent < m->current.battery_percent ||
      last->charging != m->current.charging || last->plugged != m->current.plugged) {
    return false;
  }
  for (uint8_t i = 0; i < m->event_count; ++i) {
    if (m->events[i].kind < INSPECT_EVENT_SESSION_START ||
        m->events[i].kind > INSPECT_EVENT_UNKNOWN_END ||
        m->events[i].battery_percent > 100) {
      return false;
    }
  }
  return true;
}

size_t inspect_codec_encode(const InspectModel *m, uint8_t *out, size_t capacity) {
  if (!m || !out || !valid(m)) {
    return 0;
  }
  size_t size = 8u + 44u + (size_t)m->sample_count * 10u +
                (size_t)m->event_count * 6u + 4u;
  if (size > capacity || size > INSPECT_CODEC_MAX_SIZE) {
    return 0;
  }
  Cursor c = { .out = out, .at = 0 };
  put8(&c, 'P'); put8(&c, 'I'); put8(&c, 'N'); put8(&c, 'S');
  put8(&c, INSPECT_CODEC_VERSION);
  put8(&c, (m->active ? 1u : 0u) | (m->has_observation ? 2u : 0u));
  put8(&c, (uint8_t)size); put8(&c, (uint8_t)(size >> 8));
  put32(&c, m->current.timestamp);
  put8(&c, m->current.battery_percent);
  put8(&c, observation_flags(&m->current));
  put32(&c, m->first_observed); put32(&c, m->last_observed);
  put32(&c, m->connected_seconds); put32(&c, m->disconnected_seconds);
  put32(&c, m->drop_count); put32(&c, m->session_count);
  put32(&c, m->gap_count); put32(&c, m->time_reset_count);
  put32(&c, m->segment);
  put8(&c, m->sample_count); put8(&c, m->event_count);
  for (uint8_t i = 0; i < m->sample_count; ++i) {
    const InspectSample *s = &m->samples[i];
    put32(&c, s->timestamp); put32(&c, s->segment);
    put8(&c, s->battery_percent);
    put8(&c, (s->charging ? 1u : 0u) | (s->plugged ? 2u : 0u));
  }
  for (uint8_t i = 0; i < m->event_count; ++i) {
    put32(&c, m->events[i].timestamp);
    put8(&c, (uint8_t)m->events[i].kind);
    put8(&c, m->events[i].battery_percent);
  }
  uint32_t checksum = crc32(out, c.at);
  put32(&c, checksum);
  return c.at == size ? size : 0;
}

bool inspect_codec_decode(InspectModel *m, const uint8_t *data, size_t size) {
  if (!m || !data || size < 56u || size > INSPECT_CODEC_MAX_SIZE ||
      memcmp(data, "PINS", 4) != 0 || data[4] != INSPECT_CODEC_VERSION ||
      (data[5] & ~3u) || ((size_t)data[6] | ((size_t)data[7] << 8)) != size) {
    return false;
  }
  Cursor trailer = { .in = data, .at = size - 4 };
  if (crc32(data, size - 4) != get32(&trailer)) {
    return false;
  }
  InspectModel decoded;
  inspect_model_init(&decoded);
  Cursor c = { .in = data, .at = 8 };
  decoded.active = (data[5] & 1u) != 0;
  decoded.has_observation = (data[5] & 2u) != 0;
  decoded.current.timestamp = get32(&c);
  decoded.current.battery_percent = get8(&c);
  uint8_t flags = get8(&c);
  if (flags & ~7u) {
    return false;
  }
  decoded.current.charging = (flags & 1u) != 0;
  decoded.current.plugged = (flags & 2u) != 0;
  decoded.current.connected = (flags & 4u) != 0;
  decoded.first_observed = get32(&c); decoded.last_observed = get32(&c);
  decoded.connected_seconds = get32(&c); decoded.disconnected_seconds = get32(&c);
  decoded.drop_count = get32(&c); decoded.session_count = get32(&c);
  decoded.gap_count = get32(&c); decoded.time_reset_count = get32(&c);
  decoded.segment = get32(&c);
  decoded.sample_count = get8(&c); decoded.event_count = get8(&c);
  if (decoded.sample_count > INSPECT_SAMPLE_CAPACITY ||
      decoded.event_count > INSPECT_EVENT_CAPACITY ||
      size != 56u + (size_t)decoded.sample_count * 10u +
                       (size_t)decoded.event_count * 6u) {
    return false;
  }
  for (uint8_t i = 0; i < decoded.sample_count; ++i) {
    InspectSample *s = &decoded.samples[i];
    s->timestamp = get32(&c); s->segment = get32(&c);
    s->battery_percent = get8(&c);
    flags = get8(&c);
    if (flags & ~3u) {
      return false;
    }
    s->charging = (flags & 1u) != 0; s->plugged = (flags & 2u) != 0;
  }
  for (uint8_t i = 0; i < decoded.event_count; ++i) {
    decoded.events[i].timestamp = get32(&c);
    decoded.events[i].kind = (InspectEventKind)get8(&c);
    decoded.events[i].battery_percent = get8(&c);
  }
  if (!valid(&decoded)) {
    return false;
  }
  if (decoded.active) {
    decoded.active = false;
    if (decoded.event_count == INSPECT_EVENT_CAPACITY) {
      memmove(decoded.events, decoded.events + 1,
              (INSPECT_EVENT_CAPACITY - 1) * sizeof(decoded.events[0]));
      decoded.event_count--;
    }
    InspectEvent *e = &decoded.events[decoded.event_count++];
    e->timestamp = decoded.last_observed;
    e->kind = INSPECT_EVENT_UNKNOWN_END;
    e->battery_percent = decoded.current.battery_percent;
  }
  *m = decoded;
  return true;
}
