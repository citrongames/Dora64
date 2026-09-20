#include "doraemon_recomp_hooks.h"
#include "funcs.h"

extern int doraemon_collectible_autosave_enabled(void);
extern void doraemon_show_autosave_notification(void);
extern int doraemon_game_reset_requested(void);
extern void doraemon_clear_game_reset_request(void);
extern void doraemon_prepare_localization_for_game_reset(uint8_t* rdram);
extern void recomp_request_game_reset(uint8_t* rdram);

static int doraemon_autosave_rebaseline_requested = 0;

int doraemon_game_reset_poll(uint8_t* rdram, recomp_context* ctx) {
    if (!doraemon_game_reset_requested()) {
        return 0;
    }

    doraemon_clear_game_reset_request();
    doraemon_autosave_rebaseline_requested = 1;
    doraemon_prepare_localization_for_game_reset(rdram);
    puts("Game soft reset requested at completed frame");
    recomp_request_game_reset(rdram);
    return 1;
}

void doraemon_collectible_autosave_poll(uint8_t* rdram, recomp_context* ctx) {
    static uint32_t doraemon_last_collectibles_low = 0;
    static uint32_t doraemon_last_collectibles_high = 0;
    static uint32_t doraemon_pending_collectibles_low = 0;
    static uint32_t doraemon_pending_collectibles_high = 0;
    static uint32_t doraemon_last_active_slot = 0xFFFFFFFFU;
    static int doraemon_autosave_poll_initialized = 0;

    const gpr doraemon_progress_address = S32(0x800F38A0U);
    const gpr doraemon_save_state_address = S32(0x800EEFB8U);
    const gpr doraemon_active_slot_address = S32(0x800EEFB0U);
    const uint32_t doraemon_active_slot =
        MEM_BU(0X0, doraemon_active_slot_address);
    const uint32_t doraemon_collectibles_low =
        MEM_W(0X28, doraemon_progress_address);
    const uint32_t doraemon_collectibles_high =
        MEM_W(0X2C, doraemon_progress_address);

    if (doraemon_autosave_rebaseline_requested) {
        doraemon_autosave_rebaseline_requested = 0;
        doraemon_autosave_poll_initialized = 0;
        doraemon_pending_collectibles_low = 0;
        doraemon_pending_collectibles_high = 0;
    }

    if (doraemon_active_slot >= 4U) {
        doraemon_autosave_poll_initialized = 0;
        doraemon_pending_collectibles_low = 0;
        doraemon_pending_collectibles_high = 0;
        return;
    }

    // Establish a baseline when the runtime starts or the player changes save
    // slots. This prevents progress loaded from another slot from looking like
    // a newly collected item.
    if (!doraemon_autosave_poll_initialized ||
        doraemon_last_active_slot != doraemon_active_slot) {
        doraemon_autosave_poll_initialized = 1;
        doraemon_last_active_slot = doraemon_active_slot;
        doraemon_last_collectibles_low = doraemon_collectibles_low;
        doraemon_last_collectibles_high = doraemon_collectibles_high;
        doraemon_pending_collectibles_low = 0;
        doraemon_pending_collectibles_high = 0;
        return;
    }

    const uint32_t doraemon_gained_collectibles_low =
        doraemon_collectibles_low & ~doraemon_last_collectibles_low;
    const uint32_t doraemon_gained_collectibles_high =
        doraemon_collectibles_high & ~doraemon_last_collectibles_high;
    doraemon_last_collectibles_low = doraemon_collectibles_low;
    doraemon_last_collectibles_high = doraemon_collectibles_high;

    if (!doraemon_collectible_autosave_enabled()) {
        doraemon_pending_collectibles_low = 0;
        doraemon_pending_collectibles_high = 0;
        return;
    }

    const gpr doraemon_save_record_address = S32(
        0x800EEFC8U + doraemon_active_slot * 0x50U);
    const uint32_t doraemon_saved_collectibles_low =
        MEM_W(0X18, doraemon_save_record_address);
    const uint32_t doraemon_saved_collectibles_high =
        MEM_W(0X1C, doraemon_save_record_address);

    // Only additions to the collectible mask are autosave events. Loading a
    // file, starting a new game, or clearing transient state therefore cannot
    // overwrite a slot. Keep a pending gain until an in-flight regular save is
    // finished, then retry it on the next completed frame.
    doraemon_pending_collectibles_low =
        (doraemon_pending_collectibles_low |
         doraemon_gained_collectibles_low) &
        (doraemon_collectibles_low & ~doraemon_saved_collectibles_low);
    doraemon_pending_collectibles_high =
        (doraemon_pending_collectibles_high |
         doraemon_gained_collectibles_high) &
        (doraemon_collectibles_high & ~doraemon_saved_collectibles_high);

    if ((doraemon_pending_collectibles_low == 0U &&
         doraemon_pending_collectibles_high == 0U) ||
        MEM_W(0X8, doraemon_save_state_address) != 0U) {
        return;
    }

    // Mirror the mutable progress fields used by the tree and level save
    // paths. Keep +10/+12/+14 intact so autosave never moves the established
    // continue/checkpoint location. New files already contain the game's valid
    // starting checkpoint.
    MEM_H(0X16, doraemon_save_record_address) =
        MEM_HU(0X22, doraemon_progress_address);
    MEM_W(0X18, doraemon_save_record_address) =
        doraemon_collectibles_low;
    MEM_W(0X1C, doraemon_save_record_address) =
        doraemon_collectibles_high;
    MEM_W(0X20, doraemon_save_record_address) =
        MEM_W(0X7C, doraemon_progress_address);
    MEM_B(0X24, doraemon_save_record_address) =
        MEM_BU(0X43, doraemon_progress_address);

    for (uint32_t doraemon_index = 0;
         doraemon_index < 18U;
         doraemon_index++) {
        MEM_H(0X26 + doraemon_index * 2U,
              doraemon_save_record_address) =
            MEM_HU(0X46 + doraemon_index * 2U,
                   doraemon_progress_address);
    }

    // The PC runtime writes EEPROM synchronously and guards the backing save
    // buffer. A private context leaves the scheduler's emulated registers
    // untouched.
    recomp_context doraemon_save_ctx = *ctx;
    doraemon_save_ctx.r5 = 2U + doraemon_active_slot * 10U;
    doraemon_save_ctx.r6 = doraemon_save_record_address;
    doraemon_save_ctx.r7 = 0x50U;
    osEepromLongWrite_recomp(rdram, &doraemon_save_ctx);
    if (doraemon_save_ctx.r2 == 0U) {
        doraemon_pending_collectibles_low = 0;
        doraemon_pending_collectibles_high = 0;
        doraemon_show_autosave_notification();
    }
}
