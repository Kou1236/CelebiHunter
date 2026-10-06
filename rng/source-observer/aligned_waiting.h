#ifndef SOURCE_ALIGNED_WAITING_H
#define SOURCE_ALIGNED_WAITING_H
#include "source_observer.h"
#include "../candidate-query/query.h"
/* Raw byte blocks in callback order. Unread portions remain zero on failure. */
typedef struct {
    uint8_t pointers[8],cpu[20],lcd[20],lcd_mode[8],enabled[4],phase[12];
    uint8_t io_clocks[4],iflags,ie,lcd_io[4],hram_flags[4],bg,rng[2];
} AlignedFields;
enum {
    SOURCE_ALIGNED_OK=0,SOURCE_ALIGNED_BOUNDARY=1,SOURCE_ALIGNED_READ=2,
    SOURCE_ALIGNED_DRIFT=3,SOURCE_ALIGNED_DOMAIN=4
};
typedef struct {
    uint32_t abi,stage,read_ordinal,read_address,read_size,read_pass,domain_guard,raw_valid;
    AlignedFields first,second;
} SourceAlignedDiagnostic;
/* Owned first scheduler only. Reuses caller's actual coherent boundary/scene.
   Reads small fields twice (26 callbacks, 178 bytes), never reads physical HID
   or rejects upper-byte host buttons while all low eight guest bits are released.
   A regular released mask is <=65535, has low byte FF and is not 00FF.
   Return 1 only for the aligned released-clock domain. Failure leaves outputs
   unchanged. This validates a waiting seed, never initial source admission. */
int source_aligned_waiting(const ChReadOps *,const ChBoundarySample *,const ChFinalPrompt *,
    uint64_t epoch,SourceObservation *,CQWaitingState *);
/* Optional diagnostic is updated even for invalid/null input. On failure both
   raw blocks are retained; on success only metadata is updated (raw_valid=0),
   avoiding a raw-block copy. READ ordinal is 1..26; pass is 1 or 2. DOMAIN
   guard groups: 1 pointers,2 CPU,3 TMA,4 TAC,5 IRQ,6 hVBlank,7 LCD countdown,
   8 periods,9 LCD mode,10 LY/STAT,11 DIV enabled,12 BG,13 phase,14 countdown,
   15 deadline,16 budget. A NULL diagnostic is allowed. */
int source_aligned_waiting_diagnostic(const ChReadOps *,const ChBoundarySample *,const ChFinalPrompt *,
    uint64_t epoch,SourceObservation *,CQWaitingState *,SourceAlignedDiagnostic *);
#endif
