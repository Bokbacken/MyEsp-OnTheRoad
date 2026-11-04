#include <cstdio>

#include "my_rtc_pcf85063_time.h"

int main()
{
    uint8_t registers[7] = {};
    MyRtcPcf85063Time::encode(static_cast<time_t>(1793820605), registers); // 2026-11-04 19:30:05 UTC
    if (registers[0] != 0x05 || registers[1] != 0x30 || registers[2] != 0x19 ||
        registers[3] != 0x04 || registers[4] != 0x03 || registers[5] != 0x11 || registers[6] != 0x26)
        return 1;

    time_t decoded = 0;
    if (!MyRtcPcf85063Time::decode(registers, decoded) || decoded != static_cast<time_t>(1793820605))
        return 1;

    MyRtcPcf85063Time::encode(static_cast<time_t>(1709208000), registers); // 2024-02-29 12:00:00 UTC
    if (!MyRtcPcf85063Time::decode(registers, decoded) || decoded != static_cast<time_t>(1709208000))
        return 1;

    const uint8_t invalidBcd[7] = {0x6A, 0x00, 0x00, 0x01, 0x04, 0x01, 0x24};
    const uint8_t invalidWeekday[7] = {0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x24};
    const uint8_t invalidDate[7] = {0x00, 0x00, 0x00, 0x29, 0x04, 0x02, 0x25};
    if (MyRtcPcf85063Time::decode(invalidBcd, decoded) ||
        MyRtcPcf85063Time::decode(invalidWeekday, decoded) ||
        MyRtcPcf85063Time::decode(invalidDate, decoded))
        return 1;

    std::puts("PCF85063 UTC encode/decode checks passed");
    return 0;
}
