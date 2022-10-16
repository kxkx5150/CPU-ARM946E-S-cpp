
#ifndef DEFINES_H
#define DEFINES_H
#include <cstdio>


#define LOG(...)     (0)
#define FORCE_INLINE inline __attribute__((always_inline))
#define BIT(i)       (1 << (i))
#define SWAP(a, b)                                                                                                     \
    {                                                                                                                  \
        auto c = a;                                                                                                    \
        a      = b;                                                                                                    \
        b      = c;                                                                                                    \
    }
#define U8TO16(data, index) ((data)[index] | ((data)[(index) + 1] << 8))
#define U8TO32(data, index)                                                                                            \
    ((data)[index] | ((data)[(index) + 1] << 8) | ((data)[(index) + 2] << 16) | ((data)[(index) + 3] << 24))
#define U8TO64(data, index) ((uint64_t)U8TO32(data, (index) + 4) << 32) | (uint32_t)U8TO32(data, index)
#define U32TO8(data, index, value)                                                                                     \
    (data)[(index) + 0] = (uint8_t)((value) >> 0);                                                                     \
    (data)[(index) + 1] = (uint8_t)((value) >> 8);                                                                     \
    (data)[(index) + 2] = (uint8_t)((value) >> 16);                                                                    \
    (data)[(index) + 3] = (uint8_t)((value) >> 24);
#endif
