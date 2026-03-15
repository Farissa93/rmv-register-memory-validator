#pragma once
#include "register_types.hpp"
#include <unordered_map>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace rmv {

// ── Transaction log entry ────────────────────────────────────────────────────
struct Transaction {
    enum class Op { READ, WRITE, RESET };
    Op          op;
    std::string registerName;
    uint64_t    address;
    uint64_t    value;
    uint64_t    prevValue;
    std::string timestamp;
};

// ── Register Map ─────────────────────────────────────────────────────────────
// Simulates a memory-mapped hardware register space
class RegisterMap {
public:
    using WriteCallback = std::function<void(const RegisterDef&, uint64_t oldVal, uint64_t newVal)>;
    using ReadCallback  = std::function<void(const RegisterDef&, uint64_t val)>;

    // Register a hardware register definition
    void addRegister(RegisterDef def) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t addr = def.address;
        std::string name = def.name;
        registerDefs_[addr]  = std::move(def);
        nameToAddr_[name]    = addr;
        registerValues_[addr] = registerDefs_[addr].resetValue;  // Initialize to reset value
    }

    // Perform a register write
    OpResult write(uint64_t address, uint64_t value) {
        std::lock_guard<std::mutex> lock(mutex_);
        OpResult result;

        auto it = registerDefs_.find(address);
        if (it == registerDefs_.end()) {
            result.status = OpStatus::ERROR;
            result.anomalies.push_back({
                AnomalyType::VALUE_OUT_OF_RANGE, "UNKNOWN", address, value,
                "Write to unmapped address 0x" + toHex(address), true
            });
            return result;
        }

        const RegisterDef& def = it->second;
        uint64_t prevValue = registerValues_[address];

        // ── Validation checks ────────────────────────────────────────────
        // 1. Write to read-only
        if (def.access == AccessType::RO) {
            result.anomalies.push_back({
                AnomalyType::WRITE_TO_RO, def.name, address, value,
                "Attempted write 0x" + toHex(value) + " to read-only register", true
            });
            result.status = OpStatus::ANOMALY;
            logTransaction(Transaction::Op::WRITE, def.name, address, value, prevValue);
            return result;
        }

        // 2. Value out of range for register width
        if (value > def.maxVal()) {
            result.anomalies.push_back({
                AnomalyType::VALUE_OUT_OF_RANGE, def.name, address, value,
                "Value 0x" + toHex(value) + " exceeds " + std::to_string(def.width) + "-bit width", true
            });
            result.status = OpStatus::ANOMALY;
        }

        // 3. Check reserved bit fields
        for (const auto& field : def.fields) {
            if (field.access == AccessType::RSVD) {
                uint64_t fieldVal = field.extract(value);
                if (fieldVal != 0) {
                    result.anomalies.push_back({
                        AnomalyType::RESERVED_BIT_SET, def.name, address, value,
                        "Reserved field '" + field.name + "' [" +
                        std::to_string(field.bitHigh) + ":" + std::to_string(field.bitLow) +
                        "] set to 0x" + toHex(fieldVal), false
                    });
                    result.status = OpStatus::ANOMALY;
                }
            }
            // 4. Field overflow check
            if (field.access != AccessType::RSVD) {
                uint64_t fieldVal  = field.extract(value);
                uint64_t fieldMax  = (1ULL << (field.bitHigh - field.bitLow + 1)) - 1;
                if (fieldVal > fieldMax) {
                    result.anomalies.push_back({
                        AnomalyType::FIELD_OVERFLOW, def.name, address, value,
                        "Field '" + field.name + "' value 0x" + toHex(fieldVal) +
                        " overflows " + std::to_string(field.bitHigh - field.bitLow + 1) + " bits",
                        false
                    });
                    result.status = OpStatus::ANOMALY;
                }
            }
        }

        // Apply write (mask to register width)
        uint64_t maskedValue = value & def.maxVal();

        // Handle WC (write-clear) fields: writing 1 clears the bit
        for (const auto& field : def.fields) {
            if (field.access == AccessType::WC) {
                uint64_t writeBits = field.extract(maskedValue);
                uint64_t curBits   = field.extract(prevValue);
                uint64_t newBits   = curBits & ~writeBits;  // Clear bits where 1 was written
                maskedValue = field.insert(maskedValue, newBits);
            }
        }

        registerValues_[address] = maskedValue;
        result.value = maskedValue;
        if (result.status != OpStatus::ANOMALY) result.status = OpStatus::OK;

        logTransaction(Transaction::Op::WRITE, def.name, address, maskedValue, prevValue);

        // Invoke write callbacks
        if (writeCallback_) writeCallback_(def, prevValue, maskedValue);

        return result;
    }

    // Perform a register read
    OpResult read(uint64_t address) {
        std::lock_guard<std::mutex> lock(mutex_);
        OpResult result;

        auto it = registerDefs_.find(address);
        if (it == registerDefs_.end()) {
            result.status = OpStatus::ERROR;
            result.anomalies.push_back({
                AnomalyType::VALUE_OUT_OF_RANGE, "UNKNOWN", address, 0,
                "Read from unmapped address 0x" + toHex(address), true
            });
            return result;
        }

        const RegisterDef& def = it->second;

        // Read from write-only
        if (def.access == AccessType::WO) {
            result.anomalies.push_back({
                AnomalyType::READ_FROM_WO, def.name, address, 0,
                "Attempted read from write-only register", true
            });
            result.status = OpStatus::ANOMALY;
            return result;
        }

        uint64_t val = registerValues_[address];

        // Handle RC (read-clear) fields: reading clears the field
        for (const auto& field : def.fields) {
            if (field.access == AccessType::RC) {
                val = field.insert(val, 0);  // Clear on read
                registerValues_[address] = val;
            }
        }

        result.value  = val;
        result.status = OpStatus::OK;
        logTransaction(Transaction::Op::READ, def.name, address, val, val);

        if (readCallback_) readCallback_(def, val);
        return result;
    }

    // Write by name
    OpResult writeByName(const std::string& name, uint64_t value) {
        auto it = nameToAddr_.find(name);
        if (it == nameToAddr_.end()) {
            OpResult r; r.status = OpStatus::ERROR;
            r.anomalies.push_back({AnomalyType::VALUE_OUT_OF_RANGE, name, 0, value,
                "Unknown register: " + name, true});
            return r;
        }
        return write(it->second, value);
    }

    // Read by name
    OpResult readByName(const std::string& name) {
        auto it = nameToAddr_.find(name);
        if (it == nameToAddr_.end()) {
            OpResult r; r.status = OpStatus::ERROR;
            r.anomalies.push_back({AnomalyType::VALUE_OUT_OF_RANGE, name, 0, 0,
                "Unknown register: " + name, true});
            return r;
        }
        return read(it->second);
    }

    // Reset all registers to their reset values
    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [addr, def] : registerDefs_) {
            registerValues_[addr] = def.resetValue;
            logTransaction(Transaction::Op::RESET, def.name, addr, def.resetValue, registerValues_[addr]);
        }
    }

    // Verify all registers match their reset values
    std::vector<Anomaly> verifyResetValues() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Anomaly> anomalies;
        for (const auto& [addr, def] : registerDefs_) {
            uint64_t cur = registerValues_[addr];
            if (cur != def.resetValue) {
                anomalies.push_back({
                    AnomalyType::RESET_VALUE_MISMATCH, def.name, addr, cur,
                    "Expected 0x" + toHex(def.resetValue) + ", got 0x" + toHex(cur), false
                });
            }
        }
        return anomalies;
    }

    const std::vector<Transaction>& transactions() const { return transactions_; }
    const std::unordered_map<uint64_t, RegisterDef>& definitions() const { return registerDefs_; }
    uint64_t currentValue(uint64_t addr) const {
        auto it = registerValues_.find(addr);
        return (it != registerValues_.end()) ? it->second : 0;
    }

    void onWrite(WriteCallback cb) { writeCallback_ = std::move(cb); }
    void onRead(ReadCallback cb)   { readCallback_  = std::move(cb); }

private:
    static std::string toHex(uint64_t v) {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << v;
        return oss.str();
    }

    void logTransaction(Transaction::Op op, const std::string& name,
                        uint64_t addr, uint64_t val, uint64_t prev) {
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::localtime(&time), "%H:%M:%S");
        transactions_.push_back({op, name, addr, val, prev, ts.str()});
    }

    std::unordered_map<uint64_t, RegisterDef>   registerDefs_;
    std::unordered_map<uint64_t, uint64_t>       registerValues_;
    std::unordered_map<std::string, uint64_t>    nameToAddr_;
    std::vector<Transaction>                     transactions_;
    WriteCallback                                writeCallback_;
    ReadCallback                                 readCallback_;
    mutable std::mutex                           mutex_;
};

} // namespace rmv
