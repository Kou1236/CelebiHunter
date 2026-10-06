#ifndef CH_MANUAL_PLATFORM_H
#define CH_MANUAL_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CH_RAW_PRE_SITE UINT32_C(0x001042f0)
#define CH_POST_SCAN_SITE UINT32_C(0x00104374)
#define CH_MARKER_SITE UINT32_C(0x001a82dc)
#define CH_SOURCE_SITE UINT32_C(0x001a833c)
#define CH_PRESENT_SITE UINT32_C(0x0014547c)
#define CH_MAX_INSTALL_POINTS 5u
#define CH_ALIAS_PAGE_SIZE 4096u
#define CH_PRISTINE_PREFIX_SIZE UINT32_C(0x12b000)
#define CH_ENGINE_POINTER_ADDRESS UINT32_C(0x0022f698)
#define CH_HOST_CACHE_ADDRESS UINT32_C(0x0027bff0)
#define CH_HOST_BATCH_ADDRESS UINT32_C(0x0027c060)
#define CH_HOST_PHASE_ADDRESS UINT32_C(0x00230fd0)
#define CH_CONFIG_POINTER_ADDRESS UINT32_C(0x005e1928)

typedef struct {
    void *user;
    int (*read_bytes)(void *, uint32_t, void *, uint32_t);
    /* Actual physical HID observation. Never the old cached guest mask. */
    int (*read_physical_keys)(void *, uint32_t *);
} ChReadOps;

typedef struct {
    uint32_t engine_pointer, counter, engine_type, recording_gate, queued_tasks;
    uint32_t engine_mode, engine_flags;
    uint32_t host_cache[4], batch_count, host_phase;
    uint32_t selected_config_pointer, selected_config_mode;
    uint32_t previous_provider_keys;
    uint16_t guest_active_low_mask;
    uint8_t input_enable, counter_valid;
    uint8_t complete, coherent, ordinary_supported, reserved;
} ChBoundarySample;

enum {
    CH_SAMPLE_OK = 1,
    CH_SAMPLE_READ_FAILED = 0,
    CH_SAMPLE_INCOHERENT = -1,
    CH_SAMPLE_UNSUPPORTED = -2
};
/* Call only at the owned original native boundary. Pair-read consistency is
   a guard, not a substitute for exclusive thread/boundary ownership. */
int ch_sample_boundary(const ChReadOps *, ChBoundarySample *);
int ch_read_physical_keys(const ChReadOps *, uint32_t *);

typedef struct {
    uint32_t site, expected_original, bridge_offset;
} ChInstallPoint;
typedef struct {
    void *user;
    int (*startup_owned)(void *);
    int (*pristine_identity)(void *);
    int (*free_page)(void *, uint32_t);
    /* map failure (0) must mean no mapping occurred; -1 is ambiguous. */
    int (*map_alias)(void *, uint32_t alias, uint32_t source);
    int (*unmap_alias)(void *, uint32_t alias);
    int (*read_word)(void *, uint32_t, uint32_t *);
    /* The store may use an uncached alias. A failed store is reconciled only
       after publication and readback; no blind retry. */
    int (*write_word)(void *, uint32_t, uint32_t);
    /* Refresh subsequent data reads at the original VA as well as instruction
       fetches. Must succeed before inspecting any possibly stored word. */
    int (*publish_code)(void *, uint32_t address, uint32_t size);
    int (*probe_alias)(void *, uint32_t probe);
} ChInstallOps;
typedef struct {
    uint32_t source_page, alias_page, point_count, attempted_count;
    uint32_t alias_mapped, poisoned;
    ChInstallPoint points[CH_MAX_INSTALL_POINTS];
    uint32_t installed_words[CH_MAX_INSTALL_POINTS];
} ChInstall;
enum { CH_INSTALL_OK=1, CH_INSTALL_REJECTED=0, CH_INSTALL_POISONED=-1 };

int ch_encode_bl(uint32_t site, uint32_t target, uint32_t *word);
/* Startup only, before the original native thread starts. No live uninstall.
   Five allowed sites are fixed above, each must be its pristine original BL.
   Raw pre-scan is required; all other observer points are caller-selected.
   No provider filter/lease or RNG/DV/environment mutation is permitted. */
int ch_install_startup(ChInstall *, const ChInstallOps *, uint32_t source_page,
                       uint32_t probe_offset, const ChInstallPoint *, uint32_t count);
void ch_platform_sha256(const uint8_t *, size_t, uint8_t digest[32]);
extern const uint8_t ch_pristine_prefix_sha256[32];
#ifdef __cplusplus
}
#endif
#endif
