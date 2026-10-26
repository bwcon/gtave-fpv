#include "race.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"
#include "natives.h"
#include "shv.h"

namespace race {
namespace {

namespace fs = std::filesystem;
constexpr float kRadToDeg = 57.2957795f;

struct Gate {
    float x, y, z;
    float heading;  // degrees, GTA's: 0 north, 90 west; the way through
    float radius;   // metres
};

struct Settings {
    int mark_aux = 3;       // [Race] MarkSwitch: AUX number of the gate mark switch
    float radius = 2.0f;    // [Race] GateRadius, metres
    float marker_yaw = 0;   // [Race] MarkerYaw: degrees added to the ring's rotation
    int laps = 3;           // [Race] Laps: laps in a circuit race, 0 endless
};

std::mutex g_mutex;  // the HUD reads from the render thread
std::vector<Gate> g_gates;
std::string g_name;     // the loaded race's file name, empty: none
bool g_point_to_point = false;  // the last gate is the finish, not the first
bool g_recording = false;
int g_next = 0;         // the gate to fly through next
bool g_running = false; // a race is under way (the start gate was passed)
bool g_finished = false;  // the last race is over; g_total is its time
double g_race_start = 0, g_lap_start = 0, g_last_lap = 0, g_best_lap = 0, g_total = 0;
int g_laps = 0;         // laps done in this race; point to point: runs done
double g_sim_time = 0;
float g_prev[3];
bool g_have_prev = false;
bool g_switch_was_on = false;
Settings g_settings;
bool g_settings_read = false;

std::string game_dir() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/"));
}

fs::path races_dir() { return fs::path(game_dir()) / "fpv-data" / "races"; }

std::string ini_path() { return game_dir() + "\\GTAVE.FPV.ini"; }

// Under g_mutex.
void read_settings() {
    if (g_settings_read) return;
    g_settings_read = true;
    const std::string ini = ini_path();
    char buf[32];
    auto value = [&](const char *key, const char *fallback) {
        GetPrivateProfileStringA("Race", key, "", buf, sizeof buf, ini.c_str());
        if (!buf[0]) {
            WritePrivateProfileStringA("Race", key, fallback, ini.c_str());  // documents the key
            std::snprintf(buf, sizeof buf, "%s", fallback);
        }
        return std::atof(buf);
    };
    g_settings.mark_aux = std::clamp(int(value("MarkSwitch", "3")), 1, FPV_CHANNEL_COUNT - 4);
    g_settings.radius = std::clamp(float(value("GateRadius", "2.0")), 0.5f, 20.0f);
    g_settings.marker_yaw = float(value("MarkerYaw", "0"));
    g_settings.laps = std::clamp(int(value("Laps", "3")), 0, 99);
}

// Edge-detected key press; the first reading of each key is only a baseline.
bool key_pressed(int vk) {
    static bool was_down[256], seen[256];
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = seen[vk] && down && !was_down[vk];
    was_down[vk] = down;
    seen[vk] = true;
    return pressed;
}

void reset_lap() {
    g_next = 0;
    g_running = false;
    g_have_prev = false;
}

// Text file: one gate per line, "gate x y z heading radius", and
// "mode point_to_point" for a race that ends at its last gate.
bool save(const fs::path &file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    FILE *f = _wfopen(file.c_str(), L"w");
    if (!f) return false;
    std::fprintf(f, "# GTAVE.FPV race v1: gate x y z heading radius (metres, degrees). The first gate is the start;\n"
                    "# the finish is the first gate again, or the last gate with mode point_to_point.\n");
    if (g_point_to_point) std::fprintf(f, "mode point_to_point\n");
    for (const Gate &g : g_gates) std::fprintf(f, "gate %.2f %.2f %.2f %.1f %.2f\n", g.x, g.y, g.z, g.heading, g.radius);
    std::fclose(f);
    return true;
}

bool load(const fs::path &file) {
    FILE *f = _wfopen(file.c_str(), L"r");
    if (!f) return false;
    std::vector<Gate> gates;
    bool point_to_point = false;
    char line[256], mode[32];
    while (std::fgets(line, sizeof line, f)) {
        Gate g{};
        if (std::sscanf(line, "gate %f %f %f %f %f", &g.x, &g.y, &g.z, &g.heading, &g.radius) == 5) gates.push_back(g);
        if (std::sscanf(line, "mode %31s", mode) == 1) point_to_point = std::strcmp(mode, "point_to_point") == 0;
    }
    std::fclose(f);
    if (gates.size() < 2) return false;
    g_gates = std::move(gates);
    g_point_to_point = point_to_point;
    g_name = file.filename().string();
    g_laps = 0;
    g_finished = false;
    g_last_lap = g_best_lap = 0;
    reset_lap();
    return true;
}

std::vector<fs::path> saved_races() {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(races_dir(), ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".race") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

// L: the next saved race, then none.
void cycle() {
    std::vector<fs::path> files = saved_races();
    size_t i = 0;
    if (!g_name.empty())
        while (i < files.size() && files[i].filename().string() != g_name) ++i;
    i = g_name.empty() ? 0 : i + 1;
    g_gates.clear();
    g_name.clear();
    reset_lap();
    for (; i < files.size(); ++i)
        if (load(files[i])) break;
    fpvlog::write("race: %s", g_name.empty() ? "off" : g_name.c_str());
}

void mark(const fpv_frame &f) {
    // Facing the way the drone flies; when it hovers, the way its nose points.
    float fx = f.velocity[0], fy = f.velocity[1];
    if (fx * fx + fy * fy < 4.0f) {
        // host forward (0, 1, 0) rotated by the drone's quaternion (x, y, z, w)
        const float *q = f.rotation;
        fx = 2 * (q[0] * q[1] - q[3] * q[2]);
        fy = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
    }
    Gate g{f.position[0], f.position[1], f.position[2], std::atan2(-fx, fy) * kRadToDeg, g_settings.radius};
    g_gates.push_back(g);
    fpvlog::write("race: gate %d at %.1f %.1f %.1f heading %.0f", int(g_gates.size()), g.x, g.y, g.z, g.heading);
}

void toggle_recording() {
    if (!g_recording) {
        g_recording = true;
        g_point_to_point = false;
        g_finished = false;
        g_gates.clear();
        g_name.clear();
        reset_lap();
        fpvlog::write("race: recording");
        return;
    }
    g_recording = false;
    if (g_gates.size() < 2) {
        g_gates.clear();
        fpvlog::write("race: recording dropped, it needs two gates");
        return;
    }
    char name[64];
    std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof name, "race_%Y-%m-%d_%H%M%S.race", std::localtime(&t));
    fs::path file = races_dir() / name;
    if (save(file)) {
        g_name = name;
        fpvlog::write("race: saved %d gates to %s", int(g_gates.size()), file.string().c_str());
    } else {
        fpvlog::write("race: cannot write %s", file.string().c_str());
    }
    g_laps = 0;
    g_last_lap = g_best_lap = 0;
    reset_lap();
}

// The drone's path this frame went through the gate's disc, front to back.
bool crossed(const Gate &g, const float *a, const float *b) {
    const float h = g.heading / kRadToDeg, nx = -std::sin(h), ny = std::cos(h);  // horizontal normal
    float da = (a[0] - g.x) * nx + (a[1] - g.y) * ny, db = (b[0] - g.x) * nx + (b[1] - g.y) * ny;
    if (!(da < 0 && db >= 0)) return false;
    float t = da / (da - db), p[3];
    for (int i = 0; i < 3; ++i) p[i] = a[i] + (b[i] - a[i]) * t;
    float dx = p[0] - g.x, dy = p[1] - g.y, dz = p[2] - g.z;
    return dx * dx + dy * dy + dz * dz <= g.radius * g.radius;
}

void finish(double total) {
    g_running = false;
    g_finished = true;
    g_total = total;
    g_next = 0;
    fpvlog::write("race: finished in %.2f s", total);
}

// Through g_gates[g_next].
void pass_gate(double t) {
    const int n = int(g_gates.size());
    if (g_point_to_point && g_running && g_next == n - 1) {
        g_last_lap = t - g_lap_start;
        if (g_best_lap <= 0 || g_last_lap < g_best_lap) g_best_lap = g_last_lap;
        ++g_laps;
        finish(g_last_lap);
        return;
    }
    if (g_next == 0) {  // the start, and on a circuit the finish of each lap
        if (g_running) {
            g_last_lap = t - g_lap_start;
            if (g_best_lap <= 0 || g_last_lap < g_best_lap) g_best_lap = g_last_lap;
            ++g_laps;
            fpvlog::write("race: lap %d %.2f s (best %.2f s)", g_laps, g_last_lap, g_best_lap);
            if (g_settings.laps > 0 && g_laps >= g_settings.laps) {
                finish(t - g_race_start);
                return;
            }
        } else {
            g_running = true;
            g_finished = false;
            g_race_start = t;
            g_laps = 0;
        }
        g_lap_start = t;
    }
    g_next = (g_next + 1) % n;
}

void check_gates(const fpv_frame &f) {
    if (g_gates.size() < 2 || g_recording) return;
    const float *b = f.position;
    if (g_have_prev) {
        float dx = b[0] - g_prev[0], dy = b[1] - g_prev[1], dz = b[2] - g_prev[2];
        // a respawn or a teleport is not a path
        if (dx * dx + dy * dy + dz * dz < 900 && crossed(g_gates[g_next], g_prev, b)) pass_gate(f.sim_time);
    }
    std::copy(b, b + 3, g_prev);
    g_have_prev = true;
}

// Rings: the next gate bright (green at the start, white at a point to point
// finish), the one after it dim.
void draw_gates() {
    const int n = int(g_gates.size());
    for (int i = 0; i < n; ++i) {
        const Gate &g = g_gates[i];
        int r = 255, gr = 255, b = 255, a = 40;
        if (g_recording) {
            r = 255, gr = 80, b = 80, a = 140;
        } else if (i == g_next) {
            a = 170;
            if (i == 0) r = 60, gr = 255, b = 90;
            else if (g_point_to_point && i == n - 1) a = 220;
            else r = 255, gr = 200, b = 0;
        } else if (i == (g_next + 1) % n && !(g_point_to_point && i == 0)) {
            a = 90;
        }
        const float d = g.radius * 2;
        // Marker 6 is a vertical ring; rotated about Z, it faces the gate's heading.
        shv::call(nat::DRAW_MARKER, 6, g.x, g.y, g.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                  g.heading + g_settings.marker_yaw, d, d, d, r, gr, b, a, false, false, 2, false,
                  (const char *)nullptr, (const char *)nullptr, false);
    }
}

std::string clock(double s) {
    char buf[32];
    if (s <= 0) return "--:--.--";
    int m = int(s / 60);
    std::snprintf(buf, sizeof buf, "%d:%05.2f", m, s - m * 60);
    return buf;
}

}  // namespace

void tick(const fpv_frame &frame, bool keys) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    g_sim_time = frame.sim_time;
    if (keys) {
        if (key_pressed('N')) toggle_recording();
        if (key_pressed('L') && !g_recording) cycle();
        if (g_recording && key_pressed('M')) mark(frame);
        if (g_recording && key_pressed('E') && !g_gates.empty()) {  // the finish: a point to point race
            mark(frame);
            g_point_to_point = true;
            toggle_recording();
        }
        if (g_recording && key_pressed('U') && !g_gates.empty()) g_gates.pop_back();
    }
    const bool on = frame.channels[3 + g_settings.mark_aux] > 0.5f;  // AETR, then AUX1..
    if (g_recording && on && !g_switch_was_on) mark(frame);
    g_switch_was_on = on;
    check_gates(frame);
    draw_gates();
}

void restart() {
    std::lock_guard<std::mutex> lock(g_mutex);
    reset_lap();
}

bool hud(char *line, size_t size) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_recording) {
        std::snprintf(line, size, "RECORDING  %zu gates   AUX%d / M mark   U undo   N save lap race   E finish here",
                      g_gates.size(), g_settings.mark_aux);
        return true;
    }
    if (g_gates.empty()) return false;
    const int n = int(g_gates.size());
    if (!g_running) {
        if (!g_finished)
            std::snprintf(line, size, "%s   fly through the green gate to start", g_name.c_str());
        else if (g_point_to_point)
            std::snprintf(line, size, "FINISHED  %s   BEST %s   back to the green gate to go again",
                          clock(g_total).c_str(), clock(g_best_lap).c_str());
        else
            std::snprintf(line, size, "FINISHED  %d LAPS  %s   BEST LAP %s   green gate to race again", g_laps,
                          clock(g_total).c_str(), clock(g_best_lap).c_str());
        return true;
    }
    const bool at_finish = g_point_to_point ? g_next == n - 1 : g_next == 0;
    char gate[32];
    if (at_finish) std::snprintf(gate, sizeof gate, "FINISH");
    else std::snprintf(gate, sizeof gate, "GATE %d/%d", g_next + 1, n);
    if (g_point_to_point) {
        std::snprintf(line, size, "%s   %s   BEST %s", clock(g_sim_time - g_lap_start).c_str(), gate,
                      clock(g_best_lap).c_str());
    } else {
        char lap[16];
        if (g_settings.laps > 0) std::snprintf(lap, sizeof lap, "%d/%d", g_laps + 1, g_settings.laps);
        else std::snprintf(lap, sizeof lap, "%d", g_laps + 1);
        std::snprintf(line, size, "LAP %s  %s   %s   LAST %s   BEST %s", lap, clock(g_sim_time - g_lap_start).c_str(),
                      gate, clock(g_last_lap).c_str(), clock(g_best_lap).c_str());
    }
    return true;
}

int laps() {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    return g_settings.laps;
}

void set_laps(int laps) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    g_settings.laps = std::clamp(laps, 0, 99);
    WritePrivateProfileStringA("Race", "Laps", std::to_string(g_settings.laps).c_str(), ini_path().c_str());
}

}  // namespace race
