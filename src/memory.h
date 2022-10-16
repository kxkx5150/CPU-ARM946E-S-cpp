
#ifndef MEMORY_H
#define MEMORY_H
#include <cstdint>
#include "defines.h"


class Core;
class VramMapping {
  public:
    void                       add(uint8_t *mapping);
    template <typename T> T    read(uint32_t address);
    template <typename T> void write(uint32_t address, T value);
    uint8_t                   *getBaseMapping()
    {
        return mappings[0];
    }
    int getCount()
    {
        return count;
    }

  private:
    uint8_t *mappings[7];
    int      count = 0;
};


class Memory {
  public:
    Memory(Core *core) : core(core){};
    bool                       loadBios9();
    bool                       loadBios7();
    bool                       loadGbaBios();
    void                       updateMap9(uint32_t start, uint32_t end);
    void                       updateMap7(uint32_t start, uint32_t end);
    template <typename T> T    read(bool cpu, uint32_t address);
    template <typename T> void write(bool cpu, uint32_t address, T value);
    uint8_t                   *getPalette()
    {
        return palette;
    }
    uint8_t *getOam()
    {
        return oam;
    }
    uint8_t **getEngAExtPal()
    {
        return engAExtPal;
    }
    uint8_t **getEngBExtPal()
    {
        return engBExtPal;
    }
    uint8_t **getTex3D()
    {
        return tex3D;
    }
    uint8_t **getPal3D()
    {
        return pal3D;
    }

  private:
    Core    *core;
    uint8_t *readMap9[0x100000]  = {};
    uint8_t *readMap7[0x100000]  = {};
    uint8_t *writeMap9[0x100000] = {};
    uint8_t *writeMap7[0x100000] = {};
    uint8_t  bios9[0x8000]       = {};
    uint8_t  bios7[0x4000]       = {};
    uint8_t  gbaBios[0x4000]     = {};
    uint8_t  ram[0x400000]       = {};
    uint8_t  wram[0x8000]        = {};
    uint8_t  instrTcm[0x8000]    = {};
    uint8_t  dataTcm[0x4000]     = {};
    uint8_t  wram7[0x10000]      = {};
    uint8_t  wifiRam[0x2000]     = {};
    uint8_t  palette[0x800]      = {};
    uint8_t  vramA[0x20000]      = {};
    uint8_t  vramB[0x20000]      = {};
    uint8_t  vramC[0x20000]      = {};
    uint8_t  vramD[0x20000]      = {};
    uint8_t  vramE[0x10000]      = {};
    uint8_t  vramF[0x4000]       = {};
    uint8_t  vramG[0x4000]       = {};
    uint8_t  vramH[0x8000]       = {};
    uint8_t  vramI[0x4000]       = {};
    uint8_t  oam[0x800]          = {};

    VramMapping engABg[32];
    VramMapping engBBg[8];
    VramMapping engAObj[16];
    VramMapping engBObj[8];
    VramMapping lcdc[64];
    VramMapping vram7[2];

    uint8_t *engAExtPal[5] = {};
    uint8_t *engBExtPal[5] = {};
    uint8_t *tex3D[4]      = {};
    uint8_t *pal3D[6]      = {};
    uint8_t *lastGbaBios   = nullptr;
    uint32_t dmaFill[4]    = {};
    uint8_t  vramCnt[9]    = {};
    uint8_t  vramStat      = 0;
    uint8_t  wramCnt       = 0;
    uint8_t  haltCnt       = 0;

    template <typename T> T    readFallback(bool cpu, uint32_t address);
    template <typename T> void writeFallback(bool cpu, uint32_t address, T value);
    template <typename T> T    ioRead9(uint32_t address);
    template <typename T> T    ioRead7(uint32_t address);
    template <typename T> T    ioReadGba(uint32_t address);
    template <typename T> void ioWrite9(uint32_t address, T value);
    template <typename T> void ioWrite7(uint32_t address, T value);
    template <typename T> void ioWriteGba(uint32_t address, T value);

    uint32_t readDmaFill(int channel)
    {
        return dmaFill[channel];
    }
    uint8_t readVramCnt(int block)
    {
        return vramCnt[block];
    }
    uint8_t readVramStat()
    {
        return vramStat;
    }
    uint8_t readWramCnt()
    {
        return wramCnt;
    }
    uint8_t readHaltCnt()
    {
        return haltCnt;
    }

    void writeDmaFill(int channel, uint32_t mask, uint32_t value);
    void writeVramCnt(int index, uint8_t value);
    void writeWramCnt(uint8_t value);
    void writeHaltCnt(uint8_t value);
    void writeGbaHaltCnt(uint8_t value);
};

template uint8_t                     Memory::read(bool cpu, uint32_t address);
template uint16_t                    Memory::read(bool cpu, uint32_t address);
template uint32_t                    Memory::read(bool cpu, uint32_t address);
template <typename T> FORCE_INLINE T Memory::read(bool cpu, uint32_t address)
{
    address &= ~(sizeof(T) - 1);
    uint8_t **readMap = (cpu == 0) ? readMap9 : readMap7;
    if (readMap[address >> 12]) {
        uint8_t *data  = &readMap[address >> 12][address & 0xFFF];
        T        value = 0;
        for (size_t i = 0; i < sizeof(T); i++)
            value |= data[i] << (i * 8);
        return value;
    }
    return readFallback<T>(cpu, address);
}

template void                           Memory::write(bool cpu, uint32_t address, uint8_t value);
template void                           Memory::write(bool cpu, uint32_t address, uint16_t value);
template void                           Memory::write(bool cpu, uint32_t address, uint32_t value);
template <typename T> FORCE_INLINE void Memory::write(bool cpu, uint32_t address, T value)
{
    address &= ~(sizeof(T) - 1);
    uint8_t **writeMap = (cpu == 0) ? writeMap9 : writeMap7;
    if (writeMap[address >> 12]) {
        uint8_t *data = &writeMap[address >> 12][address & 0xFFF];
        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = value >> (i * 8);
        return;
    }
    return writeFallback<T>(cpu, address, value);
}
#endif
