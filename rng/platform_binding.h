#ifndef CH_RAW_PLATFORM_BINDING_H
#define CH_RAW_PLATFORM_BINDING_H
#include "raw_service.h"
#include "platform/platform.h"
#include "platform/scene.h"
typedef struct {
    ChReadOps read;
    uint64_t source_epoch;
    ChBoundarySample last_boundary;
    ChFinalPrompt last_prompt;
    int last_sample_status,last_scene_status;
    uint32_t omit_arrival_check;
} RawPlatformBinding;
/* This reader does not capture/normalize an entire model origin. It supplies
 * actual original counters/host words to the service, separately from source
 * binding. source_epoch must stay fixed while the captured source is in use. */
int raw_platform_sample(void *,RawSample *);
int raw_platform_keys(void *,uint32_t *);
#endif
