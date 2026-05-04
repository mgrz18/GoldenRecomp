#include "patches.h"

/*
 * FPS Fire Rate Fix
 * =================
 * Problem:
 *   GoldenEye 007 on N64 ran at ~20 fps. The weapon fire rate is controlled by
 *   frame counters that increment by 1 each frame and are checked against
 *   AutomaticFiringRate (e.g. KF7 = 3, RC-P90 = 2). At 20 fps a weapon with
 *   AutomaticFiringRate = 3 fires every 3 frames => ~6.7 shots/sec.
 *
 *   GoldenRecomp runs at 60 fps (3x more frames per second). The same counters
 *   still increment by 1 per frame, so the weapon reaches the firing threshold
 *   3x sooner => 20 shots/sec for the KF7. This affects both the player-
 *   controlled weapons (field_1c counter in handle_weapon_id_values_…) and
 *   NPC weapons (ChrRecord::firecount in chrlvFireWeaponRelated).
 *
 * Solution:
 *   Patch bondwalkItemGetAutomaticFiringRate() to multiply its return value by
 *   FPS_SCALE (3). This scales the threshold that the frame counters must reach
 *   before a shot is allowed, restoring the original cadence at 60 fps.
 *
 *   Value -1 (0xFF as s8) means "fire every frame / no auto-limit" – we leave
 *   it untouched so fully-automatic lasers etc. are not broken.
 *
 *   Applies to all weapons for both the player and all NPCs through the single
 *   shared accessor function.
 *
 * Factor derivation:
 *   TARGET_FPS (60) / ORIGINAL_N64_FPS (20) = 3
 */

/* Ratio of target PC fps to original N64 fps. */
#define FPS_SCALE 3

/*
 * Partial mirror of WeaponStats from the decomp (gun.h).
 * Only fields up to AutomaticFiringRate are needed; the rest are omitted.
 * Field offsets (MIPS, no packed attribute, natural alignment):
 *   0x00 MuzzleFlashExtension f32
 *   0x04 PosX                 f32
 *   0x08 PosY                 f32
 *   0x0C PosZ                 f32
 *   0x10 PlayX                f32
 *   0x14 PlayY                f32
 *   0x18 PlayZ                f32
 *   0x1C AmmoType             s32
 *   0x20 MagSize              s16
 *   0x22 AutomaticFiringRate  u8
 *   0x23 SingleFiringRate     s8
 */
typedef struct WeaponStats {
    f32 MuzzleFlashExtension; /* 0x00 */
    f32 PosX;                 /* 0x04 */
    f32 PosY;                 /* 0x08 */
    f32 PosZ;                 /* 0x0C */
    f32 PlayX;                /* 0x10 */
    f32 PlayY;                /* 0x14 */
    f32 PlayZ;                /* 0x18 */
    s32 AmmoType;             /* 0x1C */
    s16 MagSize;              /* 0x20 */
    u8  AutomaticFiringRate;  /* 0x22 */
    s8  SingleFiringRate;     /* 0x23 */
    /* Remaining fields omitted; not accessed here. */
} WeaponStats;

/* Declared in gun.h of the decomp; resolved by the linker via dump.toml. */
WeaponStats *get_ptr_item_statistics(ITEM_IDS item);

/*
 * RECOMP_PATCH replaces the original bondwalkItemGetAutomaticFiringRate().
 *
 * Original (address 0x7F05DFCC):
 *   s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {
 *       return get_ptr_item_statistics(item)->AutomaticFiringRate;
 *   }
 */
RECOMP_PATCH s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {
    s8 rate = (s8) get_ptr_item_statistics(item)->AutomaticFiringRate;

    /*
     * -1 (stored as 0xFF in u8, cast to s8 becomes -1) means "no limit" –
     * fire every single frame. Leave it as-is.
     */
    if (rate < 0) {
        return rate;
    }

    return (s8)(rate * FPS_SCALE);
}
