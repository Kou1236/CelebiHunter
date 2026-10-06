#ifndef CELEBI_MANUAL_INPUT_QUERY_ADAPTER_H
#define CELEBI_MANUAL_INPUT_QUERY_ADAPTER_H
#include "manual_input_plan.h"
#include "../prediction/manual_prediction.h"
#include "../inverse_frontier.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Pure conditional query for a released player-input source. Under the
 * original two-scan intersection filter, physical press precedes effective
 * guest A by one call. Consequently min_N = elapsed + 4 excludes physical
 * press at or before the REAL current boundary. No counter is synthesized.
 * Code identity, derivation certificate and source epoch are explicit records,
 * not live hardware certificates. manual_input_validated always remains zero.
 * These historical signatures cannot express captured BG and now reject.
 * Live jobs use mp_query_inverse_bg_future with their captured source phase.
 */
MP_API int mp_query_manual_future(const ManualSourceBinding *source,
    const ManualCurrentBoundary *current, const ManualModelCertificate *certificate,
    const MpOriginalInputEvidence *original_code_evidence,
    Sol61Workspace *workspace, ManualPrediction *prediction);

/* Later selections read the sealed table only; lead is in real physical boundaries. */
#define MP_QUERY_NEEDS_PLAYER_PAUSE 7u
MP_API int mp_query_cached_future(const ManualSourceBinding *,const ManualCurrentBoundary *,
    const ManualModelCertificate *,const MpOriginalInputEvidence *,Sol61Workspace *,
    Sol61Frontier *,uint32_t build_allowed,uint32_t minimum_player_lead,ManualPrediction *);
MP_API int mp_query_inverse_future(const ManualSourceBinding *,const ManualCurrentBoundary *,
    const ManualModelCertificate *,const MpOriginalInputEvidence *,Sol61Workspace *,
    RawInverseWorkspace *,Sol61Frontier *,uint32_t,uint32_t,ManualPrediction *);

MP_API int mp_query_inverse_bg_future(const ManualSourceBinding *,const ManualCurrentBoundary *,
    const ManualModelCertificate *,const MpOriginalInputEvidence *,Sol61Workspace *,
    RawInverseWorkspace *,Sol61Frontier *,uint32_t source_bg,uint32_t build_allowed,
    uint32_t minimum_player_lead,ManualPrediction *);
#ifdef __cplusplus
}
#endif
#endif
