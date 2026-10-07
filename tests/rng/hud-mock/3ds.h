#ifndef HUD_REGRESSION_3DS_H
#define HUD_REGRESSION_3DS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int32_t LightLock;
typedef int32_t Result;
typedef struct {uint32_t base_addr,size,perm,state;} MemInfo;
typedef struct {uint32_t flags;} PageInfo;
#define MEMSTATE_FREE 0u
#define MEMSTATE_STATIC 8u
#define MEMSTATE_SHARED 6u
#define MEMPERM_READ 1u
#define MEMPERM_WRITE 2u
#define MEMPERM_READWRITE (MEMPERM_READ|MEMPERM_WRITE)
#define OS_VRAM_VADDR 0x1f000000u
#define OS_VRAM_PADDR 0x18000000u
#define OS_VRAM_SIZE 0x00600000u
#define CUR_PROCESS_HANDLE 0xffff8001u
#define R_FAILED(result) ((result)<0)
#define R_SUCCEEDED(result) ((result)>=0)
void LightLock_Init(LightLock *);
void LightLock_Lock(LightLock *);
void LightLock_Unlock(LightLock *);
int LightLock_TryLock(LightLock *);
int32_t svcQueryMemory(MemInfo *,PageInfo *,uint32_t);
int32_t svcFlushProcessDataCache(uint32_t,uint32_t,uint32_t);
void mock_dsb(void);
#define __dsb() mock_dsb()
#endif
