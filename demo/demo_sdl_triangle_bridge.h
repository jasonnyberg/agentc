#ifndef AGENTC_DEMO_SDL_TRIANGLE_BRIDGE_H
#define AGENTC_DEMO_SDL_TRIANGLE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

int agentc_sdl_demo_open(int a1, int a2);
void agentc_sdl_demo_set_clear_color(int a1, int a2, int a3);
void agentc_sdl_demo_set_draw_color(int a1, int a2, int a3);
void agentc_sdl_demo_clear(void);
void agentc_sdl_demo_draw_triangle(int a1, int a2, int a3, int a4, int a5, int a6);
void agentc_sdl_demo_present(void);
int agentc_sdl_demo_pump_events(void);
void agentc_sdl_demo_delay(int a1);
void agentc_sdl_demo_close(void);

#ifdef __cplusplus
}
#endif

#endif
