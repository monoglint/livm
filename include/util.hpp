#pragma once

#include <cstdint>
#include <utility>

namespace util {
    inline uint16_t mergel_16(uint8_t b0, uint8_t b1) {
        return (static_cast<uint16_t>(b1) << 8) | b0;
    }

    inline uint32_t mergel_32(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
        return (static_cast<uint32_t>(b3) << 24) |
            (static_cast<uint32_t>(b2) << 16) |
            (static_cast<uint32_t>(b1) << 8)  |
            (static_cast<uint32_t>(b0));
    }

    inline uint64_t mergel_64(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7) {
        return  static_cast<uint64_t>(b0)        |
           (static_cast<uint64_t>(b1) << 8)  |
           (static_cast<uint64_t>(b2) << 16) |
           (static_cast<uint64_t>(b3) << 24) |
           (static_cast<uint64_t>(b4) << 32) |
           (static_cast<uint64_t>(b5) << 40) |
           (static_cast<uint64_t>(b6) << 48) |
           (static_cast<uint64_t>(b7) << 56);
    }

    template <typename CAST_TO, typename BINARY>
    inline CAST_TO cast_binary(BINARY binary) {
        CAST_TO value;
        memcpy(&value, &binary, sizeof(value));
        return value;
    }
}
