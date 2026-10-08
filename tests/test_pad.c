#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "test_platform.h"
#include "../src/runtime_pad.c"

static int capture;
static Uint32 mouse_buttons;
static uint8_t camera_x, camera_y;
static uint8_t traced_left_x, traced_left_y;
static uint32_t traced_buttons;
int bbgpu_overlay_captures_input(void) { return capture; }
uint32_t bbgpu_mouse_buttons(void) { return mouse_buttons; }
void bbgpu_camera_pad(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint32_t buttons) {
    traced_left_x=lx; traced_left_y=ly; traced_buttons=buttons;
    camera_x=rx; camera_y=ry;
}
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}

static void movement_keys(bool *keys, unsigned held) {
    keys[SDL_SCANCODE_W]=(held&1)!=0;
    keys[SDL_SCANCODE_S]=(held&2)!=0;
    keys[SDL_SCANCODE_A]=(held&4)!=0;
    keys[SDL_SCANCODE_D]=(held&8)!=0;
}

static PadData keyboard_movement_sample(bool *keys, unsigned held) {
    movement_keys(keys,held);
    PadData mapped={.left_x=128,.left_y=128,.right_x=17,.right_y=231};
    apply_keyboard(&mapped,keys,0);
    assert(mapped.right_x==17 && mapped.right_y==231);
    return mapped;
}

static void check_movement_primary(PadData mapped, unsigned primary) {
    const int dx=(int)mapped.left_x-128, dy=(int)mapped.left_y-128;
    assert(dx*dx+dy*dy==90*90+91*91);
    /* A diagonal must stay less than 45 degrees from the direction already
     * held, for horizontal starts as well as vertical starts. */
    if (primary&3) assert(abs(dy)==91 && abs(dx)==90);
    else assert(abs(dx)==91 && abs(dy)==90);
    if (primary==1) assert(dy<0);
    if (primary==2) assert(dy>0);
    if (primary==4) assert(dx<0);
    if (primary==8) assert(dx>0);
}

static void check_movement_turn(PadData before, PadData after) {
    const int ax=(int)before.left_x-128, ay=(int)before.left_y-128;
    const int bx=(int)after.left_x-128, by=(int)after.left_y-128;
    const int dot=ax*bx+ay*by, cross=ax*by-ay*bx;
    /* Adding or releasing one orthogonal direction must not cross 45 degrees
     * in one delivered step. Keep full movement strength throughout. */
    assert(dot>0 && abs(cross)<dot);
    assert(bx*bx+by*by>=127*127 && bx*bx+by*by<=128*128);
}

static PadData keyboard_movement_at(bool *keys, unsigned held, uint64_t timestamp) {
    movement_keys(keys,held);
    PadData mapped={.left_x=128,.left_y=128,.right_x=17,.right_y=231,.timestamp=timestamp};
    apply_keyboard(&mapped,keys,0);
    assert(mapped.right_x==17 && mapped.right_y==231);
    return mapped;
}

static void test_keyboard_release_handoff(void) {
    bool keys[SDL_SCANCODE_COUNT]={0};
    static const unsigned directions[]={1,2,4,8}, rates[]={30,60,90,120};
    for (unsigned p=0;p<4;++p) for (unsigned s=0;s<4;++s) {
        const unsigned primary=directions[p], secondary=directions[s];
        if (!!(primary&3)==!!(secondary&3)) continue;
        for (unsigned rate=0;rate<4;++rate) {
            const uint64_t step=1000000/rates[rate], start=1000000;
            keyboard_movement_at(keys,0,start);
            PadData cardinal=keyboard_movement_at(keys,primary,start+step);
            PadData diagonal=keyboard_movement_at(keys,primary|secondary,start+2*step);
            check_movement_turn(cardinal,diagonal);
            PadData handoff=keyboard_movement_at(keys,secondary,start+3*step);
            check_movement_turn(diagonal,handoff); /* previous mapper exceeds 45 degrees here */
            check_movement_primary(handoff,secondary);
            /* Extra pad polls in the same update must retain the handoff. */
            for (unsigned poll=0;poll<80;++poll) {
                PadData burst=keyboard_movement_at(keys,secondary,start+3*step+poll*100);
                assert(burst.left_x==handoff.left_x && burst.left_y==handoff.left_y);
            }
            PadData final=keyboard_movement_at(keys,secondary,start+4*step);
            check_movement_turn(handoff,final);
            assert(final.left_x==(secondary==4 ? 0 : secondary==8 ? 255 : 128));
            assert(final.left_y==(secondary==1 ? 0 : secondary==2 ? 255 : 128));
            for (unsigned frame=1;frame<120;++frame) {
                PadData held=keyboard_movement_at(keys,secondary,start+(4+frame)*step);
                assert(held.left_x==final.left_x && held.left_y==final.left_y);
            }
            /* Releasing the secondary returns to the original primary directly. */
            keyboard_movement_at(keys,0,start);
            keyboard_movement_at(keys,primary,start+step);
            diagonal=keyboard_movement_at(keys,primary|secondary,start+2*step);
            final=keyboard_movement_at(keys,primary,start+3*step);
            check_movement_turn(diagonal,final);
            assert(final.left_x==cardinal.left_x && final.left_y==cardinal.left_y);
            /* No released-key inertia when all keys are lifted during handoff. */
            keyboard_movement_at(keys,primary|secondary,start+4*step);
            keyboard_movement_at(keys,secondary,start+5*step);
            final=keyboard_movement_at(keys,0,start+5*step+1);
            assert(final.left_x==128 && final.left_y==128);
            /* A new direction cancels the old handoff immediately. */
            keyboard_movement_at(keys,primary,start+6*step);
            keyboard_movement_at(keys,primary|secondary,start+7*step);
            keyboard_movement_at(keys,secondary,start+8*step);
            final=keyboard_movement_at(keys,primary,start+8*step+1);
            assert(final.left_x==cardinal.left_x && final.left_y==cardinal.left_y);
        }
    }
    /* Expire exactly at the time limit, including high-frequency input reads. */
    keyboard_movement_at(keys,0,10000);
    keyboard_movement_at(keys,1,11000);
    keyboard_movement_at(keys,9,12000);
    PadData handoff=keyboard_movement_at(keys,8,13000);
    PadData final=keyboard_movement_at(keys,8,21000);
    check_movement_turn(handoff,final);
    assert(final.left_x==255 && final.left_y==128);
    /* Missing or discontinuous clocks cannot retain a released direction. */
    keyboard_movement_at(keys,0,0);
    keyboard_movement_at(keys,1,0);
    keyboard_movement_at(keys,9,0);
    check_movement_primary(keyboard_movement_at(keys,8,0),8);
    final=keyboard_movement_at(keys,8,0);
    assert(final.left_x==255 && final.left_y==128);
    keyboard_movement_at(keys,1,11000);
    keyboard_movement_at(keys,9,12000);
    keyboard_movement_at(keys,8,13000);
    final=keyboard_movement_at(keys,8,12999);
    assert(final.left_x==255 && final.left_y==128);
    /* Opposing keys cancel even when their addition leaves the resolved
     * direction unchanged during a handoff (D -> W+S+D). */
    keyboard_movement_at(keys,1,22000);
    keyboard_movement_at(keys,9,23000);
    keyboard_movement_at(keys,8,24000);
    final=keyboard_movement_at(keys,11,24001);
    assert(final.left_x==255 && final.left_y==128);
    /* Releasing keyboard input hands native axes back without a tail. */
    keyboard_movement_at(keys,1,25000);
    keyboard_movement_at(keys,9,26000);
    keyboard_movement_at(keys,8,27000);
    movement_keys(keys,0);
    final=(PadData){.left_x=63,.left_y=211,.timestamp=27001};
    apply_keyboard(&final,keys,0);
    assert(final.left_x==63 && final.left_y==211);
    /* Neither opening the menu nor reopening the pad can retain a handoff. */
    keyboard_movement_at(keys,1,28000);
    keyboard_movement_at(keys,9,29000);
    keyboard_movement_at(keys,8,30000);
    capture=1;
    PadData menu;
    assert(pad_read_state(1,&menu)==0 && menu.left_x==128 && menu.left_y==128);
    capture=0;
    final=keyboard_movement_at(keys,8,30001);
    assert(final.left_x==255 && final.left_y==128);
    keyboard_movement_at(keys,1,31000);
    keyboard_movement_at(keys,9,32000);
    keyboard_movement_at(keys,8,33000);
    assert(pad_close(1)==0 && pad_open(1,0,0,NULL)==1);
    final=keyboard_movement_at(keys,8,33001);
    assert(final.left_x==255 && final.left_y==128);
    keyboard_movement_at(keys,0,1);
}

static void test_keyboard_transition_order(void) {
    bool keys[SDL_SCANCODE_COUNT]={0};
    static const unsigned directions[]={1,2,4,8};
    /* Eight ordered cardinal-to-diagonal transitions. Keep both held over
     * repeated reads, release/re-add the first key, then reverse the secondary. */
    for (unsigned p=0;p<4;++p) for (unsigned s=0;s<4;++s) {
        const unsigned primary=directions[p], secondary=directions[s];
        if (!!(primary&3)==!!(secondary&3)) continue;
        keyboard_movement_sample(keys,0);
        PadData cardinal=keyboard_movement_sample(keys,primary);
        assert(cardinal.left_x==(primary==4 ? 0 : primary==8 ? 255 : 128));
        assert(cardinal.left_y==(primary==1 ? 0 : primary==2 ? 255 : 128));
        for (unsigned frame=0;frame<240;++frame)
            check_movement_primary(keyboard_movement_sample(keys,primary|secondary),primary);
        PadData again=keyboard_movement_sample(keys,primary);
        assert(again.left_x==cardinal.left_x && again.left_y==cardinal.left_y);
        check_movement_primary(keyboard_movement_sample(keys,primary|secondary),primary);
        keyboard_movement_sample(keys,secondary);
        for (unsigned frame=0;frame<240;++frame)
            check_movement_primary(keyboard_movement_sample(keys,primary|secondary),secondary);
        /* Reversing the secondary direction does not change the held primary. */
        const unsigned opposite_primary=primary==1 ? 2 : primary==2 ? 1 : primary==4 ? 8 : 4;
        check_movement_primary(keyboard_movement_sample(keys,opposite_primary|secondary),secondary);
    }
    /* Opposing keys cancel an axis. The remaining axis becomes primary. */
    keyboard_movement_sample(keys,0);
    keyboard_movement_sample(keys,4);
    check_movement_primary(keyboard_movement_sample(keys,5),4);
    PadData cancelled=keyboard_movement_sample(keys,13); /* A+D+W */
    assert(cancelled.left_x==128 && cancelled.left_y==0);
    check_movement_primary(keyboard_movement_sample(keys,5),1);
    keyboard_movement_sample(keys,15);
    check_movement_primary(keyboard_movement_sample(keys,5),1); /* simultaneous start */
    keyboard_movement_sample(keys,0);
    keyboard_movement_sample(keys,4);
    check_movement_primary(keyboard_movement_sample(keys,5),4);
    assert(pad_close(1)==0 && pad_open(1,0,0,NULL)==1);
    check_movement_primary(keyboard_movement_sample(keys,5),1); /* new pad session */
    keyboard_movement_sample(keys,0);
}

static void test_keyboard_movement(SDL_Joystick *joystick) {
    bool keys[SDL_SCANCODE_COUNT]={0};
    /* The report's W+D=(218,37) worked while W+A=(37,37) jerked. Preserve
     * that forward/right vector and mirror it for forward/left. All diagonal
     * directions retain near-full strength without equal-axis 45-degree ties. */
    for (unsigned held=0;held<16;++held) {
        movement_keys(keys,held);
        PadData mapped={.left_x=128,.left_y=128,.right_x=17,.right_y=231};
        apply_keyboard(&mapped,keys,0);
        const int x=((held&8)!=0)-((held&4)!=0);
        const int y=((held&2)!=0)-((held&1)!=0);
        const int delivered_x=(int)mapped.left_x-128, delivered_y=(int)mapped.left_y-128;
        assert(delivered_x*delivered_x+delivered_y*delivered_y<=128*128);
        if (x && y) {
            assert(delivered_x==90*x && delivered_y==91*y);
            assert(delivered_x*delivered_x+delivered_y*delivered_y>=128*128-4);
        }
        const int raw_x=x && y ? x*23040 : x<0 ? -32768 : x*32767;
        const int raw_y=x && y ? y*23296 : y<0 ? -32768 : y*32767;
        assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,(Sint16)raw_x));
        assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,(Sint16)raw_y));
        SDL_UpdateJoysticks(); SDL_UpdateGamepads();
        PadData analog;
        assert(pad_read_state(1,&analog)==0);
        assert(traced_left_x==analog.left_x && traced_left_y==analog.left_y && traced_buttons==analog.buttons);
        assert(mapped.left_x==analog.left_x && mapped.left_y==analog.left_y);
        assert(mapped.right_x==17 && mapped.right_y==231);
        const float vx=((int)mapped.left_x-128)/128.0f;
        const float vy=((int)mapped.left_y-128)/128.0f;
        const float strength=SDL_sqrtf(vx*vx+vy*vy);
        assert(x || y ? strength>.99f && strength<=1.0f : strength==0.f);
    }
    /* Keep W held while repeatedly adding/releasing/changing A and D. Every
     * sample remains near full movement strength; no neutral or square-corner
     * sample can leak into the transition. */
    static const unsigned transitions[]={1,5,1,9,1,5,9,1};
    for (unsigned repeat=0;repeat<60;++repeat) for (unsigned n=0;n<sizeof(transitions)/sizeof(*transitions);++n) {
        movement_keys(keys,transitions[n]);
        PadData mapped={.left_x=128,.left_y=128};
        apply_keyboard(&mapped,keys,0);
        assert(mapped.left_y==(transitions[n]==1 ? 0 : 37));
        assert(mapped.left_x==(transitions[n]==1 ? 128 : transitions[n]==5 ? 38 : 218));
    }
    /* Every possible physical axis pair mixed with each nonempty WASD state
     * stays bounded for both vertical-first and horizontal-first histories. */
    for (unsigned horizontal=0;horizontal<2;++horizontal) for (unsigned held=1;held<16;++held) {
        const int key_x=((held&8)!=0)-((held&4)!=0), key_y=((held&2)!=0)-((held&1)!=0);
        keyboard_movement_sample(keys,0);
        if (key_x && key_y)
            keyboard_movement_sample(keys,horizontal ? (key_x<0 ? 4 : 8) : (key_y<0 ? 1 : 2));
        movement_keys(keys,held);
        for (unsigned px=0;px<256;++px) for (unsigned py=0;py<256;++py) {
            PadData mapped={.left_x=(uint8_t)px,.left_y=(uint8_t)py,.right_x=17,.right_y=231};
            apply_keyboard(&mapped,keys,0);
            const int dx=(int)mapped.left_x-128, dy=(int)mapped.left_y-128;
            assert(dx*dx+dy*dy<=128*128);
            assert(mapped.right_x==17 && mapped.right_y==231);
        }
    }
    memset(keys,0,sizeof(keys));
    PadData mixed={.left_x=0,.left_y=255,.right_x=17,.right_y=231};
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==0 && mixed.left_y==255); /* controller-only passthrough */
    keys[SDL_SCANCODE_W]=true;
    mixed.left_x=255; mixed.left_y=128;
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==218 && mixed.left_y==37); /* W + native X share the circle */
    /* A normalized negative component can truncate to zero. Its rounding
     * candidate must still use the original sign, like its positive mirror. */
    for (unsigned px=127;px<=129;px+=2) {
        mixed.left_x=(uint8_t)px; mixed.left_y=128;
        apply_keyboard(&mixed,keys,0);
        assert(mixed.left_x==px && mixed.left_y==1);
    }
    keys[SDL_SCANCODE_S]=true;
    mixed.left_x=63; mixed.left_y=211;
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==63 && mixed.left_y==128);
    memset(keys,0,sizeof(keys));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-23170));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,-23170));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    PadData native;
    assert(pad_read_state(1,&native)==0 && native.left_x==37 && native.left_y==37);
    apply_keyboard(&native,keys,0);
    assert(native.left_x==37 && native.left_y==37); /* native axes are not re-quantized */
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
}

int main(void) {
    char path[4096];
    int fd=bb_test_temp(path,sizeof(path),"bbport-pad-test");
    assert(fd>=0);
    close(fd);
    setenv("BB_PAD_FILE",path,1);
    /* bbport.ini controls: buttons moved, a trigger as a button and a button as a trigger. */
    char config[4096];
    int config_fd=bb_test_temp(config,sizeof(config),"bbport-pad-config");
    assert(config_fd>=0);
    const char controls[]="upscaler=fsr3\npad.cross=b\npad.circle=a\npad.r2=rightshoulder\n"
                          "pad.r1=righttrigger\nkey.cross=X, Space\npad.bogus=a\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    setenv("BB_CONFIG",config,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    /* A new touch gets a new id (1..127), as from a DualShock 4: the game ignores id 0. */
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471 && data.touches[0].id==1);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    // The final native axes also drive mouse/controller ownership, including
    // vertical-only movement and neutralization while the port menu is open.
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-32768));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.right_y==0);
    assert(camera_x==data.right_x && camera_y==0);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,32767));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && camera_x==255 && camera_y==0);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,0));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,0));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && camera_x==128 && camera_y==128);
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==2);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==3);
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    assert(camera_x==128 && camera_y==128);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    // A touchpad/button still held when the menu closes must not become a new gesture.
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(data.buttons==(BTN_CROSS|BTN_R2|BTN_R1) && data.r2==255);
    assert(bindings[IN_CROSS].key_count==2 && bindings[IN_CROSS].keys[0]==SDL_SCANCODE_X &&
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);
    test_keyboard_movement(joystick);
    test_keyboard_release_handoff();
    test_keyboard_transition_order();
    /* Capture-helper names round-trip through the actual INI parser and pad ABI. */
    FILE *mouse_config=fopen(config,"w");
    assert(mouse_config);
    fputs("key.cross=Space, Mouse Left\nkey.r1=Mouse X1\nkey.r2=Mouse Right\n"
          "key.square=Mouse Middle\nkey.circle=Mouse X2\nkey.up=Mouse Wheel Up\n"
          "key.down=Mouse Wheel Down\nkey.left=Mouse Wheel Left\nkey.right=Mouse Wheel Right\n"
          "key.triangle=Comma\nkey.move_up=Mouse Left, T\nkey.move_right=H\n",mouse_config);
    fclose(mouse_config);
    load_bindings();
    assert(bindings[IN_CROSS].key_count==1 && bindings[IN_CROSS].mouse_count==1);
    bool keyboard[SDL_SCANCODE_COUNT]={0};
    PadData mapped={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    apply_keyboard(&mapped,keyboard,SDL_BUTTON_LMASK|SDL_BUTTON_MMASK|SDL_BUTTON_RMASK|
                   SDL_BUTTON_X1MASK|SDL_BUTTON_X2MASK|BB_WHEEL_UP|BB_WHEEL_RIGHT);
    assert(mapped.buttons==(BTN_CROSS|BTN_SQUARE|BTN_CIRCLE|BTN_R1|BTN_R2|BTN_UP|BTN_RIGHT));
    assert(mapped.r2==255 && mapped.left_y==0);
    keyboard[SDL_SCANCODE_COMMA]=true;
    mapped=(PadData){0};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.buttons==BTN_TRIANGLE);
    keyboard[SDL_SCANCODE_COMMA]=false;
    mapped=(PadData){0};
    apply_keyboard(&mapped,keyboard,BB_WHEEL_DOWN|BB_WHEEL_LEFT);
    assert(mapped.buttons==(BTN_DOWN|BTN_LEFT));
    mapped=(PadData){0};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.buttons==0);
    /* Normalize actions after remapping, including a mouse-bound movement key. */
    keyboard[SDL_SCANCODE_T]=keyboard[SDL_SCANCODE_H]=true;
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.left_x==218 && mapped.left_y==37);
    keyboard[SDL_SCANCODE_T]=false;
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,SDL_BUTTON_LMASK);
    assert(mapped.left_x==218 && mapped.left_y==37);
    /* Start with the remapped horizontal action, then add either the mouse-
     * bound or keyboard-bound vertical action. The held action retains priority. */
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.left_x==219 && mapped.left_y==38); /* mouse-bound primary release */
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.left_x==255 && mapped.left_y==128);
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,SDL_BUTTON_LMASK);
    assert(mapped.left_x==219 && mapped.left_y==38);
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    keyboard[SDL_SCANCODE_T]=true;
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.left_x==219 && mapped.left_y==38);
    keyboard[SDL_SCANCODE_H]=false;
    mouse_buttons=SDL_BUTTON_RMASK;
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.r2==0);
    capture=0;
    mouse_buttons=0;
    keyboard[SDL_SCANCODE_H]=true;
    mapped=(PadData){.left_x=128,.left_y=128};
    apply_keyboard(&mapped,keyboard,0);
    assert(mapped.left_x==218 && mapped.left_y==37); /* menu capture cleared old priority */
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    puts("PASS: pad ABI, near-full keyboard diagonals, all eight transition orders, stable held-axis priority, bounded primary-release handoffs, immediate stops and cancellations, exhaustive mixed input in both orders, opposing keys, menu/session resets, native/remapped input, debug camera chord, left/right clicks, SDL touch coordinates, overlay capture, controls");
}
