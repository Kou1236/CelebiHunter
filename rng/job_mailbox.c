#include "job_mailbox.h"
#include <string.h>
void raw_mailbox_init(RawMailbox *m){memset(m,0,sizeof(*m));}
int raw_mailbox_submit(RawMailbox *m,const RawJob *j){
    if(!m||!j||m->stopped||!j->token.query_id||j->source.source_epoch<m->minimum_epoch)return 0;
    /* Only the latest copied request remains queued. Replacing an unstarted
       stale request does not modify an already-running private solver input. */
    m->pending=*j;m->pending_valid=1;return 1;
}
int raw_mailbox_take(RawMailbox *m,RawJob *j){
    if(!m||!j||m->stopped||m->running||!m->pending_valid||m->ready_valid)return 0;
    *j=m->pending;m->pending_valid=0;m->running=1;return 1;
}
int raw_mailbox_finish(RawMailbox *m,const RawJob *j,const ManualPrediction *p){
    if(!m||!j||!p||!m->running)return 0;
    m->running=0;
    if(m->stopped||j->source.source_epoch<m->minimum_epoch)return 0;
    if(m->ready_valid)return 0;
    m->ready_job=*j;m->ready_result=*p;m->ready_valid=1;return 1;
}
int raw_mailbox_receive(RawMailbox *m,RawJob *j,ManualPrediction *p){
    if(!m||!j||!p||!m->ready_valid)return 0;
    *j=m->ready_job;*p=m->ready_result;m->ready_valid=0;return 1;
}
void raw_mailbox_stop(RawMailbox *m){if(m){m->stopped=1;m->pending_valid=0;m->ready_valid=0;}}
void raw_mailbox_retire(RawMailbox *m,uint64_t epoch){if(m&&epoch>m->minimum_epoch){
    m->minimum_epoch=epoch;m->pending_valid=0u;m->ready_valid=0u;
    memset(&m->pending,0,sizeof(m->pending));memset(&m->ready_job,0,sizeof(m->ready_job));
    memset(&m->ready_result,0,sizeof(m->ready_result));
}}
