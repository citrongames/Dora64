#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void doraemon_draw_distance_trace(uint8_t* rdram);
void doraemon_draw_distance_allocation(uint8_t* rdram, uint32_t actor, int result);
float doraemon_draw_distance_configure(float multiplier);
float doraemon_draw_distance_limit(float original, uint32_t actor_flags);
float doraemon_model_lod_configure(float multiplier);
float doraemon_model_lod_limit(float original);
#ifdef __cplusplus
}
#endif
