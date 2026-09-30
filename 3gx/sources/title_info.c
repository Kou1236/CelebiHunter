#include <3ds.h>
#include "title_info.h"

u64 g_program_id = 0;

u64 get_title_id()
{
  if (g_program_id == 0)
  {
    if (R_FAILED(fsInit()))
      return 0;
    u32 process_id = 0;
    FS_ProgramInfo info = {0};
    if (R_SUCCEEDED(svcGetProcessId(&process_id, CUR_PROCESS_HANDLE)) &&
        R_SUCCEEDED(FSUSER_GetProgramLaunchInfo(&info, process_id)))
      g_program_id = info.programId;
    fsExit();
  }

  return g_program_id;
}

u16 g_remaster_version = 0;
bool g_version_loaded = false;

u16 get_remaster_version() {
  if (!g_version_loaded) {
    if (R_FAILED(fsInit()))
      return 0xffff;
    u32 processId = 0;
    FS_ProductInfo info = {0};
    Result result = svcGetProcessId(&processId, CUR_PROCESS_HANDLE);
    if (R_SUCCEEDED(result))
      result = FSUSER_GetProductInfo(&info, processId);
    fsExit();
    if (R_FAILED(result))
      return 0xffff;
    g_remaster_version = info.remasterVersion;
    g_version_loaded = true;
  }

  return g_remaster_version;
}
