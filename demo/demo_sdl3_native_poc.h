#ifndef AGENTC_DEMO_SDL3_NATIVE_POC_H
#define AGENTC_DEMO_SDL3_NATIVE_POC_H

typedef unsigned int SDL_InitFlags;
typedef unsigned int SDL_WindowFlags;
typedef unsigned int Uint32;
typedef unsigned char Uint8;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

void SDL_Init(SDL_InitFlags flags);
SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name);
void SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void SDL_RenderClear(SDL_Renderer* renderer);
void SDL_RenderLine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2);
void SDL_RenderGeometryRaw(SDL_Renderer* renderer, SDL_Texture* texture, const float* xy, int xy_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices);
void SDL_RenderPresent(SDL_Renderer* renderer);
void SDL_Delay(Uint32 ms);
void SDL_DestroyRenderer(SDL_Renderer* renderer);
void SDL_DestroyWindow(SDL_Window* window);
void SDL_Quit(void);

#endif
