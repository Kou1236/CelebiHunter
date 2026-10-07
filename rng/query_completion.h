#ifndef CH_RAW_QUERY_COMPLETION_H
#define CH_RAW_QUERY_COMPLETION_H
#include "raw_service.h"
enum {
    RAW_QUERY_IGNORED=0,RAW_QUERY_ACCEPTED=1,RAW_QUERY_RETRY=2,
    RAW_QUERY_REFRESH=3,RAW_QUERY_REJECTED=4,RAW_QUERY_DEFERRED=5
};
enum {
    RAW_QUERY_DETAIL_NONE=0,RAW_QUERY_DETAIL_JOB_BINDING=1,
    RAW_QUERY_DETAIL_RESULT_BINDING=2,RAW_QUERY_DETAIL_PLAN_ADMISSION=3,
    RAW_QUERY_DETAIL_SUBMISSION=4,RAW_QUERY_DETAIL_WORKER_STATUS=5,
    RAW_QUERY_DETAIL_WINDOW_EXPIRED=6
};
/* Complete one immutable copied worker result at the current owned UI gate.
 * There is no I/O, game command, key, counter or pause operation here. Old
 * tokens leave both the active request and its diagnostics untouched.
 * RAW_QUERY_DEFERRED requires the caller to retain the immutable job/result
 * for a later checked gate; it never consumes or fails the active token. */
int raw_query_complete(RawRuntime *,const RawJob *,const ManualPrediction *);
#endif
