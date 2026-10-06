#ifndef CH_MANUAL_SOURCE_CAPTURE_H
#define CH_MANUAL_SOURCE_CAPTURE_H
#include "platform_binding.h"
#define RAW_SOURCE_REGION_COUNT 10u
#define RAW_SOURCE_MAX_BYTES 38713u
typedef struct {uint32_t address,size,offset;uint8_t identity[32];} RawSourceRegion;
typedef struct {
    uint32_t abi,counter,rng_add,rng_sub,byte_count,region_count;
    uint32_t pointers[8];
    uint64_t epoch;
    ChNativeContext caller;
    uint32_t tls;
    ChBoundarySample boundary;
    RawSourceRegion regions[RAW_SOURCE_REGION_COUNT];
    uint8_t bytes[RAW_SOURCE_MAX_BYTES],identity[32];
    uint32_t structural_match;
    /* A readonly structural capture is not proof that fixed timing was
       applied, or that all future native paths/physical input are closed. */
    uint32_t fixed_context_applied,manual_hardware_verified;
} RawSourceCapture;
/* Exclusively from the first 1A833C dispatch immediately after 1A82DC marker.
   first_dispatch is an internal marker-generation equality, not a UI claim.
   Full native saved caller and actual TLS are copied without restoration. */
int raw_capture_source(const ChReadOps *,const ChNativeContext *,uint32_t actual_tls,
    uint64_t epoch,uint32_t first_dispatch,RawSourceCapture *);
/* Structural observation after the actual applied-mask receipt. This accepts
   only FFFF/FFFE; it does not itself attest a raw/effective input plan. */
int raw_capture_applied_source(const ChReadOps *,const ChNativeContext *,uint32_t actual_tls,
    uint64_t epoch,uint32_t first_dispatch,uint16_t actual_expected_mask,RawSourceCapture *);
#endif
