#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <stdexcept>

namespace rmv {

// ── Register access types ────────────────────────────────────────────────────
enum class AccessType { RO, WO, RW, RC, WC, RSVD };  // Read-Only, Write-Only, Read-Write, Read-Clear, Write-Clear, Reserved

inline std::string accessStr(AccessType a) {
    switch (a) {
        case AccessType::RO:   return "RO";
        case AccessType::WO:   return "WO";
        case AccessType::RW:   return "RW";
        case AccessType::RC:   return "RC";
        case AccessType::WC:   return "WC";
        case AccessType::RSVD: return "RSVD";
    }
    return "??";
}

// ── Bit field within a register ──────────────────────────────────────────────
struct BitField {
    std::string name;
    uint8_t     bitHigh;        // MSB position (0-based)
    uint8_t     bitLow;         // LSB position (0-based)
    AccessType  access;
    uint64_t    resetValue;     // Value after reset
    std::string description;

    uint64_t mask() const {
        uint64_t m = 0;
        for (uint8_t i = bitLow; i <= bitHigh; ++i) m |= (1ULL << i);
        return m;
    }

    uint64_t extract(uint64_t regVal) const {
        return (regVal & mask()) >> bitLow;
    }

    uint64_t insert(uint64_t regVal, uint64_t fieldVal) const {
        return (regVal & ~mask()) | ((fieldVal << bitLow) & mask());
    }
};

// ── Register definition ──────────────────────────────────────────────────────
struct RegisterDef {
    std::string            name;
    uint64_t               address;     // Base address or offset
    uint8_t                width;       // 8, 16, 32, or 64 bits
    AccessType             access;
    uint64_t               resetValue;
    std::string            description;
    std::vector<BitField>  fields;

    uint64_t maxVal() const {
        if (width >= 64) return UINT64_MAX;
        return (1ULL << width) - 1;
    }

    std::optional<BitField> findField(const std::string& fieldName) const {
        for (const auto& f : fields)
            if (f.name == fieldName) return f;
        return std::nullopt;
    }
};

// ── Anomaly types ────────────────────────────────────────────────────────────
enum class AnomalyType {
    WRITE_TO_RO,          // Writing to a read-only register
    READ_FROM_WO,         // Reading from a write-only register
    VALUE_OUT_OF_RANGE,   // Value exceeds register width
    RESET_VALUE_MISMATCH, // Value doesn't match expected reset value
    RESERVED_BIT_SET,     // Reserved bit was written
    FIELD_OVERFLOW,       // Field value exceeds bit width
    STICKY_BIT_NOT_CLEAR, // RC/WC bit not cleared after operation
    UNEXPECTED_CHANGE,    // Register changed without a write
};

inline std::string anomalyStr(AnomalyType t) {
    switch (t) {
        case AnomalyType::WRITE_TO_RO:           return "WRITE_TO_RO";
        case AnomalyType::READ_FROM_WO:          return "READ_FROM_WO";
        case AnomalyType::VALUE_OUT_OF_RANGE:    return "VALUE_OUT_OF_RANGE";
        case AnomalyType::RESET_VALUE_MISMATCH:  return "RESET_VALUE_MISMATCH";
        case AnomalyType::RESERVED_BIT_SET:      return "RESERVED_BIT_SET";
        case AnomalyType::FIELD_OVERFLOW:        return "FIELD_OVERFLOW";
        case AnomalyType::STICKY_BIT_NOT_CLEAR:  return "STICKY_BIT_NOT_CLEAR";
        case AnomalyType::UNEXPECTED_CHANGE:     return "UNEXPECTED_CHANGE";
    }
    return "UNKNOWN";
}

// ── Anomaly record ───────────────────────────────────────────────────────────
struct Anomaly {
    AnomalyType type;
    std::string registerName;
    uint64_t    address;
    uint64_t    value;
    std::string detail;
    bool        isCritical;
};

// ── Operation result ─────────────────────────────────────────────────────────
enum class OpStatus { OK, ANOMALY, ERROR };

struct OpResult {
    OpStatus            status{OpStatus::OK};
    uint64_t            value{0};
    std::vector<Anomaly> anomalies;
};

} // namespace rmv
