// GTA V native hashes used by the mod, checked against alloc8or's nativedb
// (GTA V Enhanced uses the same hashes through ScriptHookV).
#pragma once
#include <cstdint>

namespace nat {

constexpr uint64_t PLAYER_ID = 0x4F8644AF03D0E0D6;
constexpr uint64_t PLAYER_PED_ID = 0xD80958FC74E988A6;
constexpr uint64_t SET_PLAYER_CONTROL = 0x8D32347D6D4C40A2;  // (player, bool, int flags)

constexpr uint64_t GET_ENTITY_COORDS = 0x3FEF770D40960D5A;   // (entity, bool alive) -> Vector3
constexpr uint64_t GET_ENTITY_HEADING = 0xE83D4F9BA2A38914;  // (entity) -> float
constexpr uint64_t GET_ENTITY_TYPE = 0x8ACD366038D14505;     // 0 none, 1 ped, 2 vehicle, 3 object
constexpr uint64_t IS_ENTITY_DEAD = 0x5F9532F3B5CC2551;      // (entity, bool)
constexpr uint64_t FREEZE_ENTITY_POSITION = 0x428CA6DBD1094446;
constexpr uint64_t SET_ENTITY_INVINCIBLE = 0x3882114BDE571AD4;  // (entity, bool, bool)
constexpr uint64_t SET_ENTITY_COORDS = 0x06843DA7060A026B;  // (entity, x, y, z, bool, bool, bool, bool clearArea)
constexpr uint64_t SET_ENTITY_HEADING = 0x8E2530AA8ADA980E;

constexpr uint64_t CREATE_CAM = 0xC3981DCE61D9E13F;  // (name, bool)
constexpr uint64_t SET_CAM_ACTIVE = 0x026FB97D0A425F84;
constexpr uint64_t RENDER_SCRIPT_CAMS = 0x07E5B515DB0636FC;  // (render, ease, easeTime, p3, p4, p5)
constexpr uint64_t SET_CAM_COORD = 0x4D41783FB745E42E;
constexpr uint64_t SET_CAM_ROT = 0x85973643155D0B07;  // (cam, pitch, roll, yaw, rotationOrder)
constexpr uint64_t SET_CAM_FOV = 0xB13C14F66A00D047;
constexpr uint64_t SET_CAM_NEAR_CLIP = 0xC7848EFCCC545182;
constexpr uint64_t DESTROY_CAM = 0x865908C81A2C22E9;  // (cam, bool)
constexpr uint64_t SET_GAMEPLAY_CAM_RELATIVE_HEADING = 0xB4EC2312F4E5B1F1;

constexpr uint64_t SET_FOCUS_POS_AND_VEL = 0xBB7454BAFF08FE25;  // streaming focus (x, y, z, vx, vy, vz)
constexpr uint64_t CLEAR_FOCUS = 0x31B73D1EA9F01DA2;

constexpr uint64_t HIDE_HUD_AND_RADAR_THIS_FRAME = 0x719FF505F097FD20;
constexpr uint64_t DISABLE_ALL_CONTROL_ACTIONS = 0x5F4B6931816E599B;  // (control)
constexpr uint64_t ENABLE_CONTROL_ACTION = 0x351220255D64C155;        // (control, action, bool)
constexpr uint64_t IS_PAUSE_MENU_ACTIVE = 0xB0034A223497FFCB;

constexpr uint64_t START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE = 0x377906D8A31E5586;
constexpr uint64_t GET_SHAPE_TEST_RESULT = 0x3D87450E15D98694;

// Control actions kept alive while flying, so Esc still pauses the game.
constexpr int INPUT_FRONTEND_PAUSE = 199;
constexpr int INPUT_FRONTEND_PAUSE_ALTERNATE = 200;

}  // namespace nat
