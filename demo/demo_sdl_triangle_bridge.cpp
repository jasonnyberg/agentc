#include "demo_sdl_triangle_bridge.h"

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define AGENTC_HAVE_SDL 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define AGENTC_HAVE_SDL 1
#else
#define AGENTC_HAVE_SDL 0
#endif

#include <algorithm>
#include <array>
#include <cmath>

#if AGENTC_HAVE_SDL

namespace {

struct DemoState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int clearRed = 244;
    int clearGreen = 240;
    int clearBlue = 232;
    int drawRed = 28;
    int drawGreen = 92;
    int drawBlue = 148;
    bool videoReady = false;
};

DemoState g_demo;

void applyColor(int red, int green, int blue) {
    if (!g_demo.renderer) {
        return;
    }
    SDL_SetRenderDrawColor(g_demo.renderer, red, green, blue, 255);
}

void drawFlatBottomTriangle(const SDL_FPoint& top, const SDL_FPoint& left, const SDL_FPoint& right) {
    if (!g_demo.renderer) {
        return;
    }

    const float leftSlope = (left.x - top.x) / (left.y - top.y);
    const float rightSlope = (right.x - top.x) / (right.y - top.y);
    float currentLeft = top.x;
    float currentRight = top.x;

    for (int y = static_cast<int>(std::ceil(top.y)); y <= static_cast<int>(std::floor(left.y)); ++y) {
        SDL_RenderLine(g_demo.renderer,
                       static_cast<float>(std::round(currentLeft)),
                       static_cast<float>(y),
                       static_cast<float>(std::round(currentRight)),
                       static_cast<float>(y));
        currentLeft += leftSlope;
        currentRight += rightSlope;
    }
}

void drawFlatTopTriangle(const SDL_FPoint& left, const SDL_FPoint& right, const SDL_FPoint& bottom) {
    if (!g_demo.renderer) {
        return;
    }

    const float leftSlope = (bottom.x - left.x) / (bottom.y - left.y);
    const float rightSlope = (bottom.x - right.x) / (bottom.y - right.y);
    float currentLeft = bottom.x;
    float currentRight = bottom.x;

    for (int y = static_cast<int>(std::floor(bottom.y)); y >= static_cast<int>(std::ceil(left.y)); --y) {
        SDL_RenderLine(g_demo.renderer,
                       static_cast<float>(std::round(currentLeft)),
                       static_cast<float>(y),
                       static_cast<float>(std::round(currentRight)),
                       static_cast<float>(y));
        currentLeft -= leftSlope;
        currentRight -= rightSlope;
    }
}

void fillTriangle(SDL_FPoint p1, SDL_FPoint p2, SDL_FPoint p3) {
    std::array<SDL_FPoint, 3> points = {p1, p2, p3};
    std::sort(points.begin(), points.end(), [](const SDL_FPoint& lhs, const SDL_FPoint& rhs) {
        return lhs.y < rhs.y;
    });

    const SDL_FPoint& top = points[0];
    const SDL_FPoint& middle = points[1];
    const SDL_FPoint& bottom = points[2];

    if (std::fabs(bottom.y - top.y) < 0.001f) {
        return;
    }

    if (std::fabs(middle.y - bottom.y) < 0.001f) {
        drawFlatBottomTriangle(top, middle, bottom);
        return;
    }

    if (std::fabs(top.y - middle.y) < 0.001f) {
        drawFlatTopTriangle(top, middle, bottom);
        return;
    }

    const float alpha = (middle.y - top.y) / (bottom.y - top.y);
    const SDL_FPoint split = {
        top.x + ((bottom.x - top.x) * alpha),
        middle.y,
    };

    drawFlatBottomTriangle(top, middle.x < split.x ? middle : split, middle.x < split.x ? split : middle);
    drawFlatTopTriangle(middle.x < split.x ? middle : split, middle.x < split.x ? split : middle, bottom);
}

}

extern "C" int agentc_sdl_demo_open(int width, int height) {
    agentc_sdl_demo_close();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 0;
    }
    g_demo.videoReady = true;

    g_demo.window = SDL_CreateWindow("AgentC SDL Triangle", width, height, 0);
    if (!g_demo.window) {
        agentc_sdl_demo_close();
        return 0;
    }

    g_demo.renderer = SDL_CreateRenderer(g_demo.window, nullptr);
    if (!g_demo.renderer) {
        agentc_sdl_demo_close();
        return 0;
    }

    return 1;
}

extern "C" void agentc_sdl_demo_set_clear_color(int red, int green, int blue) {
    g_demo.clearRed = red;
    g_demo.clearGreen = green;
    g_demo.clearBlue = blue;
}

extern "C" void agentc_sdl_demo_set_draw_color(int red, int green, int blue) {
    g_demo.drawRed = red;
    g_demo.drawGreen = green;
    g_demo.drawBlue = blue;
}

extern "C" void agentc_sdl_demo_clear(void) {
    applyColor(g_demo.clearRed, g_demo.clearGreen, g_demo.clearBlue);
    if (g_demo.renderer) {
        SDL_RenderClear(g_demo.renderer);
    }
}

extern "C" void agentc_sdl_demo_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3) {
    applyColor(g_demo.drawRed, g_demo.drawGreen, g_demo.drawBlue);
    fillTriangle(SDL_FPoint{static_cast<float>(x1), static_cast<float>(y1)},
                 SDL_FPoint{static_cast<float>(x2), static_cast<float>(y2)},
                 SDL_FPoint{static_cast<float>(x3), static_cast<float>(y3)});
    if (g_demo.renderer) {
        SDL_RenderLine(g_demo.renderer, static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2));
        SDL_RenderLine(g_demo.renderer, static_cast<float>(x2), static_cast<float>(y2), static_cast<float>(x3), static_cast<float>(y3));
        SDL_RenderLine(g_demo.renderer, static_cast<float>(x3), static_cast<float>(y3), static_cast<float>(x1), static_cast<float>(y1));
    }
}

extern "C" void agentc_sdl_demo_present(void) {
    if (g_demo.renderer) {
        SDL_RenderPresent(g_demo.renderer);
    }
}

extern "C" int agentc_sdl_demo_pump_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return 1;
        }
    }
    return 0;
}

extern "C" void agentc_sdl_demo_delay(int milliseconds) {
    const Uint64 start = SDL_GetTicks();
    while ((SDL_GetTicks() - start) < static_cast<Uint64>(milliseconds)) {
        if (agentc_sdl_demo_pump_events()) {
            break;
        }
        SDL_Delay(16);
    }
}

extern "C" void agentc_sdl_demo_close(void) {
    if (g_demo.renderer) {
        SDL_DestroyRenderer(g_demo.renderer);
        g_demo.renderer = nullptr;
    }
    if (g_demo.window) {
        SDL_DestroyWindow(g_demo.window);
        g_demo.window = nullptr;
    }
    if (g_demo.videoReady) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
        g_demo.videoReady = false;
    }
}

#else

extern "C" int agentc_sdl_demo_open(int, int) {
    return 0;
}

extern "C" void agentc_sdl_demo_set_clear_color(int, int, int) {}

extern "C" void agentc_sdl_demo_set_draw_color(int, int, int) {}

extern "C" void agentc_sdl_demo_clear(void) {}

extern "C" void agentc_sdl_demo_draw_triangle(int, int, int, int, int, int) {}

extern "C" void agentc_sdl_demo_present(void) {}

extern "C" int agentc_sdl_demo_pump_events(void) {
    return 1;
}

extern "C" void agentc_sdl_demo_delay(int) {}

extern "C" void agentc_sdl_demo_close(void) {}

#endif
