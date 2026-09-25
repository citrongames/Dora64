#include "doraemon_camera.hpp"
#include "doraemon_camera_math.hpp"
#include "librecomp/addresses.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

extern "C" void func_80005FEC(uint8_t*, recomp_context*);
extern "C" void func_80004794(uint8_t*, recomp_context*);
extern "C" void func_80019058(uint8_t*, recomp_context*);

namespace {
    using namespace doraemon::camera;
    using Clock = std::chrono::steady_clock;
    constexpr uint32_t View = 0x800F0548, CameraState = 0x800F0588;
    constexpr uint32_t GameState = 0x800F38A0;
    std::atomic_bool enabled{false}, available{false}, invertX{false}, invertY{false}, captureMouse{false};
    std::atomic_bool dialogueActive{false};
    std::atomic<float> stickSpeed{120.0f}, mouseSensitivity{0.15f};
    std::atomic<int64_t> lastAvailable{0};
    std::atomic<unsigned> generation{0};
    struct Input { float x=0, y=0, mx=0, my=0, zoom=0, wheel=0; };
    float preferredDistance=300.0f; // User choice; never infer it from a cinematic or collision endpoint.
    std::mutex inputMutex;
    Input pending;
    struct Orbit {
        bool initialized = false, hasPose = false;
        unsigned generation = 0;
        float yaw=0, pitch=20, distance=300, actualDistance=300, height=35;
        Vec3 focus{}, player{};
        Clock::time_point lastUpdate{};
    } orbit;
    int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }
    float get_float(uint8_t* rdram, uint32_t address) {
        uint32_t bits=MEM_W(0, gpr(S32(address))); float result;
        std::memcpy(&result, &bits, 4); return result;
    }
    uint32_t float_bits(float value) { uint32_t bits; std::memcpy(&bits,&value,4); return bits; }
    void put_float(uint8_t* rdram, uint32_t address, float value) { MEM_W(0,gpr(S32(address)))=float_bits(value); }
    Vec3 get_vec(uint8_t* rdram, uint32_t address) {
        return {get_float(rdram,address),get_float(rdram,address+4),get_float(rdram,address+8)};
    }
    void put_vec(uint8_t* rdram, uint32_t address, Vec3 value) {
        put_float(rdram,address,value.x); put_float(rdram,address+4,value.y); put_float(rdram,address+8,value.z);
    }
    bool manual(uint8_t* rdram) {
        // Dialogues disable logical controller buttons, not CameraState's lock.
        // func_80012D40 sets unkA4[0..4]; func_800016D0 filters C/R with it.
        const unsigned slot=MEM_BU(0x41,gpr(S32(GameState)));
        if(slot>=4) return false;
        const uint32_t pad=MEM_W(slot*4,gpr(S32(0x800F3940)));
        if(pad<0x80000000U || pad>0x803FFF4CU || (pad&3)) return false;
        for(unsigned i=0;i<5;i++) if(MEM_BU(0xA4+i,gpr(S32(pad)))) return false;
        return allows_manual(MEM_BU(0,gpr(S32(0x800F0630))), MEM_B(0x1B,gpr(S32(CameraState))),
            MEM_B(0x30,gpr(S32(CameraState))), MEM_B(0x40,gpr(S32(GameState))),
            MEM_BU(0xF,gpr(S32(0x800E69C0))));
    }
    void drop_input() { std::lock_guard lock(inputMutex); pending={}; }
    void rebind(recomp_context& copy, const recomp_context& original) {
        copy.f_odd = original.f_odd == &original.f1.u32l ? &copy.f1.u32l : &copy.f0.u32h;
    }
    Vec3 sweep(uint8_t* rdram, const recomp_context& original, Vec3 from, Vec3 displacement) {
        // Same level-collision solver and 15-unit radius used by the original
        // camera. Start at the focus so an obstacle shortens the orbit instead
        // of preventing its angle from changing. Scratch is below the caller SP.
        recomp_context call=original; rebind(call,original);
        call.r29=ADD32(original.r29,-0x200);
        const uint32_t result=uint32_t(call.r29)+0x60;
        call.r4=S32(result);
        call.r5=MEM_BU(0x1D,gpr(S32(GameState)));
        call.r6=float_bits(15.0f); call.r7=float_bits(from.x);
        MEM_W(0x10,call.r29)=float_bits(from.y);
        MEM_W(0x14,call.r29)=float_bits(from.z);
        MEM_W(0x18,call.r29)=float_bits(displacement.x);
        MEM_W(0x1C,call.r29)=float_bits(displacement.y);
        MEM_W(0x20,call.r29)=float_bits(displacement.z);
        func_80019058(rdram,&call);
        return get_vec(rdram,result+0xC);
    }
    float clear_distance(uint8_t* rdram, const recomp_context& ctx, Vec3 focus, Vec3 ray, float distance) {
        // The original solver slides its endpoint along walls. Projecting that
        // endpoint onto the orbit can put the camera back inside an oblique wall.
        // Find the longest unmodified sweep on the requested ray instead.
        const auto clear = [&](float d) {
            const Vec3 wanted=focus+ray*d;
            const Vec3 resolved=sweep(rdram,ctx,focus,ray*d);
            return finite(resolved) && length(resolved-wanted)<0.01f;
        };
        if(clear(distance)) return distance;
        float low=0, high=distance;
        // Only obstructed orbits need these probes; at most 2.35 units of
        // conservative shortening with the maximum supported 600-unit radius.
        for(int i=0;i<8;i++) {
            const float middle=(low+high)*0.5f;
            if(clear(middle)) low=middle; else high=middle;
        }
        return std::max(1.0f,low);
    }
}

void doraemon::camera::configure(bool useModern, float speed, float sensitivity, bool invert, bool capture, bool horizontalInvert) {
    stickSpeed.store(std::isfinite(speed) ? std::clamp(speed,30.0f,360.0f) : 120.0f);
    mouseSensitivity.store(std::isfinite(sensitivity) ? std::clamp(sensitivity,0.02f,1.0f) : 0.15f);
    invertX.store(horizontalInvert);
    invertY.store(invert);
    captureMouse.store(capture);
    if(enabled.exchange(useModern)!=useModern) { generation.fetch_add(1); drop_input(); }
    if(!useModern) available.store(false);
}

bool doraemon::camera::is_enabled() { return enabled.load(); }
bool doraemon::camera::is_dialogue_active() { return dialogueActive.load(); }

bool doraemon::camera::is_mouse_capture_enabled() { return captureMouse.load(); }

bool doraemon::camera::is_active() {
    // Release mouse capture even if a paused game stops calling its update hook.
    return enabled.load() && available.load() && now_ms()-lastAvailable.load()<150;
}

void doraemon::camera::submit_input(float yaw, float pitch, float mx, float my, float zoom, float wheel) {
    std::lock_guard lock(inputMutex);
    if(!is_active()) { pending={}; return; }
    pending.x=std::clamp(yaw,-1.0f,1.0f); pending.y=std::clamp(pitch,-1.0f,1.0f);
    pending.mx+=mx; pending.my+=my;
    pending.zoom=std::clamp(zoom,-1.0f,1.0f); pending.wheel+=wheel;
}

void doraemon::camera::clear_input() { drop_input(); }

void doraemon::camera::reset() {
    orbit={}; available.store(false); dialogueActive.store(false); lastAvailable.store(0); drop_input();
}

extern "C" void doraemon_camera_sync(uint8_t* rdram) {
    // Read on the game thread, publish to the overlay without racing RDRAM.
    // func_80012B80: states 1..4 draw/prepare dialogue; 0 and -1 are inactive.
    const int dialogueState=MEM_H(0,gpr(S32(0x800E6B20)));
    dialogueActive.store(dialogueState>=1 && dialogueState<=4);
    const bool active=enabled.load() && manual(rdram);
    available.store(active); lastAvailable.store(now_ms());
    if(!active) { orbit.initialized=false; drop_input(); }
}

extern "C" void doraemon_camera_reset() { doraemon::camera::reset(); }

extern "C" int doraemon_camera_update(uint8_t* rdram, recomp_context* ctx) {
    doraemon_camera_sync(rdram);
    if(!available.load()) return 0; // Run the untouched original A/B or scripted path.
    const Vec3 player=get_vec(rdram,GameState+0x10);
    const Vec3 eye=get_vec(rdram,View), look=get_vec(rdram,View+0xC);
    if(!finite(player) || !finite(eye) || !finite(look)) {
        orbit.initialized=false; available.store(false); drop_input(); return 0;
    }
    const auto now=Clock::now();
    const float elapsed=std::chrono::duration<float>(now-orbit.lastUpdate).count();
    const bool initialize=!orbit.initialized || orbit.generation!=generation.load() ||
        elapsed>0.25f || length(player-orbit.player)>1000.0f;
    const float dt=initialize ? 1.0f/30.0f : std::clamp(elapsed,1.0f/240.0f,1.0f/15.0f);
    if(initialize) {
        const Vec3 offset=eye-look;
        const float radius=length(offset);
        if(radius<1.0f) { available.store(false); return 0; }
        const bool resume=orbit.hasPose && orbit.generation==generation.load() && length(player-orbit.player)<=1000.0f;
        if(!resume) {
            orbit.yaw=std::atan2(offset.x,offset.z)/ToRadians;
            orbit.pitch=std::clamp(std::asin(std::clamp(offset.y/radius,-1.0f,1.0f))/ToRadians,MinPitch,MaxPitch);
            orbit.height=std::clamp(look.y-player.y,20.0f,100.0f);
        }
        orbit.distance=preferredDistance;
        orbit.actualDistance=std::min(radius,orbit.distance);
        orbit.hasPose=true;
        orbit.focus=look; orbit.initialized=true; orbit.generation=generation.load();
        drop_input();
    }
    orbit.lastUpdate=now; orbit.player=player;
    Input input;
    { std::lock_guard lock(inputMutex); input=pending; pending.mx=pending.my=pending.wheel=0; }
    const float horizontalSign=invertX.load() ? -1.0f : 1.0f;
    rotate(orbit.yaw,orbit.pitch,input.x*horizontalSign,input.y,input.mx*horizontalSign,input.my,
        stickSpeed.load(),mouseSensitivity.load(),invertY.load(),dt);
    preferredDistance=std::clamp(preferredDistance+input.zoom*240.0f*dt-input.wheel*30.0f,80.0f,600.0f);
    orbit.distance=preferredDistance;
    // Retain the game's shake/offset effects, but bypass its C-button steps,
    // angle restrictions, automatic recentering and step-change sounds.
    recomp_context effects=*ctx; rebind(effects,*ctx); func_80005FEC(rdram,&effects);
    const Vec3 target=player+Vec3{0,orbit.height,0};
    orbit.focus=orbit.focus+(target-orbit.focus)*blend(14.0f,dt);
    const Vec3 ray=direction(orbit.yaw,orbit.pitch);
    const float allowed=clear_distance(rdram,*ctx,orbit.focus,ray,orbit.distance);
    orbit.actualDistance=collision_distance(orbit.actualDistance,allowed,dt);
    const Vec3 position=orbit.focus+ray*orbit.actualDistance;
    put_vec(rdram,View,position); put_vec(rdram,View+0xC,orbit.focus);
    // Keep the original state coherent for movement-relative input and for
    // handing control back to A/B or an event camera without stale targets.
    put_vec(rdram,CameraState+0x40,orbit.focus); put_vec(rdram,CameraState+0x4C,position);
    put_float(rdram,GameState+4,orbit.pitch);
    put_float(rdram,GameState+8,orbit.yaw<0 ? orbit.yaw+360 : orbit.yaw);
    put_float(rdram,GameState+0xC,orbit.distance);
    MEM_H(4,gpr(S32(CameraState)))=0; MEM_H(0xC,gpr(S32(CameraState)))=0;
    MEM_H(0x14,gpr(S32(CameraState)))=0;
    recomp_context movement=*ctx; rebind(movement,*ctx); func_80004794(rdram,&movement);
    return 1;
}
