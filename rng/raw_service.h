#ifndef CH_RAW_SERVICE_H
#define CH_RAW_SERVICE_H
#include "raw_runtime.h"
#include "input-plan/query_adapter.h"
typedef struct {
    ch_query token;
    ManualSourceBinding source;
    ManualCurrentBoundary boundary;
    uint32_t cache_build_allowed,minimum_player_lead,source_bg;
    CQWaitingState waiting;
    uint32_t waiting_counter,waiting_valid;
} RawJob;
typedef struct {
    CQTerminalPrepared terminal;
    uint32_t prepared;
} RawQueryCache;
#define RAW_MINIMUM_PLAYER_LEAD 120u
#define RAW_DISPLAY_EXIT_POLLS 120u
typedef struct { uint32_t visible,count,color[12];char line[12][96]; } RawHud;
typedef struct {
    void *user;
    int (*sample)(void *,RawSample *);
    int (*physical_keys)(void *,uint32_t *);
    void (*draw)(void *,const RawHud *);
    void (*wait_poll)(void *);
    int (*submit_copy)(void *,const RawJob *);
    int (*receive_copy)(void *,RawJob *,ManualPrediction *);
    /* Optional: collect immutable source at the first original scheduler call.
     * Adapter must establish actual fixed context separately. Source identity
     * is a captured-file identity, never a boolean attestation. Returns 1 for
     * a completed bound acquisition, 0 for ineligible, -1 for a failed attempt. */
    int (*read_new_source)(void *,const ChNativeContext *,ManualSourceBinding *,uint64_t *);
    /* Actual first scheduler only. Typed environment coordinator may observe
       or withdraw a forecast; it cannot issue any game/input command here.
       Negative return means environment verification failed. */
    int (*first_scheduler)(void *,const ChNativeContext *,const RawRuntime *);
    /* Current original present parameters only. It must not capture source,
       poll controls or issue game operations. The original submission follows. */
    void (*present)(void *,const ChNativeContext *);
    /* Read-only result gate at the next pre-HID boundary, after a completed
       original unit. It may record actual DV separately from the forecast.
       A failed sample must invalidate its observation chain. */
    void (*completed_unit)(void *,const ChNativeContext *,RawRuntime *,uint32_t sample_ok);
    /* Player-owned pause only: action1 updates a plugin-owned display;
       action2 restores the original display; action3 retains all possibly
       scanned buffers after bounded failed restoration. It performs no
       display write, free, input, counter or engine operations. action4 keeps
       the current private image/audio mute for an explicit one-unit L step. */
    int (*paused_display)(void *,const ChNativeContext *,const RawRuntime *,uint32_t action);
} RawServiceOps;
typedef struct {
    RawRuntime runtime;
    RawServiceOps ops;
    uint32_t initialized,pending_marker,display_owned;
    uint32_t display_exit_polls,display_abandoned;
} RawService;
int raw_service_init(RawService *,const RawServiceOps *);
uint32_t raw_service_route(RawService *,const ChNativeContext *);
int raw_job_solve(const RawJob *,Sol61Workspace *,ManualPrediction *);
int raw_job_solve_cached(const RawJob *,Sol61Workspace *,RawQueryCache *,ManualPrediction *);
void raw_runtime_hud(const RawRuntime *,RawHud *);
extern ChNativeCallback ch_raw_callback;
extern RawService ch_raw_service;
int ch_raw_bind(const RawServiceOps *);
void ch_raw_bridge(void);
#endif
