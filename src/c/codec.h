#ifndef INSPECT_CODEC_H
#define INSPECT_CODEC_H

#include "model.h"

#define INSPECT_CODEC_VERSION 1u
#define INSPECT_CODEC_MAX_SIZE 1024u

/* Portable little-endian format with CRC32; no native struct layout is stored. */
size_t inspect_codec_encode(const InspectModel *model, uint8_t *out, size_t capacity);
/* Atomic: invalid, corrupt, truncated and unknown-version input leaves model intact.
 * Restored sessions are inactive: time while the process was absent is unknown. */
bool inspect_codec_decode(InspectModel *model, const uint8_t *data, size_t size);

#endif
