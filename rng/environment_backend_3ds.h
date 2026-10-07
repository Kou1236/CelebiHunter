#ifndef CH_MANUAL_ENVIRONMENT_BACKEND_3DS_H
#define CH_MANUAL_ENVIRONMENT_BACKEND_3DS_H
#include "environment.h"
#include "platform/backend_3ds.h"
typedef struct {Ch3dsBackend *backend;ChReadOps original_read;uint32_t active,thread_id,cleanup_only;} RawEnvironmentBackend3ds;
int raw_environment_backend_init(RawEnvironmentBackend3ds *,Ch3dsBackend *,const ChReadOps *,RawEnvironmentOps *);
/* Owned callback scope: only exact first scheduler and successful pinned
   startup installation; never UI/worker/startup polling writers. */
int raw_environment_backend_enter(RawEnvironmentBackend3ds *,const ChNativeContext *);
/* Exact completed Restart receipt; this scope permits only RTC restoration.
   It never admits preparation, terminal writes or arbitrary data stores. */
int raw_environment_backend_enter_restart(RawEnvironmentBackend3ds *,const ChNativeContext *);
/* Main-thread consumption of an already captured native worker receipt.
   Cleanup scope only; the adapter verifies the receipt's current epoch. */
int raw_environment_backend_enter_restart_boundary(RawEnvironmentBackend3ds *,const ChNativeContext *);
void raw_environment_backend_leave(RawEnvironmentBackend3ds *);
#endif
