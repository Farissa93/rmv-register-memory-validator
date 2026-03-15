# RMV — C++20 Register & Memory Validator

A **hardware register validation framework** written in Modern C++20. RMV simulates an Intel-style memory-mapped register space and runs a suite of automated validation tests to detect anomalies — modelling the register verification techniques used in real semiconductor validation workflows.

Built to demonstrate hardware-aware automation engineering skills relevant to roles in the semiconductor and embedded systems industry.

---

## Demo

```
╔══════════════════════════════════════════════════════════╗
║       Register & Memory Validator  —  RMV v1.0          ║
║       Validation Report                                  ║
╚══════════════════════════════════════════════════════════╝

[ Register Map ]
  PWRSTS       0x00000000   RO   32-bit   reset=0x00000003
  PWRCTL       0x00000004   RW   32-bit   reset=0x00000001
  INTSTS       0x00000010   RW   32-bit   reset=0x00000000
  INTMSK       0x00000014   RW   32-bit   reset=0x0000000F
  DMACTL       0x00000020   RW   32-bit   reset=0x00000000
  DMASTS       0x00000024   RO   32-bit   reset=0x00000000
  TMRCTL       0x00000030   RW   16-bit   reset=0x00000000
  TMRCNT       0x00000032   RW   16-bit   reset=0x00000000

[ Test Results ]
  PASS   ResetValueVerification      1.09 ms
  PASS   ReadOnlyProtection          0.20 ms
  PASS   ReadWriteRoundtrip          0.72 ms
  PASS   ReservedBitProtection       0.39 ms
  PASS   WalkingOnesPattern         30.72 ms
  PASS   BoundaryValues              4.21 ms

[ Transaction Log ]
  [09:14:22] WRITE   PWRCTL    0x00000004   val=0x00000001
  [09:14:22] READ    PWRCTL    0x00000004   val=0x00000001
  [09:14:22] WRITE   INTMSK    0x00000014   val=0x0000000F

╔══════════════════ SUMMARY ══════════════════╗
  Tests  : 6 passed, 0 failed
  Time   : 36.33 ms total
  Registers validated : 9
  Transactions logged : 465
✔ All validation tests passed.
```

---

## Features

- **Memory-mapped register simulation** — models Intel PCH-style registers (power management, DMA, interrupt controller, hardware timer)
- **Access-type enforcement** — correctly handles `RO`, `RW`, `WO`, `RC` (read-clear), `WC` (write-clear), and `RSVD` (reserved) bit field semantics
- **6-suite validation engine** — reset verification, read-only protection, read/write roundtrip, reserved-bit detection, walking-ones pattern, and boundary value tests
- **8-type anomaly detection** — classifies faults as `WRITE_TO_RO`, `READ_FROM_WO`, `RESERVED_BIT_SET`, `RESET_VALUE_MISMATCH`, `UNEXPECTED_CHANGE`, `FIELD_OVERFLOW`, and more
- **Transaction logger** — records every read, write, and reset operation with timestamps
- **CSV report exporter** — exports full validation results for post-run analysis
- **Write/read callbacks** — hook into register events for custom monitoring

---

## Project Structure

```
RMV/
├── CMakeLists.txt              # CMake build (C++20)
├── include/
│   ├── register_types.hpp      # Core types — BitField, RegisterDef, Anomaly
│   ├── register_map.hpp        # Simulated memory-mapped register space
│   ├── validator.hpp           # Validation engine and test suite runner
│   └── reporter.hpp            # Console and CSV report generation
├── src/
│   └── main.cpp                # Intel-style register map + test runner
└── reports/                    # CSV report output
```

---

## Build & Run

### Prerequisites
- GCC 11+ or Clang 13+ with C++20 support
- CMake 3.20+
- Linux / macOS / WSL2

### Build
```bash
# Using CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4

# Or directly with g++
mkdir build
g++ -std=c++20 -O2 -Iinclude src/main.cpp -lpthread -o build/rmv
```

### Run
```bash
# Basic validation run
./build/rmv

# Verbose mode — shows register change events
./build/rmv --verbose

# Export results to CSV
./build/rmv --csv

# Help
./build/rmv --help
```

---

## Register Map (Simulated Intel PCH)

| Register | Address | Access | Width | Description |
|---|---|---|---|---|
| `PWRSTS` | `0x0000` | RO | 32-bit | Power status — current state, thermal trip |
| `PWRCTL` | `0x0004` | RW | 32-bit | Power control — enable, sleep, wake, clock gate |
| `INTSTS` | `0x0010` | RC | 32-bit | Interrupt status — clears on read |
| `INTMSK` | `0x0014` | RW | 32-bit | Interrupt mask — GPIO, DMA, timer, error |
| `DMACTL` | `0x0020` | RW | 32-bit | DMA control — enable, burst size, priority |
| `DMASTS` | `0x0024` | RO | 32-bit | DMA status — busy, error, FIFO full |
| `TMRCTL` | `0x0030` | RW | 16-bit | Timer control — enable, auto-reload, clock select |
| `TMRCNT` | `0x0032` | RW | 16-bit | Timer count register |

---

## Validation Test Suites

| Test | What It Checks |
|---|---|
| `ResetValueVerification` | All registers match their defined reset values after a reset |
| `ReadOnlyProtection` | RO registers correctly reject write attempts |
| `ReadWriteRoundtrip` | Write 0xA5A5... pattern to all RW registers and verify readback |
| `ReservedBitProtection` | Writing to reserved fields raises the correct anomaly |
| `WalkingOnesPattern` | Walk a single 1-bit across every RW register to detect stuck bits |
| `BoundaryValues` | Test 0x0, max, 0x5555..., 0xAAAA... patterns on all RW registers |

---

## Anomaly Types

| Anomaly | Severity | Description |
|---|---|---|
| `WRITE_TO_RO` | Critical | Write attempted on a read-only register |
| `READ_FROM_WO` | Critical | Read attempted on a write-only register |
| `RESERVED_BIT_SET` | Warning | Reserved bit field written with non-zero value |
| `RESET_VALUE_MISMATCH` | Warning | Register value does not match defined reset value |
| `UNEXPECTED_CHANGE` | Critical | Register value changed without an explicit write |
| `VALUE_OUT_OF_RANGE` | Critical | Value exceeds register bit width |
| `FIELD_OVERFLOW` | Warning | Bit field value exceeds its defined width |
| `STICKY_BIT_NOT_CLEAR` | Warning | RC/WC bit not cleared after expected operation |

---

## Key C++ Concepts Demonstrated

| Concept | Where Used |
|---|---|
| `std::unordered_map` | Register address-to-value and address-to-definition lookup |
| `std::optional` | Safe bit field lookup by name |
| `std::function` | Write/read callback hooks |
| `std::mutex`, `std::lock_guard` | Thread-safe register read/write operations |
| `std::variant`, `enum class` | Typed anomaly and access-type classification |
| `std::chrono` | Per-test duration measurement |
| `std::ofstream` | CSV report file generation |
| Bit manipulation | Mask, extract, insert operations on register bit fields |
| RAII | Lock guards, file handles managed automatically |

---

