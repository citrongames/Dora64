#include "doraemon_touch_layout.hpp"
#include <cstdio>
#include <cstdlib>

using namespace doraemon::touch;
void check(bool value,const char* text) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
bool near(float a,float b) { return std::abs(a-b)<.0001f; }
Point at(State& s,Control c) { return s.layout.positions[static_cast<int>(c)]; }
int main() {
    State s;
    s.down(1,1,at(s,Control::A)); s.up(1,1);
    check(s.read(false).buttons==0x8000,"quick tap survives until controller sample");
    check(s.read(false).buttons==0,"quick tap consumed once");
    s.down(1,1,at(s,Control::A)); s.down(1,2,at(s,Control::Z)); s.down(1,3,at(s,Control::Stick));
    auto p=at(s,Control::Stick); p.x+=.2f; p.y-=.2f; s.move(1,3,p);
    auto input=s.read(false);
    check(input.buttons==0xA000 && input.x>0 && input.y>0 && near(std::hypot(input.x,input.y),1),"independent simultaneous buttons and circular stick");
    s.up(1,1); check(s.read(false).buttons==0x2000,"release does not affect another finger");
    s.set_context(true,false,true);
    check(s.read(false).buttons==0 && !s.read(false).stickOwned,"opening menu releases everything");
    s.down(1,4,at(s,Control::A)); s.set_context(true,false,false);
    check(s.read(false).buttons==0,"menu taps never leak back to game");
    s.down(1,1,at(s,Control::A)); s.gamepad_activity();
    check(!s.visible && s.read(false).buttons==0,"gamepad hides HUD and releases touch");
    s.down(1,9,at(s,Control::A)); s.up(1,9);
    check(s.visible && s.read(false).buttons==0,"first hidden tap only reveals controls");
    s.down(1,10,at(s,Control::A)); check(s.read(false).buttons==0x8000,"next tap works");
    s.clear(); s.down(1,1,at(s,Control::Stick));
    check(s.read(false).stickOwned && s.read(false).x==0,"centered touch overrides physical stick drift");
    s.down(2,1,at(s,Control::Stick)); s.move(2,1,{.5f,.5f});
    check(s.read(false).x==0,"second finger cannot steal stick even with same finger ID");
    s.resize(2560,1600,2); check(!s.read(false).stickOwned,"resize cancels contact");
    s.set_context(false,false,false);
    check(!s.shown(Control::ZoomIn),"classic has no modern zoom buttons");
    s.down(1,1,{.6f,.4f}); s.move(1,1,{.7f,.4f});
    check(s.read(false).buttons==1 && s.read(false).buttons==0,"classic swipe delivers one C-right pulse");
    s.clear(); s.down(1,1,at(s,Control::R)); check(s.read(false).buttons==0x10,"classic R retained");
    s.clear(); s.set_context(true,false,false);
    s.down(1,1,{.6f,.4f}); s.move(1,1,{.7f,.45f});
    auto camera=s.take_camera(); check(camera.dx>0 && camera.dy>0 && s.take_camera().dx==0,"modern deltas consumed once");
    s.set_context(true,true,false);
    check(s.visible && s.shown(Control::ZoomIn),"dialogue dims without hiding controls");
    s.down(1,8,at(s,Control::Start)); check(s.read(false).buttons==0x1000,"START remains available during dialogue");
    s.move(1,1,{.8f,.5f}); check(s.take_camera().dx==0,"dialogue discards outstanding camera drag");
    s.set_context(true,false,false); s.clear();
    auto original=at(s,Control::A); s.begin_edit();
    s.down(1,1,original); s.move(1,1,{.7f,.6f});
    check(near(at(s,Control::A).x,.7f) && !s.read(false).buttons,"editor moves button without playing");
    s.gamepad_activity(); check(s.visible,"gamepad cannot hide editor");
    s.finish_edit(false); check(near(at(s,Control::A).x,original.x),"cancel restores layout");
    s.begin_edit(); s.down(1,1,original); s.move(1,1,{.7f,.6f}); s.finish_edit(true);
    check(near(at(s,Control::A).x,.7f) && !s.read(false).buttons,"save keeps position and releases contact");
    auto encoded=encode_layout(s.layout); auto loaded=decode_layout(nlohmann::json::parse(encoded.dump()));
    check(near(loaded.positions[1].x,.7f) && near(loaded.opacity,.5f),"JSON persistence round trip");
    auto bad=encoded; bad["scale"]="bad"; bad["positions"]["a"]={-9,99}; bad["opacity"]=nullptr;
    loaded=decode_layout(bad);
    check(loaded.scale==1 && loaded.opacity==.5f && loaded.positions[1].x==0 && loaded.positions[1].y==1,"malformed values fall back and coordinates clamp");
    for(auto j:{nlohmann::json(nullptr),nlohmann::json::array(),nlohmann::json{{"version",999}}})
        check(decode_layout(j).positions[1].x==Layout{}.positions[1].x,"unknown schema defaults safely");
    State a,b; a.resize(1280,720,1); b.resize(2560,1440,2);
    a.down(1,1,{.6f,.4f}); b.down(1,1,{.6f,.4f}); a.move(1,1,{.7f,.4f}); b.move(1,1,{.7f,.4f});
    check(near(a.take_camera().dx,b.take_camera().dx),"camera sensitivity independent of resolution");
    check(near(a.halfSize(Control::A).x,b.halfSize(Control::A).x),"DPI-scaled hit boxes match rendering");
    std::puts("Touch controls: all regression checks passed.");
}
