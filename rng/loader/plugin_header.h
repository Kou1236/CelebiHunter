#ifndef CH_MANUAL_LOADER_HEADER_H
#define CH_MANUAL_LOADER_HEADER_H
#include <3ds.h>
#include <stddef.h>

/* Pinned Luma PluginHeader ABI. The loader owns these mappings; this
 * research ELF must never free or remap its executable or header heap. */
typedef struct {
    u32 magic,version,heapVA,heapSize,exeSize,isDefaultPlugin;
    s32 *plgldrEvent,*plgldrReply;
    u8 notifyHomeEvent,padding[7];
    u64 waitForReplyTimeout;
    u32 reserved[20],config[32];
} ChPluginHeader;
_Static_assert(sizeof(ChPluginHeader)==0x100,"pinned Luma header size");
_Static_assert(offsetof(ChPluginHeader,heapVA)==8,"pinned heap VA offset");
_Static_assert(offsetof(ChPluginHeader,config)==0x80,"pinned config offset");
#define CH_LOADER_BASE 0x07000000u
#define CH_LOADER_HEAP_BASE 0x06000000u
#define CH_LOADER_MAGIC 0x24584733u
#define CH_LOADER_MIN_HEAP 0x00020000u
#endif
