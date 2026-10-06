#ifndef RAW_SOURCE_OBSERVER_H
#define RAW_SOURCE_OBSERVER_H
#include "../platform/platform.h"
#include "../platform/scene.h"
#define SOURCE_OBSERVER_ABI 1u
enum { SOURCE_OBSERVER_OK=0,SOURCE_OBSERVER_READ_FAILED=1,SOURCE_OBSERVER_INCOHERENT=2,
    SOURCE_OBSERVER_GUARD_REJECTED=3,SOURCE_OBSERVER_NORMAL_PROOF_MISSING=4 };
typedef struct {
    uint32_t abi,complete;
    uint64_t epoch;
    ChBoundarySample boundary;
    ChFinalPrompt scene;
    uint32_t counter,add,sub,vblank,bg,applied_mask;
    /* These are direct process reads. They are not the operands consumed
       at the two Normal RNG instructions inside the completed unit. */
    uint32_t io_div,budget,clock_phase[3];
} SourceObservation;
typedef struct {
    uint32_t abi,restricted_wait_path_model;
    uint64_t epoch;
    uint32_t before_counter,after_counter,normal_pair_count,entry_carry;
    /* Explicit model conditions supplied by caller for the retained final
       waiting path: one Normal per VBlank with zero entry carry. The observer
       does not directly sample Normal instruction execution counts. */
} SourceNormalConditions;
typedef struct {
    uint32_t div1,div2,carry;
    uint64_t epoch;
    uint32_t before_counter,after_counter;
} SourceInferredNormalPair;
/* Owned first-scheduler sampling only. Calls read callbacks, never stores.
   Pair reads reject any boundary, scene, pointer or field drift. */
int source_observer_capture(const ChReadOps *,uint64_t epoch,SourceObservation *);
/* Output is conditionally derived from RNG changes, not a direct DIV operand
   read. Requires caller's explicit restricted-path model conditions plus
   coherent released boundaries, exact waiting PC/SP, counter+1 and VBlank+1.
   Unknown paths are rejected; terminal live RNG/DIV matching stays separate. */
int source_observer_infer(const SourceObservation *,const SourceObservation *,
    const SourceNormalConditions *,SourceInferredNormalPair *);
#endif
