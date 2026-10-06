// Adapted from Docs/blue/main/media_keys.h. Keep HID decoding shared in behavior.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_KEY_COUNT 5
#define MEDIA_HOLD_MS 650
#define MEDIA_STALE_MS 15000
#define MEDIA_MAX_FIELDS 64

typedef struct {
    uint16_t bit, count;
    uint32_t selector;
    uint8_t report_id, width, key;
    bool array;
} media_field_t;
typedef struct {
    media_field_t fields[MEDIA_MAX_FIELDS];
    uint16_t bits[256];
    unsigned count;
    bool report_ids, valid;
} media_map_t;

extern const char *const media_key_names[MEDIA_KEY_COUNT];
extern const char *const media_key_ids[MEDIA_KEY_COUNT];
bool media_parse_descriptor(media_map_t *map, const uint8_t *data, size_t len);
bool media_decode(const media_map_t *map, const uint8_t *data, size_t len,
                  uint8_t *down, uint8_t *covered);

typedef enum { MEDIA_DOWN, MEDIA_PRESS, MEDIA_HOLD, MEDIA_HOLD_END, MEDIA_CANCEL } media_action_t;
extern const char *const media_action_names[5];
typedef struct {
    bool down, held;
    uint32_t started, duration, presses, holds;
    media_action_t last;
} media_key_t;
typedef struct { media_key_t keys[MEDIA_KEY_COUNT]; } media_tracker_t;
typedef void (*media_emit_t)(unsigned key, media_action_t action, uint32_t duration, void *ctx);
void media_update(media_tracker_t *t, uint8_t down, uint8_t covered, uint32_t now, media_emit_t emit, void *ctx);
void media_tick(media_tracker_t *t, uint32_t now, media_emit_t emit, void *ctx);
void media_cancel(media_tracker_t *t, uint32_t now, media_emit_t emit, void *ctx);

bool media_parse_mac_query(const char *text, uint8_t address[6]);

#ifdef __cplusplus
}
#endif
