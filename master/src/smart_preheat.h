#ifndef SMART_PREHEAT_H
#define SMART_PREHEAT_H

#include <stdint.h>

// -----------------------------------------------------------------------------
//  Smart Preheat — a small, deterministic decision-tree "brain" for the master.
//
//  Three operation modes (chosen on the Schedule page, read via ui_manager):
//    OP_DUMB     : reactive only — the brain does nothing (plain boiler).
//    OP_READY_BY : user set a "ready-by" time — preheat so the tank is warm by then.
//    OP_SMART    : learn the household's shower times and preheat automatically.
//
//  "Preheat" simply means: ask SystemManager to warm the tank to its base
//  target (40 C) ahead of the expected shower, so hot water is ready on time.
//  SystemManager still regulates to 40 C and handles safety — the brain only
//  raises the "boiler ON" request during a preheat window. It NEVER overrides
//  a manual ON/OFF and NEVER touches the safety interlocks.
//
//  Learning model: a 96-slot histogram (24 h x 15 min). Every real shower
//  increments its slot. After >= 7 days with the busiest slot seen >= 3 times,
//  that slot is a reliable predicted shower time. Persisted to SPIFFS flash
//  (via DataManager) so it survives reboots and accumulates over 1-2 weeks.
//
//  This is NOT AI — it is transparent, auditable If/Else logic, exactly as the
//  project book specifies (decision tree implemented as efficient firmware code).
// -----------------------------------------------------------------------------

enum OpMode : uint8_t {
    OP_DUMB     = 0,
    OP_READY_BY = 1,
    OP_SMART    = 2,
};

struct PreheatInputs {
    uint32_t unixNow;        ///< epoch seconds (0/invalid => clock not synced yet)
    float    tankTempC;      ///< current tank temperature [C]
    OpMode   mode;           ///< current operation mode
    uint16_t readyByMinute;  ///< minute-of-day the user wants hot water (READY_BY mode)
    uint8_t  household;      ///< number of people (1..8) — small lead-time factor
    bool     manualOn;       ///< user pressed ON on the touchscreen
    bool     plcConnected;   ///< slave link healthy
};

namespace SmartPreheat {
    // Load the saved histogram from flash. Call once, after DataManager::init().
    void init();

    // Record one real shower start (call on the flow rising edge). Persists.
    void recordShower(uint32_t unixNow);

    // Run the decision tree. Call roughly every 60 s with fresh inputs.
    void update(const PreheatInputs& in);

    // Result of the last update(): true => force the internal heater on (preheat).
    bool wantsHeat();

    // --- Diagnostics (for the UI / serial log) ---
    bool     isReliable();       ///< enough data to trust the learned pattern
    uint16_t predictedMinute();  ///< learned peak shower time [min-of-day], 0xFFFF if none
    uint16_t daysObserved();
    uint16_t totalShowers();
}

#endif // SMART_PREHEAT_H
