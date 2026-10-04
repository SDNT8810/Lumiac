#include "media_keys.h"
#include <string.h>

const char *const media_key_names[] = {"Volume +", "Volume -", "Next", "Back", "Play / Pause"};
const char *const media_key_ids[] = {"volume_up", "volume_down", "next", "back", "play_pause"};
const char *const media_action_names[] = {"DOWN", "PRESS", "HOLD", "HOLD END", "CANCELLED"};

/* Consumer usage page: normal media keys, plus transport hold usages. */
static const struct { uint16_t usage; uint8_t key; } bindings[] = {
    {0xe9, 0}, {0xea, 1}, {0xb5, 2}, {0xb6, 3}, {0xcd, 4},
    {0xb3, 2}, {0xb4, 3}, {0xb0, 4}, {0xb1, 4}
};

typedef struct { uint32_t page, size, count; int32_t minimum; uint8_t id; } globals_t;
typedef struct { uint32_t usages[64], minimum, maximum; unsigned count; bool range; } locals_t;

static uint32_t usage_at(const locals_t *l, unsigned index) {
    if (index < l->count) return l->usages[index];
    if (l->range && l->maximum >= l->minimum) {
        uint32_t n = index - l->count;
        return l->minimum + (n <= l->maximum - l->minimum ? n : l->maximum - l->minimum);
    }
    return l->count ? l->usages[l->count - 1] : 0;
}

static bool append_field(media_map_t *m, globals_t g, uint16_t bit,
                         unsigned key, bool array, uint32_t selector) {
    if (m->count == MEDIA_MAX_FIELDS) return false;
    m->fields[m->count++] = (media_field_t){ .bit = bit, .count = array ? g.count : 1,
        .selector = selector, .report_id = g.id, .width = g.size, .key = key, .array = array };
    return true;
}

bool media_parse_descriptor(media_map_t *m, const uint8_t *data, size_t len) {
    memset(m, 0, sizeof(*m));
    if (!data || !len) return false;
    globals_t g = {0}, stack[8];
    locals_t l = {0};
    unsigned depth = 0;
    for (size_t p = 0; p < len;) {
        uint8_t prefix = data[p++];
        if (prefix == 0xfe) { /* Reserved long item: skip only after checking its length. */
            if (p + 2 > len) return false;
            size_t bytes = data[p]; p += 2;
            if (bytes > len - p) return false;
            p += bytes; continue;
        }
        unsigned size = prefix & 3; if (size == 3) size = 4;
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (size > len - p) return false;
        uint32_t v = 0;
        for (unsigned i = 0; i < size; i++) v |= (uint32_t)data[p++] << (i * 8);
        if (type == 1) {
            switch (tag) {
                case 0: g.page = v; break;
                case 1: g.minimum = size == 1 ? (int8_t)v : size == 2 ? (int16_t)v : (int32_t)v; break;
                case 7: g.size = v; break;
                case 8: if (!v || v > 255) return false; g.id = v; m->report_ids = true; break;
                case 9: g.count = v; break;
                case 10: if (depth == 8) return false; stack[depth++] = g; break;
                case 11: if (!depth) return false; g = stack[--depth]; break;
                default: break;
            }
        } else if (type == 2) {
            uint32_t usage = size == 4 ? v : (g.page << 16) | v;
            if (tag == 0) {
                if (l.count == 64) return false;
                l.usages[l.count++] = usage;
            } else if (tag == 1) { l.minimum = usage; l.range = true; }
            else if (tag == 2) l.maximum = usage;
            else if (tag == 10 && v) return false; /* Alternative usage sets aren't supported. */
        } else if (type == 0) {
            if (tag == 8) { /* Input: offsets are independent of Output/Feature reports. */
                if (!g.size || g.size > 32 || g.count > 512 || g.size * g.count > 4096 - m->bits[g.id]) return false;
                uint16_t offset = m->bits[g.id];
                if (!(v & 1) && !(v & 4)) { /* Absolute data only; relative controls have no release state. */
                    if (v & 2) {
                        for (unsigned i = 0; i < g.count; i++) {
                            uint32_t usage = usage_at(&l, i);
                            for (unsigned b = 0; b < sizeof(bindings)/sizeof(bindings[0]); b++)
                                if (usage == (0xc0000u | bindings[b].usage) &&
                                    !append_field(m, g, offset + i * g.size, bindings[b].key, false, 0)) return false;
                        }
                    } else {
                        for (unsigned b = 0; b < sizeof(bindings)/sizeof(bindings[0]); b++) {
                            uint32_t target = 0xc0000u | bindings[b].usage;
                            int32_t index = -1;
                            for (unsigned i = 0; i < l.count; i++) if (l.usages[i] == target) index = (int32_t)i;
                            if (index < 0 && l.range && target >= l.minimum && target <= l.maximum)
                                index = (int32_t)(l.count + target - l.minimum);
                            if (index >= 0 && !append_field(m, g, offset, bindings[b].key, true, (uint32_t)(g.minimum + index))) return false;
                        }
                    }
                }
                m->bits[g.id] += g.size * g.count;
            }
            memset(&l, 0, sizeof(l));
        }
    }
    m->valid = depth == 0;
    return m->valid;
}

static uint32_t read_bits(const uint8_t *data, unsigned bit, unsigned width) {
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v |= (uint32_t)((data[(bit+i)/8] >> ((bit+i)%8)) & 1) << i;
    return v;
}

bool media_decode(const media_map_t *m, const uint8_t *data, size_t len, uint8_t *down, uint8_t *covered) {
    *down = *covered = 0;
    if (!m->valid || !data || !len) return false;
    uint8_t id = m->report_ids ? *data++ : 0;
    if (m->report_ids) len--;
    if (!m->bits[id] || len < (m->bits[id] + 7u)/8) return false;
    for (unsigned i = 0; i < m->count; i++) {
        const media_field_t *f = &m->fields[i];
        if (f->report_id != id) continue;
        *covered |= 1u << f->key;
        for (unsigned j = 0; j < f->count; j++) {
            uint32_t v = read_bits(data, f->bit + j * f->width, f->width);
            if (f->array ? v == f->selector : v != 0) *down |= 1u << f->key;
        }
    }
    return *covered != 0;
}

static void send_event(media_key_t *k, unsigned key, media_action_t action, uint32_t duration, media_emit_t emit, void *ctx) {
    k->last = action; k->duration = duration;
    if (emit) emit(key, action, duration, ctx);
}

void media_tick(media_tracker_t *t, uint32_t now, media_emit_t emit, void *ctx) {
    for (unsigned i = 0; i < MEDIA_KEY_COUNT; i++) {
        media_key_t *k = &t->keys[i];
        if (!k->down) continue;
        uint32_t duration = now - k->started;
        if (duration >= MEDIA_STALE_MS) {
            k->down = k->held = false;
            send_event(k, i, MEDIA_CANCEL, duration, emit, ctx);
        } else if (!k->held && duration >= MEDIA_HOLD_MS) {
            k->held = true; k->holds++;
            send_event(k, i, MEDIA_HOLD, duration, emit, ctx);
        }
    }
}

void media_update(media_tracker_t *t, uint8_t down, uint8_t covered, uint32_t now, media_emit_t emit, void *ctx) {
    media_tick(t, now, emit, ctx);
    for (unsigned i = 0; i < MEDIA_KEY_COUNT; i++) {
        media_key_t *k = &t->keys[i];
        if (!(covered & (1u << i))) continue;
        bool pressed = (down & (1u << i)) != 0;
        if (pressed && !k->down) {
            k->down = true; k->held = false; k->started = now;
            send_event(k, i, MEDIA_DOWN, 0, emit, ctx);
        } else if (!pressed && k->down) {
            uint32_t duration = now - k->started;
            if (!k->held) k->presses++;
            k->down = false;
            send_event(k, i, k->held ? MEDIA_HOLD_END : MEDIA_PRESS, duration, emit, ctx);
            k->held = false;
        }
    }
}

void media_cancel(media_tracker_t *t, uint32_t now, media_emit_t emit, void *ctx) {
    for (unsigned i = 0; i < MEDIA_KEY_COUNT; i++) if (t->keys[i].down) {
        t->keys[i].down = t->keys[i].held = false;
        send_event(&t->keys[i], i, MEDIA_CANCEL, now - t->keys[i].started, emit, ctx);
    }
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool media_parse_mac_query(const char *s, uint8_t address[6]) {
    char decoded[18]; size_t n = 0;
    if (!s) return false;
    while (*s) {
        if (n >= 17) return false;
        char c = *s++;
        if (c == '%') {
            if (!s[0] || !s[1]) return false;
            int a = hex_digit(s[0]), b = hex_digit(s[1]);
            if (a < 0 || b < 0) return false;
            c = (char)((a << 4) | b); s += 2;
        }
        decoded[n++] = c;
    }
    if (n != 17) return false;
    for (unsigned i = 0; i < 6; i++) {
        int a = hex_digit(decoded[i*3]), b = hex_digit(decoded[i*3+1]);
        if (a < 0 || b < 0 || (i < 5 && decoded[i*3+2] != ':')) return false;
        address[i] = (uint8_t)((a << 4) | b);
    }
    return true;
}
