#ifndef CH_MANUAL_ENVIRONMENT_BACKEND_3DS_H
#define CH_MANUAL_ENVIRONMENT_BACKEND_3DS_H
#include "environment.h"
#include "platform/backend_3ds.h"
typedef struct {Ch3dsBackend *backend;ChReadOps original_read;uint32_t active,thread_id;} RawEnvironmentBackend3ds;
int raw_environment_backend_init(RawEnvironmentBackend3ds *,Ch3dsBackend *,const ChReadOps *,RawEnvironmentOps *);
/* Owned callback scope: only exact first scheduler and successful pinned
   startup installation; never UI/worker/startup polling writers. */
int raw_environment_backend_enter(RawEnvironmentBackend3ds *,const ChNativeContext *);
void raw_environment_backend_leave(RawEnvironmentBackend3ds *);
#endif
