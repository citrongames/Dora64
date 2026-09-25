#pragma once
#include <stdint.h>
#define DORAEMON_MODEL_POOL_CAPACITY 2048
#define DORAEMON_MODEL_STRIDE 0x358
#define DORAEMON_MODEL_MATRICES_OFFSET 0x60
#define DORAEMON_MODEL_MATRICES_SIZE 0x2C0
#ifdef __cplusplus
extern "C" {
#endif
// Offsets relative to the original MIPS LUI bases (not truncated immediates).
extern int32_t doraemon_model_table_offset;
extern int32_t doraemon_model_scratch_offset;
extern int32_t doraemon_model_storage_offset;
void doraemon_model_pool_init(uint8_t* rdram);
void doraemon_model_pool_reset(void);
int doraemon_model_pool_matrix_address(uint32_t address);
uint32_t doraemon_model_pool_base(void);
uint32_t doraemon_model_pool_used(uint8_t* rdram);
#ifdef __cplusplus
}
#endif
