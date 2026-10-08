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

static void test_keyboard_movement(SDL_Joystick *joystick) {
    bool keys[SDL_SCANCODE_COUNT]={0};
    /* Compare every digital direction with a virtual analog controller on the
     * unit circle, through scePadReadState and the actual SDL axis conversion.
     * Opposite keys cancel exactly, with no unintended -1 axis value. */
    for (unsigned held=0;held<16;++held) {
        movement_keys(keys,held);
        PadData mapped={.left_x=128,.left_y=128,.right_x=17,.right_y=231};
        apply_keyboard(&mapped,keys,0);
        const int x=((held&8)!=0)-((held&4)!=0);
        const int y=((held&2)!=0)-((held&1)!=0);
        const int magnitude=x && y ? 23170 : 32767;
        const int raw_x=x<0 && !y ? -32768 : x*magnitude;
        const int raw_y=y<0 && !x ? -32768 : y*magnitude;
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
        assert(x || y ? strength>.99f && strength<1.01f : strength==0.f);
    }
    /* Keep W held while repeatedly adding/releasing/changing A and D. Every
     * sample remains at full movement strength; no neutral or square-corner
     * sample can leak into the transition. */
    static const unsigned transitions[]={1,5,1,9,1,5,9,1};
    for (unsigned repeat=0;repeat<60;++repeat) for (unsigned n=0;n<sizeof(transitions)/sizeof(*transitions);++n) {
        movement_keys(keys,transitions[n]);
        PadData mapped={.left_x=128,.left_y=128};
        apply_keyboard(&mapped,keys,0);
        assert(mapped.left_y==(transitions[n]==1 ? 0 : 37));
        assert(mapped.left_x==(transitions[n]==1 ? 128 : transitions[n]==5 ? 37 : 218));
    }
    memset(keys,0,sizeof(keys));
    PadData mixed={.left_x=0,.left_y=255,.right_x=17,.right_y=231};
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==0 && mixed.left_y==255); /* controller-only passthrough */
    keys[SDL_SCANCODE_W]=true;
    mixed.left_x=255; mixed.left_y=128;
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==218 && mixed.left_y==37); /* W + native X share the circle */
    keys[SDL_SCANCODE_S]=true;
    mixed.left_x=63; mixed.left_y=211;
    apply_keyboard(&mixed,keys,0);
    assert(mixed.left_x==63 && mixed.left_y==128);
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
    keyboard[SDL_SCANCODE_H]=false;
    mouse_buttons=SDL_BUTTON_RMASK;
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.r2==0);
    capture=0;
    mouse_buttons=0;
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    puts("PASS: pad ABI, circular keyboard movement, W/A/D transitions, opposing keys, native/remapped input, debug camera chord, left/right clicks, SDL touch coordinates, overlay capture, controls");
}
