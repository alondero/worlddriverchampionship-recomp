#pragma once
#include <cstdint>
using SDL_AudioDeviceID = uint32_t;
struct SDL_AudioSpec { int freq; uint16_t format; uint8_t channels; uint16_t samples; };
constexpr uint16_t AUDIO_S16SYS = 0x8010;
constexpr int SDL_AUDIO_ALLOW_SAMPLES_CHANGE = 0x00000008;
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int);
void SDL_CloseAudioDevice(SDL_AudioDeviceID);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID);
const char* SDL_GetError();
int SDL_QueueAudio(SDL_AudioDeviceID, const void*, uint32_t);
uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID);
uint64_t SDL_GetTicks64();
