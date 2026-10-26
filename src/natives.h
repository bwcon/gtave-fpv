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

constexpr uint64_t GET_HASH_KEY = 0xD24D37CC275948CC;  // (const char *) -> Hash
constexpr uint64_t IS_MODEL_IN_CDIMAGE = 0x35B9E0803292B641;
constexpr uint64_t REQUEST_MODEL = 0x963D27A58DF860AC;
constexpr uint64_t HAS_MODEL_LOADED = 0x98A4EB5D89A0C952;
constexpr uint64_t SET_MODEL_AS_NO_LONGER_NEEDED = 0xE532F5D78798DAAB;
// (model, x, y, z, isNetwork, bScriptHostObj, dynamic, p7) -> Object
constexpr uint64_t CREATE_OBJECT_NO_OFFSET = 0x9A294B2138ABB884;
constexpr uint64_t DELETE_ENTITY = 0xAE3CBE5BF394C9C9;  // (Entity *)
constexpr uint64_t IS_MODEL_A_VEHICLE = 0x19AAC8F07BFEC53E;
constexpr uint64_t GET_MODEL_DIMENSIONS = 0x03E8D3D5F549087A;  // (model, Vector3 *min, Vector3 *max)
// (model, x, y, z, heading, isNetwork, bScriptHostVeh, p7) -> Vehicle
constexpr uint64_t CREATE_VEHICLE = 0xAF35D0D2583051B0;
constexpr uint64_t SET_VEHICLE_ENGINE_ON = 0x2497C4717C8B881E;  // (vehicle, on, instantly, disableAutoStart)
constexpr uint64_t DOES_ENTITY_EXIST = 0x7239B21A38F536BA;
constexpr uint64_t SET_ENTITY_AS_MISSION_ENTITY = 0xAD738C3085FE7E11;  // (entity, bool, bool)
constexpr uint64_t SET_ENTITY_COLLISION = 0x1A9205C1B9EE827F;          // (entity, toggle, keepPhysics)
constexpr uint64_t SET_ENTITY_COORDS_NO_OFFSET = 0x239A3351AC1DA385;   // (entity, x, y, z, bool, bool, bool)
constexpr uint64_t SET_ENTITY_QUATERNION = 0x77B21BE7AC540F07;         // (entity, x, y, z, w)
constexpr uint64_t SET_ENTITY_VISIBLE = 0xEA1C610A04DB6BBB;            // (entity, toggle, bool)

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

constexpr uint64_t SET_AUDIO_FLAG = 0xB9EFD5C25018725A;                       // (name, bool)
constexpr uint64_t SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY = 0x1098355A16064BB3;
constexpr uint64_t SET_MOBILE_PHONE_RADIO_STATE = 0xBF286C554784F3DF;
constexpr uint64_t IS_MOBILE_PHONE_RADIO_ACTIVE = 0xB35CE999E8EF317E;
constexpr uint64_t GET_PLAYER_RADIO_STATION_INDEX = 0xE8AF77C4C06ADC93;       // 255 = off
constexpr uint64_t GET_NUM_UNLOCKED_RADIO_STATIONS = 0xF1620ECB50E01DE7;
constexpr uint64_t GET_RADIO_STATION_NAME = 0xB28ECA15046CA8B9;               // (index) -> name
constexpr uint64_t SET_RADIO_TO_STATION_INDEX = 0xA619B168B8A8570F;
constexpr uint64_t SET_RADIO_TO_STATION_NAME = 0xC69EDA28699D5107;            // "OFF" = off
constexpr uint64_t GET_FILENAME_FOR_AUDIO_CONVERSATION = 0x7B5280EBA9840C72;  // label -> text

// (type, pos xyz, dir xyz, rot xyz, scale xyz, r, g, b, a, bob, faceCamera, rotOrder, rotate, txd, txn, onEntities)
constexpr uint64_t DRAW_MARKER = 0x28477EC23D892089;

constexpr uint64_t START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE = 0x377906D8A31E5586;
constexpr uint64_t GET_SHAPE_TEST_RESULT = 0x3D87450E15D98694;

// Control actions kept alive while flying, so Esc still pauses the game.
constexpr int INPUT_FRONTEND_PAUSE = 199;
constexpr int INPUT_FRONTEND_PAUSE_ALTERNATE = 200;

}  // namespace nat
