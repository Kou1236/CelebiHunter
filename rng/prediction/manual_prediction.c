#include "manual_prediction.h"
#include "evidence_data.h"

static void zero_bytes(void *p, uint32_t n) {
    uint8_t *q = (uint8_t *)p;
    while (n--) *q++ = 0;
}
static int equal_digest(const ManualDigest *a, const ManualDigest *b) {
    uint32_t i, difference = 0;
    for (i = 0; i < 32; ++i) difference |= a->bytes[i] ^ b->bytes[i];
    return difference == 0;
}
static int nonzero_digest(const ManualDigest *a) {
    uint32_t i, bits = 0;
    for (i = 0; i < 32; ++i) bits |= a->bytes[i];
    return bits != 0;
}
static int source_bound(const ManualSourceBinding *s, const ManualCurrentBoundary *c) {
    return s->source_epoch == c->source_epoch &&
        equal_digest(&s->source_identity, &c->source_identity) &&
        equal_digest(&s->required_context_identity, &c->required_context_identity);
}

SOL61_API const ManualModelCertificate *manual_model_certificate(void) {
    return &manual_frozen_certificate;
}
SOL61_API uint32_t manual_prediction_bytes(void) { return sizeof(ManualPrediction); }
SOL61_API uint32_t manual_source_binding_bytes(void) { return sizeof(ManualSourceBinding); }
SOL61_API uint32_t manual_current_boundary_bytes(void) { return sizeof(ManualCurrentBoundary); }
SOL61_API uint32_t manual_certificate_bytes(void) { return sizeof(ManualModelCertificate); }

SOL61_API int manual_query_future(const ManualSourceBinding *s,
    const ManualCurrentBoundary *c, const ManualModelCertificate *certificate,
    Sol61Workspace *workspace, ManualPrediction *p) {
    (void)certificate;
    if (!p) return MANUAL_QUERY_INVALID;
    zero_bytes(p, sizeof(*p));p->abi=MANUAL_PREDICTION_ABI;p->status=MANUAL_QUERY_INVALID;
    if(!s||!c||!workspace||s->abi!=MANUAL_PREDICTION_ABI||!s->source_epoch||
       s->rng_add>255u||s->rng_sub>255u||!nonzero_digest(&s->source_identity)||
       !nonzero_digest(&s->required_context_identity))return p->status;
    if(!source_bound(s,c))return p->status=MANUAL_QUERY_STALE_SOURCE;
    /* This legacy signature has no captured BG phase for the compound model. */
    return p->status=MANUAL_QUERY_UNSUPPORTED_CERTIFICATE;
}

SOL61_API int manual_prediction_state(const ManualPrediction *p,
    const ManualCurrentBoundary *c, uint32_t *remaining) {
    uint32_t elapsed;
    if (remaining) *remaining = 0;
    if (!p || !c || p->abi != MANUAL_PREDICTION_ABI || p->status != MANUAL_QUERY_OK ||
        p->press_relative > MANUAL_MODEL_MAX_PRESS ||
        p->model_n != p->press_relative + 2u ||
        p->target_counter != p->origin_counter + p->press_relative)
        return MANUAL_QUERY_INVALID;
    if (p->source_epoch != c->source_epoch ||
        !equal_digest(&p->source_identity, &c->source_identity) ||
        !equal_digest(&p->required_context_identity, &c->required_context_identity))
        return MANUAL_QUERY_STALE_SOURCE;
    elapsed = c->counter - p->origin_counter;
    if (elapsed > p->press_relative) return MANUAL_QUERY_NO_FUTURE_CANDIDATE;
    if (remaining) *remaining = p->press_relative - elapsed;
    return MANUAL_QUERY_OK;
}

SOL61_API int manual_admit_original_native_case(const ManualSourceBinding *s,ManualPrediction *p) {
    if(!s||!p||p->abi!=MANUAL_PREDICTION_ABI||p->status!=MANUAL_QUERY_OK)return MANUAL_QUERY_INVALID;
    if(p->source_epoch!=s->source_epoch||!equal_digest(&p->source_identity,&s->source_identity)||
       !equal_digest(&p->required_context_identity,&s->required_context_identity))return MANUAL_QUERY_STALE_SOURCE;
    /* Old hardware/native receipts used a different audio/refresh profile.
       No new receipt is implied by retaining the arithmetic prefix table. */
    return MANUAL_QUERY_UNSUPPORTED_CERTIFICATE;
}
