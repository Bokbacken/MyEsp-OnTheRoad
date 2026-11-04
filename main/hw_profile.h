// hw_profile.h - board profile: board-specific facts kept out of the application logic.
// Selected at build time via Kconfig (OTR_HW_PROFILE); default is the current board (ES3C35P).
#pragma once
#include <stdint.h>

namespace HwProfile {

enum class Id : uint8_t {
    ES3C35P,   // ESP32-S3, ST77922 QSPI 320x480 (current, working board)
    P4_43,     // Waveshare ESP32-P4 4.3" 480x800
    S3_35B,    // Waveshare ESP32-S3 3.5B, AXS15231B QSPI 320x480
};

struct Info {
    Id id;
    const char *name;
    int32_t width;    // logical width in the default orientation (portrait)
    int32_t height;   // logical height in the default orientation (portrait)
    bool hasTouch;
    bool hasAudio;
    bool hasSd;
    bool hasRgbLed;
    bool hasHostRadio;
    uint32_t flashBytes;
    uint32_t psramBytes;
    uint32_t soundFontMaxBytes;
    uint32_t psramReserveBytes;
    bool resampleSoundFonts;
    uint16_t maxKaraokeVoices;
    uint32_t displayActiveWidthUm;   // Physical lit area, not glass/module outline; zero if unknown
    uint32_t displayActiveHeightUm;

    uint32_t pixelPitchXUm() const
    {
        return width > 0 ? (displayActiveWidthUm + width / 2) / width : 0;
    }

    uint32_t pixelPitchYUm() const
    {
        return height > 0 ? (displayActiveHeightUm + height / 2) / height : 0;
    }

    uint32_t ppiX() const
    {
        return width > 0 && displayActiveWidthUm
                   ? (static_cast<uint64_t>(width) * 25400u + displayActiveWidthUm / 2) /
                         displayActiveWidthUm
                   : 0;
    }

    uint32_t ppiY() const
    {
        return height > 0 && displayActiveHeightUm
                   ? (static_cast<uint64_t>(height) * 25400u + displayActiveHeightUm / 2) /
                         displayActiveHeightUm
                   : 0;
    }
};

const Info &active();
const Info &get(Id id);
Id activeId();

}  // namespace HwProfile
