#pragma once
#include "register_map.hpp"
#include "register_types.hpp"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace rmv {

// ── Test case ────────────────────────────────────────────────────────────────
struct TestCase {
    std::string name;
    std::string description;
    std::function<std::vector<Anomaly>(RegisterMap&)> run;
};

// ── Test result ──────────────────────────────────────────────────────────────
struct TestResult {
    std::string          testName;
    bool                 passed{false};
    double               durationMs{0.0};
    std::vector<Anomaly> anomalies;
    std::string          detail;
};

// ── Validator ────────────────────────────────────────────────────────────────
class Validator {
public:
    explicit Validator(RegisterMap& map) : map_(map) {}

    void addTest(TestCase tc) {
        tests_.push_back(std::move(tc));
    }

    // Run all tests and return results
    std::vector<TestResult> runAll() {
        std::vector<TestResult> results;
        for (const auto& tc : tests_) {
            results.push_back(runOne(tc));
        }
        return results;
    }

    // ── Built-in test generators ─────────────────────────────────────────

    // Test: write/read back every RW register and verify value
    void addReadWriteTest() {
        addTest({
            "ReadWriteRoundtrip",
            "Write known pattern to all RW registers and verify read-back matches",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                std::vector<Anomaly> found;
                const uint64_t PATTERN = 0xA5A5A5A5A5A5A5A5ULL;

                for (const auto& [addr, def] : m.definitions()) {
                    if (def.access != AccessType::RW) continue;
                    uint64_t pat = PATTERN & def.maxVal();

                    auto wr = m.write(addr, pat);
                    if (wr.status != OpStatus::OK) {
                        found.insert(found.end(), wr.anomalies.begin(), wr.anomalies.end());
                        continue;
                    }

                    auto rd = m.read(addr);
                    if (rd.value != pat) {
                        found.push_back({
                            AnomalyType::UNEXPECTED_CHANGE, def.name, addr, rd.value,
                            "Readback mismatch: wrote 0x" + toHex(pat) +
                            ", read 0x" + toHex(rd.value), true
                        });
                    }
                }
                return found;
            }
        });
    }

    // Test: attempt to write all RO registers and verify no change
    void addReadOnlyProtectionTest() {
        addTest({
            "ReadOnlyProtection",
            "Attempt writes to all RO registers and verify values are unchanged",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                std::vector<Anomaly> found;
                for (const auto& [addr, def] : m.definitions()) {
                    if (def.access != AccessType::RO) continue;
                    uint64_t before = m.currentValue(addr);
                    auto wr = m.write(addr, ~before & def.maxVal());

                    // We EXPECT anomaly here — if NO anomaly, that's the problem
                    bool hadWriteToRO = std::any_of(wr.anomalies.begin(), wr.anomalies.end(),
                        [](const Anomaly& a){ return a.type == AnomalyType::WRITE_TO_RO; });

                    if (!hadWriteToRO) {
                        found.push_back({
                            AnomalyType::WRITE_TO_RO, def.name, addr, ~before,
                            "RO register accepted a write — protection failure!", true
                        });
                    }
                    // Verify value unchanged
                    uint64_t after = m.currentValue(addr);
                    if (after != before) {
                        found.push_back({
                            AnomalyType::UNEXPECTED_CHANGE, def.name, addr, after,
                            "RO register value changed from 0x" + toHex(before) +
                            " to 0x" + toHex(after) + " after attempted write", true
                        });
                    }
                }
                return found;
            }
        });
    }

    // Test: verify all registers match reset values after resetAll()
    void addResetValueTest() {
        addTest({
            "ResetValueVerification",
            "Reset all registers and verify every register matches its defined reset value",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                m.resetAll();
                return m.verifyResetValues();
            }
        });
    }

    // Test: write reserved bits and check anomaly is raised
    void addReservedBitTest() {
        addTest({
            "ReservedBitProtection",
            "Write 1s to reserved bit fields and verify anomalies are reported",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                std::vector<Anomaly> missed;
                for (const auto& [addr, def] : m.definitions()) {
                    if (def.access == AccessType::RO) continue;
                    for (const auto& field : def.fields) {
                        if (field.access != AccessType::RSVD) continue;
                        // Build a value that sets this reserved field
                        uint64_t val = field.mask() & def.maxVal();
                        auto wr = m.write(addr, val);
                        bool detected = std::any_of(wr.anomalies.begin(), wr.anomalies.end(),
                            [](const Anomaly& a){ return a.type == AnomalyType::RESERVED_BIT_SET; });
                        if (!detected) {
                            missed.push_back({
                                AnomalyType::RESERVED_BIT_SET, def.name, addr, val,
                                "Reserved field '" + field.name + "' write not detected!", true
                            });
                        }
                    }
                }
                return missed;
            }
        });
    }

    // Test: walking 1s pattern across all RW registers
    void addWalkingOnesTest() {
        addTest({
            "WalkingOnesPattern",
            "Walk a single '1' bit across every RW register to detect stuck bits",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                std::vector<Anomaly> found;
                for (const auto& [addr, def] : m.definitions()) {
                    if (def.access != AccessType::RW) continue;
                    for (uint8_t bit = 0; bit < def.width; ++bit) {
                        uint64_t val = (1ULL << bit) & def.maxVal();
                        m.write(addr, val);
                        auto rd = m.read(addr);

                        // Account for WC fields that may self-clear
                        bool isWCBit = false;
                        for (const auto& f : def.fields) {
                            if (f.access == AccessType::WC && (f.mask() & val)) {
                                isWCBit = true; break;
                            }
                        }
                        if (!isWCBit && rd.value != val) {
                            found.push_back({
                                AnomalyType::UNEXPECTED_CHANGE, def.name, addr, rd.value,
                                "Walking-1 bit " + std::to_string(bit) +
                                ": wrote 0x" + toHex(val) +
                                ", read 0x" + toHex(rd.value), true
                            });
                        }
                    }
                }
                return found;
            }
        });
    }

    // Test: boundary values (0x0, max, alternating 0x55/0xAA)
    void addBoundaryValueTest() {
        addTest({
            "BoundaryValues",
            "Write boundary values (0, max, 0x55..., 0xAA...) to all RW registers",
            [](RegisterMap& m) -> std::vector<Anomaly> {
                std::vector<Anomaly> found;
                for (const auto& [addr, def] : m.definitions()) {
                    if (def.access != AccessType::RW) continue;

                    const std::vector<uint64_t> patterns = {
                        0x0ULL,
                        def.maxVal(),
                        0x5555555555555555ULL & def.maxVal(),
                        0xAAAAAAAAAAAAAAAAULL & def.maxVal(),
                    };

                    for (uint64_t pat : patterns) {
                        auto wr = m.write(addr, pat);
                        for (auto& a : wr.anomalies) {
                            if (a.isCritical) found.push_back(a);
                        }
                        auto rd = m.read(addr);
                        if (rd.value != pat) {
                            found.push_back({
                                AnomalyType::UNEXPECTED_CHANGE, def.name, addr, rd.value,
                                "Boundary pattern 0x" + toHex(pat) +
                                ": readback was 0x" + toHex(rd.value), false
                            });
                        }
                    }
                }
                return found;
            }
        });
    }

private:
    TestResult runOne(const TestCase& tc) {
        TestResult result;
        result.testName = tc.name;

        auto start = std::chrono::steady_clock::now();
        try {
            result.anomalies = tc.run(map_);
            result.passed    = result.anomalies.empty();
        } catch (const std::exception& e) {
            result.passed = false;
            result.detail = std::string("Exception: ") + e.what();
        }
        auto end = std::chrono::steady_clock::now();
        result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();

        return result;
    }

    static std::string toHex(uint64_t v) {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << v;
        return oss.str();
    }

    RegisterMap&        map_;
    std::vector<TestCase> tests_;
};

} // namespace rmv
