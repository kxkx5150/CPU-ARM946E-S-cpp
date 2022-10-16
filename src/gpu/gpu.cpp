#include <cstring>
#include "gpu.h"
#include "../core.h"
#include "../settings.h"


Gpu::Gpu(Core *core) : core(core)
{
    ready.store(false);
    drawing.store(0);
    gbaScanline240Task = std::bind(&Gpu::gbaScanline240, this);
    gbaScanline308Task = std::bind(&Gpu::gbaScanline308, this);
    scanline256Task    = std::bind(&Gpu::scanline256, this);
    scanline355Task    = std::bind(&Gpu::scanline355, this);
}
Gpu::~Gpu()
{
    if (thread) {
        running = false;
        thread->join();
        delete thread;
    }
    while (!framebuffers.empty()) {
        Buffers &buffers = framebuffers.front();
        delete[] buffers.framebuffer;
        delete[] buffers.hiRes3D;
        framebuffers.pop();
    }
}
void Gpu::scheduleInit()
{
    core->schedule(Task(&scanline256Task, 256 * 6));
    core->schedule(Task(&scanline355Task, 355 * 6));
}
void Gpu::gbaScheduleInit()
{
    core->schedule(Task(&gbaScanline240Task, 240 * 4));
    core->schedule(Task(&gbaScanline308Task, 308 * 4));
}
uint32_t Gpu::rgb5ToRgb8(uint32_t color)
{
    uint8_t r = (((color >> 0) & 0x1F) << 1) * 255 / 63;
    uint8_t g = (((color >> 5) & 0x1F) << 1) * 255 / 63;
    uint8_t b = (((color >> 10) & 0x1F) << 1) * 255 / 63;
    return (0xFF << 24) | (b << 16) | (g << 8) | r;
}
uint32_t Gpu::rgb6ToRgb8(uint32_t color)
{
    uint8_t r = ((color >> 0) & 0x3F) * 255 / 63;
    uint8_t g = ((color >> 6) & 0x3F) * 255 / 63;
    uint8_t b = ((color >> 12) & 0x3F) * 255 / 63;
    return (0xFF << 24) | (b << 16) | (g << 8) | r;
}
uint16_t Gpu::rgb6ToRgb5(uint32_t color)
{
    uint8_t r = ((color >> 0) & 0x3F) / 2;
    uint8_t g = ((color >> 6) & 0x3F) / 2;
    uint8_t b = ((color >> 12) & 0x3F) / 2;
    return BIT(15) | (b << 10) | (g << 5) | r;
}
bool Gpu::getFrame(uint32_t *out, bool gbaCrop)
{
    if (!ready.load())
        return false;
    Buffers &buffers = framebuffers.front();
    if (gbaCrop) {
        if (Settings::getHighRes3D()) {
            for (int y = 0; y < 160; y++) {
                for (int x = 0; x < 240; x++) {
                    uint32_t color = rgb5ToRgb8(buffers.framebuffer[y * 256 + x]);
                    int      i     = (y * 2) * (240 * 2) + (x * 2);
                    out[i + 0]     = color;
                    out[i + 1]     = color;
                    out[i + 480]   = color;
                    out[i + 481]   = color;
                }
            }
        } else {
            for (int y = 0; y < 160; y++)
                for (int x = 0; x < 240; x++)
                    out[y * 240 + x] = rgb5ToRgb8(buffers.framebuffer[y * 256 + x]);
        }
    } else if (core->isGbaMode()) {
        int      offset = (powCnt1 & BIT(15)) ? 0 : (256 * 192);
        uint32_t base   = 0x6800000 + gbaBlock * 0x20000;
        gbaBlock        = !gbaBlock;
        if (Settings::getHighRes3D()) {
            for (int y = 0; y < 192; y++) {
                for (int x = 0; x < 256; x++) {
                    uint32_t color = rgb5ToRgb8((x >= 8 && x < 256 - 8 && y >= 16 && y < 192 - 16)
                                                    ? buffers.framebuffer[(y - 16) * 256 + (x - 8)]
                                                    : core->memory.read<uint16_t>(0, base + (y * 256 + x) * 2));
                    int      i     = (offset * 4) + (y * 2) * (256 * 2) + (x * 2);
                    out[i + 0]     = color;
                    out[i + 1]     = color;
                    out[i + 512]   = color;
                    out[i + 513]   = color;
                }
            }
            memset(&out[(256 * 192 - offset) * 4], 0, 256 * 192 * 4 * sizeof(uint32_t));
        } else {
            for (int y = 0; y < 192; y++) {
                for (int x = 0; x < 256; x++) {
                    out[offset + y * 256 + x] =
                        rgb5ToRgb8((x >= 8 && x < 256 - 8 && y >= 16 && y < 192 - 16)
                                       ? buffers.framebuffer[(y - 16) * 256 + (x - 8)]
                                       : core->memory.read<uint16_t>(0, base + (y * 256 + x) * 2));
                }
            }
            memset(&out[256 * 192 - offset], 0, 256 * 192 * sizeof(uint32_t));
        }
    } else {
        if (Settings::getHighRes3D()) {
            if (buffers.hiRes3D) {
                for (int y = 0; y < 192 * 2; y++) {
                    for (int x = 0; x < 256; x++) {
                        uint32_t value = buffers.framebuffer[y * 256 + x];
                        int      i     = (y * 2) * (256 * 2) + (x * 2);
                        if (value & BIT(26)) {
                            uint32_t value2 = buffers.hiRes3D[(i + 0) % (256 * 192 * 4)];
                            out[i + 0]      = rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 1) % (256 * 192 * 4)];
                            out[i + 1]      = rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 512) % (256 * 192 * 4)];
                            out[i + 512]    = rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 513) % (256 * 192 * 4)];
                            out[i + 513]    = rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                        } else {
                            uint32_t color = rgb6ToRgb8(value);
                            out[i + 0]     = color;
                            out[i + 1]     = color;
                            out[i + 512]   = color;
                            out[i + 513]   = color;
                        }
                    }
                }
            } else {
                for (int y = 0; y < 192 * 2; y++) {
                    for (int x = 0; x < 256; x++) {
                        uint32_t color = rgb6ToRgb8(buffers.framebuffer[y * 256 + x]);
                        int      i     = (y * 2) * (256 * 2) + (x * 2);
                        out[i + 0]     = color;
                        out[i + 1]     = color;
                        out[i + 512]   = color;
                        out[i + 513]   = color;
                    }
                }
            }
        } else {
            for (int i = 0; i < 256 * 192 * 2; i++)
                out[i] = rgb6ToRgb8(buffers.framebuffer[i]);
        }
    }
    delete[] buffers.framebuffer;
    delete[] buffers.hiRes3D;
    mutex.lock();
    framebuffers.pop();
    ready.store(!framebuffers.empty());
    mutex.unlock();
    return true;
}
void Gpu::gbaScanline240()
{
    if (vCount < 160) {
        if (thread) {
            while (drawing.load() != 0)
                std::this_thread::yield();
        } else {
            core->gpu2D[0].drawGbaScanline(vCount);
        }
        core->dma[1].trigger(2);
    }
    dispStat[1] |= BIT(1);
    if (dispStat[1] & BIT(4))
        core->interpreter[1].sendInterrupt(1);
    core->schedule(Task(&gbaScanline240Task, 308 * 4));
}
void Gpu::gbaScanline308()
{
    dispStat[1] &= ~BIT(1);
    switch (++vCount) {
        case 160: {
            if (thread) {
                running = false;
                thread->join();
                delete thread;
                thread = nullptr;
            }
            dispStat[1] |= BIT(0);
            if (dispStat[1] & BIT(3))
                core->interpreter[1].sendInterrupt(0);
            core->dma[1].trigger(1);
            if (framebuffers.size() < 2) {
                Buffers buffers;
                buffers.framebuffer = new uint32_t[256 * 160];
                memcpy(buffers.framebuffer, core->gpu2D[0].getFramebuffer(), 256 * 160 * sizeof(uint32_t));
                mutex.lock();
                framebuffers.push(buffers);
                ready.store(true);
                mutex.unlock();
            }
            break;
        }
        case 227: {
            dispStat[1] &= ~BIT(0);
            core->endFrame();
            break;
        }
        case 228: {
            vCount = 0;
            if (Settings::getThreaded2D() && !thread) {
                running = true;
                thread  = new std::thread(&Gpu::drawGbaThreaded, this);
            }
            break;
        }
    }
    if (vCount < 160 && thread)
        drawing.store(1);
    if (vCount == (dispStat[1] >> 8)) {
        dispStat[1] |= BIT(2);
        if (dispStat[1] & BIT(5))
            core->interpreter[1].sendInterrupt(2);
    } else if (dispStat[1] & BIT(2)) {
        dispStat[1] &= ~BIT(2);
    }
    core->schedule(Task(&gbaScanline308Task, 308 * 4));
}
void Gpu::scanline256()
{
    if (vCount < 192) {
        if (thread) {
            while (drawing.load() == 1)
                std::this_thread::yield();
            switch (drawing.exchange(3)) {
                case 2:
                    core->gpu2D[1].drawScanline(vCount);
                case 3:
                    while (drawing.load() != 0)
                        std::this_thread::yield();
                    break;
            }
        } else {
            core->gpu2D[0].drawScanline(vCount);
            core->gpu2D[1].drawScanline(vCount);
        }
        core->dma[0].trigger(2);
        if (vCount == 0 && (dispCapCnt & BIT(31)))
            displayCapture = true;
        if (displayCapture) {
            int width, height;
            switch ((dispCapCnt & 0x00300000) >> 20) {
                case 0:
                    width  = 128;
                    height = 128;
                    break;
                case 1:
                    width  = 256;
                    height = 64;
                    break;
                case 2:
                    width  = 256;
                    height = 128;
                    break;
                case 3:
                    width  = 256;
                    height = 192;
                    break;
            }
            uint32_t base        = 0x6800000 + ((dispCapCnt & 0x00030000) >> 16) * 0x20000;
            uint32_t writeOffset = ((dispCapCnt & 0x000C0000) >> 3) + vCount * width * 2;
            switch ((dispCapCnt & 0x60000000) >> 29) {
                case 0: {
                    uint32_t *source =
                        (dispCapCnt & BIT(24)) ? core->gpu3DRenderer.getLine(vCount) : core->gpu2D[0].getRawLine();
                    bool resShift = (Settings::getHighRes3D() && (dispCapCnt & BIT(24)));
                    for (int i = 0; i < width; i++)
                        core->memory.write<uint16_t>(0, base + ((writeOffset + i * 2) & 0x1FFFF),
                                                     rgb6ToRgb5(source[i << resShift]));
                    break;
                }
                case 1: {
                    if (dispCapCnt & BIT(25)) {
                        LOG("Unimplemented display capture source: display FIFO\n");
                        break;
                    }
                    uint32_t readOffset = ((dispCapCnt & 0x0C000000) >> 11) + vCount * width * 2;
                    for (int i = 0; i < width; i++) {
                        uint16_t color = core->memory.read<uint16_t>(0, base + ((readOffset + i * 2) & 0x1FFFF));
                        core->memory.write<uint16_t>(0, base + ((writeOffset + i * 2) & 0x1FFFF), color);
                    }
                    break;
                }
                default: {
                    if (dispCapCnt & BIT(25)) {
                        LOG("Unimplemented display capture source: display FIFO\n");
                        break;
                    }
                    uint32_t *source =
                        (dispCapCnt & BIT(24)) ? core->gpu3DRenderer.getLine(vCount) : core->gpu2D[0].getRawLine();
                    bool     resShift   = (Settings::getHighRes3D() && (dispCapCnt & BIT(24)));
                    uint32_t readOffset = ((dispCapCnt & 0x0C000000) >> 11) + vCount * width * 2;
                    uint8_t  eva        = std::min((dispCapCnt >> 0) & 0x1F, 16U);
                    uint8_t  evb        = std::min((dispCapCnt >> 8) & 0x1F, 16U);
                    for (int i = 0; i < width; i++) {
                        uint16_t c1    = rgb6ToRgb5(source[i << resShift]);
                        uint16_t c2    = core->memory.read<uint16_t>(0, base + ((readOffset + i * 2) & 0x1FFFF));
                        uint8_t  r     = std::min((((c1 >> 0) & 0x1F) * eva + ((c2 >> 0) & 0x1F) * evb) / 16, 31);
                        uint8_t  g     = std::min((((c1 >> 5) & 0x1F) * eva + ((c2 >> 5) & 0x1F) * evb) / 16, 31);
                        uint8_t  b     = std::min((((c1 >> 10) & 0x1F) * eva + ((c2 >> 10) & 0x1F) * evb) / 16, 31);
                        uint16_t color = BIT(15) | (b << 10) | (g << 5) | r;
                        core->memory.write<uint16_t>(0, base + ((writeOffset + i * 2) & 0x1FFFF), color);
                    }
                    break;
                }
            }
            if (vCount + 1 == height) {
                displayCapture = false;
                dispCapCnt &= ~BIT(31);
            }
        }
    }
    if (dirty3D && (core->gpu2D[0].readDispCnt() & BIT(3)) && ((vCount + 48) % 263) < 192) {
        if (vCount == 215)
            dirty3D = BIT(1);
        core->gpu3DRenderer.drawScanline((vCount + 48) % 263);
        if (vCount == 143)
            dirty3D &= ~BIT(1);
    }
    for (int i = 0; i < 2; i++) {
        dispStat[i] |= BIT(1);
        if (dispStat[i] & BIT(4))
            core->interpreter[i].sendInterrupt(1);
    }
    core->schedule(Task(&scanline256Task, 355 * 6));
}
void Gpu::scanline355()
{
    switch (++vCount) {
        case 192: {
            if (thread) {
                running = false;
                thread->join();
                delete thread;
                thread = nullptr;
            }
            for (int i = 0; i < 2; i++) {
                dispStat[i] |= BIT(0);
                if (dispStat[i] & BIT(3))
                    core->interpreter[i].sendInterrupt(0);
                core->dma[i].trigger(1);
            }
            if (core->gpu3D.shouldSwap())
                core->gpu3D.swapBuffers();
            if (framebuffers.size() < 2) {
                Buffers buffers;
                buffers.framebuffer = new uint32_t[256 * 192 * 2];
                if (powCnt1 & BIT(0)) {
                    if (powCnt1 & BIT(15)) {
                        memcpy(&buffers.framebuffer[0], core->gpu2D[0].getFramebuffer(), 256 * 192 * sizeof(uint32_t));
                        memcpy(&buffers.framebuffer[256 * 192], core->gpu2D[1].getFramebuffer(),
                               256 * 192 * sizeof(uint32_t));
                    } else {
                        memcpy(&buffers.framebuffer[0], core->gpu2D[1].getFramebuffer(), 256 * 192 * sizeof(uint32_t));
                        memcpy(&buffers.framebuffer[256 * 192], core->gpu2D[0].getFramebuffer(),
                               256 * 192 * sizeof(uint32_t));
                    }
                } else {
                    memset(buffers.framebuffer, 0, 256 * 192 * 2 * sizeof(uint32_t));
                }
                if (Settings::getHighRes3D() && (core->gpu2D[0].readDispCnt() & BIT(3))) {
                    buffers.hiRes3D = new uint32_t[256 * 192 * 4];
                    memcpy(buffers.hiRes3D, core->gpu3DRenderer.getLine(0), 256 * 192 * 4 * sizeof(uint32_t));
                    buffers.top3D = (powCnt1 & BIT(15));
                }
                mutex.lock();
                framebuffers.push(buffers);
                ready.store(true);
                mutex.unlock();
            }
            break;
        }
        case 262: {
            for (int i = 0; i < 2; i++)
                dispStat[i] &= ~BIT(0);
            core->endFrame();
            break;
        }
        case 263: {
            vCount = 0;
            if (Settings::getThreaded2D() && !thread) {
                running = true;
                thread  = new std::thread(&Gpu::drawThreaded, this);
            }
            break;
        }
    }
    if (vCount < 192 && thread)
        drawing.store(1);
    for (int i = 0; i < 2; i++) {
        if (vCount == ((dispStat[i] >> 8) | ((dispStat[i] & BIT(7)) << 1))) {
            dispStat[i] |= BIT(2);
            if (dispStat[i] & BIT(5))
                core->interpreter[i].sendInterrupt(2);
        } else if (dispStat[i] & BIT(2)) {
            dispStat[i] &= ~BIT(2);
        }
        dispStat[i] &= ~BIT(1);
    }
    core->schedule(Task(&scanline355Task, 355 * 6));
}
void Gpu::drawGbaThreaded()
{
    while (running) {
        while (drawing.load() != 1) {
            if (!running)
                return;
            std::this_thread::yield();
        }
        core->gpu2D[0].drawGbaScanline(vCount);
        drawing.store(0);
    }
}
void Gpu::drawThreaded()
{
    while (running) {
        while (drawing.load() != 1) {
            if (!running)
                return;
            std::this_thread::yield();
        }
        drawing.store(2);
        core->gpu2D[0].drawScanline(vCount);
        if (drawing.exchange(3) == 2)
            core->gpu2D[1].drawScanline(vCount);
        drawing.store(0);
    }
}
void Gpu::writeDispStat(bool cpu, uint16_t mask, uint16_t value)
{
    mask &= 0xFFB8;
    dispStat[cpu] = (dispStat[cpu] & ~mask) | (value & mask);
}
void Gpu::writeDispCapCnt(uint32_t mask, uint32_t value)
{
    mask &= 0xEF3F1F1F;
    dispCapCnt = (dispCapCnt & ~mask) | (value & mask);
}
void Gpu::writePowCnt1(uint16_t mask, uint16_t value)
{
    mask &= 0x820F;
    powCnt1 = (powCnt1 & ~mask) | (value & mask);
}
