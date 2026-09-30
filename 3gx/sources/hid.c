#include <3ds.h>
#include "csvc.h"
#include "common.h"

vu32 *g_key_addr = 0;
u32 g_current_keys = 0;
u32 g_previous_keys = 0;
static u32 g_suppressed_keys = 0;
static u32 g_injection_frames = 0;
static vu32 *g_key_slots[8] = {0};

void set_key_addr(vu32 *key_addr)
{
  g_key_addr = key_addr;
}

void scan_input()
{
  if (g_key_addr != 0)
  {
    g_previous_keys = g_current_keys;
    g_current_keys = *g_key_addr;
    // Continue masking a virtual key until HID has actually published a
    // sample without it. This prevents the tail of an injected reset chord
    // from being mistaken for the user's physical B cancel button.
    g_suppressed_keys &= g_current_keys;
  }
}

u32 get_current_keys()
{
  return g_current_keys;
}

u32 get_previous_keys()
{
  return g_previous_keys;
}

// libctru selects one of eight PAD samples from the HID shared-memory ring.
// Write every sample, as CTRPluginFramework's Controller::InjectKey does, so
// the game cannot select a different slot between the plugin callback and its
// own input scan. The game maps HID read-only; the plugin's uncached physical
// alias is used only for these transient input bits.
static bool prepare_key_slots()
{
  if (g_key_addr == 0)
    return false;
  if (g_key_slots[0] != 0)
    return true;

  vu32 *hid_base = g_key_addr - 10;
  for (u32 i = 0; i < 8; ++i)
  {
    u32 pa = svcConvertVAToPA((const void *)&hid_base[10 + i * 4], false);
    if (pa == 0)
      return false;
    g_key_slots[i] = (vu32 *)PA_PTR(pa);
  }
  return true;
}

void celebi_inject_keys(u32 keys)
{
  g_suppressed_keys |= keys;
  if (keys == 0 || !prepare_key_slots())
    return;

  for (u32 i = 0; i < 8; ++i)
    *g_key_slots[i] |= keys;
  __asm__ volatile("" ::: "memory");
  ++g_injection_frames;
}

u32 celebi_virtual_keys()
{
  return g_suppressed_keys;
}

u32 celebi_hid_ready()
{
  return prepare_key_slots();
}

u32 celebi_hid_injection_frames()
{
  return g_injection_frames;
}
