#ifndef CH_MANUAL_PLATFORM_BACKEND_3DS_H
#define CH_MANUAL_PLATFORM_BACKEND_3DS_H
#include "platform.h"
#include "read_scope.h"
typedef struct {
    uint32_t startup_owned,identity_checked,mapped_page,source_page,hid_ready;
    ChReadScope read_scope;
    uint32_t scoped_read_calls,last_read_calls,last_mapping_queries;
    uint32_t last_mapping_hits,last_mapping_failures;
} Ch3dsBackend;
void ch_3ds_backend_ops(Ch3dsBackend *,ChInstallOps *,ChReadOps *);
int ch_3ds_hid_init(Ch3dsBackend *);
void ch_3ds_hid_exit(Ch3dsBackend *);
void ch_3ds_startup_finished(Ch3dsBackend *);
/* Scope only a paired owned read, never native waits/mutations. Failure to
   begin does not bypass mapping validation: ordinary reads still query it. */
int ch_3ds_read_begin(Ch3dsBackend *);
void ch_3ds_read_end(Ch3dsBackend *);
/* A native worker must not share the main thread's mutable mapping cache.
   Copy only completed startup identity into caller-owned read-only storage. */
int ch_3ds_worker_read_ops(const Ch3dsBackend *,Ch3dsBackend *,ChReadOps *);
#endif
