#include <memory>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

// Undefine problematic X11 macros before including RT64 headers
#ifdef None
#undef None
#endif

// Undefine problematic X11 macros before including RT64 headers
#ifdef None
#undef None
#endif

// Undefine problematic X11 macros before including RT64 headers
#ifdef None
#undef None
#endif

#define HLSL_CPU
#include "hle/rt64_application.h"
#include "rt64_render_hooks.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "zelda_render.h"
#include "recomp_ui.h"

static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool sample_positions_supported = false;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

unsigned int VI_STATUS_REG = 0;
unsigned int VI_ORIGIN_REG = 0;
unsigned int VI_WIDTH_REG = 0;
unsigned int VI_INTR_REG = 0;
unsigned int VI_V_CURRENT_LINE_REG = 0;
unsigned int VI_TIMING_REG = 0;
unsigned int VI_V_SYNC_REG = 0;
unsigned int VI_H_SYNC_REG = 0;
unsigned int VI_LEAP_REG = 0;
unsigned int VI_H_START_REG = 0;
unsigned int VI_V_START_REG = 0;
unsigned int VI_V_BURST_REG = 0;
unsigned int VI_X_SCALE_REG = 0;
unsigned int VI_Y_SCALE_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::Antialiasing compute_max_supported_aa(RT64::RenderSampleCounts bits) {
    if (bits & RT64::RenderSampleCount::Bits::COUNT_2) {
        if (bits & RT64::RenderSampleCount::Bits::COUNT_4) {
            if (bits & RT64::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    };
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:
            return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

zelda64::renderer::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
    static unsigned char dummy_rom_header[0x40];
    recompui::set_render_hooks();

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__ANDROID__)
    assert(false && "Unimplemented");
#elif defined(__linux__)
    // On Linux, we need to get the native window handle from SDL
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window_handle, &wmInfo)) {
        appCore.window.display = wmInfo.info.x11.display;
        appCore.window.window = wmInfo.info.x11.window;
    } else {
        fprintf(stderr, "Failed to get window info: %s\n", SDL_GetError());
        assert(false);
    }
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    appCore.VI_STATUS_REG = &VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &VI_WIDTH_REG;
    appCore.VI_INTR_REG = &VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &VI_LEAP_REG;
    appCore.VI_H_START_REG = &VI_H_START_REG;
    appCore.VI_V_START_REG = &VI_V_START_REG;
    appCore.VI_V_BURST_REG = &VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    // Create the RT64 application.
    app = std::make_unique<RT64::Application>(appCore, appConfig);

    // Set initial user config settings based on the current settings.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Set postBlendNoiseNegative
    app->emulatorConfig.dither.postBlendNoiseNegative = true;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;
    // Pick an API if the user has set an override.
    switch (cur_config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        default:
        case ultramodern::renderer::GraphicsApi::Auto:
            // Don't override if auto is selected.
            break;
    }

    // Set up the RT64 application.
    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    setup_result = map_setup_result(app->setup(thread_id));
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    // Check if the selected device actually supports MSAA sample positions and MSAA for for the formats that will be used
    // and downgrade the configuration accordingly.
    if (app->device->getCapabilities().sampleLocations) {
        RT64::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::R8G8B8A8_UNORM);
        RT64::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::D32_FLOAT);
        RT64::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;
}

zelda64::renderer::RT64Context::~RT64Context() = default;

void zelda64::renderer::RT64Context::send_dl(const OSTask* task) {
    uint32_t ucode_addr = task->t.ucode & 0x3FFFFFF;
    uint32_t ucode_data_addr = task->t.ucode_data & 0x3FFFFFF;
    uint32_t data_ptr = task->t.data_ptr & 0x3FFFFFF;

    static int dl_count = 0;
    dl_count++;
    // Dump first 64 bytes of DL data so we can see if the caller passed a real DL or
    // a garbage pointer (e.g. CPU function text).
    if (dl_count <= 5 && data_ptr < 0x800000) {
        uint8_t* r = app->core.RDRAM;
        fprintf(stderr, "[send_dl #%d] data_ptr=0x%08X first-64b:", dl_count, data_ptr);
        for (int i = 0; i < 64; i++) {
            uint32_t addr = (data_ptr + i) ^ 3;  // byte access in word-swapped RDRAM
            uint8_t b = r[addr & 0x7FFFFF];
            fprintf(stderr, "%s%02X", (i % 8 == 0 ? " " : ""), b);
        }
        fprintf(stderr, "\n");
        // Also dump the full DL to disk for offline analysis
        if (dl_count == 1) {
            uint32_t sz = task->t.data_size;
            if (sz > 0 && sz < 0x100000) {
                char path[64];
                snprintf(path, sizeof(path), "/tmp/ge_dl_%02d.bin", dl_count);
                FILE* f = fopen(path, "wb");
                if (f) {
                    // Dump in BE byte order (as N64 would see) to be human-readable
                    for (uint32_t i = 0; i < sz; i++) {
                        uint32_t a = (data_ptr + i) ^ 3;
                        fputc(r[a & 0x7FFFFF], f);
                    }
                    fclose(f);
                    fprintf(stderr, "[dump] DL %u bytes -> %s\n", sz, path);
                }
            }
        }
    }
    // One-shot dump: on the first send_dl call, write the game's loaded RSP ucode text
    // and data to disk so we can disassemble it offline and implement missing opcodes.
    if (dl_count == 1 && ucode_addr != 0 && ucode_data_addr != 0) {
        uint8_t* r = app->core.RDRAM;
        FILE* f1 = fopen("/tmp/ge_ucode_text.bin", "wb");
        if (f1) {
            // Dump a large region (16KB) just in case the ucode is bigger than 4KB.
            // The actual size is task->ucode_size but we don't always trust that field.
            fwrite(r + (ucode_addr & 0x3FFFFFF), 1, 0x4000, f1);
            fclose(f1);
            fprintf(stderr, "[dump] ucode text (ram 0x%08X, 16KB) -> /tmp/ge_ucode_text.bin\n", ucode_addr);
        }
        FILE* f2 = fopen("/tmp/ge_ucode_data.bin", "wb");
        if (f2) {
            fwrite(r + (ucode_data_addr & 0x3FFFFFF), 1, 0x800, f2);
            fclose(f2);
            fprintf(stderr, "[dump] ucode data (ram 0x%08X, 2KB) -> /tmp/ge_ucode_data.bin\n", ucode_data_addr);
        }
    }
    // Align data_ptr DOWN to 8-byte boundary. With shadow-copy in submit_rsp_task,
    // data_ptr is usually already aligned (shadow addr is 8-aligned). This is insurance.
    uint32_t final_data_ptr = data_ptr & ~0x7u;

    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(ucode_addr, ucode_data_addr, true);

    // Pre-configure the color image to a valid N64 framebuffer so RT64 has a valid
    // render target before any draws. In GE, the DL's 0xFF opcode is not RDP SETCIMG —
    // it's a task-state/flag handler — so we can't rely on the DL to set the image.
    // Use the last VI_ORIGIN the game set; if VI_ORIGIN hasn't been set yet, fall back
    // to 0x00026100 which is the secondary N64 framebuffer observed in GE.
    // fmt=G_IM_FMT_RGBA(0), siz=G_IM_SIZ_16b(2), width=320.
    uint32_t fb_addr = (VI_ORIGIN_REG != 0) ? VI_ORIGIN_REG : 0x00026100;
    static int preconfig_log = 0;
    if (++preconfig_log <= 5) {
        fprintf(stderr, "[send_dl pre-config #%d] color=0x%08X (VI_ORIGIN=0x%08X) width=320 depth=0x0004C100\n",
            preconfig_log, fb_addr, VI_ORIGIN_REG);
    }
    app->state->rsp->setColorImage(0, 2, 320, fb_addr);
    // Also set the depth image to a conventional second buffer so any Z-tests have a
    // valid target. GE's Z-buffer isn't observed yet; pick something non-colliding.
    app->state->rsp->setDepthImage(0x0004C100);

    // Inject an orthographic projection matrix that scales int16 vertex range to NDC.
    // GE's vertex positions (observed: -28928, 9728, -5282 etc.) are in int16 range.
    // Standard NDC is [-1, 1]. So ortho scale = 1/32768 maps int16 range correctly.
    // N64 FixedMatrix: 32 bytes int rows then 32 bytes frac rows.
    // For 1/32768: int_part=0, frac_part=2 (since 2/65536 = 1/32768).
    // Matrix stored at DMEM format: row-major with separate int/frac halves.
    {
        static constexpr uint32_t ORTHO_RDRAM_ADDR = 0x007F0000;
        uint8_t* r = app->core.RDRAM;
        // FIX 2026-04-22: byte access in 32-bit-swapped RDRAM needs XOR 3, not XOR 2.
        // A BE 16-bit value stored at N64 offset X has its high byte at host offset X^3
        // and low byte at host offset (X+1)^3. XOR 2 was off-by-one, shifting bytes
        // into neighbouring halfwords of the u32 and corrupting every matrix/viewport field.
        auto write_s16 = [&](uint32_t offset, int16_t val) {
            uint32_t a = ORTHO_RDRAM_ADDR + offset;
            r[a ^ 3] = (val >> 8) & 0xFF;
            r[(a + 1) ^ 3] = val & 0xFF;
        };
        static bool ortho_injected = false;
        if (!ortho_injected) {
            // Zero all 64 bytes
            for (int i = 0; i < 64; i++) {
                r[(ORTHO_RDRAM_ADDR + i) ^ 3] = 0;
            }
            // N64 FixedMatrix: int part bytes 0-31 then frac bytes 32-63, each row=8 bytes.
            // FixedMatrix value = (int << 16 | frac) / 65536 interpreted as s32.
            // For negative fractions we need int=-1 AND frac=(65536 + whole_frac).
            // Ortho scale=1/32768 means values ~3e-5. Y-flip means m[1][1] = -1/32768.
            //   1/32768  = int 0, frac 2    (= 2/65536)
            //  -1/32768  = int -1, frac 65534 (= (-1*65536 + 65534)/65536 = -2/65536)
            // 2026-05-04: GE_ORTHO_SCALE env var picks the scale exponent.
            // Default 15 = 1/32768 (legacy, has produced visible green triangle).
            // Each unit = frac value: 1/2^N → frac = 2^(16-N).
            // N=15 → frac=2 (1/32768)  — legacy default
            // N=14 → frac=4 (1/16384)  — wider, less degenerate tris but
            //   non-deterministic FB output not clearly better in tests
            // N=13 → frac=8 (1/8192)
            // N=12 → frac=16 (1/4096)
            int n = 15;
            const char *e = getenv("GE_ORTHO_SCALE");
            if (e) n = std::atoi(e);
            if (n < 8) n = 8;
            if (n > 15) n = 15;
            int frac = 1 << (16 - n);
            // m[0][0] = 1/2^N
            write_s16(0, 0);
            write_s16(32, frac);
            // m[1][1] = -1/2^N (flip Y)
            write_s16(10, -1);
            write_s16(42, (int16_t)(0x10000 - frac));
            // m[2][2] = 1/2^N
            write_s16(20, 0);
            write_s16(52, frac);
            // m[3][3] = 1
            write_s16(30, 1);
            write_s16(62, 0);
            ortho_injected = true;
            fprintf(stderr, "[send_dl] injected ortho projection (scale=1/2048, Y flipped) at RDRAM 0x%08X\n", ORTHO_RDRAM_ADDR);
        }

        // 2026-05-04: optional real-perspective projection (GE_INJECT_PERSPECTIVE=1).
        // Replaces the ortho load with a libultra-style guPerspective matrix.
        // GE vertex range is large (±2500 X/Z) so we scale by GE_PERSP_SCALE (default
        // 1/2048) so the perspective divide gets sane post-transform coords.
        // Layout matches guMtxF2L: 32 bytes int part [4][4] (s16), 32 bytes frac part
        // [4][4] (u16). For each row i, col j: byte offset (i*8 + j*2) in int half,
        // (32 + i*8 + j*2) in frac half. value = (int<<16 | frac) / 65536 (signed s32).
        static constexpr uint32_t PERSP_RDRAM_ADDR = 0x007F0000; // overwrite same slot
        if (getenv("GE_INJECT_PERSPECTIVE") != nullptr) {
            uint8_t* r = app->core.RDRAM;
            auto write_s16 = [&](uint32_t offset, int16_t val) {
                uint32_t a = PERSP_RDRAM_ADDR + offset;
                r[a ^ 3] = (val >> 8) & 0xFF;
                r[(a + 1) ^ 3] = val & 0xFF;
            };
            auto inject_perspective = [&](float fovy_deg, float aspect, float near_v,
                                          float far_v, float scale) {
                float mf[4][4] = {{0}};
                mf[0][0] = mf[1][1] = mf[2][2] = mf[3][3] = 1.0f;
                float fovy = fovy_deg * 3.1415926f / 180.0f;
                float cot = cosf(fovy * 0.5f) / sinf(fovy * 0.5f);
                mf[0][0] = cot / aspect;
                mf[1][1] = cot;
                mf[2][2] = (near_v + far_v) / (near_v - far_v);
                mf[2][3] = -1.0f;
                mf[3][2] = (2.0f * near_v * far_v) / (near_v - far_v);
                mf[3][3] = 0.0f;
                // FIX 2026-05-04: scale ONLY the X/Y projection scale and the
                // Z-row, NOT m[2][3]/m[3][3] which carry the perspective division
                // semantics (m[2][3]=-1 makes w_out = -z_in). Scaling those broke
                // the perspective: m[2][3] became -1/2048 → infinities everywhere
                // after division. Apply scale as a pre-projection vertex shrink so
                // GE's ±2500 vertex range maps into a sensible pre-perspective range.
                mf[0][0] *= scale;
                mf[1][1] *= scale;
                mf[2][2] *= scale;
                mf[3][2] *= scale;  // far*near term scales with z
                // m[2][3]=-1, m[3][3]=0 stay raw.
                // Y flip (RT64/host conventions vs libultra screen-space).
                mf[1][1] = -mf[1][1];
                // Zero all 64 bytes then write split fixed-point.
                for (int b = 0; b < 64; b++) r[(PERSP_RDRAM_ADDR + b) ^ 3] = 0;
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        int32_t fx = (int32_t)(mf[i][j] * 65536.0f);
                        int16_t hi = (int16_t)(fx >> 16);
                        uint16_t lo = (uint16_t)(fx & 0xFFFF);
                        uint32_t off_int  = (uint32_t)(i * 8 + j * 2);
                        uint32_t off_frac = (uint32_t)(32 + i * 8 + j * 2);
                        write_s16(off_int, hi);
                        write_s16(off_frac, (int16_t)lo);
                    }
                }
                fprintf(stderr,
                    "[send_dl] injected PERSPECTIVE fovy=%.1f aspect=%.3f near=%.1f "
                    "far=%.1f scale=%.6f at RDRAM 0x%08X\n  m=[%.4f %.4f %.4f %.4f / "
                    "%.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f]\n",
                    fovy_deg, aspect, near_v, far_v, scale, PERSP_RDRAM_ADDR,
                    mf[0][0], mf[0][1], mf[0][2], mf[0][3],
                    mf[1][0], mf[1][1], mf[1][2], mf[1][3],
                    mf[2][0], mf[2][1], mf[2][2], mf[2][3],
                    mf[3][0], mf[3][1], mf[3][2], mf[3][3]);
            };
            static bool persp_injected = false;
            if (!persp_injected) {
                float scale = 1.0f / 2048.0f;
                if (const char* es = getenv("GE_PERSP_SCALE_DIV"))
                    scale = 1.0f / std::max(1.0f, (float)std::atof(es));
                float fovy = 60.0f, aspect = 4.0f / 3.0f, near_v = 10.0f, far_v = 30000.0f;
                if (const char* e = getenv("GE_PERSP_FOVY")) fovy = (float)std::atof(e);
                if (const char* e = getenv("GE_PERSP_NEAR")) near_v = (float)std::atof(e);
                if (const char* e = getenv("GE_PERSP_FAR"))  far_v  = (float)std::atof(e);
                inject_perspective(fovy, aspect, near_v, far_v, scale);
                persp_injected = true;
            }
        }
        app->state->rsp->matrix(ORTHO_RDRAM_ADDR, 0x03);  // projection | load | no push
    }

    // Inject an N64 standard Vp (viewport) struct at a fixed RDRAM location and call
    // setViewport so RT64 maps NDC [-1,1] to pixel [0,320] x [0,240]. Without this the
    // viewport stays at default which may not cover the framebuffer.
    // N64 Vp = 16 bytes: 4 × int16 vscale (with *4 fixed point), 4 × int16 vtrans.
    // For 320x240 full-screen: scale=(640,480,511,0) trans=(640,480,511,0)
    {
        static constexpr uint32_t VP_RDRAM_ADDR = 0x007F00C0;
        uint8_t* r = app->core.RDRAM;
        // FIX 2026-04-22: see note in ortho block — byte access needs XOR 3, not XOR 2.
        auto write_s16 = [&](uint32_t offset, int16_t val) {
            uint32_t a = VP_RDRAM_ADDR + offset;
            r[a ^ 3] = (val >> 8) & 0xFF;
            r[(a + 1) ^ 3] = val & 0xFF;
        };
        static bool vp_injected = false;
        if (!vp_injected) {
            // vscale (0-7)
            write_s16(0, 640);   // x scale
            write_s16(2, 480);   // y scale
            write_s16(4, 511);   // z scale
            write_s16(6, 0);
            // vtrans (8-15)
            write_s16(8, 640);   // x translate
            write_s16(10, 480);  // y translate
            write_s16(12, 511);  // z translate
            write_s16(14, 0);
            vp_injected = true;
            fprintf(stderr, "[send_dl] injected N64 viewport at RDRAM 0x%08X (320x240)\n", VP_RDRAM_ADDR);
        }
        app->state->rsp->setViewport(VP_RDRAM_ADDR);
    }

    // Inject identity modelview to override garbage the game's MV commands produce.
    // Without this, the game's broken MV multiplies vertex world-coords × bogus values
    // giving billions (we've observed X=6.7e10 instead of NDC range).
    {
        static constexpr uint32_t IDMV_RDRAM_ADDR = 0x007F0100;
        uint8_t* r = app->core.RDRAM;
        auto write_s16 = [&](uint32_t offset, int16_t val) {
            uint32_t a = IDMV_RDRAM_ADDR + offset;
            r[a ^ 3] = (val >> 8) & 0xFF;
            r[(a + 1) ^ 3] = val & 0xFF;
        };
        static bool idmv_injected = false;
        if (!idmv_injected) {
            for (int i = 0; i < 64; i++) {
                r[(IDMV_RDRAM_ADDR + i) ^ 3] = 0;
            }
            // Identity: int[i][i] = 1, all others 0
            write_s16(0, 1);   // m[0][0] int = 1
            write_s16(10, 1);  // m[1][1] int = 1
            write_s16(20, 1);  // m[2][2] int = 1
            write_s16(30, 1);  // m[3][3] int = 1
            // 2026-05-04: GE_MV_TZ=N injects translate(0,0,-N) so vertices
            // with z=0 don't produce w=0 when combined with perspective
            // (m[2][3]=-1 → w_out = -z_in). Default 0 = identity. Use with
            // GE_INJECT_PERSPECTIVE.
            int tz = 0;
            const char *e = getenv("GE_MV_TZ");
            if (e) tz = std::atoi(e);
            if (tz != 0) {
                // translate(0,0,-tz): m[3] (translation row in libultra row-major) → m[3][2] = -tz
                // But hlslpp/RT64 reads as XOR-pair fixed point. Standard libultra translate:
                // m[3] = [tx, ty, tz, 1] in last row.
                // For -tz: int=-1*tz_high...; for tz=5000, int=5000<<16=... wait simpler:
                // Use s16 int + u16 frac. -5000 = int=-5000, frac=0.
                write_s16(28, (int16_t)(-tz));  // m[3][2] int = -tz
                fprintf(stderr, "[send_dl] modelview translation z = %d\n", -tz);
            }
            idmv_injected = true;
            fprintf(stderr, "[send_dl] injected identity modelview at RDRAM 0x%08X\n", IDMV_RDRAM_ADDR);
        }
        app->state->rsp->matrix(IDMV_RDRAM_ADDR, 0x02);  // LOAD, modelview (no PROJ), no push
    }

    // GE_TEST_DL=1: synthesize a minimal "red fullscreen" DL in RDRAM and process THAT
    // instead of the game's DL. If the screen turns red, RT64 can present a DL we built.
    // If still black, the issue is in RT64's presentation pipeline, not the game's DL content.
    if (getenv("GE_TEST_DL") != nullptr) {
        static constexpr uint32_t TEST_DL_ADDR = 0x007F0200;
        uint8_t* r = app->core.RDRAM;
        auto write_u32 = [&](uint32_t offset, uint32_t val) {
            *(uint32_t*)(r + TEST_DL_ADDR + offset) = val;
        };
        // Rebuild each frame so CIMG tracks current VI_ORIGIN — critical because
        // RT64's present queue looks up framebuffer by VI address, not by last-set CIMG.
        uint32_t vi_phys = VI_ORIGIN_REG & 0x00FFFFFF;
        if (vi_phys == 0 || vi_phys >= 0x00800000) vi_phys = 0x00026100;
        int o = 0;
        write_u32(o+0, 0xFF10013F); write_u32(o+4, vi_phys); o += 8;       // G_SETCIMG
        write_u32(o+0, 0xED000000); write_u32(o+4, 0x004FC3BC); o += 8;    // G_SETSCISSOR (0,0)-(319,239) mode=0
        write_u32(o+0, 0xBA001402); write_u32(o+4, 0x00300000); o += 8;    // G_SETOTHERMODE_H: CYC_FILL
        write_u32(o+0, 0xF7000000); write_u32(o+4, 0xF801F801); o += 8;    // G_SETFILLCOLOR: red
        write_u32(o+0, 0xF64FC3BC); write_u32(o+4, 0x00000000); o += 8;    // G_FILLRECT (0,0)-(319,239)
        write_u32(o+0, 0xE9000000); write_u32(o+4, 0x00000000); o += 8;    // G_RDPFULLSYNC
        write_u32(o+0, 0xB8000000); write_u32(o+4, 0x00000000); o += 8;    // G_ENDDL
        static int testdl_log = 0;
        if (++testdl_log <= 5) {
            fprintf(stderr, "[send_dl] test DL targeting VI_ORIGIN=0x%08X\n", vi_phys);
        }
        app->processDisplayLists(app->core.RDRAM, TEST_DL_ADDR, TEST_DL_ADDR + 56, true);
        return;
    }

    if (final_data_ptr != 0) {
        uint32_t data_size = task->t.data_size;
        if (data_size > 0 && data_size < 0x100000) {
            app->processDisplayLists(app->core.RDRAM, final_data_ptr, final_data_ptr + data_size, true);
        } else {
            app->processDisplayLists(app->core.RDRAM, final_data_ptr, 0, true);
        }
    }
}

void zelda64::renderer::RT64Context::update_screen(uint32_t vi_origin) {
    VI_ORIGIN_REG = vi_origin;
    static int us_log = 0;
    if (++us_log <= 5 || us_log % 60 == 0) {
        fprintf(stderr, "[update_screen #%d] VI_ORIGIN=0x%08X status=0x%X width=%u\n",
            us_log, VI_ORIGIN_REG, VI_STATUS_REG, VI_WIDTH_REG);
    }

    // Red override disabled — proof-of-life achieved. Now let whatever RT64 renders
    // (via DL processing) reach the screen unmodified. Keep the PPM dump to inspect
    // what the game actually draws into VI_ORIGIN.
    if (vi_origin != 0 && VI_WIDTH_REG > 0 && VI_WIDTH_REG <= 640) {
        uint32_t phys = vi_origin & 0x3FFFFFF;
        uint32_t width = VI_WIDTH_REG;
        uint32_t height = 240;
        uint32_t pixel_count = width * height;
        uint8_t* r = app->core.RDRAM;
        static int dump_counter = 0;
        dump_counter++;
        // Dump: first frame, periodic boot snapshots, plus every call once VI is pointing
        // at a game-rendered FB (>=0x00050000 skips initial boot buffers).
        bool is_game_origin = (vi_origin & 0x3FFFFFF) >= 0x00050000;
        // 2026-05-04: dump every frame when env GE_DUMP_ALL=1, otherwise the
        // original sparse pattern.
        bool dump_all = (getenv("GE_DUMP_ALL") != nullptr);
        if (dump_all || dump_counter == 1 || dump_counter % 30 == 0 || is_game_origin) {
            char path[64];
            snprintf(path, sizeof(path), "/tmp/ge_fb_%04d.ppm", dump_counter);
            FILE* f = fopen(path, "wb");
            if (f && phys + pixel_count * 2 < 0x800000) {
                fprintf(f, "P6\n%u %u\n255\n", width, height);
                for (uint32_t i = 0; i < pixel_count; i++) {
                    uint32_t n64_addr = phys + i * 2;
                    uint16_t p = *(uint16_t*)(r + (n64_addr ^ 2));
                    uint8_t rr = ((p >> 11) & 0x1F) << 3;
                    uint8_t gg = ((p >> 6) & 0x1F) << 3;
                    uint8_t bb = ((p >> 1) & 0x1F) << 3;
                    fputc(rr, f); fputc(gg, f); fputc(bb, f);
                }
                fclose(f);
                fprintf(stderr, "[FB dump] wrote %s (%ux%u origin=0x%08X)\n", path, width, height, phys);
            }
        }
    }

    app->updateScreen();
}

void zelda64::renderer::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool zelda64::renderer::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    app->updateUserConfig(true);

    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void zelda64::renderer::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t zelda64::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float zelda64::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            else {
                return 1.0f;
            }
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}

RT64::UserConfiguration::Antialiasing zelda64::renderer::RT64MaxMSAA() {
    return device_max_msaa;
}

std::unique_ptr<ultramodern::renderer::RendererContext> zelda64::renderer::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<zelda64::renderer::RT64Context>(rdram, window_handle, developer_mode);
}

bool zelda64::renderer::RT64SamplePositionsSupported() {
    return sample_positions_supported;
}

bool zelda64::renderer::RT64HighPrecisionFBEnabled() {
    return high_precision_fb_enabled;
}
