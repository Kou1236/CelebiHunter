/*
 * Small libctru compatibility layer for the reproducible Windows fallback
 * build. Normal devkitPro builds do not define CELEBIHUNTER_MINIMAL_CTRU and
 * continue to link the official libctru archive.
 *
 * The IPC command layouts and SVC declarations below follow libctru 2.7.0.
 * Only the services used by the 3GX loader, title check, clock, and bounded
 * calibration log are included.
 */
#ifdef CELEBIHUNTER_MINIMAL_CTRU

#include <3ds.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

static Handle g_srv_handle;
static Handle g_fs_handle;
static unsigned g_fs_refs;

char *fake_heap_start;
char *fake_heap_end;
u32 __ctru_heap;
u32 __ctru_linear_heap;

static Result srv_register_client(void)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x1, 0, 2);
    cmdbuf[1] = IPC_Desc_CurProcessId();
    Result rc = svcSendSyncRequest(g_srv_handle);
    return R_FAILED(rc) ? rc : (Result)cmdbuf[1];
}

Result srvInit(void)
{
    Result rc = svcConnectToPort(&g_srv_handle, "srv:");
    if (R_SUCCEEDED(rc)) rc = srv_register_client();
    if (R_FAILED(rc) && g_srv_handle) {
        svcCloseHandle(g_srv_handle);
        g_srv_handle = 0;
    }
    return rc;
}

void srvExit(void)
{
    if (g_srv_handle) svcCloseHandle(g_srv_handle);
    g_srv_handle = 0;
}

Handle *srvGetSessionHandle(void) { return &g_srv_handle; }

Result srvGetServiceHandleDirect(Handle *out, const char *name)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x5, 4, 0);
    memset(&cmdbuf[1], 0, 8);
    strncpy((char *)&cmdbuf[1], name, 8);
    cmdbuf[3] = strnlen(name, 8);
    cmdbuf[4] = 0;
    Result rc = svcSendSyncRequest(g_srv_handle);
    if (R_SUCCEEDED(rc)) rc = (Result)cmdbuf[1];
    if (out) *out = R_SUCCEEDED(rc) ? cmdbuf[3] : 0;
    return rc;
}

Result srvGetServiceHandle(Handle *out, const char *name)
{
    return srvGetServiceHandleDirect(out, name);
}

static Result fsuser_initialize(Handle session)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x801, 0, 2);
    cmdbuf[1] = IPC_Desc_CurProcessId();
    Result rc = svcSendSyncRequest(session);
    return R_FAILED(rc) ? rc : (Result)cmdbuf[1];
}

Result fsInit(void)
{
    if (g_fs_refs++) return 0;
    Result rc = srvGetServiceHandle(&g_fs_handle, "fs:USER");
    if (R_SUCCEEDED(rc)) rc = fsuser_initialize(g_fs_handle);
    if (R_FAILED(rc)) {
        if (g_fs_handle) svcCloseHandle(g_fs_handle);
        g_fs_handle = 0;
        g_fs_refs = 0;
    }
    return rc;
}

void fsExit(void)
{
    if (!g_fs_refs || --g_fs_refs) return;
    if (g_fs_handle) svcCloseHandle(g_fs_handle);
    g_fs_handle = 0;
}

FS_Path fsMakePath(FS_PathType type, const void *path)
{
    FS_Path p = { type, 0, path };
    if (type == PATH_ASCII) p.size = strlen((const char *)path) + 1;
    else if (type == PATH_UTF16) {
        const u16 *s = (const u16 *)path;
        while (*s++) p.size++;
        p.size = (p.size + 1) * 2;
    } else if (type == PATH_EMPTY) {
        p.size = 1;
        p.data = "";
    }
    return p;
}

Result FSUSER_OpenFile(Handle *out, FS_Archive archive, FS_Path path,
                       u32 open_flags, u32 attributes)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x802, 7, 2);
    cmdbuf[1] = 0;
    cmdbuf[2] = (u32)archive;
    cmdbuf[3] = (u32)(archive >> 32);
    cmdbuf[4] = path.type;
    cmdbuf[5] = path.size;
    cmdbuf[6] = open_flags;
    cmdbuf[7] = attributes;
    cmdbuf[8] = IPC_Desc_StaticBuffer(path.size, 0);
    cmdbuf[9] = (u32)path.data;
    Result rc = svcSendSyncRequest(g_fs_handle);
    if (R_FAILED(rc)) return rc;
    if (out) *out = cmdbuf[3];
    return (Result)cmdbuf[1];
}

Result FSUSER_OpenArchive(FS_Archive *archive, FS_ArchiveID id, FS_Path path)
{
    if (!archive) return -2;
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x80C, 3, 2);
    cmdbuf[1] = id;
    cmdbuf[2] = path.type;
    cmdbuf[3] = path.size;
    cmdbuf[4] = IPC_Desc_StaticBuffer(path.size, 0);
    cmdbuf[5] = (u32)path.data;
    Result rc = svcSendSyncRequest(g_fs_handle);
    if (R_FAILED(rc)) return rc;
    *archive = cmdbuf[2] | ((u64)cmdbuf[3] << 32);
    return (Result)cmdbuf[1];
}

Result FSUSER_CloseArchive(FS_Archive archive)
{
    if (!archive) return -2;
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x80E, 2, 0);
    cmdbuf[1] = (u32)archive;
    cmdbuf[2] = (u32)(archive >> 32);
    Result rc = svcSendSyncRequest(g_fs_handle);
    return R_FAILED(rc) ? rc : (Result)cmdbuf[1];
}

Result FSUSER_GetProductInfo(FS_ProductInfo *info, u32 process_id)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x82E, 1, 0);
    cmdbuf[1] = process_id;
    Result rc = svcSendSyncRequest(g_fs_handle);
    if (R_FAILED(rc)) return rc;
    if (info) memcpy(info, &cmdbuf[2], sizeof(*info));
    return (Result)cmdbuf[1];
}

Result FSUSER_GetProgramLaunchInfo(FS_ProgramInfo *info, u32 process_id)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x82F, 1, 0);
    cmdbuf[1] = process_id;
    Result rc = svcSendSyncRequest(g_fs_handle);
    if (R_FAILED(rc)) return rc;
    if (info) memcpy(info, &cmdbuf[2], sizeof(*info));
    return (Result)cmdbuf[1];
}

Result FSFILE_Write(Handle handle, u32 *written, u64 offset,
                    const void *buffer, u32 size, u32 flags)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x803, 4, 2);
    cmdbuf[1] = (u32)offset;
    cmdbuf[2] = (u32)(offset >> 32);
    cmdbuf[3] = size;
    cmdbuf[4] = flags;
    cmdbuf[5] = IPC_Desc_Buffer(size, IPC_BUFFER_R);
    cmdbuf[6] = (u32)buffer;
    Result rc = svcSendSyncRequest(handle);
    if (R_FAILED(rc)) return rc;
    if (written) *written = cmdbuf[2];
    return (Result)cmdbuf[1];
}

Result FSFILE_GetSize(Handle handle, u64 *size)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x804, 0, 0);
    Result rc = svcSendSyncRequest(handle);
    if (R_FAILED(rc)) return rc;
    if (size) *size = cmdbuf[2] | ((u64)cmdbuf[3] << 32);
    return (Result)cmdbuf[1];
}

Result FSFILE_Close(Handle handle)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x808, 0, 0);
    Result rc = svcSendSyncRequest(handle);
    if (R_FAILED(rc)) return rc;
    rc = (Result)cmdbuf[1];
    return R_SUCCEEDED(rc) ? svcCloseHandle(handle) : rc;
}

Result FSFILE_Flush(Handle handle)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x809, 0, 0);
    Result rc = svcSendSyncRequest(handle);
    return R_FAILED(rc) ? rc : (Result)cmdbuf[1];
}

u64 osGetTime(void)
{
    osTimeRef_s tr;
    u32 next = OS_SharedConfig->timeref_cnt;
    u32 cur;
    do {
        cur = next;
        tr = OS_SharedConfig->timeref[cur & 1];
        __dmb();
        next = OS_SharedConfig->timeref_cnt;
    } while (cur != next);
    s64 elapsed_ticks = (s64)svcGetSystemTick() - tr.value_tick;
    s64 elapsed_ms = elapsed_ticks * 1000 / tr.sysclock_hz;
    const s64 hour_ms = 60 * 60 * 1000;
    if (tr.drift_ms != 0 && elapsed_ms < hour_ms)
        elapsed_ms += tr.drift_ms * (hour_ms - elapsed_ms) / hour_ms;
    return tr.value_ms + elapsed_ms;
}

void *_sbrk(ptrdiff_t increment)
{
    char *base = fake_heap_start;
    if (!base || base + increment > fake_heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    fake_heap_start += increment;
    return base;
}

int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int rc) { (void)rc; for (;;) svcSleepThread(1000000000LL); }

#endif
