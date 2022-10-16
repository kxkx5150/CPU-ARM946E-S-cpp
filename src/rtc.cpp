
#include <ctime>
#include "rtc.h"
#include "core.h"


void Rtc::updateRtc(bool cs, bool sck, bool sio)
{
    if (cs) {
        if (!sckCur && sck) {
            if (writeCount < 8) {
                command |= sio << (7 - writeCount);
                if (writeCount == 7 && (command & 0xF0) != 0x60) {
                    uint8_t value = 0;
                    for (int i = 0; i < 8; i++)
                        value |= ((command >> i) & BIT(0)) << (7 - i);
                    command = value;
                }
            } else if (command & BIT(0)) {
                sio = readRegister((command >> 1) & 0x7);
            } else {
                writeRegister((command >> 1) & 0x7, sio);
            }
            writeCount++;
        }
    } else {
        writeCount = 0;
        command    = 0;
    }
    csCur  = cs;
    sckCur = sck;
    sioCur = sio;
}
void Rtc::updateDateTime()
{
    std::time_t t    = std::time(nullptr);
    std::tm    *time = std::localtime(&t);
    time->tm_year %= 100;
    time->tm_mon++;
    if (!(control & BIT(core->isGbaMode() ? 6 : 1)))
        time->tm_hour %= 12;
    dateTime[0] = ((time->tm_year / 10) << 4) | (time->tm_year % 10);
    dateTime[1] = ((time->tm_mon / 10) << 4) | (time->tm_mon % 10);
    dateTime[2] = ((time->tm_mday / 10) << 4) | (time->tm_mday % 10);
    dateTime[4] = ((time->tm_hour / 10) << 4) | (time->tm_hour % 10);
    dateTime[5] = ((time->tm_min / 10) << 4) | (time->tm_min % 10);
    dateTime[6] = ((time->tm_sec / 10) << 4) | (time->tm_sec % 10);
    if (time->tm_hour >= 12)
        dateTime[4] |= BIT(6 << core->isGbaMode());
}
void Rtc::reset()
{
    updateRtc(0, 0, 0);
    control     = 0;
    rtc         = 0;
    gpDirection = 0;
    gpControl   = 0;
}
bool Rtc::readRegister(uint8_t index)
{
    if (core->isGbaMode()) {
        switch (index) {
            case 0:
                reset();
                return 0;
            case 1:
                return (control >> (writeCount & 7)) & BIT(0);
            case 2:
                if (writeCount == 8)
                    updateDateTime();
                return (dateTime[(writeCount / 8) - 1] >> (writeCount % 8)) & BIT(0);
            case 3:
                if (writeCount == 8)
                    updateDateTime();
                return (dateTime[(writeCount / 8) - 5] >> (writeCount % 8)) & BIT(0);
            default:
                LOG("Read from unknown GBA RTC register: %d\n", index);
                return 0;
        }
    }
    switch (index) {
        case 0:
            return (control >> (writeCount & 7)) & BIT(0);
        case 2:
            if (writeCount == 8)
                updateDateTime();
            return (dateTime[(writeCount / 8) - 1] >> (writeCount & 7)) & BIT(0);
        case 3:
            if (writeCount == 8)
                updateDateTime();
            return (dateTime[(writeCount / 8) - 5] >> (writeCount & 7)) & BIT(0);
        default:
            LOG("Read from unknown RTC register: %d\n", index);
            return 0;
    }
}
void Rtc::writeRegister(uint8_t index, bool value)
{
    if (core->isGbaMode()) {
        switch (index) {
            case 1:
                if (BIT(writeCount & 7) & 0x6A)
                    control = (control & ~BIT(writeCount & 7)) | (value << (writeCount & 7));
                return;
            default:
                LOG("Write to unknown GBA RTC register: %d\n", index);
                return;
        }
    }
    switch (index) {
        case 0:
            if ((BIT(writeCount & 7) & 0x01) && value)
                reset();
            else if (BIT(writeCount & 7) & 0x0E)
                control = (control & ~BIT(writeCount & 7)) | (value << (writeCount & 7));
            return;
        default:
            LOG("Write to unknown RTC register: %d\n", index);
            return;
    }
}
void Rtc::writeRtc(uint8_t value)
{
    rtc      = value & ~0x07;
    bool cs  = (rtc & BIT(6)) ? (value & BIT(2)) : csCur;
    bool sck = (rtc & BIT(5)) ? !(value & BIT(1)) : sckCur;
    bool sio = (rtc & BIT(4)) ? (value & BIT(0)) : sioCur;
    updateRtc(cs, sck, sio);
}
void Rtc::writeGpData(uint16_t mask, uint16_t value)
{
    if (mask & 0xFF) {
        bool cs  = (gpDirection & BIT(2)) ? (value & BIT(2)) : csCur;
        bool sio = (gpDirection & BIT(1)) ? (value & BIT(1)) : sioCur;
        bool sck = (gpDirection & BIT(0)) ? (value & BIT(0)) : sckCur;
        updateRtc(cs, sck, sio);
    }
}
void Rtc::writeGpDirection(uint16_t mask, uint16_t value)
{
    mask &= 0x000F;
    gpDirection = (gpDirection & ~mask) | (value & mask);
}
void Rtc::writeGpControl(uint16_t mask, uint16_t value)
{
    if (!gpRtc)
        return;
    mask &= 0x0001;
    gpControl = (gpControl & ~mask) | (value & mask);
    core->memory.updateMap7(0x8000000, 0x8001000);
}
uint8_t Rtc::readRtc()
{
    bool cs  = csCur;
    bool sck = (rtc & BIT(5)) ? 0 : sckCur;
    bool sio = (rtc & BIT(4)) ? 0 : sioCur;
    return rtc | (cs << 2) | (sck << 1) | (sio << 0);
}
uint16_t Rtc::readGpData()
{
    bool cs  = (gpDirection & BIT(2)) ? 0 : csCur;
    bool sio = (gpDirection & BIT(1)) ? 0 : sioCur;
    bool sck = (gpDirection & BIT(0)) ? 0 : sckCur;
    return (cs << 2) | (sio << 1) | (sck << 0);
}