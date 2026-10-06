#include "manual_input_plan.h"

static const MpOriginalInputEvidence original_evidence = {
    MP_ABI, 0x001042f0u, 0xeb00332au,
    0x00111010u, 0xe0003005u, 0x00111018u, 0xe884000fu,
    0x001a84fcu, 0xe5801140u,
    {{0xd4,0x8c,0xd5,0xc8,0x1d,0xb8,0x1e,0x71,
      0x24,0xe6,0x7e,0x4f,0x74,0xfe,0xa3,0x76,
      0x13,0xf2,0x41,0x10,0xfc,0xa0,0x51,0xb7,
      0xdf,0x7e,0xbc,0x63,0xbf,0xfd,0xc4,0x0d}}
};
static void clear(void *p, uint32_t n) {
    uint8_t *q = (uint8_t *)p;
    while (n--) *q++ = 0;
}
static int equal(const MpDigest *a, const MpDigest *b) {
    uint32_t i, difference = 0;
    for (i = 0; i < 32; ++i) difference |= a->bytes[i] ^ b->bytes[i];
    return difference == 0;
}
static int nonzero(const MpDigest *a) {
    uint32_t i, bits = 0;
    for (i = 0; i < 32; ++i) bits |= a->bytes[i];
    return bits != 0;
}
static int reject(MpInputPlan *p, uint32_t error) {
    if (p) { p->state = MP_INVALID; p->error = error; p->manual_hardware_verified = 0; }
    return (int)error;
}
static int supported_evidence(const MpOriginalInputEvidence *e) {
    return e && e->abi == original_evidence.abi &&
        e->raw_gate_address == original_evidence.raw_gate_address &&
        e->raw_gate_word == original_evidence.raw_gate_word &&
        e->and_address == original_evidence.and_address && e->and_word == original_evidence.and_word &&
        e->store_address == original_evidence.store_address && e->store_word == original_evidence.store_word &&
        e->increment_address == original_evidence.increment_address &&
        e->increment_word == original_evidence.increment_word &&
        equal(&e->native_code_identity, &original_evidence.native_code_identity);
}
static int ordinary(const MpSourceConditions *a) {
    return a && a->abi == MP_ABI && (a->engine_type == 1u || a->engine_type == 2u) &&
        a->host_batch_count == 1u && a->host_phase != 2u && a->input_enable == 0u;
}
static int guard(MpInputPlan *p, const MpSourceConditions *a, int next_counter) {
    if (!p || !a || p->abi != MP_ABI || p->state == MP_INVALID) return MP_BAD_ARGUMENT;
    if (p->source_epoch != a->source_epoch || !equal(&p->source_identity, &a->source_identity))
        return reject(p, MP_STALE_SOURCE);
    if (!ordinary(a) || p->original_engine_type != a->engine_type ||
        p->original_host_batch_count != a->host_batch_count || p->original_host_phase != a->host_phase)
        return reject(p, MP_UNSUPPORTED_SOURCE);
    if (a->counter != p->current_counter + (next_counter ? 1u : 0u))
        return reject(p, MP_COUNTER_MISMATCH);
    if (p->log_count >= MP_LOG_CAPACITY) return reject(p, MP_LOG_FULL);
    return MP_OK;
}
static MpObservation *append(MpInputPlan *p, uint32_t kind) {
    MpObservation *o = &p->log[p->log_count++];
    clear(o, sizeof(*o)); o->kind = kind; o->counter = p->current_counter;
    return o;
}
static int expected_a(MpInputPlan *p) {
    uint32_t elapsed = p->current_counter - p->effective_a_counter;
    return elapsed < p->expected_release_counter - p->effective_a_counter;
}

MP_API const MpOriginalInputEvidence *mp_original_input_evidence(void) { return &original_evidence; }
MP_API uint32_t mp_input_plan_bytes(void) { return sizeof(MpInputPlan); }
MP_API uint32_t mp_source_conditions_bytes(void) { return sizeof(MpSourceConditions); }
MP_API uint32_t mp_conditional_target_bytes(void) { return sizeof(MpConditionalTarget); }
MP_API uint32_t mp_original_input_evidence_bytes(void) { return sizeof(MpOriginalInputEvidence); }

MP_API int mp_plan_create(const MpSourceConditions *a, const MpConditionalTarget *t,
    const MpOriginalInputEvidence *code, MpInputPlan *p) {
    uint32_t distance;
    if (!p) return MP_BAD_ARGUMENT;
    clear(p, sizeof(*p)); p->abi = MP_ABI;
    if (!a || !t || a->abi != MP_ABI || t->abi != MP_ABI || !a->source_epoch ||
        !nonzero(&a->source_identity) || !nonzero(&t->model_certificate_identity) ||
        !nonzero(&t->required_context_identity)) return reject(p, MP_BAD_ARGUMENT);
    if (a->source_epoch != t->source_epoch || !equal(&a->source_identity, &t->source_identity))
        return reject(p, MP_STALE_SOURCE);
    if (!ordinary(a) || !supported_evidence(code) ||
        ((a->original_host_held | a->original_host_intersection) & MP_A_BIT) ||
        a->actual_applied_mask > 65535u || (a->actual_applied_mask & 255u) != 255u ||
        a->actual_applied_mask == 255u)
        return reject(p, MP_UNSUPPORTED_SOURCE);
    distance = t->effective_a_counter - a->counter;
    if (distance < 2u || distance >= 0x80000000u)
        return reject(p, MP_RAW_TARGET_NOT_FUTURE);
    /* Existing conditional model's original input held for three engine calls.
     * This is a condition to observe, never an automatically delivered pulse. */
    if (t->expected_release_counter - t->effective_a_counter != 3u)
        return reject(p, MP_UNSUPPORTED_SOURCE);
    p->state = MP_CONDITIONAL_WAIT_RAW_PRESS;
    p->source_epoch = a->source_epoch; p->source_identity = a->source_identity;
    p->model_certificate_identity = t->model_certificate_identity;
    p->required_context_identity = t->required_context_identity;
    p->origin_counter = p->current_counter = a->counter;
    p->player_raw_press_counter = t->effective_a_counter - 1u;
    p->effective_a_counter = t->effective_a_counter;
    p->player_raw_release_counter = p->expected_release_counter = t->expected_release_counter;
    p->original_engine_type = a->engine_type; p->original_host_batch_count = a->host_batch_count;
    p->original_host_phase = a->host_phase;
    p->last_held = a->original_host_held; p->last_intersection = a->original_host_intersection;
    /* Actual released mask was read from the source, never fabricated. */
    p->last_provider_mask = a->actual_applied_mask;
    return MP_OK;
}

MP_API int mp_plan_observe_scan(MpInputPlan *p, const MpSourceConditions *a,
    uint32_t previous, uint32_t current, uint32_t intersection) {
    MpObservation *o; uint32_t d; int status = guard(p, a, 0);
    if (status) return status;
    if (p->scan_recorded) return reject(p, MP_OUT_OF_ORDER);
    if (previous != p->last_held || intersection != (previous & current) ||
        current != a->original_host_held || intersection != a->original_host_intersection)
        return reject(p, MP_INPUT_MISMATCH);
    o = append(p, MP_LOG_SCAN); o->previous_held = previous;
    o->current_held = current; o->intersection = intersection;
    p->scan_recorded = 1u; p->scan_seen_at_counter = p->current_counter;
    p->last_held = current; p->last_intersection = intersection;
    if (!p->raw_press_seen) {
        d = p->player_raw_press_counter - p->current_counter;
        if (d >= 0x80000000u) return reject(p, MP_RAW_TARGET_NOT_FUTURE);
        if (d != 0u) {
            if ((current | intersection) & MP_A_BIT) return reject(p, MP_INPUT_MISMATCH);
        } else {
            if (!(current & MP_A_BIT) || (intersection & MP_A_BIT))
                return reject(p, MP_INPUT_MISMATCH);
            p->raw_press_seen = 1u; p->observed_raw_press_counter = p->current_counter;
            p->state = MP_CONDITIONAL_WAIT_SECOND_SCAN;
        }
    } else if (expected_a(p)) {
        if (!(current & MP_A_BIT) || !(intersection & MP_A_BIT))
            return reject(p, MP_INPUT_MISMATCH);
    } else if (p->current_counter == p->player_raw_release_counter) {
        if ((current | intersection) & MP_A_BIT) return reject(p, MP_INPUT_MISMATCH);
        p->raw_release_seen = 1u; p->observed_raw_release_counter = p->current_counter;
        p->state = MP_CONDITIONAL_WAIT_RELEASE_PROVIDER;
    } else if (!p->effective_release_seen) return reject(p, MP_COUNTER_MISMATCH);
    return MP_OK;
}

static int observe_mask(MpInputPlan *p, const MpSourceConditions *a, uint32_t mask, uint32_t point) {
    MpObservation *o; int want, status = guard(p, a, 0);
    if (status) return status;
    if (!p->scan_recorded || p->scan_seen_at_counter != p->current_counter || p->mask_recorded)
        return reject(p, MP_OUT_OF_ORDER);
    if (mask > 0xffffu || a->actual_applied_mask != mask) return reject(p, MP_BAD_ARGUMENT);
    o = append(p, MP_LOG_PROVIDER); o->provider_mask = mask; o->observation_point = point;
    want = expected_a(p);
    /* Original upper host-only bits may remain in the provider halfword. Log
     * them intact. A and other GB buttons must match the admitted condition;
     * the special mask==00FF leaf branch is excluded from this three-call plan. */
    if ((mask & 255u) != (want ? 254u : 255u) || mask == 255u)
        return reject(p, MP_INPUT_MISMATCH);
    p->last_provider_mask = mask; p->provider_seen_at_counter = p->current_counter; p->mask_recorded = 1u;
    if (want && !p->effective_a_seen) {
        if (!p->raw_press_seen || p->current_counter != p->effective_a_counter)
            return reject(p, MP_INPUT_MISMATCH);
        p->effective_a_seen = 1u; p->observed_effective_a_counter = p->current_counter;
        p->state = MP_CONDITIONAL_HOLD;
    }
    if (!want && p->raw_release_seen && !p->effective_release_seen) {
        if (p->current_counter != p->expected_release_counter)
            return reject(p, MP_INPUT_MISMATCH);
        p->effective_release_seen = 1u; p->observed_effective_release_counter = p->current_counter;
        p->state = MP_CONDITIONAL_INPUT_OBSERVED;
    }
    return MP_OK;
}

MP_API int mp_plan_observe_provider(MpInputPlan *p, const MpSourceConditions *a, uint32_t mask) {
    return observe_mask(p, a, mask, MP_POINT_PROVIDER_RETURN_1044DC);
}
MP_API int mp_plan_observe_applied_mask(MpInputPlan *p, const MpSourceConditions *a, uint32_t mask) {
    return observe_mask(p, a, mask, MP_POINT_APPLIED_MASK_MARKER_1A82DC);
}

MP_API int mp_plan_observe_counter(MpInputPlan *p, const MpSourceConditions *a, uint32_t before) {
    MpObservation *o; int status = guard(p, a, 1);
    if (status) return status;
    if (before != p->current_counter || !p->scan_recorded || !p->mask_recorded ||
        p->scan_seen_at_counter != before)
        return reject(p, MP_COUNTER_MISMATCH);
    if (((p->last_provider_mask & MP_A_BIT) == 0u) != expected_a(p))
        return reject(p, MP_INPUT_MISMATCH);
    if (before == p->effective_a_counter && !p->effective_a_seen)
        return reject(p, MP_INPUT_MISMATCH);
    if (before == p->expected_release_counter && !p->effective_release_seen)
        return reject(p, MP_INPUT_MISMATCH);
    if (a->actual_applied_mask != p->last_provider_mask ||
        a->original_host_held != p->last_held || a->original_host_intersection != p->last_intersection)
        return reject(p, MP_INPUT_MISMATCH);
    o = append(p, MP_LOG_COUNTER); o->next_counter = a->counter;
    p->current_counter = a->counter; p->scan_recorded = p->mask_recorded = 0u;
    return MP_OK;
}

MP_API int mp_plan_state(MpInputPlan *p, const MpSourceConditions *a, uint32_t *distance) {
    uint32_t d; int status;
    if (distance) *distance = 0;
    status = guard(p, a, 0);
    if (status) return status;
    if (a->actual_applied_mask != p->last_provider_mask || a->original_host_held != p->last_held ||
        a->original_host_intersection != p->last_intersection) return reject(p, MP_INPUT_MISMATCH);
    if (!p->raw_press_seen) {
        d = p->player_raw_press_counter - p->current_counter;
        if (d >= 0x80000000u) return reject(p, MP_RAW_TARGET_NOT_FUTURE);
        if (distance) *distance = d;
    }
    return MP_OK;
}
