
#include <cstring>
#include "cartridge.h"
#include "core.h"
#include "settings.h"


Cartridge::~Cartridge()
{
    writeSave();
    if (romFile)
        fclose(romFile);
    if (rom)
        delete[] rom;
    if (save)
        delete[] save;
}
bool Cartridge::loadRom(std::string path)
{
    romName = path;
    romFile = fopen(romName.c_str(), "rb");
    if (!romFile)
        return false;
    fseek(romFile, 0, SEEK_END);
    romSize = ftell(romFile);
    fseek(romFile, 0, SEEK_SET);
    std::string ext = core->getId() ? (".sv" + std::to_string(core->getId() + 1)) : ".sav";
    saveName        = path.substr(0, path.rfind(".")) + ext;
    FILE *saveFile  = fopen(saveName.c_str(), "rb");
    if (saveFile) {
        fseek(saveFile, 0, SEEK_END);
        saveSize = ftell(saveFile);
        fseek(saveFile, 0, SEEK_SET);
        save   = new uint8_t[saveSize];
        auto _ = fread(save, sizeof(uint8_t), saveSize, saveFile);
        fclose(saveFile);
    }
    return true;
}
void Cartridge::loadRomSection(size_t offset, size_t size)
{
    if (rom)
        delete[] rom;
    rom = new uint8_t[size];
    fseek(romFile, offset, SEEK_SET);
    auto _ = fread(rom, sizeof(uint8_t), size, romFile);
}
void Cartridge::writeSave()
{
    mutex.lock();
    if (saveDirty) {
        if (FILE *saveFile = fopen(saveName.c_str(), "wb")) {
            LOG("Writing save file to disk\n");
            fwrite(save, sizeof(uint8_t), saveSize, saveFile);
            fclose(saveFile);
            saveDirty = false;
        }
    }
    mutex.unlock();
}
void Cartridge::trimRom()
{
    int newSize;
    for (newSize = romSize & ~3; newSize > 0; newSize -= 4) {
        if (U8TO32(rom, newSize - 4) != 0xFFFFFFFF)
            break;
    }
    if (newSize < romSize) {
        romSize         = newSize;
        uint8_t *newRom = new uint8_t[newSize];
        memcpy(newRom, rom, newSize * sizeof(uint8_t));
        delete[] rom;
        rom           = newRom;
        FILE *romFile = fopen(romName.c_str(), "wb");
        if (romFile) {
            if (newSize > 0)
                fwrite(rom, sizeof(uint8_t), newSize, romFile);
            fclose(romFile);
        }
    }
}
void Cartridge::resizeSave(int newSize, bool dirty)
{
    mutex.lock();
    uint8_t *newSave = new uint8_t[newSize];
    if (saveSize < newSize) {
        if (saveSize < 0)
            saveSize = 0;
        memcpy(newSave, save, saveSize * sizeof(uint8_t));
        memset(&newSave[saveSize], 0xFF, (newSize - saveSize) * sizeof(uint8_t));
    } else {
        memcpy(newSave, save, newSize * sizeof(uint8_t));
    }
    delete[] save;
    save     = newSave;
    saveSize = newSize;
    if (dirty)
        saveDirty = true;
    mutex.unlock();
}
CartridgeNds::CartridgeNds(Core *core) : Cartridge(core)
{
    wordReadyTasks[0] = std::bind(&CartridgeNds::wordReady, this, 0);
    wordReadyTasks[1] = std::bind(&CartridgeNds::wordReady, this, 1);
}
bool CartridgeNds::loadRom(std::string path)
{
    bool res = Cartridge::loadRom(path);
    if (romSize <= 0x20000000) {
        try {
            loadRomSection(0, romSize);
            fclose(romFile);
            romFile = nullptr;
        } catch (std::bad_alloc &ba) {
            loadRomSection(0, 0x5000);
        }
    } else {
        loadRomSection(0, 0x5000);
    }
    for (romMask = 1; romMask < romSize; romMask <<= 1)
        ;
    romMask -= 1;
    romCode = U8TO32(rom, 0x0C);
    if (romSize >= 0x8000) {
        uint64_t data = U8TO64(rom, 0x4000);
        initKeycode(2);
        data = decrypt64(data);
        initKeycode(3);
        data = decrypt64(data);
        if (data == 0x6A624F7972636E65) {
            LOG("Detected an encrypted ROM!\n");
            romEncrypted = true;
        }
    }
    if (!save)
        saveSize = -1;
    return res;
}
void CartridgeNds::directBoot()
{
    if (romFile)
        loadRomSection(0, 0x170);

    uint32_t offset9    = U8TO32(rom, 0x20);
    uint32_t entryAddr9 = U8TO32(rom, 0x24);
    uint32_t ramAddr9   = U8TO32(rom, 0x28);
    uint32_t size9      = U8TO32(rom, 0x2C);

    LOG("ARM9 code ROM offset:    0x%X\n", offset9);
    LOG("ARM9 code entry address: 0x%X\n", entryAddr9);
    LOG("ARM9 RAM address:        0x%X\n", ramAddr9);
    LOG("ARM9 code size:          0x%X\n", size9);

    uint32_t offset7    = U8TO32(rom, 0x30);
    uint32_t entryAddr7 = U8TO32(rom, 0x34);
    uint32_t ramAddr7   = U8TO32(rom, 0x38);
    uint32_t size7      = U8TO32(rom, 0x3C);

    LOG("ARM7 code ROM offset:    0x%X\n", offset7);
    LOG("ARM7 code entry address: 0x%X\n", entryAddr7);
    LOG("ARM7 RAM address:        0x%X\n", ramAddr7);
    LOG("ARM7 code size:          0x%X\n", size7);

    for (uint32_t i = 0; i < 0x170; i += 4)
        core->memory.write<uint32_t>(0, 0x27FFE00 + i, U8TO32(rom, i));

    uint32_t offset;
    if (romFile) {
        loadRomSection(offset9, size9);
        offset = 0;
    } else {
        offset = offset9;
    }

    for (uint32_t i = 0; i < size9; i += 4) {
        if (romEncrypted && offset9 + i >= 0x4000 && offset9 + i < 0x4800) {
            if (offset9 + i < 0x4008) {
                core->memory.write<uint32_t>(0, ramAddr9 + i, 0xE7FFDEFF);
            } else {
                initKeycode(3);
                uint64_t data = decrypt64(U8TO64(rom, (offset + i) & ~7));
                core->memory.write<uint32_t>(0, ramAddr9 + i, data >> (((offset + i) & 4) ? 32 : 0));
            }
        } else {
            core->memory.write<uint32_t>(0, ramAddr9 + i, U8TO32(rom, offset + i));
        }
    }

    if (romFile) {
        loadRomSection(offset7, size7);
        offset = 0;
    } else {
        offset = offset7;
    }

    for (uint32_t i = 0; i < size7; i += 4) {
        if (romEncrypted && offset7 + i >= 0x4000 && offset7 + i < 0x4800) {
            if (offset7 + i < 0x4008) {
                core->memory.write<uint32_t>(1, ramAddr7 + i, 0xE7FFDEFF);
            } else {
                initKeycode(3);
                uint64_t data = decrypt64(U8TO64(rom, (offset + i) & ~7));
                core->memory.write<uint32_t>(1, ramAddr7 + i, data >> (((offset + i) & 4) ? 32 : 0));
            }
        } else {
            core->memory.write<uint32_t>(1, ramAddr7 + i, U8TO32(rom, offset + i));
        }
    }
}
uint64_t CartridgeNds::encrypt64(uint64_t value)
{
    uint32_t y = value;
    uint32_t x = value >> 32;
    for (int i = 0x00; i <= 0x0F; i++) {
        uint32_t z = encTable[i] ^ x;
        x          = encTable[0x012 + ((z >> 24) & 0xFF)];
        x          = encTable[0x112 + ((z >> 16) & 0xFF)] + x;
        x          = encTable[0x212 + ((z >> 8) & 0xFF)] ^ x;
        x          = encTable[0x312 + ((z >> 0) & 0xFF)] + x;
        x ^= y;
        y = z;
    }
    return ((uint64_t)(y ^ encTable[0x11]) << 32) | (x ^ encTable[0x10]);
}
uint64_t CartridgeNds::decrypt64(uint64_t value)
{
    uint32_t y = value;
    uint32_t x = value >> 32;
    for (int i = 0x11; i >= 0x02; i--) {
        uint32_t z = encTable[i] ^ x;
        x          = encTable[0x012 + ((z >> 24) & 0xFF)];
        x          = encTable[0x112 + ((z >> 16) & 0xFF)] + x;
        x          = encTable[0x212 + ((z >> 8) & 0xFF)] ^ x;
        x          = encTable[0x312 + ((z >> 0) & 0xFF)] + x;
        x ^= y;
        y = z;
    }
    return ((uint64_t)(y ^ encTable[0x00]) << 32) | (x ^ encTable[0x01]);
}
void CartridgeNds::initKeycode(int level)
{
    for (int i = 0; i < 0x412; i++)
        encTable[i] = core->memory.read<uint32_t>(1, 0x30 + i * 4);
    encCode[0] = romCode;
    encCode[1] = romCode / 2;
    encCode[2] = romCode * 2;
    if (level >= 1)
        applyKeycode();
    if (level >= 2)
        applyKeycode();
    encCode[1] *= 2;
    encCode[2] /= 2;
    if (level >= 3)
        applyKeycode();
}
void CartridgeNds::applyKeycode()
{
    uint64_t enc1 = encrypt64(((uint64_t)encCode[2] << 32) | encCode[1]);
    encCode[1]    = enc1;
    encCode[2]    = enc1 >> 32;
    uint64_t enc2 = encrypt64(((uint64_t)encCode[1] << 32) | encCode[0]);
    encCode[0]    = enc2;
    encCode[1]    = enc2 >> 32;
    for (int i = 0; i <= 0x11; i++) {
        uint32_t byteReverse = 0;
        for (int j = 0; j < 4; j++)
            byteReverse |= ((encCode[i % 2] >> (j * 8)) & 0xFF) << ((3 - j) * 8);
        encTable[i] ^= byteReverse;
    }
    uint64_t scratch = 0;
    for (int i = 0; i <= 0x410; i += 2) {
        scratch         = encrypt64(scratch);
        encTable[i + 0] = scratch >> 32;
        encTable[i + 1] = scratch;
    }
}
void CartridgeNds::writeAuxSpiCnt(bool cpu, uint16_t mask, uint16_t value)
{
    mask &= 0xE043;
    auxSpiCnt[cpu] = (auxSpiCnt[cpu] & ~mask) | (value & mask);
}
void CartridgeNds::writeRomCmdOutL(bool cpu, uint32_t mask, uint32_t value)
{
    romCmdOut[cpu] = (romCmdOut[cpu] & ~((uint64_t)mask)) | (value & mask);
}
void CartridgeNds::writeRomCmdOutH(bool cpu, uint32_t mask, uint32_t value)
{
    romCmdOut[cpu] = (romCmdOut[cpu] & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32);
}
void CartridgeNds::writeAuxSpiData(bool cpu, uint8_t value)
{
    if (saveSize == 0)
        return;
    if (auxWriteCount[cpu] == 0) {
        if (value == 0)
            return;
        auxCommand[cpu] = value;
        auxAddress[cpu] = 0;
        auxSpiData[cpu] = 0;
    } else {
        if (saveSize == -1) {
            switch (auxCommand[cpu]) {
                case 0x0B:
                    LOG("Detected EEPROM 0.5KB save type\n");
                    resizeSave(0x200, false);
                    break;
                case 0x02:
                    LOG("Detected EEPROM 64KB save type\n");
                    resizeSave(0x10000, false);
                    break;
                case 0x0A:
                    LOG("Detected FLASH 512KB save type\n");
                    resizeSave(0x80000, false);
                    break;
                default:
                    if (!(auxSpiCnt[cpu] & BIT(6)))
                        auxWriteCount[cpu] = 0;
                    return;
            }
        }
        switch (saveSize) {
            case 0x200: {
                switch (auxCommand[cpu]) {
                    case 0x03: {
                        if (auxWriteCount[cpu] < 2) {
                            auxAddress[cpu] = value;
                            auxSpiData[cpu] = 0;
                        } else {
                            auxSpiData[cpu] = (auxAddress[cpu] < 0x200) ? save[auxAddress[cpu]] : 0;
                            auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x0B: {
                        if (auxWriteCount[cpu] < 2) {
                            auxAddress[cpu] = 0x100 + value;
                            auxSpiData[cpu] = 0;
                        } else {
                            auxSpiData[cpu] = (auxAddress[cpu] < 0x200) ? save[auxAddress[cpu]] : 0;
                            auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x02: {
                        if (auxWriteCount[cpu] < 2) {
                            auxAddress[cpu] = value;
                            auxSpiData[cpu] = 0;
                        } else {
                            if (auxAddress[cpu] < 0x200) {
                                mutex.lock();
                                save[auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            auxAddress[cpu]++;
                            auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    case 0x0A: {
                        if (auxWriteCount[cpu] < 2) {
                            auxAddress[cpu] = 0x100 + value;
                            auxSpiData[cpu] = 0;
                        } else {
                            if (auxAddress[cpu] < 0x200) {
                                mutex.lock();
                                save[auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            auxAddress[cpu]++;
                            auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    default: {
                        LOG("Write to AUX SPI with unknown EEPROM 0.5KB command: 0x%X\n", auxCommand[cpu]);
                        auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            case 0x2000:
            case 0x8000:
            case 0x10000:
            case 0x20000: {
                switch (auxCommand[cpu]) {
                    case 0x03: {
                        if (auxWriteCount[cpu] < ((saveSize == 0x20000) ? 4 : 3)) {
                            auxAddress[cpu] |= value << ((((saveSize == 0x20000) ? 3 : 2) - auxWriteCount[cpu]) * 8);
                            auxSpiData[cpu] = 0;
                        } else {
                            auxSpiData[cpu] = (auxAddress[cpu] < saveSize) ? save[auxAddress[cpu]] : 0;
                            auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x02: {
                        if (auxWriteCount[cpu] < ((saveSize == 0x20000) ? 4 : 3)) {
                            auxAddress[cpu] |= value << ((((saveSize == 0x20000) ? 3 : 2) - auxWriteCount[cpu]) * 8);
                            auxSpiData[cpu] = 0;
                        } else {
                            if (auxAddress[cpu] < saveSize) {
                                mutex.lock();
                                save[auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            auxAddress[cpu]++;
                            auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    default: {
                        LOG("Write to AUX SPI with unknown EEPROM/FRAM command: 0x%X\n", auxCommand[cpu]);
                        auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            case 0x40000:
            case 0x80000:
            case 0x100000:
            case 0x800000: {
                switch (auxCommand[cpu]) {
                    case 0x03: {
                        if (auxWriteCount[cpu] < 4) {
                            auxAddress[cpu] |= value << ((3 - auxWriteCount[cpu]) * 8);
                            auxSpiData[cpu] = 0;
                        } else {
                            auxSpiData[cpu] = (auxAddress[cpu] < saveSize) ? save[auxAddress[cpu]] : 0;
                            auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x0A: {
                        if (auxWriteCount[cpu] < 4) {
                            auxAddress[cpu] |= value << ((3 - auxWriteCount[cpu]) * 8);
                            auxSpiData[cpu] = 0;
                        } else {
                            if (auxAddress[cpu] < saveSize) {
                                mutex.lock();
                                save[auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            auxAddress[cpu]++;
                            auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    case 0x08: {
                        auxSpiData[cpu] = ((romCode & 0xFF) == 'I') ? 0xAA : 0;
                        break;
                    }
                    default: {
                        LOG("Write to AUX SPI with unknown FLASH command: 0x%X\n", auxCommand[cpu]);
                        auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            default: {
                LOG("Write to AUX SPI with unknown save size: 0x%X\n", saveSize);
                break;
            }
        }
    }
    if (auxSpiCnt[cpu] & BIT(6))
        auxWriteCount[cpu]++;
    else
        auxWriteCount[cpu] = 0;
}
void CartridgeNds::writeRomCtrl(bool cpu, uint32_t mask, uint32_t value)
{
    bool transfer = false;
    romCtrl[cpu] |= (value & BIT(29));
    if (!(romCtrl[cpu] & BIT(31)) && (value & BIT(31)))
        transfer = true;
    mask &= 0xDF7F7FFF;
    romCtrl[cpu]    = (romCtrl[cpu] & ~mask) | (value & mask);
    wordCycles[cpu] = (romCtrl[cpu] & BIT(27)) ? (4 * 8) : (4 * 5);
    if (!transfer)
        return;
    uint8_t size = (romCtrl[cpu] & 0x07000000) >> 24;
    switch (size) {
        case 0:
            blockSize[cpu] = 0;
            break;
        case 7:
            blockSize[cpu] = 4;
            break;
        default:
            blockSize[cpu] = 0x100 << size;
            break;
    }
    uint64_t command = 0;
    for (int i = 0; i < 8; i++)
        command |= ((romCmdOut[cpu] >> (i * 8)) & 0xFF) << ((7 - i) * 8);
    if (encrypted[cpu]) {
        initKeycode(2);
        command = decrypt64(command);
    }
    cmdMode = CMD_NONE;
    if (rom) {
        if (command == 0x0000000000000000) {
            cmdMode = CMD_HEADER;
            if (romFile)
                loadRomSection(0, blockSize[cpu]);
        } else if (command == 0x9000000000000000 || (command >> 60) == 0x1 || command == 0xB800000000000000) {
            cmdMode = CMD_CHIP;
        } else if ((command >> 56) == 0x3C) {
            encrypted[cpu] = true;
        } else if ((command >> 60) == 0x2) {
            cmdMode          = CMD_SECURE;
            romAddrReal[cpu] = ((command & 0x0FFFF00000000000) >> 44) * 0x1000;
            if (romFile) {
                loadRomSection(romAddrReal[cpu], blockSize[cpu]);
                romAddrVirt[cpu] = 0;
            } else {
                romAddrVirt[cpu] = romAddrReal[cpu];
            }
        } else if ((command >> 60) == 0xA) {
            encrypted[cpu] = false;
        } else if ((command >> 56) == 0xB7) {
            cmdMode          = CMD_DATA;
            romAddrReal[cpu] = (command >> 24) & romMask;
            if (romFile) {
                if (romAddrReal[cpu] < 0x8000) {
                    loadRomSection(0, 0x8200 + blockSize[cpu]);
                    romAddrVirt[cpu] = romAddrReal[cpu];
                } else {
                    loadRomSection(romAddrReal[cpu], blockSize[cpu]);
                    romAddrVirt[cpu] = 0;
                }
            } else {
                romAddrVirt[cpu] = romAddrReal[cpu];
            }
        } else if (command != 0x9F00000000000000) {
            LOG("ROM transfer with unknown command: 0x%llX\n", command);
        }
    }
    if (blockSize[cpu] == 0) {
        romCtrl[cpu] &= ~BIT(23);
        romCtrl[cpu] &= ~BIT(31);
        if (auxSpiCnt[cpu] & BIT(14))
            core->interpreter[cpu].sendInterrupt(19);
    } else {
        core->schedule(Task(&wordReadyTasks[cpu], wordCycles[cpu]));
        readCount[cpu] = 0;
    }
}
void CartridgeNds::wordReady(bool cpu)
{
    romCtrl[cpu] |= BIT(23);
    core->dma[cpu].trigger((cpu == 0) ? 5 : 2);
}
uint32_t CartridgeNds::readRomDataIn(bool cpu)
{
    if (!(romCtrl[cpu] & BIT(23)))
        return 0;
    romCtrl[cpu] &= ~BIT(23);
    if ((readCount[cpu] += 4) == blockSize[cpu]) {
        romCtrl[cpu] &= ~BIT(31);
        if (auxSpiCnt[cpu] & BIT(14))
            core->interpreter[cpu].sendInterrupt(19);
    } else {
        core->schedule(Task(&wordReadyTasks[cpu], wordCycles[cpu]));
    }
    switch (cmdMode) {
        case CMD_HEADER: {
            return U8TO32(rom, (readCount[cpu] - 4) & 0xFFF);
        }
        case CMD_CHIP: {
            return 0x00001FC2;
        }
        case CMD_SECURE: {
            if (!romEncrypted && romAddrReal[cpu] == 0x4000 && readCount[cpu] <= 0x800) {
                uint64_t data = (readCount[cpu] <= 8) ? 0x6A624F7972636E65
                                                      : U8TO64(rom, (romAddrVirt[cpu] + readCount[cpu] - 4) & ~7);
                initKeycode(3);
                data = encrypt64(data);
                if (readCount[cpu] <= 8) {
                    initKeycode(2);
                    data = encrypt64(data);
                }
                return data >> (((romAddrReal[cpu] + readCount[cpu]) & 4) ? 0 : 32);
            }
            return U8TO32(rom, romAddrVirt[cpu] + readCount[cpu] - 4);
        }
        case CMD_DATA: {
            uint32_t address = romAddrVirt[cpu] + readCount[cpu] - 4;
            if (romAddrReal[cpu] + readCount[cpu] <= 0x8000)
                address = 0x8000 + (address & 0x1FF);
            if (address < romSize)
                return U8TO32(rom, address);
        }
        case CMD_NONE: {
            return 0xFFFFFFFF;
        }
    }
    return 0xFFFFFFFF;
}
bool CartridgeGba::findString(std::string string)
{
    for (int i = 0; i < romSize; i += 4) {
        bool found = true;
        for (size_t j = 0; j < string.length(); j++) {
            if (i + j >= romSize || rom[i + j] != string[j]) {
                found = false;
                break;
            }
        }
        if (found)
            return true;
    }
    return false;
}
bool CartridgeGba::loadRom(std::string path)
{
    bool res = Cartridge::loadRom(path);
    loadRomSection(0, romSize);
    fclose(romFile);
    romFile = nullptr;
    if (romSize > 0xAC && rom[0xAC] == 'F') {
        for (romMask = 1; romMask < romSize; romMask <<= 1)
            ;
        romMask--;
    } else {
        romMask = 0x1FFFFFF;
    }
    if (findString("SIIRTC_V"))
        core->rtc.enableGpRtc();
    core->memory.updateMap9(0x08000000, 0x0A000000);
    core->memory.updateMap7(0x08000000, 0x0D000000);
    if (!save) {
        const std::string saveStrs[] = {"EEPROM_V", "SRAM_V", "FLASH_V", "FLASH512_V", "FLASH1M_V"};
        for (int i = 0; i < 5; i++) {
            if (!findString(saveStrs[i]))
                continue;
            switch (i) {
                case 0:
                    saveSize = -1;
                    return res;
                case 1:
                    LOG("Detected SRAM 32KB save type\n");
                    resizeSave(0x8000, false);
                    return res;
                case 2:
                case 3:
                    LOG("Detected FLASH 64KB save type\n");
                    resizeSave(0x10000, false);
                    return res;
                case 4:
                    LOG("Detected FLASH 128KB save type\n");
                    resizeSave(0x20000, false);
                    return res;
            }
        }
    }
    return res;
}
uint8_t CartridgeGba::eepromRead()
{
    if (saveSize == -1) {
        if (eepromCount == 9) {
            LOG("Detected EEPROM 0.5KB save type\n");
            resizeSave(0x200, false);
        } else {
            LOG("Detected EEPROM 8KB save type\n");
            resizeSave(0x2000, false);
        }
    }
    int length = (saveSize == 0x200) ? 8 : 16;
    if (((eepromCmd & 0xC000) >> 14) == 0x3 && eepromCount >= length + 1) {
        if (++eepromCount >= length + 6) {
            int      bit   = 63 - (eepromCount - (length + 6));
            uint16_t addr  = (saveSize == 0x200) ? ((eepromCmd & 0x3F00) >> 8) : (eepromCmd & 0x03FF);
            uint8_t  value = (save[addr * 8 + bit / 8] & BIT(bit % 8)) >> (bit % 8);
            if (eepromCount >= length + 69) {
                eepromCount = 0;
                eepromCmd   = 0;
                eepromData  = 0;
            }
            return value;
        }
    } else if (eepromDone) {
        return 1;
    }
    return 0;
}
void CartridgeGba::eepromWrite(uint8_t value)
{
    eepromDone = false;
    int length = (saveSize == 0x200) ? 8 : 16;
    if (eepromCount < length) {
        eepromCmd |= (value & BIT(0)) << (16 - ++eepromCount);
    } else if (((eepromCmd & 0xC000) >> 14) == 0x3) {
        if (eepromCount < length + 1)
            eepromCount++;
    } else if (((eepromCmd & 0xC000) >> 14) == 0x2) {
        if (++eepromCount <= length + 64)
            eepromData |= (uint64_t)(value & BIT(0)) << (length + 64 - eepromCount);
        if (eepromCount >= length + 65) {
            if (saveSize == -1) {
                LOG("Detected EEPROM 8KB save type\n");
                resizeSave(0x2000, false);
            }
            mutex.lock();
            uint16_t addr = (saveSize == 0x200) ? ((eepromCmd & 0x3F00) >> 8) : (eepromCmd & 0x03FF);
            for (unsigned int i = 0; i < 8; i++)
                save[addr * 8 + i] = eepromData >> (i * 8);
            saveDirty = true;
            mutex.unlock();
            eepromCount = 0;
            eepromCmd   = 0;
            eepromData  = 0;
            eepromDone  = true;
        }
    }
}
uint8_t CartridgeGba::sramRead(uint32_t address)
{
    if (saveSize == 0x8000 && address < 0xE008000) {
        return save[address - 0xE000000];
    } else if ((saveSize == 0x10000 || saveSize == 0x20000) && address < 0xE010000) {
        if (flashCmd == 0x90 && address == 0xE000000) {
            return 0xC2;
        } else if (flashCmd == 0x90 && address == 0xE000001) {
            return (saveSize == 0x10000) ? 0x1C : 0x09;
        } else {
            if (bankSwap)
                address += 0x10000;
            return save[address - 0xE000000];
        }
    }
    return 0xFF;
}
void CartridgeGba::sramWrite(uint32_t address, uint8_t value)
{
    if (saveSize == 0x8000 && address < 0xE008000) {
        mutex.lock();
        save[address - 0xE000000] = value;
        saveDirty                 = true;
        mutex.unlock();
    } else if ((saveSize == 0x10000 || saveSize == 0x20000) && address < 0xE010000) {
        if (flashCmd == 0xA0) {
            if (bankSwap)
                address += 0x10000;
            mutex.lock();
            save[address - 0xE000000] = value;
            saveDirty                 = true;
            mutex.unlock();
            flashCmd = 0xF0;
        } else if (flashErase && (address & ~0x000F000) == 0xE000000 && (value & 0xFF) == 0x30) {
            if (bankSwap)
                address += 0x10000;
            mutex.lock();
            memset(&save[address - 0xE000000], 0xFF, 0x1000 * sizeof(uint8_t));
            saveDirty = true;
            mutex.unlock();
            flashErase = false;
        } else if (saveSize == 0x20000 && flashCmd == 0xB0 && address == 0xE000000) {
            bankSwap = value;
            flashCmd = 0xF0;
        } else if (address == 0xE005555) {
            flashCmd = value;
            if (flashCmd == 0x80) {
                flashErase = true;
            } else if (flashCmd != 0xAA) {
                flashErase = false;
            } else if (flashErase && flashCmd == 0x10) {
                mutex.lock();
                memset(save, 0xFF, saveSize * sizeof(uint8_t));
                saveDirty = true;
                mutex.unlock();
            }
        }
    }
}
