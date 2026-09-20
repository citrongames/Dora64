#include "doraemon_model_pool.h"
#include "librecomp/addresses.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
void func_8001CC70(uint8_t*, recomp_context*);
void func_8001F7FC(uint8_t*, recomp_context*);
void func_8001F878(uint8_t*, recomp_context*);
void func_8001E8A8(uint8_t*, recomp_context*);
// Non-pool initialization dependencies; pool setup itself is the guest code.
void func_80091A40(uint8_t*, recomp_context*) {}
void func_8001CECC(uint8_t*, recomp_context*) {}
void func_8001CF5C(uint8_t*, recomp_context*) {}
void func_8001D088(uint8_t*, recomp_context*) {}
}

namespace {
    std::vector<uint8_t> memory(32 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    unsigned allocations = 0;
    uint32_t allocationOffset = 0x01010000;
    constexpr uint32_t Used = 0x80141CA0, Actors = 0x800FB820;
    uint32_t table() { return uint32_t(0x80140000U + doraemon_model_table_offset); }
    uint32_t storage() { return uint32_t(0x80110000U + doraemon_model_storage_offset); }
    uint32_t word(uint32_t at) { return MEM_W(0, gpr(S32(at))); }
    void word(uint32_t at, uint32_t value) { MEM_W(0, gpr(S32(at))) = value; }
    void require(bool value, const char* text) {
        if (!value) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); }
    }
    recomp_context context() {
        recomp_context ctx{}; ctx.r29 = int32_t(0x803F0000); return ctx;
    }
    int allocate(unsigned count) {
        auto ctx = context(); ctx.f_odd = &ctx.f1.u32l; ctx.r4 = count;
        func_8001F7FC(rdram,&ctx); return int16_t(ctx.r2);
    }
    void initialize() {
        auto ctx = context(); ctx.f_odd = &ctx.f1.u32l;
        func_8001CC70(rdram,&ctx);
    }
    void actor(unsigned index, unsigned model) {
        const auto at = Actors + index*256;
        MEM_H(0,gpr(S32(at))) = 1;
        word(at+0x38,model); word(at+0x8C,0);
    }
}

void* recomp::alloc(uint8_t* ram, size_t size) {
    require(size==2048*(0x358+8),"storage and both tables allocated together");
    ++allocations;
    return ram+allocationOffset;
}

int main() {
    // Old tables and storage must no longer receive pool initialization writes.
    std::memset(rdram+0x10BCA0,0xA5,0x141CA0-0x10BCA0);
    initialize();
    require(allocations==1 && MEM_HU(0,gpr(S32(Used)))==0,"initial allocation and usage reset");
    require(storage()>0x81000000U,"pool tests exercise addresses beyond 16 MiB");
    for (unsigned i=0;i<2048;i++) {
        require(word(table()+i*4)==storage()+i*0x358,"all 2048 pointers initialized");
        for (unsigned m=0;m<11;m++) require(doraemon_model_pool_matrix_address(storage()+i*0x358+0x60+m*64),"all embedded matrix addresses recognized");
        // func_8001E94C uses model + (frameBuffer << 6) + 0x60 for scale.
        // Both buffers must bypass segmented addressing, including high indices.
        for (unsigned frameBuffer=0;frameBuffer<2;frameBuffer++) {
            const uint32_t scale = storage()+i*0x358+(frameBuffer<<6)+0x60;
            require(doraemon_model_pool_matrix_address(scale),"scale matrix retains extended RDRAM addressing");
            require(!doraemon_model_pool_matrix_address(scale & 0x1FFFFFFF),"scale physical address is not a virtual pool pointer");
        }
    }
    for (uint32_t at=0x8010BCA0;at<0x80141CA0;at+=4) require(word(at)==0xA5A5A5A5,"old fixed pool memory untouched");
    require(!doraemon_model_pool_matrix_address(storage()+0xDF),"non-matrix field rejected");
    require(!doraemon_model_pool_matrix_address(table()),"pointer table is not matrix storage");
    require(!doraemon_model_pool_matrix_address((storage()+0xE0)&0x1FFFFFFF),"physical address cannot alias a segmented matrix");
    require(!doraemon_model_pool_matrix_address(0x801010E0),"original matrix addressing preserved");

    require(allocate(224)==0 && allocate(9)==224,"logged full-pool enemy spawn now succeeds");
    require(allocate(23)==233 && allocate(1)==256,"cross old 256 boundary without wrapping");
    require(allocate(1759)==257,"fill new gameplay capacity");
    require(allocate(1)==-1 && MEM_HU(0,gpr(S32(Used)))==2016,"original 32-element reserve retained at new capacity");
    MEM_B(0,gpr(S32(0x800F38E0)))=1;
    require(allocate(32)==2016 && allocate(1)==-1,"pause can use final 32 slots, no overflow");
    MEM_B(0,gpr(S32(0x800F38E0)))=0;

    // Real guest compactor must shift past 256 and recycle >256 pointers.
    actor(0,0); actor(1,100); actor(2,400); actor(255,2047);
    const auto oldFirst = word(table()+100*4);
    auto ctx=context(); ctx.f_odd=&ctx.f1.u32l; ctx.r4=100; ctx.r5=300;
    func_8001F878(rdram,&ctx);
    word(Actors+256+0x38,0xFFFF); // Original unload caller completes its own state.
    require(MEM_HU(0,gpr(S32(Used)))==1748,"usage after freeing 300 slots");
    require(word(Actors+2*256+0x38)==100 && word(Actors+255*256+0x38)==1747,"all original 256 actors have corrected model indices");
    require(word(table()+100*4)==storage()+400*0x358,"live pointers shifted across old limit");
    require(word(table()+1748*4)==oldFirst,"large scratch table returns freed storage to tail");
    require(allocate(268)==1748 && allocate(1)==-1,"recycled slots allocate up to gameplay cap");

    initialize();
    require(allocations==1 && MEM_HU(0,gpr(S32(Used)))==0,"level restart reuses allocation and resets pool");
    require(word(table()+2047*4)==storage()+2047*0x358,"level restart restores final pointer");
    actor(255,1500); word(Actors+255*256+0x10,0x42F60000); // x=123
    ctx=context();ctx.f_odd=&ctx.f1.u32l;func_8001E8A8(rdram,&ctx);
    require(word(storage()+1500*0x358+0x24)==0x42F60000,"guest update reaches model 1500 through relocated table");
    const auto oldBase=storage();
    doraemon_model_pool_reset();
    require(!doraemon_model_pool_matrix_address(oldBase+0xE0),"cold reset invalidates renderer address range");
    allocationOffset+=0x200000;
    initialize();
    require(allocations==2 && storage()!=oldBase,"new guest heap gets fresh pool addresses");
    std::puts("Model pool: 2048 slots, allocation limits, real compaction, relocation, matrix bounds and resets passed.");
}
