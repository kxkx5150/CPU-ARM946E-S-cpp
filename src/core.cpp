
#include <algorithm>
#include <cstring>
#include <thread>
#include "core.h"
#include "settings.h"


Core::Core(std::string ndsPath, std::string gbaPath, int id)
    : id(id), bios9(this), bios7(this), cartridgeNds(this), cartridgeGba(this), cp15(this),
      divSqrt(this), dma{Dma(this, 0), Dma(this, 1)}, gpu(this), gpu2D{Gpu2D(this, 0), Gpu2D(this, 1)}, gpu3D(this),
      gpu3DRenderer(this), input(this), interpreter{CPU(this, 0), CPU(this, 1)}, ipc(this), memory(this), rtc(this),
      spi(this), spu(this), timers{Timers(this, 0), Timers(this, 1)}, wifi(this)
{
    if (!memory.loadBios9() && (!Settings::getDirectBoot() || (ndsPath == "" && gbaPath == "")))
        throw ERROR_BIOS;
    if (!memory.loadBios7() && (!Settings::getDirectBoot() || (ndsPath == "" && gbaPath == "")))
        throw ERROR_BIOS;
    if (!spi.loadFirmware() && (!Settings::getDirectBoot() || (ndsPath == "" && gbaPath == "")))
        throw ERROR_FIRM;
    if (!memory.loadGbaBios() && (gbaPath != "" && (ndsPath == "" || !Settings::getDirectBoot())))
        throw ERROR_BIOS;

    resetCyclesTask = std::bind(&Core::resetCycles, this);
    schedule(Task(&resetCyclesTask, 0x7FFFFFFF));
    gpu.scheduleInit();
    spu.scheduleInit();
    memory.updateMap9(0x00000000, 0xFFFFFFFF);
    memory.updateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[0].init();
    interpreter[1].init();

    if (gbaPath != "") {
        if (!cartridgeGba.loadRom(gbaPath))
            throw ERROR_ROM;
        if (ndsPath == "" && Settings::getDirectBoot()) {
            memory.write<uint16_t>(0, 0x4000304, 0x8003);
            enterGbaMode();
        }
    }

    if (ndsPath != "") {
        if (!cartridgeNds.loadRom(ndsPath))
            throw ERROR_ROM;

        if (Settings::getDirectBoot()) {
            cp15.write(1, 0, 0, 0x0005707D);
            cp15.write(9, 1, 0, 0x0300000A);
            cp15.write(9, 1, 1, 0x00000020);
            memory.write<uint8_t>(0, 0x4000247, 0x03);
            memory.write<uint8_t>(0, 0x4000300, 0x01);
            memory.write<uint8_t>(1, 0x4000300, 0x01);
            memory.write<uint16_t>(0, 0x4000304, 0x0001);
            memory.write<uint16_t>(1, 0x4000504, 0x0200);
            memory.write<uint32_t>(0, 0x27FF800, 0x00001FC2);
            memory.write<uint32_t>(0, 0x27FF804, 0x00001FC2);
            memory.write<uint16_t>(0, 0x27FF850, 0x5835);
            memory.write<uint16_t>(0, 0x27FF880, 0x0007);
            memory.write<uint16_t>(0, 0x27FF884, 0x0006);
            memory.write<uint32_t>(0, 0x27FFC00, 0x00001FC2);
            memory.write<uint32_t>(0, 0x27FFC04, 0x00001FC2);
            memory.write<uint16_t>(0, 0x27FFC10, 0x5835);
            memory.write<uint16_t>(0, 0x27FFC40, 0x0001);
            cartridgeNds.directBoot();
            interpreter[0].directBoot();
            interpreter[1].directBoot();
            spi.directBoot();
        }
    }

    running.store(true);
}
void Core::resetCycles()
{
    for (unsigned int i = 0; i < tasks.size(); i++)
        tasks[i].cycles -= globalCycles;

    arm9Cycles -= std::min(globalCycles, arm9Cycles);
    arm7Cycles -= std::min(globalCycles, arm7Cycles);
    timers[0].resetCycles();
    timers[1].resetCycles();
    globalCycles -= globalCycles;
    schedule(Task(&resetCyclesTask, 0x7FFFFFFF));
}
void Core::runGbaFrame()
{
    while (running.exchange(true)) {
        if (arm7Cycles > globalCycles)
            globalCycles = arm7Cycles;

        while (interpreter[1].shouldRun() && tasks[0].cycles > arm7Cycles)
            arm7Cycles = (globalCycles += interpreter[1].runOpcode());

        globalCycles = tasks[0].cycles;
        while (tasks[0].cycles <= globalCycles) {
            (*tasks[0].task)();
            tasks.erase(tasks.begin());
        }
    }
}
void Core::runNdsFrame()
{
    while (running.exchange(true)) {
        while (tasks[0].cycles > globalCycles) {
            if (interpreter[0].shouldRun() && globalCycles >= arm9Cycles)
                arm9Cycles = globalCycles + interpreter[0].runOpcode();

            if (interpreter[1].shouldRun() && globalCycles >= arm7Cycles)
                arm7Cycles = globalCycles + (interpreter[1].runOpcode() << 1);

            globalCycles = std::min<uint32_t>((interpreter[0].shouldRun() ? arm9Cycles : -1),
                                              (interpreter[1].shouldRun() ? arm7Cycles : -1));
        }

        globalCycles = tasks[0].cycles;
        while (tasks[0].cycles <= globalCycles) {
            (*tasks[0].task)();
            tasks.erase(tasks.begin());
        }
    }
}
void Core::schedule(Task task)
{
    task.cycles += globalCycles;
    auto it = std::upper_bound(tasks.cbegin(), tasks.cend(), task);
    tasks.insert(it, task);
}
void Core::enterGbaMode()
{
    gbaMode = true;
    runFunc = &Core::runGbaFrame;
    running.store(false);
    tasks.clear();

    schedule(Task(&resetCyclesTask, 1));
    gpu.gbaScheduleInit();
    spu.gbaScheduleInit();

    memory.updateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[1].init();
    interpreter[1].setBios(nullptr);
    rtc.reset();
    memory.write<uint8_t>(0, 0x4000240, 0x80);
    memory.write<uint8_t>(0, 0x4000241, 0x80);
}
void Core::endFrame()
{
    running.store(false);
    fpsCount++;
    std::chrono::duration<double> fpsTime = std::chrono::steady_clock::now() - lastFpsTime;
    if (fpsTime.count() >= 1.0f) {
        fps         = fpsCount;
        fpsCount    = 0;
        lastFpsTime = std::chrono::steady_clock::now();
    }

    if (wifi.shouldSchedule())
        wifi.scheduleInit();
}
FORCE_INLINE int CPU::runOpcode()
{
    uint32_t opcode = pipeline[0];
    pipeline[0]     = pipeline[1];
    if (cpsr & BIT(5)) {
        pipeline[1] = core->memory.read<uint16_t>(cpu, *registers[15] += 2);
        return (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
    } else {
        pipeline[1] = core->memory.read<uint32_t>(cpu, *registers[15] += 4);
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
            case 0:
                return 1;
            case 2:
                return handleReserved(opcode);
            default:
                return (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
        }
    }
}
