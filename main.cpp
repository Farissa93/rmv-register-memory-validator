#include "register_map.hpp"
#include "validator.hpp"
#include "reporter.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace rmv;

// ─────────────────────────────────────────────────────────────────────────────
// Simulated Intel-style register map
// Models a PCH (Platform Controller Hub) block with:
//   - Power management registers
//   - Interrupt control registers
//   - DMA control registers
// ─────────────────────────────────────────────────────────────────────────────
void buildIntelRegisterMap(RegisterMap& map) {

    // ── PWRSTS — Power Status Register (RO, 32-bit) ──────────────────────────
    map.addRegister({
        "PWRSTS", 0x00000000, 32, AccessType::RO, 0x00000003,
        "Power Management Status Register",
        {
            {"PWR_STATE",   1, 0, AccessType::RO,   0x3, "Current power state (00=S0, 01=S1, 10=S3, 11=S5)"},
            {"THERM_TRIP",  2, 2, AccessType::RO,   0x0, "Thermal trip indicator"},
            {"RSVD",       31, 3, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── PWRCTL — Power Control Register (RW, 32-bit) ─────────────────────────
    map.addRegister({
        "PWRCTL", 0x00000004, 32, AccessType::RW, 0x00000001,
        "Power Management Control Register",
        {
            {"PWR_EN",     0, 0, AccessType::RW,   0x1, "Power enable"},
            {"SLEEP_EN",   1, 1, AccessType::RW,   0x0, "Sleep mode enable"},
            {"WAKE_EN",    2, 2, AccessType::RW,   0x0, "Wake event enable"},
            {"CLK_GATE",   4, 3, AccessType::RW,   0x0, "Clock gating control"},
            {"RSVD",      31, 5, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── INTSTS — Interrupt Status Register (RC, 32-bit) ──────────────────────
    map.addRegister({
        "INTSTS", 0x00000010, 32, AccessType::RW, 0x00000000,
        "Interrupt Status Register — bits clear on read",
        {
            {"GPIO_INT",   0, 0, AccessType::RC,   0x0, "GPIO interrupt pending"},
            {"DMA_INT",    1, 1, AccessType::RC,   0x0, "DMA interrupt pending"},
            {"TIMER_INT",  2, 2, AccessType::RC,   0x0, "Timer interrupt pending"},
            {"ERR_INT",    3, 3, AccessType::RC,   0x0, "Error interrupt pending"},
            {"RSVD",      31, 4, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── INTMSK — Interrupt Mask Register (RW, 32-bit) ────────────────────────
    map.addRegister({
        "INTMSK", 0x00000014, 32, AccessType::RW, 0x0000000F,
        "Interrupt Mask Register — 1=masked, 0=enabled",
        {
            {"GPIO_MSK",  0, 0, AccessType::RW, 0x1, "Mask GPIO interrupt"},
            {"DMA_MSK",   1, 1, AccessType::RW, 0x1, "Mask DMA interrupt"},
            {"TIMER_MSK", 2, 2, AccessType::RW, 0x1, "Mask timer interrupt"},
            {"ERR_MSK",   3, 3, AccessType::RW, 0x1, "Mask error interrupt"},
            {"RSVD",     31, 4, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── DMACTL — DMA Control Register (RW, 32-bit) ───────────────────────────
    map.addRegister({
        "DMACTL", 0x00000020, 32, AccessType::RW, 0x00000000,
        "DMA Channel Control Register",
        {
            {"DMA_EN",    0,  0, AccessType::RW,   0x0, "DMA enable"},
            {"BURST_SZ",  3,  1, AccessType::RW,   0x0, "Burst size (0=4B,1=8B,2=16B,3=32B)"},
            {"PRIORITY",  5,  4, AccessType::RW,   0x0, "Channel priority (0=low, 3=high)"},
            {"RSVD",     31,  6, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── DMASTS — DMA Status Register (RO, 32-bit) ────────────────────────────
    map.addRegister({
        "DMASTS", 0x00000024, 32, AccessType::RO, 0x00000000,
        "DMA Status Register",
        {
            {"DMA_BUSY",  0, 0, AccessType::RO, 0x0, "DMA transfer in progress"},
            {"DMA_ERR",   1, 1, AccessType::RO, 0x0, "DMA error flag"},
            {"FIFO_FULL", 2, 2, AccessType::RO, 0x0, "FIFO full indicator"},
            {"RSVD",     31, 3, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── TMRCTL — Timer Control Register (RW, 16-bit) ─────────────────────────
    map.addRegister({
        "TMRCTL", 0x00000030, 16, AccessType::RW, 0x0000,
        "Hardware Timer Control Register",
        {
            {"TMR_EN",    0, 0, AccessType::RW,   0x0, "Timer enable"},
            {"AUTO_RLD",  1, 1, AccessType::RW,   0x0, "Auto-reload enable"},
            {"CLK_SEL",   3, 2, AccessType::RW,   0x0, "Clock source select"},
            {"RSVD",     15, 4, AccessType::RSVD, 0x0, "Reserved"},
        }
    });

    // ── TMRCNT — Timer Count Register (RW, 16-bit) ───────────────────────────
    map.addRegister({
        "TMRCNT", 0x00000032, 16, AccessType::RW, 0x0000,
        "Hardware Timer Count Register (16-bit counter value)",
        {}  // No named fields — raw 16-bit counter
    });

    // ── SCRATCH — Scratch Pad Register (RW, 32-bit) ──────────────────────────
    map.addRegister({
        "SCRATCH", 0x000000FF, 32, AccessType::RW, 0x00000000,
        "Scratch pad register for firmware use",
        {}
    });
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    bool verbose  = false;
    bool csvOut   = false;
    std::string csvPath = "reports/validation_report.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") verbose  = true;
        if (arg == "--csv"     || arg == "-o") csvOut   = true;
        if (arg == "--help"    || arg == "-h") {
            std::cout << "\nUsage: rmv [--verbose] [--csv] [--help]\n\n";
            std::cout << "  --verbose / -v   Show extra detail during validation\n";
            std::cout << "  --csv    / -o    Export results to CSV report\n";
            std::cout << "  --help   / -h    Show this message\n\n";
            return 0;
        }
    }

    // ── Build register map ───────────────────────────────────────────────────
    RegisterMap map;
    buildIntelRegisterMap(map);

    // ── Optional: hook into write events ────────────────────────────────────
    if (verbose) {
        map.onWrite([](const RegisterDef& def, uint64_t oldVal, uint64_t newVal) {
            std::cout << "  [HOOK] " << def.name
                      << " changed: 0x" << std::hex << std::uppercase << oldVal
                      << " → 0x" << newVal << std::dec << "\n";
        });
    }

    // ── Build validation suite ───────────────────────────────────────────────
    Validator validator(map);
    validator.addResetValueTest();
    validator.addReadOnlyProtectionTest();
    validator.addReadWriteTest();
    validator.addReservedBitTest();
    validator.addWalkingOnesTest();
    validator.addBoundaryValueTest();

    // ── Run all tests ────────────────────────────────────────────────────────
    auto results = validator.runAll();

    // ── Print report ─────────────────────────────────────────────────────────
    ConsoleReporter console;
    console.print(results, map);

    // ── Optional CSV export ──────────────────────────────────────────────────
    if (csvOut) {
        CSVReporter csv;
        if (csv.write(csvPath, results))
            std::cout << "CSV report written to: " << csvPath << "\n";
        else
            std::cerr << "Failed to write CSV to: " << csvPath << "\n";
    }

    // Exit code: 0=pass, 1=failures
    bool anyFail = std::any_of(results.begin(), results.end(),
        [](const TestResult& r){ return !r.passed; });
    return anyFail ? 1 : 0;
}
