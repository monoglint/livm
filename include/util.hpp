#pragma once

#include <cstdint>
#include <utility>
#include <string>

template <typename T>
inline void do_not_optimize_away(T&& value) {
    asm volatile("" :: "r,m"(value) : "memory");
}

namespace bit_util {
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

    inline void splitl_16(const uint16_t value, uint8_t& b0, uint8_t& b1) {
        b0 = value & 0xFF;
        b1 = (value >> 8) & 0xFF;
    }

    inline void splitl_32(const uint32_t value, uint8_t& b0, uint8_t& b1, uint8_t& b2, uint8_t& b3) {
        b0 = value & 0xFF;
        b1 = (value >> 8) & 0xFF;
        b2 = (value >> 16) & 0xFF;
        b3 = (value >> 24) & 0xFF;
    }

    inline void splitl_64(const uint64_t value, uint8_t& b0, uint8_t& b1, uint8_t& b2, uint8_t& b3, uint8_t& b4, uint8_t& b5, uint8_t& b6, uint8_t& b7) {
        b0 = value & 0xFF;
        b1 = (value >> 8) & 0xFF;
        b2 = (value >> 16) & 0xFF;
        b3 = (value >> 24) & 0xFF;
        b4 = (value >> 32) & 0xFF;
        b5 = (value >> 40) & 0xFF;
        b6 = (value >> 48) & 0xFF;
        b7 = (value >> 56) & 0xFF;
    }

    template <typename FROM, typename CAST_TO>
    // Unsafe function - sizes of both types are not checked.
    inline CAST_TO bit_cast(FROM from) {
        CAST_TO to;
        memcpy(&to, &from, std::min(sizeof(from), sizeof(to)));
        return to;
    }
}

namespace str_util {
    static inline void write_8(std::string& buffer, const uint8_t data) {
        buffer += data;
    }

    static inline void write_16(std::string& buffer, const uint16_t data) {
        uint8_t b0, b1;
        bit_util::splitl_16(data, b0, b1);
        buffer += b0;
        buffer += b1;
    }

    static inline void write_32(std::string& buffer, const uint32_t data) {
        uint8_t b0, b1, b2, b3;
        bit_util::splitl_32(data, b0, b1, b2, b3);
        buffer += b0;
        buffer += b1;
        buffer += b2;
        buffer += b3;
    }

    static inline void write_64(std::string& buffer, const uint64_t data) {
        uint8_t b0, b1, b2, b3, b4, b5, b6, b7;
        bit_util::splitl_64(data, b0, b1, b2, b3, b4, b5, b6, b7);
        buffer += b0;
        buffer += b1;
        buffer += b2;
        buffer += b3;
        buffer += b4;
        buffer += b5;
        buffer += b6;
        buffer += b7;
    }
}