#pragma once

namespace MySys {
template <typename T> inline T clamp(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}
}
