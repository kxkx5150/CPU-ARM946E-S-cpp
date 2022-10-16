#include "cpu.h"
#include "../core.h"


CPU::CPU(Core *core, bool cpu) : core(core), cpu(cpu)
{
    for (int i = 0; i < 16; i++)
        registers[i] = &registersUsr[i];
    interruptTask = std::bind(&CPU::interrupt, this);
}
void CPU::init()
{
    setCpsr(0x000000D3);
    registersUsr[15] = (cpu == 0) ? 0xFFFF0000 : 0x00000000;
    flushPipeline();
    ime = 0;
    ie = irf = 0;
    postFlg  = 0;
}
void CPU::directBoot()
{
    uint32_t entryAddr;
    if (cpu == 0) {
        entryAddr        = core->memory.read<uint32_t>(0, 0x27FFE24);
        registersUsr[13] = 0x03002F7C;
        registersIrq[0]  = 0x03003F80;
        registersSvc[0]  = 0x03003FC0;
    } else {
        entryAddr        = core->memory.read<uint32_t>(0, 0x27FFE34);
        registersUsr[13] = 0x0380FD80;
        registersIrq[0]  = 0x0380FF80;
        registersSvc[0]  = 0x0380FFC0;
    }
    setCpsr(0x000000DF);
    registersUsr[12] = entryAddr;
    registersUsr[14] = entryAddr;
    registersUsr[15] = entryAddr;
    flushPipeline();
}
void CPU::sendInterrupt(int bit)
{
    irf |= BIT(bit);
    if (ie & irf) {
        if (ime && !(cpsr & BIT(7)))
            core->schedule(Task(&interruptTask, (cpu == 1 && !core->isGbaMode()) ? 2 : 1));
        else if (ime || cpu == 1)
            halted &= ~BIT(0);
    }
}
void CPU::interrupt()
{
    if (ime && (ie & irf) && !(cpsr & BIT(7))) {
        exception(0x18);
        halted &= ~BIT(0);
    }
}
int CPU::exception(uint8_t vector)
{
    if (bios && (cpu || core->cp15.getExceptionAddr()))
        return bios->execute(vector, cpu, registers);

    static uint8_t modes[] = {0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11};
    setCpsr((cpsr & ~0x3F) | BIT(7) | modes[vector >> 2], true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) ? 2 : 0);
    *registers[15] = (cpu ? 0 : core->cp15.getExceptionAddr()) + vector;
    flushPipeline();
    return 3;
}
void CPU::flushPipeline()
{
    if (cpsr & BIT(5)) {
        *registers[15] = (*registers[15] & ~1) + 2;
        pipeline[0]    = core->memory.read<uint16_t>(cpu, *registers[15] - 2);
        pipeline[1]    = core->memory.read<uint16_t>(cpu, *registers[15]);
    } else {
        *registers[15] = (*registers[15] & ~3) + 4;
        pipeline[0]    = core->memory.read<uint32_t>(cpu, *registers[15] - 4);
        pipeline[1]    = core->memory.read<uint32_t>(cpu, *registers[15]);
    }
}
void CPU::setCpsr(uint32_t value, bool save)
{
    if ((value & 0x1F) != (cpsr & 0x1F)) {
        switch (value & 0x1F) {
            case 0x10:
            case 0x1F:
                registers[8]  = &registersUsr[8];
                registers[9]  = &registersUsr[9];
                registers[10] = &registersUsr[10];
                registers[11] = &registersUsr[11];
                registers[12] = &registersUsr[12];
                registers[13] = &registersUsr[13];
                registers[14] = &registersUsr[14];
                spsr          = nullptr;
                break;
            case 0x11:
                registers[8]  = &registersFiq[0];
                registers[9]  = &registersFiq[1];
                registers[10] = &registersFiq[2];
                registers[11] = &registersFiq[3];
                registers[12] = &registersFiq[4];
                registers[13] = &registersFiq[5];
                registers[14] = &registersFiq[6];
                spsr          = &spsrFiq;
                break;
            case 0x12:
                registers[8]  = &registersUsr[8];
                registers[9]  = &registersUsr[9];
                registers[10] = &registersUsr[10];
                registers[11] = &registersUsr[11];
                registers[12] = &registersUsr[12];
                registers[13] = &registersIrq[0];
                registers[14] = &registersIrq[1];
                spsr          = &spsrIrq;
                break;
            case 0x13:
                registers[8]  = &registersUsr[8];
                registers[9]  = &registersUsr[9];
                registers[10] = &registersUsr[10];
                registers[11] = &registersUsr[11];
                registers[12] = &registersUsr[12];
                registers[13] = &registersSvc[0];
                registers[14] = &registersSvc[1];
                spsr          = &spsrSvc;
                break;
            case 0x17:
                registers[8]  = &registersUsr[8];
                registers[9]  = &registersUsr[9];
                registers[10] = &registersUsr[10];
                registers[11] = &registersUsr[11];
                registers[12] = &registersUsr[12];
                registers[13] = &registersAbt[0];
                registers[14] = &registersAbt[1];
                spsr          = &spsrAbt;
                break;
            case 0x1B:
                registers[8]  = &registersUsr[8];
                registers[9]  = &registersUsr[9];
                registers[10] = &registersUsr[10];
                registers[11] = &registersUsr[11];
                registers[12] = &registersUsr[12];
                registers[13] = &registersUnd[0];
                registers[14] = &registersUnd[1];
                spsr          = &spsrUnd;
                break;
            default:
                LOG("Unknown ARM%d CPU mode: 0x%X\n", ((cpu == 0) ? 9 : 7), value & 0x1F);
                break;
        }
    }
    if (save && spsr)
        *spsr = cpsr;
    cpsr = value;
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(Task(&interruptTask, (cpu == 1 && !core->isGbaMode()) ? 2 : 1));
}
int CPU::handleReserved(uint32_t opcode)
{
    if ((opcode & 0x0E000000) == 0x0A000000)
        return blx(opcode);
    if (bios && opcode == 0xFF000000)
        return finishHleIrq();

    return unkArm(opcode);
}
int CPU::handleHleIrq()
{
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x12, true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) ? 2 : 0);
    stmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[14] = cpu ? 0x00000000 : 0xFFFF0000;
    *registers[15] = core->memory.read<uint32_t>(cpu, cpu ? 0x3FFFFFC : (core->cp15.getDtcmAddr() + 0x3FFC));
    flushPipeline();
    return 3;
}
int CPU::finishHleIrq()
{
    if (bios->shouldCheck())
        bios->checkWaitFlags(cpu);
    ldmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[15] = *registers[14] - 4;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}
int CPU::unkArm(uint32_t opcode)
{
    LOG("Unknown ARM%d ARM opcode: 0x%X\n", ((cpu == 0) ? 9 : 7), opcode);
    return 1;
}
int CPU::unkThumb(uint16_t opcode)
{
    LOG("Unknown ARM%d THUMB opcode: 0x%X\n", ((cpu == 0) ? 9 : 7), opcode);
    return 1;
}
void CPU::writeIme(uint8_t value)
{
    ime = value & 0x01;
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(Task(&interruptTask, (cpu == 1 && !core->isGbaMode()) ? 2 : 1));
}
void CPU::writeIe(uint32_t mask, uint32_t value)
{
    mask &= ((cpu == 0) ? 0x003F3F7F : (core->isGbaMode() ? 0x3FFF : 0x01FF3FFF));
    ie = (ie & ~mask) | (value & mask);
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(Task(&interruptTask, (cpu == 1 && !core->isGbaMode()) ? 2 : 1));
}
void CPU::writeIrf(uint32_t mask, uint32_t value)
{
    irf &= ~(value & mask);
}
void CPU::writePostFlg(uint8_t value)
{
    postFlg |= value & 0x01;
    if (cpu == 0)
        postFlg = (postFlg & ~0x02) | (value & 0x02);
}
