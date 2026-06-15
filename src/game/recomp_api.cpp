#include <cmath>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "zelda_config.h"
#include "recomp_input.h"
#include "recomp_ui.h"
#include "zelda_render.h"
#include "zelda_sound.h"
#include "librecomp/helpers.hpp"
// #include "../patches/input.h"
// #include "../patches/graphics.h"
// #include "../patches/sound.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"

extern "C" void boot_osPiRawStartDma(uint8_t* rdram, recomp_context* ctx) {
    uint32_t direction = ctx->r4;
    uint32_t device_address = ctx->r5;
    gpr rdram_address = ctx->r6;
    uint32_t size = ctx->r7;

    assert(direction == 0); // Only reads

    // Complete the DMA synchronously (the game immediately waits until it's done anyways)
    recomp::do_rom_read(rdram, rdram_address, device_address + recomp::rom_base, size);
}

extern "C" void osDpGetCounters_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Empty
}

// Default GoldenEye setup token string. On the real N64, this data lives at
// PI bus address 0x00FFB000 in a special cartridge region. The game reads it
// via osPiReadIo to configure memory pools for each level.
// Format: "-ml<N> -me<N> -mgfx<N> -mvtx<N> -mt<N> -ma<N>"
// -ma = mema pool size in KB (most critical value)
static const char ge_default_tokens[] = "-ml0 -me0 -mgfx100 -mvtx50 -mt625 -ma300";

// Level token forwarded from the command line (-level_NN) by main.cpp. When non-empty,
// it is prepended to the token string served at 0x00FFB000 so the game boots straight
// into that level (the game reads it via tokenReadIo -> tokenFind("-level_") in bossMainloop).
char g_boot_level_token[16] = {0};

// Build (once) the token string actually served at 0x00FFB000:
//   "<-level_NN> -ml0 -me0 ..."  if a level was requested, else just the pool defaults.
static const char* ge_get_tokens(uint32_t* out_len) {
    static char buf[128];
    static uint32_t buf_len = 0;
    static bool built = false;
    if (!built) {
        int n;
        if (g_boot_level_token[0] != '\0') {
            n = snprintf(buf, sizeof(buf), "%s %s", g_boot_level_token, ge_default_tokens);
        } else {
            n = snprintf(buf, sizeof(buf), "%s", ge_default_tokens);
        }
        buf_len = (n > 0 ? (uint32_t)n : 0) + 1; // include NUL terminator
        built = true;
        fprintf(stderr, "[INFO] ge_tokens served: \"%s\" (level_token=\"%s\")\n", buf, g_boot_level_token);
    }
    if (out_len) *out_len = buf_len;
    return buf;
}

extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    // osPiReadIo(u32 devAddr, u32 *data): reads a word from PI bus
    uint32_t devAddr = (uint32_t)ctx->r4;
    gpr dataPtr = ctx->r5;

    uint32_t value = 0;

    if (devAddr >= recomp::rom_base) {
        // Cart ROM range
        uint32_t physical_addr = devAddr - recomp::rom_base;
        auto rom = recomp::get_rom();
        if (physical_addr + 4 <= rom.size()) {
            value = (rom[physical_addr] << 24) | (rom[physical_addr+1] << 16) |
                    (rom[physical_addr+2] << 8) | rom[physical_addr+3];
        }
    } else if (devAddr >= 0x00FFB000 && devAddr < 0x00FFB000 + 640) {
        static bool logged = false;
        if (!logged) { fprintf(stderr, "[INFO] osPiReadIo: reading tokens from 0x%08X\n", devAddr); logged = true; }
        // GoldenEye token area - provide setup string (prepended with -level_ if requested)
        uint32_t offset = devAddr - 0x00FFB000;
        uint32_t tok_len = 0;
        const char* tokens = ge_get_tokens(&tok_len);
        const uint8_t* src = (const uint8_t*)tokens + offset;
        uint32_t remaining = (offset < tok_len) ? (tok_len - offset) : 0;
        uint8_t b0 = remaining > 0 ? src[0] : 0;
        uint8_t b1 = remaining > 1 ? src[1] : 0;
        uint8_t b2 = remaining > 2 ? src[2] : 0;
        uint8_t b3 = remaining > 3 ? src[3] : 0;
        value = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }

    MEM_W(0, dataPtr) = (int32_t)value;
    ctx->r2 = 0; // success
}

extern "C" void osPfsInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Empty
}

// TODO: Validate this
extern "C" void __f_to_ll_recomp(uint8_t* rdram, recomp_context* ctx) {
    float input = ctx->f12.fl;

    int64_t result = (int64_t) input;

    ctx->r2 = (int32_t) (result >> 32); // Signed high part
    ctx->r3 = (int32_t) (result >> 0);  // Low part as unsigned
}

// TODO: Validate this
extern "C" void __ll_to_d_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Properly sign-extend high and zero-extend low
    int64_t input = ((int64_t) (ctx->r4) << 32) | (uint32_t) ctx->r5;

    double result = (double) input;
    ctx->f0.d = result;
}

extern "C" void recomp_update_inputs(uint8_t* rdram, recomp_context* ctx) {
    recomp::poll_inputs();
}

extern "C" void rmonPrintf_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Empty
}

extern "C" void __ll_lshift_recomp(uint8_t* rdram, recomp_context* ctx) {
    int64_t a = (ctx->r4 << 32) | ((ctx->r5 << 0) & 0xFFFFFFFFu);
    int64_t b = (ctx->r6 << 32) | ((ctx->r7 << 0) & 0xFFFFFFFFu);
    int64_t ret = a << b;

    ctx->r2 = (int32_t) (ret >> 32);
    ctx->r3 = (int32_t) (ret >> 0);
}

extern "C" void __ll_rshift_recomp(uint8_t* rdram, recomp_context* ctx) {
    int64_t a = (ctx->r4 << 32) | ((ctx->r5 << 0) & 0xFFFFFFFFu);
    int64_t b = (ctx->r6 << 32) | ((ctx->r7 << 0) & 0xFFFFFFFFu);
    int64_t ret = a >> b;

    ctx->r2 = (int32_t) (ret >> 32);
    ctx->r3 = (int32_t) (ret >> 0);
}

extern "C" void recomp_puts(uint8_t* rdram, recomp_context* ctx) {
    PTR(char) cur_str = _arg<0, PTR(char)>(rdram, ctx);
    u32 length = _arg<1, u32>(rdram, ctx);

    for (u32 i = 0; i < length; i++) {
        fputc(MEM_B(i, (gpr) cur_str), stdout);
    }
}

extern "C" void recomp_exit(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::quit();
}

extern "C" void recomp_get_gyro_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    recomp::get_gyro_deltas(x_out, y_out);
}

extern "C" void recomp_get_mouse_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    recomp::get_mouse_deltas(x_out, y_out);
}

extern "C" void recomp_powf(uint8_t* rdram, recomp_context* ctx) {
    float a = _arg<0, float>(rdram, ctx);
    float b = ctx->f14.fl; //_arg<1, float>(rdram, ctx);

    _return(ctx, std::pow(a, b));
}

extern "C" void recomp_get_target_framerate(uint8_t* rdram, recomp_context* ctx) {
    int frame_divisor = _arg<0, u32>(rdram, ctx);

    _return(ctx, ultramodern::get_target_framerate(60 / frame_divisor));
}

extern "C" void recomp_get_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::renderer::GraphicsConfig graphics_config = ultramodern::renderer::get_graphics_config();
    float original = _arg<0, float>(rdram, ctx);
    int width, height;
    recompui::get_window_size(width, height);

    switch (graphics_config.ar_option) {
        case ultramodern::renderer::AspectRatio::Original:
        default:
            _return(ctx, original);
            return;
        case ultramodern::renderer::AspectRatio::Expand:
            _return(ctx, std::max(static_cast<float>(width) / height, original));
            return;
    }
}

extern "C" void recomp_get_targeting_mode(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<int>(zelda64::get_targeting_mode()));
}

extern "C" void recomp_get_bgm_volume(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, zelda64::get_bgm_volume() / 100.0f);
}

extern "C" void recomp_get_sfx_volume(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, zelda64::get_sfx_volume() / 100.0f);
}

extern "C" void recomp_get_voice_volume(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, zelda64::get_voice_volume() / 100.0f);
}

extern "C" void recomp_get_low_health_beeps_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<u32>(zelda64::get_low_health_beeps_enabled()));
}

extern "C" void recomp_time_us(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<u32>(
                     std::chrono::duration_cast<std::chrono::microseconds>(ultramodern::time_since_start()).count()));
}

extern "C" void recomp_autosave_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<s32>(zelda64::get_autosave_mode() == zelda64::AutosaveMode::On));
}

// @recomp: Patch the file table for setup files missing from the TLBFREE ROM.
// These files were loaded via IndyComm in the dev build. We inject the correct
// ROM offsets from the original retail ROM where the data is identical.
// The file table is at 0x8012A344 with 12-byte entries: [compressed_size, metadata_ptr, rom_offset]
extern "C" void recomp_patch_setup_file_table(uint8_t* rdram, recomp_context* ctx) {
    // Setup file candidates from the original ROM (1172 compressed)
    // These are the Usetup* files in filename table order:
    // [0]=sevbunker, [1]=statue, [2]=control, [3]=arch, [4]=tra, [5]=dest,
    // [6]=sevb, [7]=azt, [8]=pete, [9]=depo, [10]=ref, [11]=cryp,
    // [12]=dam, [13]=ark, [14]=run, [15]=sevx, [16]=jun, [17]=dish,
    // [18]=cave, [19]=cat, [20]=crad, [21]=sho
    struct { uint32_t file_idx; uint32_t rom_offset; uint32_t comp_size; } patches[] = {
        // File 727 = UsetupdamZ (candidate 12)
        { 727, 0x421480, 0xE70 },
    };

    // Patch BOTH file tables:
    // 12-byte table at 0x8012A344: [compressed_size, metadata_ptr, rom_offset]
    gpr table12 = ADD32(S32(0X8013 << 16), -0X5CBC); // 0x8012A344
    // 20-byte table at 0x8016CBA0: [word0, cached_addr, compressed_size, ...]
    // word0 is decoded ROM offset, used by fileIndexLoadToBank
    gpr table20 = ADD32(S32(0X8017 << 16), -0X3460); // 0x8016CBA0

    for (auto& p : patches) {
        // Patch 12-byte table
        gpr entry12 = ADD32(table12, (gpr)(p.file_idx * 12));
        if ((uint32_t)MEM_W(entry12, 8) == 0) {
            MEM_W(entry12, 0) = (int32_t)p.comp_size;
            MEM_W(entry12, 8) = (int32_t)p.rom_offset;
        }

        // Patch 20-byte table - set the compressed size at word[2] (offset 8)
        // and ensure word[0] is non-zero (so fileIndexLoadToBank uses load_resource)
        gpr entry20 = ADD32(table20, (gpr)(p.file_idx * 20));
        MEM_W(entry20, 8) = (int32_t)p.comp_size; // compressed size

        // Verify the write
        uint32_t verify12 = (uint32_t)MEM_W(entry12, 8);
        uint32_t verify20 = (uint32_t)MEM_W(entry20, 8);
        fprintf(stderr, "[INFO] Patched file %d: ROM=0x%X size=0x%X (verify: 12tbl[8]=0x%X 20tbl[8]=0x%X)\n",
                p.file_idx, p.rom_offset, p.comp_size, verify12, verify20);
    }

    // Set the mema pool size at 0x801084A0 (-ma token value)
    // Normally set by tokenFind("-ma") → strtol("300") → 300 << 10 = 0x4B000
    gpr mema_addr = ADD32(S32(0X8011 << 16), -0X7B60);
    MEM_W(mema_addr, 0) = (int32_t)0x4B000;
    fprintf(stderr, "[INFO] Pre-set mema pool size to 0x4B000 (300KB)\n");
}

extern "C" void recomp_load_overlays(uint8_t* rdram, recomp_context* ctx) {
    u32 rom = _arg<0, u32>(rdram, ctx);
    PTR(void) ram = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);

    load_overlays(rom, ram, size);
}

extern "C" void recomp_high_precision_fb_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, static_cast<s32>(zelda64::renderer::RT64HighPrecisionFBEnabled()));
}

extern "C" void recomp_get_resolution_scale(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, ultramodern::get_resolution_scale());
}

extern "C" void recomp_get_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    zelda64::CameraInvertMode mode = zelda64::get_camera_invert_mode();

    *x_out = (mode == zelda64::CameraInvertMode::InvertX || mode == zelda64::CameraInvertMode::InvertBoth);
    *y_out = (mode == zelda64::CameraInvertMode::InvertY || mode == zelda64::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_get_analog_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    zelda64::CameraInvertMode mode = zelda64::get_analog_camera_invert_mode();

    *x_out = (mode == zelda64::CameraInvertMode::InvertX || mode == zelda64::CameraInvertMode::InvertBoth);
    *y_out = (mode == zelda64::CameraInvertMode::InvertY || mode == zelda64::CameraInvertMode::InvertBoth);
}

extern "C" void recomp_analog_cam_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, zelda64::get_analog_cam_mode() == zelda64::AnalogCamMode::On);
}

extern "C" void recomp_get_camera_inputs(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    // TODO expose this in the menu
    constexpr float radial_deadzone = 0.05f;

    float x, y;

    recomp::get_right_analog(&x, &y);

    float magnitude = sqrtf(x * x + y * y);

    if (magnitude < radial_deadzone) {
        *x_out = 0.0f;
        *y_out = 0.0f;
    } else {
        float x_normalized = x / magnitude;
        float y_normalized = y / magnitude;

        *x_out = x_normalized * ((magnitude - radial_deadzone) / (1 - radial_deadzone));
        *y_out = y_normalized * ((magnitude - radial_deadzone) / (1 - radial_deadzone));
    }
}

extern "C" void recomp_set_right_analog_suppressed(uint8_t* rdram, recomp_context* ctx) {
    s32 suppressed = _arg<0, s32>(rdram, ctx);

    recomp::set_right_analog_suppressed(suppressed);
}
