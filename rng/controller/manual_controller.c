#include "manual_controller.h"
#include <string.h>

#define CH_HALF_RANGE UINT32_C(0x80000000)

static uint32_t next_id(uint32_t *value)
{
    *value += 1u;
    if (*value == 0u) *value = 1u;
    return *value;
}

static int forward_or_same(uint32_t from, uint32_t to)
{
    return (uint32_t)(to - from) < CH_HALF_RANGE;
}

static int query_equal(const ch_query *a, const ch_query *b)
{
    return a->query_id != 0u && a->query_id == b->query_id &&
           a->source_counter == b->source_counter &&
           a->minimum_target_counter == b->minimum_target_counter &&
           a->scene_epoch == b->scene_epoch &&
           a->source_generation == b->source_generation;
}

static void invalidate_prediction(ch_controller *c)
{
    c->candidate_valid = 0u;
    memset(&c->active_query, 0, sizeof(c->active_query));
    c->prediction = CH_PREDICTION_UNAVAILABLE;
    c->need_query = 1u;
}

static void fault(ch_controller *c, ch_control_fault reason)
{
    if (c->fault == CH_FAULT_NONE) c->fault = reason;
    invalidate_prediction(c);
    c->need_query = 0u;
}

void ch_controller_init(ch_controller *c, uint32_t initial_keys)
{
    memset(c, 0, sizeof(*c));
    c->previous_keys = initial_keys;
    /* Keys held before initialization must be released before use. */
    c->blocked_keys = initial_keys;
    c->overlay_visible = 1u;
    c->need_query = 1u;
}

static void issue_command(ch_controller *c, ch_output *o,
                          ch_command_kind kind, uint8_t physical_a)
{
    int explicit_fault_resume = c->fault != CH_FAULT_NONE &&
        kind == CH_COMMAND_RESUME && c->runtime == CH_RUNTIME_PAUSED &&
        c->at_native_boundary;
    /* A fault is not permission to run. A new physical R/A request may leave
       an actually held, admitted native boundary, without clearing the fault
       or reviving a stale one-step request/prediction. */
    if (!explicit_fault_resume &&
        (c->pending.kind != CH_COMMAND_NONE || c->fault != CH_FAULT_NONE))
        return;
    memset(&c->pending, 0, sizeof(c->pending));
    c->pending.kind = kind;
    c->pending.request_id = next_id(&c->next_request_id);
    c->pending.expected_counter = c->last_counter;
    c->pending.target_counter = c->last_counter + 1u;
    c->pending.scene_epoch = c->scene_epoch;
    c->pending.initiated_by_physical_a = physical_a;
    o->command = c->pending;
}

static void complete_pending(ch_controller *c, const ch_input *in)
{
    if (c->pending.kind == CH_COMMAND_NONE) return;
    if (in->scene_epoch != c->pending.scene_epoch) {
        fault(c, CH_FAULT_SCENE_CHANGED_DURING_COMMAND);
        return;
    }
    if (c->pending.kind == CH_COMMAND_STEP_ONE_BOUNDARY &&
        in->counter_valid &&
        !forward_or_same(in->counter, c->pending.target_counter)) {
        fault(c, CH_FAULT_STEP_OVERSHOT);
        return;
    }
    if (in->completed_request_id != c->pending.request_id) return;
    if (c->pending.kind == CH_COMMAND_RESUME) {
        if (in->runtime != CH_RUNTIME_RUNNING) {
            fault(c, CH_FAULT_BAD_ACK);
            return;
        }
    } else if (in->runtime != CH_RUNTIME_PAUSED || !in->counter_valid ||
               !in->at_native_boundary ||
               (c->pending.kind == CH_COMMAND_STEP_ONE_BOUNDARY &&
                in->counter != c->pending.target_counter)) {
        fault(c, CH_FAULT_BAD_ACK);
        return;
    }
    memset(&c->pending, 0, sizeof(c->pending));
}

static int consume_chord(ch_controller *c, uint32_t keys, uint32_t pressed,
                         uint32_t chord, ch_output *o)
{
    if ((keys & chord) != chord || (pressed & chord) == 0u ||
        (c->blocked_keys & chord) != 0u) return 0;
    c->blocked_keys |= chord;
    if (chord & CH_KEY_L) c->pending_l_key = 0u;
    if (chord & CH_KEY_R) c->pending_r_key = 0u;
    o->ui_consumed_keys |= chord;
    return 1;
}

static void handle_keys(ch_controller *c, const ch_input *in, ch_output *o)
{
    uint32_t keys = in->physical_keys;
    uint32_t pressed = keys & ~c->previous_keys;
    uint32_t released = c->previous_keys & ~keys;
    uint32_t previously_blocked = c->blocked_keys;
    c->blocked_keys &= keys;
    o->ui_consumed_keys = previously_blocked;

    /* The UI lock never blocks the player's pause, step, or resume keys. */
    if (consume_chord(c, keys, pressed, CH_KEY_X | CH_KEY_Y, o))
        c->interface_locked ^= 1u;
    if (consume_chord(c, keys, pressed, CH_KEY_START | CH_KEY_UP, o))
        if (!c->interface_locked) c->overlay_visible ^= 1u;
    if (consume_chord(c, keys, pressed, CH_KEY_L | CH_KEY_R, o)) {
        if (in->runtime == CH_RUNTIME_RUNNING)
            issue_command(c, o, CH_COMMAND_PAUSE_AT_BOUNDARY, 0u);
    }

    if ((pressed & CH_KEY_L) && !(c->blocked_keys & CH_KEY_L))
        c->pending_l_key = 1u;
    if ((pressed & CH_KEY_R) && !(c->blocked_keys & CH_KEY_R))
        c->pending_r_key = 1u;

    /* Defer chord-capable single keys until release: either press order can
       form a chord without accidentally resuming or stepping first. */
    if ((released & CH_KEY_R) && c->pending_r_key) {
        c->pending_r_key = 0u;
        if (in->runtime == CH_RUNTIME_PAUSED)
            issue_command(c, o, CH_COMMAND_RESUME, 0u);
    }
    if ((released & CH_KEY_L) && c->pending_l_key) {
        c->pending_l_key = 0u;
        if (in->runtime == CH_RUNTIME_PAUSED && in->counter_valid &&
            in->at_native_boundary)
            issue_command(c, o, CH_COMMAND_STEP_ONE_BOUNDARY, 0u);
    }
    /* A is an actual physical edge, never consumed by the overlay and never
       generated or released by this module. Runtime resumes with real HID. */
    if ((pressed & CH_KEY_A) && !(c->blocked_keys & CH_KEY_A) &&
        in->runtime == CH_RUNTIME_PAUSED)
        issue_command(c, o, CH_COMMAND_RESUME, 1u);
    o->ui_consumed_keys &= ~CH_KEY_A;
    c->previous_keys = keys;
}

static void maybe_query(ch_controller *c, ch_output *o)
{
    if (!c->need_query || c->active_query.query_id || c->candidate_valid ||
        !c->counter_valid || !c->scene_eligible ||
        c->fault != CH_FAULT_NONE) return;
    memset(&c->active_query, 0, sizeof(c->active_query));
    c->active_query.query_id = next_id(&c->next_query_id);
    c->active_query.source_counter = c->last_counter;
    c->active_query.minimum_target_counter = c->last_counter + 1u;
    c->active_query.scene_epoch = c->scene_epoch;
    c->active_query.source_generation = c->source_generation;
    c->need_query = 0u;
    c->prediction = CH_PREDICTION_QUERYING;
    o->query = c->active_query;
}

void ch_controller_update(ch_controller *c, const ch_input *in, ch_output *o)
{
    uint8_t changed;
    memset(o, 0, sizeof(*o));
    changed = (uint8_t)(!c->initialized_scene ||
        c->scene_epoch != in->scene_epoch ||
        c->source_generation != in->source_generation);
    if (changed) invalidate_prediction(c);
    if (c->initialized_scene && in->counter_valid && c->counter_valid &&
        c->scene_epoch == in->scene_epoch &&
        !forward_or_same(c->last_counter, in->counter))
        fault(c, CH_FAULT_COUNTER_DISCONTINUITY);
    complete_pending(c, in);
    c->initialized_scene = 1u;
    c->scene_epoch = in->scene_epoch;
    c->source_generation = in->source_generation;
    c->last_counter = in->counter;
    c->counter_valid = in->counter_valid;
    c->at_native_boundary = in->at_native_boundary;
    c->scene_eligible = in->scene_eligible;
    c->runtime = in->runtime;
    if (in->runtime == CH_RUNTIME_FAILED) fault(c, CH_FAULT_RUNTIME_FAILED);
    if (!in->counter_valid || !in->scene_eligible) invalidate_prediction(c);
    if (c->candidate_valid) {
        if (!forward_or_same(in->counter, c->target_counter)) {
            invalidate_prediction(c);
            c->prediction = CH_PREDICTION_MISSED;
        } else c->prediction = in->counter == c->target_counter ?
                                CH_PREDICTION_READY : CH_PREDICTION_FUTURE;
    }
    handle_keys(c, in, o);
    maybe_query(c, o);
    o->overlay_visible = c->overlay_visible;
    o->interface_locked = c->interface_locked;
    o->candidate_valid = c->candidate_valid;
    o->prediction = c->prediction;
    o->fault = c->fault;
    if (c->candidate_valid) {
        o->target_counter = c->target_counter;
        o->predicted_dv = c->predicted_dv;
        o->remaining_advances = c->target_counter - c->last_counter;
    }
}

int ch_controller_accept_candidate(ch_controller *c, const ch_query *q,
                                   uint32_t target, uint16_t dv)
{
    if (!q || !query_equal(q, &c->active_query) ||
        q->scene_epoch != c->scene_epoch ||
        q->source_generation != c->source_generation ||
        !c->counter_valid || !c->scene_eligible ||
        c->fault != CH_FAULT_NONE) return 0;
    if (!forward_or_same(q->minimum_target_counter, target) ||
        !forward_or_same(q->source_counter, target) ||
        !forward_or_same(c->last_counter, target)) {
        invalidate_prediction(c);
        c->prediction = CH_PREDICTION_MISSED;
        return 0;
    }
    c->target_counter = target;
    c->predicted_dv = dv;
    c->candidate_valid = 1u;
    c->need_query = 0u;
    memset(&c->active_query, 0, sizeof(c->active_query));
    c->prediction = target == c->last_counter ? CH_PREDICTION_READY :
                                               CH_PREDICTION_FUTURE;
    return 1;
}

int ch_controller_query_failed(ch_controller *c, const ch_query *q)
{
    if (!q || !query_equal(q, &c->active_query)) return 0;
    memset(&c->active_query, 0, sizeof(c->active_query));
    c->need_query = 0u;
    c->prediction = CH_PREDICTION_QUERY_FAILED;
    return 1;
}

void ch_controller_request_query(ch_controller *c)
{
    invalidate_prediction(c);
}
