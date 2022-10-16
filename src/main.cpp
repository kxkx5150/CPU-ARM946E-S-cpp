#include <SDL2/SDL.h>
#include <cstdio>
#include <GL/glew.h>
#include <thread>
#include "core.h"

#define FRAME_WIDTH   256
#define FRAME_HEIGHT  192
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define SCALE         2
#define FRAME_PITCH   (FRAME_WIDTH * sizeof(uint32_t))


Core       *core;
std::string ndsPath, gbaPath;
uint32_t    framebuffer[FRAME_WIDTH * FRAME_HEIGHT * 8];


bool createCore()
{
    core = nullptr;
    try {
        if (core)
            delete core;
        core = new Core(ndsPath, gbaPath);
        return true;
    } catch (CoreError e) {
        std::string text;
        switch (e) {
            case ERROR_BIOS:
                text = "Error loading BIOS.\n";
                break;
            case ERROR_FIRM:
                text = "Error loading firmware.\n";
                break;
            case ERROR_ROM:
                text = "Error loading ROM.\n";
                break;
        }
        printf("%s", text.c_str());
        return false;
    }
}
int setPath(std::string path)
{
    if (path.find(".nds", path.length() - 4) != std::string::npos) {
        gbaPath = "";
        ndsPath = path;
        if (createCore()) {
            printf("create core nds\n");
            return 2;
        }
        ndsPath = "";
        return 1;
    } else if (path.find(".gba", path.length() - 4) != std::string::npos) {
        ndsPath = "";
        gbaPath = path;
        if (createCore()) {
            printf("create core gba\n");
            return 2;
        }
        gbaPath = "";
        return 1;
    }
    return 0;
}
void draw(SDL_Renderer *renderer, SDL_Texture *tex, int resShift)
{
    if (core->gpu.getFrame(framebuffer, false)) {
        SDL_UpdateTexture(tex, NULL, framebuffer, FRAME_PITCH);
        SDL_RenderCopy(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
}
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("error : loading rom");
        return 1;
    }
    setPath(argv[1]);

    bool Running = true;

    int screenRotation = 0;
    int width          = (screenRotation ? SCREEN_HEIGHT : SCREEN_WIDTH);
    int height         = (screenRotation ? SCREEN_WIDTH : SCREEN_HEIGHT);
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window   *window   = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width * SCALE,
                                              height * SCALE * 2, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture  *tex =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, FRAME_WIDTH, FRAME_HEIGHT * 2);
    SDL_RenderSetScale(renderer, SCALE, SCALE);
    SDL_SetRenderTarget(renderer, NULL);

    SDL_Event   Event;
    SDL_Keycode Key;
    uint32_t    last_tic = SDL_GetTicks();

    while (Running) {
        core->runFrame();
        if ((SDL_GetTicks() - last_tic) >= 1000.0 / 60.0) {
            last_tic = SDL_GetTicks();
            draw(renderer, tex, 0);
        }
        while (SDL_PollEvent(&Event)) {
            switch (Event.type) {
                case SDL_QUIT:
                    Running = 0;
                    break;
                case SDL_KEYDOWN:
                    Key = Event.key.keysym.sym;
                    break;
                case SDL_KEYUP:
                    Key = Event.key.keysym.sym;
                    break;
            }
        }
    }
}
