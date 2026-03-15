#pragma once
#include "validator.hpp"
#include "register_map.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <numeric>
#include <iostream>

namespace rmv {

inline std::string toHex(uint64_t v, int width = 8) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << v;
    return oss.str();
}

// ── Console reporter ──────────────────────────────────────────────────────────
class ConsoleReporter {
public:
    void print(const std::vector<TestResult>& results, const RegisterMap& map) {
        const std::string GREEN  = "\033[32m";
        const std::string RED    = "\033[31m";
        const std::string YELLOW = "\033[33m";
        const std::string CYAN   = "\033[36m";
        const std::string RESET  = "\033[0m";
        const std::string BOLD   = "\033[1m";

        std::cout << "\n" << BOLD << CYAN;
        std::cout << "╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║       Register & Memory Validator  —  RMV v1.0          ║\n";
        std::cout << "║       Validation Report                                  ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n";
        std::cout << RESET;

        // Register map summary
        std::cout << BOLD << "\n[ Register Map ]\n" << RESET;
        for (const auto& [addr, def] : map.definitions()) {
            std::cout << "  " << std::left << std::setw(28) << def.name
                      << toHex(addr) << "  " << std::setw(4) << accessStr(def.access)
                      << "  " << std::to_string(def.width) << "-bit"
                      << "  reset=" << toHex(def.resetValue) << "\n";
        }

        // Test results
        std::cout << BOLD << "\n[ Test Results ]\n" << RESET;
        int pass = 0, fail = 0;
        double totalMs = 0;

        for (const auto& r : results) {
            std::string status = r.passed ? (GREEN + "PASS") : (RED + "FAIL");
            std::cout << "  " << status << RESET << "  "
                      << std::left << std::setw(32) << r.testName
                      << std::fixed << std::setprecision(2) << r.durationMs << " ms\n";

            if (!r.anomalies.empty()) {
                for (const auto& a : r.anomalies) {
                    std::string severity = a.isCritical ? (RED + "[CRITICAL]") : (YELLOW + "[WARNING] ");
                    std::cout << "       " << severity << RESET << " "
                              << anomalyStr(a.type) << " @ " << a.registerName
                              << " (" << toHex(a.address) << ")"
                              << " — " << a.detail << "\n";
                }
            }
            if (!r.detail.empty())
                std::cout << "       " << YELLOW << r.detail << RESET << "\n";

            r.passed ? ++pass : ++fail;
            totalMs += r.durationMs;
        }

        // Transaction log snippet
        std::cout << BOLD << "\n[ Transaction Log (last 10) ]\n" << RESET;
        const auto& txns = map.transactions();
        int start = std::max(0, (int)txns.size() - 10);
        for (int i = start; i < (int)txns.size(); ++i) {
            const auto& t = txns[i];
            std::string opStr;
            switch (t.op) {
                case Transaction::Op::READ:  opStr = CYAN  + "READ "; break;
                case Transaction::Op::WRITE: opStr = GREEN + "WRITE"; break;
                case Transaction::Op::RESET: opStr = YELLOW+ "RESET"; break;
            }
            std::cout << "  [" << t.timestamp << "] " << opStr << RESET
                      << "  " << std::left << std::setw(24) << t.registerName
                      << toHex(t.address) << "  val=" << toHex(t.value) << "\n";
        }

        // Summary
        std::cout << BOLD << "\n╔══════════════════ SUMMARY ══════════════════╗\n" << RESET;
        std::cout << "  Tests  : " << GREEN << pass << " passed" << RESET
                  << ", " << RED << fail << " failed" << RESET << "\n";
        std::cout << "  Time   : " << std::fixed << std::setprecision(2) << totalMs << " ms total\n";
        std::cout << "  Registers validated: " << map.definitions().size() << "\n";
        std::cout << "  Transactions logged: " << txns.size() << "\n";

        bool allOk = (fail == 0);
        if (allOk)
            std::cout << GREEN << BOLD << "\n  ✔ All validation tests passed.\n" << RESET;
        else
            std::cout << RED << BOLD << "\n  ✖ Validation failures detected. Review anomalies above.\n" << RESET;
        std::cout << "\n";
    }
};

// ── CSV reporter ──────────────────────────────────────────────────────────────
class CSVReporter {
public:
    bool write(const std::string& path, const std::vector<TestResult>& results) {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "TestName,Status,DurationMs,AnomalyCount,CriticalCount,Details\n";
        for (const auto& r : results) {
            int critical = 0;
            for (const auto& a : r.anomalies) if (a.isCritical) ++critical;

            f << r.testName << ","
              << (r.passed ? "PASS" : "FAIL") << ","
              << std::fixed << std::setprecision(2) << r.durationMs << ","
              << r.anomalies.size() << ","
              << critical << ","
              << "\"" << r.detail << "\"\n";
        }
        return true;
    }
};

} // namespace rmv
