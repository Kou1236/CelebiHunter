#ifndef CH_RAW_JOB_MAILBOX_H
#define CH_RAW_JOB_MAILBOX_H
#include "raw_service.h"
/* Caller supplies one short critical section. Solver runs after take() on a
   private copy, outside this section; UI never waits for a completed search. */
typedef struct {
    RawJob pending,ready_job;
    ManualPrediction ready_result;
    uint32_t pending_valid,ready_valid,running,stopped;
} RawMailbox;
void raw_mailbox_init(RawMailbox *);
int raw_mailbox_submit(RawMailbox *,const RawJob *);
int raw_mailbox_take(RawMailbox *,RawJob *);
int raw_mailbox_finish(RawMailbox *,const RawJob *,const ManualPrediction *);
int raw_mailbox_receive(RawMailbox *,RawJob *,ManualPrediction *);
void raw_mailbox_stop(RawMailbox *);
#endif
