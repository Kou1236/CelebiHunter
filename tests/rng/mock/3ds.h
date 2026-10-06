#ifndef WAITING_REGRESSION_3DS_H
#define WAITING_REGRESSION_3DS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int32_t LightLock;
typedef void *Thread;
void LightLock_Init(LightLock *);
void LightLock_Lock(LightLock *);
void LightLock_Unlock(LightLock *);
int32_t svcSleepThread(int64_t);
Thread threadCreate(void (*)(void *),void *,size_t,int32_t,int32_t,bool);
int32_t threadJoin(Thread,uint64_t);
void threadFree(Thread);
#endif
