/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic track-data provider for SDK mods. It never reads EP147 or the filesystem.
 * Production cache, parser and source tests live in the core; these fixtures test mod use. */
static inline uint32_t ht_record_size(const void *record)
{
    uint32_t size;
    memcpy(&size, record, sizeof size); /* Short caller prefixes must be read without a full-record lvalue. */
    return size;
}
static const az_beat ht_beats[] = { { 0, 120, 1, 0 }, { .5, 120, 2, 0 }, { 1.5, 60, 3, 0 } };
static const az_cue_entry ht_cues[] = {
    { .size = sizeof(az_cue_entry), .version = 1, .valid = AZ_CUE_START | AZ_CUE_SAVED, .classification = AZ_CUE_MEMORY, .entry_id = 1, .type = AZ_CUE_POINT, .saved = 1, .modified = -1, .auto_active = -1 },
    { .size = sizeof(az_cue_entry), .version = 1, .valid = AZ_CUE_START | AZ_CUE_END | AZ_CUE_SAVED, .classification = AZ_CUE_HOT, .entry_id = 1, .slot = 1, .type = AZ_CUE_LOOP, .end_s = 1, .saved = 1, .modified = -1, .auto_active = -1 }
};
static const az_phrase ht_phrases[] = { { 0, 2, 7, 2, 0, 0 } };
static const uint8_t ht_wave[] = { 1, 2, 3, 4 };
static pthread_mutex_t ht_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t ht_next;
static struct {
    uint64_t ticket;
    int cancelled, detail;
    az_track_snapshot value;
    az_wave_snapshot wave;
    uint8_t columns[AZ_WAVE_WINDOW_MAX * 6];
} ht_slots[4];
static int ht_caps(az_data_capabilities *out)
{
    if (!out || ht_record_size(out) < sizeof *out) return AZ_INVALID;
    az_data_capabilities v = { out->size, 1, 31, AZ_LIVE_LOADED, 0, 4, 8, 4096, 0 };
    memcpy(out, &v, sizeof v);
    return AZ_OK;
}
static int ht_deck(int deck, az_deck_snapshot *out)
{
    lifetime_gate();
    if (!out || ht_record_size(out) < offsetof(az_deck_snapshot, loop_unit_generation) || deck < 0 || deck > 3) return AZ_INVALID;
    az_deck_snapshot v = { 0 };
    v.size = ht_record_size(out);
    v.version = 1;
    v.deck = deck;
    v.valid = AZ_LIVE_LOADED;
    v.track.size = sizeof v.track;
    v.track.version = 1;
    v.loop.size = sizeof v.loop;
    v.loop.version = 1;
    v.loop.defined = v.loop.engaged = v.loop.reloop_available = -1;
    v.main_cue.size = sizeof v.main_cue;
    v.main_cue.version = 1;
    v.master_tempo = v.quantize = v.play_mode_is_2 = v.play_pause = v.play_aux = v.key_lock = -1;
    v.sync = v.sync_raw_mode = -1;
    pthread_mutex_lock(&dmu);
    v.loaded = D.loaded[deck];
    pthread_mutex_unlock(&dmu);
    size_t bytes = v.size < sizeof v ? v.size : sizeof v;
    memcpy(out, &v, bytes);
    return AZ_OK;
}
static int ht_resolve(const char *id, int stick, uint32_t cid, az_track_ref *out)
{
    check_id(id, "track_resolve");
    if (!thread_lifetime) {
        fail(K_API, "track_resolve requires a worker");
        return AZ_INVALID;
    }
    if (!out || ht_record_size(out) < sizeof *out || stick < 0 || stick > 1 || !cid) return AZ_INVALID;
    az_track_ref v = { out->size, 1, (uint64_t)stick + 1, 1, (uint64_t)stick + 10, cid, 0 };
    memcpy(out, &v, sizeof v);
    return AZ_OK;
}
static int ht_request(const char *id, const az_track_ref *ref, uint32_t mask, uint64_t *ticket)
{
    check_id(id, "track_request");
    if (!ref || ht_record_size(ref) < sizeof *ref || ref->version != 1 || !ref->source_id || !ref->source_generation ||
        !ref->library_id || !ref->track_id || !ticket || !mask || (mask & ~31u)) return AZ_INVALID;
    if (pthread_mutex_trylock(&ht_mu)) return AZ_BUSY;
    int rc = AZ_LIMIT;
    for (int i = 0; i < 4; i++)
        if (!ht_slots[i].ticket) {
            az_track_snapshot *v = &ht_slots[i].value;
            memset(v, 0, sizeof *v);
            v->size = sizeof *v;
            v->version = 1;
            v->track = *ref;
            v->track.size = sizeof v->track;
            v->revision = 1;
            az_component_info ci = { AZ_AVAIL_READY, AZ_PROV_FILE, 1, 1 };
            if (mask & AZ_DATA_METADATA) {
                v->metadata_info = ci;
                v->metadata.size = sizeof v->metadata;
                v->metadata.version = 1;
                v->metadata.valid = AZ_META_TITLE | AZ_META_DURATION | AZ_META_TEMPO;
                snprintf(v->metadata.title, sizeof v->metadata.title, "Synthetic track %u", ref->track_id);
                v->metadata.duration_s = 2;
                v->metadata.original_bpm = 120;
            }
            if (mask & AZ_DATA_CUES) {
                v->cues_info = ci;
                v->cue_count = 2;
                v->cues = ht_cues;
            }
            if (mask & AZ_DATA_GRID) {
                v->grid_info = ci;
                v->beat_count = 3;
                v->beats = ht_beats;
            }
            if (mask & AZ_DATA_PHRASES) {
                v->phrases_info = ci;
                v->phrase_count = 1;
                v->phrases = ht_phrases;
            }
            if (mask & AZ_DATA_WAVEFORM) {
                v->waveform_info = ci;
                v->waveform = (az_waveform){ sizeof(az_waveform), 1, AZ_WAVE_PREVIEW, 4, 0, .5, ht_wave };
            }
            ht_slots[i].cancelled = ht_slots[i].detail = 0;
            *ticket = ht_slots[i].ticket = ++ht_next;
            rc = AZ_OK;
            break;
        }
    pthread_mutex_unlock(&ht_mu);
    return rc;
}
static int ht_acquire(const char *id, uint64_t ticket, const az_track_snapshot **out)
{
    check_id(id, "track_acquire");
    if (!out) return AZ_INVALID;
    *out = NULL;
    if (pthread_mutex_trylock(&ht_mu)) return AZ_BUSY;
    int rc = AZ_STALE;
    for (int i = 0; i < 4; i++)
        if (ht_slots[i].ticket == ticket && ticket) {
            rc = ht_slots[i].detail ? AZ_INVALID : ht_slots[i].cancelled ? AZ_CANCELLED
                                                                         : AZ_OK;
            if (!rc) *out = &ht_slots[i].value;
        }
    pthread_mutex_unlock(&ht_mu);
    return rc;
}
static int ht_wave_request(const char *id, const az_track_ref *ref, const az_wave_query *q, uint64_t *ticket)
{
    check_id(id, "waveform_request");
    if (!ref || ht_record_size(ref) < sizeof *ref || ref->version != 1 || !ref->source_id || !ref->source_generation ||
        !ref->library_id || !ref->track_id || !ticket ||
        !q || ht_record_size(q) < sizeof *q || q->version != 1 || !q->column_count || q->reserved) return AZ_INVALID;
    if (q->encoding < AZ_WAVE_PWV3_MONO || q->encoding > AZ_WAVE_PWAV_PREVIEW) return AZ_UNSUPPORTED;
    if (q->column_count > AZ_WAVE_WINDOW_MAX) return AZ_LIMIT;
    uint32_t total = q->encoding == AZ_WAVE_PWV2_TINY_PREVIEW ? 100u : q->encoding == AZ_WAVE_PWAV_PREVIEW ? 400u
                                                        : q->encoding >= AZ_WAVE_PWV4_PREVIEW ? 1200u : 300u;
    if (q->first_column >= total) return AZ_INVALID;
    if (pthread_mutex_trylock(&ht_mu)) return AZ_BUSY;
    int rc = AZ_LIMIT;
    for (int i = 0; i < 4; i++)
        if (!ht_slots[i].ticket) {
            az_wave_snapshot *v = &ht_slots[i].wave;
            memset(v, 0, sizeof *v);
            v->size = sizeof *v;
            v->version = 1;
            v->track = *ref;
            v->track.size = sizeof v->track;
            v->revision = v->observed_ns = 1;
            v->info = (az_component_info){ AZ_AVAIL_READY, AZ_PROV_FILE, 1, 1 };
            v->encoding = q->encoding;
            v->first_column = q->first_column;
            v->total_columns = total;
            v->column_count = total - q->first_column;
            if (v->column_count > q->column_count) v->column_count = q->column_count;
            v->bytes_per_column = q->encoding == AZ_WAVE_PWV2_TINY_PREVIEW || q->encoding == AZ_WAVE_PWAV_PREVIEW ? 1u
                                               : q->encoding == AZ_WAVE_PWV4_PREVIEW ? 6u : q->encoding == AZ_WAVE_PWV6_PREVIEW ? 3u
                                                                                                                 : q->encoding;
            v->data_bytes = v->column_count * v->bytes_per_column;
            v->seconds_per_column = q->encoding >= AZ_WAVE_PWV4_PREVIEW ? 2.0 / total : 1.0 / 150.0;
            v->start_s = q->first_column * v->seconds_per_column;
            for (uint32_t n = 0; n < v->data_bytes; n++) ht_slots[i].columns[n] = (uint8_t)((n + q->first_column * v->bytes_per_column) % 256);
            v->columns = ht_slots[i].columns;
            ht_slots[i].cancelled = 0;
            ht_slots[i].detail = 1;
            *ticket = ht_slots[i].ticket = ++ht_next;
            rc = AZ_OK;
            break;
        }
    pthread_mutex_unlock(&ht_mu);
    return rc;
}
static int ht_wave_acquire(const char *id, uint64_t ticket, const az_wave_snapshot **out)
{
    check_id(id, "waveform_acquire");
    if (!out) return AZ_INVALID;
    *out = NULL;
    if (pthread_mutex_trylock(&ht_mu)) return AZ_BUSY;
    int rc = AZ_STALE;
    for (int i = 0; i < 4; i++)
        if (ht_slots[i].ticket == ticket && ticket) {
            rc = !ht_slots[i].detail ? AZ_INVALID : ht_slots[i].cancelled ? AZ_CANCELLED
                                                                          : AZ_OK;
            if (!rc) *out = &ht_slots[i].wave;
        }
    pthread_mutex_unlock(&ht_mu);
    return rc;
}
static int ht_drop(const char *id, uint64_t ticket, int cancel)
{
    check_id(id, cancel ? "track_cancel" : "track_release");
    if (pthread_mutex_trylock(&ht_mu)) return AZ_BUSY;
    int rc = AZ_STALE;
    for (int i = 0; i < 4; i++)
        if (ht_slots[i].ticket == ticket && ticket) {
            if (cancel) ht_slots[i].cancelled = 1;
            else ht_slots[i].ticket = 0;
            rc = AZ_OK;
        }
    pthread_mutex_unlock(&ht_mu);
    return rc;
}
static int ht_release(const char *id, uint64_t ticket) { return ht_drop(id, ticket, 0); }
static int ht_cancel(const char *id, uint64_t ticket) { return ht_drop(id, ticket, 1); }
static int ht_event(const char *id, uint64_t after, az_data_change *out)
{
    check_id(id, "data_event");
    (void)after;
    return !out || ht_record_size(out) < sizeof *out ? AZ_INVALID : AZ_EMPTY;
}
static int ht_command(const char *id, const az_command_request *r, az_command_result *out)
{
    check_id(id, "command_submit");
    if (!r || ht_record_size(r) < sizeof *r || r->version != 1 || !out || ht_record_size(out) < sizeof *out || r->deck > 3 ||
        r->operation < AZ_CMD_LOOP_IN || r->operation > AZ_CMD_CUE_COLOR || !r->request_id ||
        !r->expected_load_generation || !r->deadline_ns || r->quantization > AZ_QUANTIZE_ON ||
        (r->flags & ~(AZ_COMMAND_PERSIST | AZ_COMMAND_NATIVE_LOAD | AZ_COMMAND_NATIVE_CUE_IDS)) ||
        ((r->flags & AZ_COMMAND_NATIVE_CUE_IDS) &&
         (!(r->flags & AZ_COMMAND_NATIVE_LOAD) || r->operation <= AZ_CMD_LOOP_ADJUST_OUT || !r->expected_revision)) ||
        ((r->flags & AZ_COMMAND_NATIVE_LOAD) &&
         r->operation <= AZ_CMD_LOOP_ADJUST_OUT &&
         ((r->flags & AZ_COMMAND_PERSIST) || r->expected_revision)) ||
        r->reserved || r->classification > AZ_CUE_MEMORY ||
        (r->rgb & UINT32_C(0xff000000)) || !isfinite(r->position_s) || !isfinite(r->end_s) ||
        !isfinite(r->beats) || !memchr(r->comment, 0, sizeof r->comment)) return AZ_INVALID;
    az_command_result v = { out->size, 1, AZ_COMMAND_REJECTED, AZ_UNSUPPORTED, r->request_id, 0, 1 };
    memcpy(out, &v, sizeof v);
    return AZ_UNSUPPORTED;
}
static int ht_command_poll(const char *id, uint64_t request_id, az_command_result *out)
{
    check_id(id, "command_poll");
    return !request_id || !out || ht_record_size(out) < sizeof *out ? AZ_INVALID : AZ_STALE;
}
static int ht_command_cancel(const char *id, uint64_t request_id, az_command_result *out)
{
    check_id(id, "command_cancel");
    return !request_id || !out || ht_record_size(out) < sizeof *out ? AZ_INVALID : AZ_STALE;
}
static int ht_command_release(const char *id, uint64_t request_id)
{
    check_id(id, "command_release");
    return request_id ? AZ_STALE : AZ_INVALID;
}
