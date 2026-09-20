#include "doraemon_camera.hpp"
#include "doraemon_camera_math.hpp"
#include "librecomp/addresses.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int doraemon_camera_update(uint8_t*, recomp_context*);
extern "C" void doraemon_camera_sync(uint8_t*);
namespace {
    using namespace doraemon::camera;
    int sweeps=0, effects=0, movementUpdates=0;
    float wallZ=10000.0f;
    void require(bool v,const char* text) { if(!v) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
    bool near(float a,float b) { return std::abs(a-b)<0.002f; }
    void put(uint8_t* rdram,uint32_t at,float v) { uint32_t b; std::memcpy(&b,&v,4); MEM_W(0,gpr(S32(at)))=b; }
    float get(uint8_t* rdram,uint32_t at) { uint32_t b=MEM_W(0,gpr(S32(at))); float v; std::memcpy(&v,&b,4); return v; }
    void byte(uint8_t* rdram,uint32_t at,int v) { MEM_B(0,gpr(S32(at)))=v; }
}
extern "C" void func_80005FEC(uint8_t*,recomp_context* ctx) { effects++; ctx->r5=123; }
extern "C" void func_80004794(uint8_t*,recomp_context* ctx) { movementUpdates++; ctx->r5=456; }
extern "C" void func_80019058(uint8_t* rdram,recomp_context* ctx) {
    sweeps++;
    float radius; uint32_t bits=uint32_t(ctx->r6); std::memcpy(&radius,&bits,4);
    require(near(radius,15),"original camera collision radius");
    require(ctx->r5==2,"current level passed to collision solver");
    float x; bits=uint32_t(ctx->r7); std::memcpy(&x,&bits,4);
    const uint32_t stack=uint32_t(ctx->r29),out=uint32_t(ctx->r4);
    // An oblique plane x+z=wallZ. Mimic the original solver's normal
    // push/slide, deliberately returning a point off the requested orbit ray.
    Vec3 endpoint={x+get(rdram,stack+0x18),get(rdram,stack+0x10)+get(rdram,stack+0x1C),get(rdram,stack+0x14)+get(rdram,stack+0x20)};
    const float penetration=std::max(0.0f,endpoint.x+endpoint.z-wallZ);
    endpoint.x-=penetration*0.5f; endpoint.z-=penetration*0.5f;
    put(rdram,out+0xC,endpoint.x); put(rdram,out+0x10,endpoint.y); put(rdram,out+0x14,endpoint.z);
    ctx->r5=789;
}
int main() {
    using namespace doraemon::camera;
    for(unsigned mode=0;mode<13;mode++) require(allows_manual(mode,0,0,0,2)==(mode<=1),"only A/B permit modern control");
    require(!allows_manual(0,1,0,0,2) && !allows_manual(1,0,1,0,2),"boss/event locks respected");
    require(!allows_manual(0,0,0,1,2) && !allows_manual(0,0,0,0,0),"pause and update gate respected");
    float yaw30=0,p30=0,yaw120=0,p120=0;
    for(int i=0;i<30;i++) rotate(yaw30,p30,1,0,0,0,120,0.15f,false,1.0f/30);
    for(int i=0;i<120;i++) rotate(yaw120,p120,1,0,0,0,120,0.15f,false,1.0f/120);
    require(near(yaw30,yaw120) && near(yaw30,-120),"stick rotation independent of update rate");
    float yaw=0,pitch=0; rotate(yaw,pitch,0,0,100,100,120,0.15f,false,0.01f);
    require(near(yaw,-15) && near(pitch,15),"mouse pixel deltas are not multiplied by dt");
    rotate(yaw,pitch,0,0,0,10000,120,0.15f,false,1);
    require(near(pitch,80),"pitch does not flip at poles");
    rotate(yaw,pitch,0,0,0,-10000,120,0.15f,false,1);
    require(near(pitch,0),"mouse cannot lower camera below horizontal");
    rotate(yaw,pitch,0,-1,0,0,120,0.15f,false,1);
    require(near(pitch,0),"stick respects lower pitch bound");
    pitch=20; rotate(yaw,pitch,0,0,0,10000,120,0.15f,true,1);
    require(near(pitch,0),"inverted input respects lower pitch bound");
    rotate(yaw,pitch,0,0,0,-100,120,0.15f,true,0.01f);
    require(near(pitch,15),"camera responds immediately when moving away from lower bound");
    require(near(collision_distance(300,100,0.03f),100),"immediate collision pull-in");
    require(collision_distance(100,300,0.03f)>100 && collision_distance(100,300,0.03f)<300,"smooth outward recovery");
    std::vector<uint8_t> memory(0x400000); auto* rdram=memory.data();
    recomp_context ctx{}; ctx.f_odd=&ctx.f0.u32h; ctx.r29=S32(0x80300000); ctx.r16=42;
    byte(rdram,0x800E69CF,2); byte(rdram,0x800F38BD,2);
    put(rdram,0x800F054C,35); put(rdram,0x800F0550,300); put(rdram,0x800F0558,35);
    MEM_W(0,gpr(S32(0x800F3940)))=0x80200000;
    const auto caller=ctx;
    require(doraemon_camera_update(rdram,&ctx)==0,"default camera is original");
    configure(true,120,0.15f,false);
    require(doraemon_camera_update(rdram,&ctx)==1,"take over manual camera");
    require(std::memcmp(&ctx,&caller,sizeof(ctx))==0,"preserve original caller context");
    require(sweeps==1 && effects==1 && movementUpdates==1,"retain collision, shake and movement heading updates");
    submit_input(0,0,100,0); doraemon_camera_update(rdram,&ctx);
    const float first=get(rdram,0x800F0548); require(first < -50,"mouse rotates orbit");
    doraemon_camera_update(rdram,&ctx); require(near(first,get(rdram,0x800F0548)),"mouse delta consumed once");
    wallZ=100.0f; const int beforeCollision=sweeps; doraemon_camera_update(rdram,&ctx);
    const Vec3 offset={get(rdram,0x800F0548)-get(rdram,0x800F0554),get(rdram,0x800F054C)-get(rdram,0x800F0558),get(rdram,0x800F0550)-get(rdram,0x800F055C)};
    const float contact=100.0f/(direction(-15,0).x+direction(-15,0).z);
    require(length(offset)<=contact && length(offset)>contact-2.4f,"shorten orbit before oblique wall without reprojecting a sliding endpoint");
    require(offset.x+offset.z<=wallZ,"final eye stays on near side of wall");
    require(sweeps-beforeCollision==9,"bounded collision probes");
    wallZ=10000.0f;
    for(uint32_t lock : {0x800F05A3U,0x800F05B8U,0x800F38E0U,0x802000A4U,0x802000A5U,0x802000A6U,0x802000A7U,0x802000A8U}) {
        byte(rdram,lock,1); const auto before=memory; const int oldSweeps=sweeps;
        require(doraemon_camera_update(rdram,&ctx)==0,"blocked camera delegates to original");
        require(memory==before && sweeps==oldSweeps,"blocked camera leaves guest memory untouched");
        require(!is_active(),"blocked camera releases input ownership"); byte(rdram,lock,0);
    }
    byte(rdram,0x800F0630,8); const auto before=memory;
    require(doraemon_camera_update(rdram,&ctx)==0 && memory==before,"special mode untouched");
    byte(rdram,0x800F0630,0); reset(); doraemon_camera_update(rdram,&ctx);
    submit_input(0,0,200,0); clear_input(); const float afterReset=get(rdram,0x800F38A8);
    doraemon_camera_update(rdram,&ctx); require(near(afterReset,get(rdram,0x800F38A8)),"menu discards pending mouse deltas");

    // Item dialogue moves the original eye close to the player and changes its
    // radius. Neither is a new user distance; resume the pre-dialogue orbit.
    const float priorYaw=get(rdram,0x800F38A8);
    byte(rdram,0x802000A4,1); doraemon_camera_sync(rdram);
    submit_input(0,0,100,100,1,-10); // Blocked input must not leak through.
    put(rdram,0x800F0548,0); put(rdram,0x800F054C,35); put(rdram,0x800F0550,80);
    put(rdram,0x800F0554,0); put(rdram,0x800F0558,35); put(rdram,0x800F055C,0);
    put(rdram,0x800F38AC,80);
    byte(rdram,0x802000A4,0); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),300),"item close-up does not replace chosen distance");
    require(near(get(rdram,0x800F38A8),priorYaw),"restore orbit angle after dialogue");
    for(int i=0;i<120;i++) doraemon_camera_update(rdram,&ctx);
    Vec3 recovered={get(rdram,0x800F0548),get(rdram,0x800F054C)-35,get(rdram,0x800F0550)};
    require(length(recovered)>290,"smooth recovery from item close-up");
    submit_input(0,0,0,0,0,2); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),240),"wheel zooms in");
    doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),240),"wheel delta consumed once");
    submit_input(0,0,0,0,1,0); doraemon_camera_update(rdram,&ctx);
    require(get(rdram,0x800F38AC)>240,"held zoom axis moves outward");
    submit_input(0,0,0,0,0,-100); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),600),"zoom out bound");
    reset(); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),600),"scene reset preserves chosen distance");
    submit_input(0,0,0,0,0,100); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38AC),80),"zoom in bound");
    // A scripted camera may hand back an eye below its look-at point.
    reset();
    put(rdram,0x800F0548,0); put(rdram,0x800F054C,-100); put(rdram,0x800F0550,300);
    put(rdram,0x800F0554,0); put(rdram,0x800F0558,35); put(rdram,0x800F055C,0);
    doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38A4),0),"initial orbit clamps a negative scripted pitch");
    require(get(rdram,0x800F054C)>=get(rdram,0x800F0558),"initial eye cannot fall below focus");
    submit_input(0,0,0,-10000); doraemon_camera_update(rdram,&ctx);
    require(near(get(rdram,0x800F38A4),0),"update clamps downward mouse input");
    require(get(rdram,0x800F054C)>=get(rdram,0x800F0558),"updated eye cannot fall below focus");
    // Live configuration affects both horizontal inputs independently of Y.
    for (bool invertedX : {true, false}) {
        configure(true,120,0.15f,false,false,invertedX);
        const float sign=invertedX ? 1.0f : -1.0f;
        const float beforeMouseYaw=get(rdram,0x800F38A8);
        const float beforeMousePitch=get(rdram,0x800F38A4);
        submit_input(0,0,20,20); doraemon_camera_update(rdram,&ctx);
        require(near(wrap(get(rdram,0x800F38A8)-beforeMouseYaw),sign*3),"X setting reverses mouse yaw live");
        require(near(get(rdram,0x800F38A4)-beforeMousePitch,3),"X inversion preserves vertical mouse direction");
        const float beforeStickYaw=get(rdram,0x800F38A8);
        submit_input(1,0,0,0); doraemon_camera_update(rdram,&ctx);
        require(wrap(get(rdram,0x800F38A8)-beforeStickYaw)*sign>0,"X setting reverses stick yaw live");
        clear_input();
    }
    configure(true,120,0.15f,true,false,true);
    const float beforeBothYaw=get(rdram,0x800F38A8),beforeBothPitch=get(rdram,0x800F38A4);
    submit_input(0,0,20,20); doraemon_camera_update(rdram,&ctx);
    require(near(wrap(get(rdram,0x800F38A8)-beforeBothYaw),3) && near(get(rdram,0x800F38A4)-beforeBothPitch,-3),"X and Y inversion can be enabled together");
    configure(false,120,0.15f,false); require(!is_active() && doraemon_camera_update(rdram,&ctx)==0,"disable restores original path");
    std::puts("Modern camera: orbit, collision response, input consumption, ownership, locks and reset passed.");
}
