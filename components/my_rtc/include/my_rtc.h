// my_rtc.h - board-selected RTC: DS3231 on S3, internal RTC timer on P4.
//
// The DS3231 keeps UTC (24 h mode). Its begin() probes 0x68 and runs a function test:
//   - answers on the bus and the registers read back
//   - OSF (oscillator stop flag) clear = the time survived on battery
//   - date/time fields are valid and within 2024..2099
//   - the seconds register ticks (waits for the next second edge, max ~1.1 s; this also aligns
//     the system clock to the RTC second boundary)
//   - temperature register in -40..+85 degC
// write() sets the time at the next second boundary and clears OSF.
//
// P4 uses the internal RTC counter with a trusted UTC anchor in retained RTC memory.
// NTP/manual clock updates establish the anchor; software resets and deep sleep can retain it.
// An abrupt loss of main power is NOT treated as a valid battery-backed calendar.
#pragma once

#include <stdint.h>
#include <time.h>

#include "driver/i2c_master.h"

namespace MyRtc {

constexpr uint8_t I2C_ADDR = 0x68;

enum class State : uint8_t {
    NOT_PROBED = 0,
    MISSING,        // No answer on the bus
    BUS_ERROR,      // Answered on probe but register access failed
    TIME_INVALID,   // OSF set or garbage date: battery empty / never set
    NOT_TICKING,    // Oscillator not running
    OK,
};

struct Status {
    State state;
    bool present;
    bool osf;              // Oscillator stop flag at boot (before any write)
    bool ticking;
    bool timeValid;        // Holds a trustworthy time right now
    float tempC;
    time_t bootTime;       // UTC read at boot (0 = invalid)
    time_t lastWrite;      // UTC of the last write (0 = never this boot)
    uint32_t writes;
    uint32_t errors;
    int32_t lastDriftS;    // RTC - system time at the last sync check
};

// bus: an i2c_master bus already created by the board (unused by the P4 internal RTC).
// Returns true when the RTC holds a valid time (outTime set, UTC).
bool begin(i2c_master_bus_handle_t bus, time_t *outTime = nullptr);

bool read(time_t &utc);                 // Current RTC time (UTC); false on bus error / invalid
bool write(time_t utcNow);              // Waits for the next system-clock second edge, writes, clears OSF
bool readTemp(float &c);
const char *chipName();
bool temperatureAvailable();
// Compares the RTC with the (NTP-correct) system clock and writes it when the drift is >= 1 s,
// the RTC was invalid, or force is set. Returns true when the RTC holds the system time after the call.
bool syncFromSystem(bool force = false);

Status status();
const char *stateText(State s);         // Swedish UI text

}  // namespace MyRtc
