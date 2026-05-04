#include "patches.h"

int dummy;
int dummy3 = 1;

void init(void);

// TLBFREE addresses (built from TLBFREE ELF with physical addresses):
// code   segment: ROM 0x001050, RAM 0x80000450, size 0x020940
// inflate segment: ROM 0x033590, RAM 0x80020D90, size 0x0015A0
// game   segment: ROM 0x034B30, RAM 0x80022330, size 0x0E2D50
// cseg   segment: ROM 0xC00000, RAM 0x80105080, size 0x03C550

RECOMP_PATCH void boot(void) {
    // Skip TLB setup (not needed with TLBFREE ELF) and jump directly to init
    recomp_printf("[DEBUG-PATCH] boot() called, jumping to init()\n");
    init();
}

RECOMP_PATCH void init(void) {
    s32* stack_pointer;

    recomp_printf("[DEBUG-PATCH] init() called\n");

    osInitialize();
    recomp_printf("[DEBUG-PATCH] osInitialize done\n");

    // Load the game segment (0x80022330) - partially beyond the initial 1MB DMA
    recomp_printf("[DEBUG-PATCH] Loading game segment...\n");
    recomp_load_overlays((u32) 0x00034B30, (void*) 0x80022330, (u32) 0x000E2D50);
    boot_osPiRawStartDma(OS_READ, (u32) 0x00034B30, (void*) 0x80022330, (u32) 0x000E2D50);
    recomp_printf("[DEBUG-PATCH] Game segment loaded\n");

    // Load the csegment (data/rodata) beyond the initial 1MB DMA
    recomp_printf("[DEBUG-PATCH] Loading cseg segment...\n");
    boot_osPiRawStartDma(OS_READ, (u32) 0x00C00000, (void*) 0x80105080, (u32) 0x0003C550);
    recomp_printf("[DEBUG-PATCH] cseg segment loaded\n");

    stack_pointer = setSPToEnd(sp_main, sizeof(sp_main));
    recomp_printf("[DEBUG-PATCH] Creating main thread...\n");
    osCreateThread(&mainThread, (OSId) 3, &mainproc, NULL, stack_pointer, (OSPri) 10);
    recomp_printf("[DEBUG-PATCH] Starting main thread...\n");
    osStartThread(&mainThread);
    recomp_printf("[DEBUG-PATCH] Main thread started\n");

    // @recomp: ModelDistance always disabled
    g_ModelDistanceDisabled = 1;
    recomp_printf("[DEBUG-PATCH] init() complete\n");
}

/**
 * Stubbing this since it causes the runtime to crash while trying to resolve rmonMain
 * It isn't needed for the game anyway
 */
#if 1
RECOMP_PATCH void rmonCreateThread(void) {
    return;
}
#endif

/**
 * tlbmanageGetTlbAllocatedBlock returns the end of the usable RAM area
 * (start of the stacks segment), used by mempCheckMemflagTokens to compute
 * the memory pool size.  The original function reads g_tlbmanageTlbAllocatedBlock
 * which is set by tlbmanageEstablishManagementTable (also stubbed).
 *
 * In the TLBFREE binary: _stacksSegmentStart = 0x803AB400
 * Pool spans from _bssSegmentEnd (0x80172650) to _stacksSegmentStart.
 */
RECOMP_PATCH void* tlbmanageGetTlbAllocatedBlock(void) {
    return (void*)0x803AB400U;  // _stacksSegmentStart from ge007.tlbfree.u.map
}

/**
 * joyGamePakProbe: the original calls joyDisablePoll/joyEnablePoll around
 * osEepromProbe, but joyDisablePoll blocks waiting for the controller polling
 * thread to respond. During early init, the polling thread hasn't started
 * processing messages yet, causing a deadlock.
 *
 * Since the recomp always has EEPROM available (file-backed saves),
 * just call osEepromProbe directly without disabling/enabling polling.
 */
extern OSMesgQueue g_ContInputMessageQueue;

RECOMP_PATCH s32 joyGamePakProbe(void) {
    return osEepromProbe(&g_ContInputMessageQueue);
}

RECOMP_PATCH s32 joyGamePakRead(u8 address, u8 *buffer) {
    return osEepromRead(&g_ContInputMessageQueue, address, buffer);
}

RECOMP_PATCH s32 joyGamePakWrite(u8 address, u8 *buffer) {
    return osEepromWrite(&g_ContInputMessageQueue, address, buffer);
}

RECOMP_PATCH s32 joyGamePakLongRead(u8 address, u8 *buffer, s32 nbytes) {
    return osEepromLongRead(&g_ContInputMessageQueue, address, buffer, nbytes);
}

RECOMP_PATCH s32 joyGamePakLongWrite(u8 address, u8 *buffer, s32 nbytes) {
    return osEepromLongWrite(&g_ContInputMessageQueue, address, buffer, nbytes);
}
