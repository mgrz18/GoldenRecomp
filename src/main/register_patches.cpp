#include "ovl_patches.hpp"
#include "../../RecompiledPatches/patches_bin.h"
#include "../../RecompiledPatches/recomp_overlays.inl"

#include "librecomp/overlays.hpp"
#include "librecomp/game.hpp"

void zelda64::register_patches() {
  recomp::overlays::register_patches(mm_patches_bin, mm_patches_bin_len, section_table, ARRLEN(section_table));
}
