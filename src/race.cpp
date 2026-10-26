#include "race.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "drone.h"
#include "log.h"
#include "natives.h"
#include "shv.h"

namespace race {
namespace {

namespace fs = std::filesystem;
constexpr float kRadToDeg = 57.2957795f;
constexpr double kGhostStep = 1.0 / 30;  // seconds between ghost samples

struct Gate {
    float x, y, z;
    float heading;  // degrees, GTA's: 0 north, 90 west; the way through
    float radius;   // metres
};

// A race as stored and shared: the gates and how to read them.
struct Track {
    std::vector<Gate> gates;
    bool point_to_point = false;  // the last gate is the finish, not the first
    std::string name, author;
};

struct Settings {
    int mark_aux = 3;       // [Race] MarkSwitch: AUX number of the gate mark switch
    float radius = 2.0f;    // [Race] GateRadius, metres
    float marker_yaw = 0;   // [Race] MarkerYaw: degrees added to the ring's rotation
    int laps = 3;           // [Race] Laps: laps in a circuit race, 0 endless
    bool ghost = true;      // [Race] Ghost: race your best lap's ghost
    std::string author;     // [Race] Author: your name on the tracks you record
};

// One pose of the ghost: seconds into the lap, position, rotation (x, y, z, w).
struct Sample {
    float t, p[3], q[4];
};

std::mutex g_mutex;  // the HUD and the tab read from the render thread
Track g_track;
std::string g_file;     // the loaded race's file name, empty: none
std::string g_id;       // the loaded track's id (track_id)
bool g_recording = false;
int g_next = 0;         // the gate to fly through next
bool g_running = false; // a race is under way (the start gate was passed)
bool g_finished = false;  // the last race is over; g_total is its time
double g_race_start = 0, g_lap_start = 0, g_last_lap = 0, g_total = 0;
double g_best_lap = 0;    // the track's personal best lap (point to point: run), all time
double g_best_race = 0;   // the personal best race of g_settings.laps laps
double g_new_pb_until = 0;  // sim time until which the HUD says NEW PB
int g_laps = 0;         // laps done in this race; point to point: runs done
double g_sim_time = 0;
float g_prev[3];
bool g_have_prev = false;
bool g_switch_was_on = false;
Settings g_settings;
bool g_settings_read = false;
std::map<std::string, double> g_bests;  // bests.txt: "lap <id>" and "race <id> <laps>" -> seconds
bool g_bests_read = false;
std::vector<Sample> g_rec, g_ghost;  // this lap so far; the best lap's ghost
double g_rec_last = -1;
int g_ghost_entity = 0;
size_t g_ghost_cursor = 0;

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
    char buf[64];
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
    g_settings.ghost = value("Ghost", "1") != 0;
    GetPrivateProfileStringA("Race", "Author", "", buf, sizeof buf, ini.c_str());
    g_settings.author = buf;
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
    g_rec.clear();
}

// The same course gives the same id, whatever its name or file: FNV-1a over
// the gates (to 0.1 m) and the mode.
std::string track_id(const Track &t) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](const char *s) {
        for (; *s; ++s) h = (h ^ uint8_t(*s)) * 1099511628211ull;
    };
    char buf[96];
    for (const Gate &g : t.gates) {
        std::snprintf(buf, sizeof buf, "%.1f %.1f %.1f %.0f %.1f;", g.x, g.y, g.z, g.heading, g.radius);
        add(buf);
    }
    add(t.point_to_point ? "point_to_point" : "circuit");
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

// The race file, also what is copied to the clipboard to share it: "name",
// "author", "mode point_to_point" for a race that ends at its last gate, and
// one "gate x y z heading radius" line per gate.
std::string format(const Track &t) {
    std::string s = "# GTAVE.FPV race v1: gate x y z heading radius (metres, degrees). The first gate is the start;\n"
                    "# the finish is the first gate again, or the last gate with mode point_to_point.\n";
    char buf[160];
    if (!t.name.empty()) s += "name " + t.name + "\n";
    if (!t.author.empty()) s += "author " + t.author + "\n";
    if (t.point_to_point) s += "mode point_to_point\n";
    for (const Gate &g : t.gates) {
        std::snprintf(buf, sizeof buf, "gate %.2f %.2f %.2f %.1f %.2f\n", g.x, g.y, g.z, g.heading, g.radius);
        s += buf;
    }
    return s;
}

// The rest of a "key value" line, trimmed.
std::string rest(const char *line, size_t key) {
    std::string v(line + key);
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
    while (!v.empty() && v.front() == ' ') v.erase(0, 1);
    return v.substr(0, 60);
}

bool parse(const std::string &text, Track &out) {
    Track t;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        Gate g{};
        char mode[32];
        if (std::sscanf(line.c_str(), "gate %f %f %f %f %f", &g.x, &g.y, &g.z, &g.heading, &g.radius) == 5) {
            g.radius = std::clamp(g.radius, 0.5f, 20.0f);
            t.gates.push_back(g);
        } else if (std::sscanf(line.c_str(), "mode %31s", mode) == 1) {
            t.point_to_point = std::strcmp(mode, "point_to_point") == 0;
        } else if (line.rfind("name ", 0) == 0) {
            t.name = rest(line.c_str(), 5);
        } else if (line.rfind("author ", 0) == 0) {
            t.author = rest(line.c_str(), 7);
        }
    }
    if (t.gates.size() < 2) return false;
    out = std::move(t);
    return true;
}

bool write_file(const fs::path &file, const std::string &text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    FILE *f = _wfopen(file.c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

bool read_file(const fs::path &file, std::string &text) {
    FILE *f = _wfopen(file.c_str(), L"rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    text.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    return true;
}

// Personal bests: fpv-data\races\bests.txt, "lap <id> <seconds>" (a point to
// point race's run counts as its lap) and "race <id> <laps> <seconds>".
void read_bests() {
    if (g_bests_read) return;
    g_bests_read = true;
    std::string text;
    if (!read_file(races_dir() / "bests.txt", text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        char id[32];
        int laps;
        double s;
        if (std::sscanf(line.c_str(), "lap %31s %lf", id, &s) == 2) g_bests["lap " + std::string(id)] = s;
        if (std::sscanf(line.c_str(), "race %31s %d %lf", id, &laps, &s) == 3)
            g_bests["race " + std::string(id) + " " + std::to_string(laps)] = s;
    }
}

void write_bests() {
    std::string text = "# GTAVE.FPV personal bests: lap <track id> <seconds>, race <track id> <laps> <seconds>\n";
    char buf[96];
    for (const auto &[key, s] : g_bests) {
        std::snprintf(buf, sizeof buf, "%s %.3f\n", key.c_str(), s);
        text += buf;
    }
    write_file(races_dir() / "bests.txt", text);
}

double best(const std::string &key) {
    read_bests();
    auto it = g_bests.find(key);
    return it == g_bests.end() ? 0 : it->second;
}

std::string race_key() { return "race " + g_id + " " + std::to_string(g_settings.laps); }

// Ghosts: fpv-data\races\ghosts\<track id>.ghost, "GFPVGHO1", a sample count,
// then the samples as floats.
fs::path ghost_file(const std::string &id) { return races_dir() / "ghosts" / (id + ".ghost"); }

void save_ghost() {
    std::string data("GFPVGHO1", 8);
    const uint32_t n = uint32_t(g_ghost.size());
    data.append(reinterpret_cast<const char *>(&n), sizeof n);
    data.append(reinterpret_cast<const char *>(g_ghost.data()), g_ghost.size() * sizeof(Sample));
    if (!write_file(ghost_file(g_id), data)) fpvlog::write("race: cannot write the ghost");
}

void load_ghost() {
    g_ghost.clear();
    std::string data;
    if (!read_file(ghost_file(g_id), data) || data.size() < 12 || data.compare(0, 8, "GFPVGHO1") != 0) return;
    uint32_t n;
    std::memcpy(&n, data.data() + 8, sizeof n);
    if (data.size() < 12 + size_t(n) * sizeof(Sample)) return;
    g_ghost.resize(n);
    std::memcpy(g_ghost.data(), data.data() + 12, n * sizeof(Sample));
}

void use(Track t, const std::string &file) {
    g_track = std::move(t);
    g_file = file;
    g_id = track_id(g_track);
    g_laps = 0;
    g_finished = false;
    g_last_lap = 0;
    g_best_lap = best("lap " + g_id);
    g_best_race = best(race_key());
    load_ghost();
    reset_lap();
}

bool load(const fs::path &file) {
    std::string text;
    Track t;
    if (!read_file(file, text) || !parse(text, t)) return false;
    if (t.name.empty()) t.name = file.stem().string();
    use(std::move(t), file.filename().string());
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

void unload() {
    g_track = Track{};
    g_file.clear();
    g_id.clear();
    g_ghost.clear();
    reset_lap();
}

// L: the next saved race, then none.
void cycle() {
    std::vector<fs::path> files = saved_races();
    size_t i = 0;
    if (!g_file.empty())
        while (i < files.size() && files[i].filename().string() != g_file) ++i;
    i = g_file.empty() ? 0 : i + 1;
    unload();
    for (; i < files.size(); ++i)
        if (load(files[i])) break;
    fpvlog::write("race: %s", g_file.empty() ? "off" : g_file.c_str());
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
    g_track.gates.push_back(g);
    fpvlog::write("race: gate %d at %.1f %.1f %.1f heading %.0f", int(g_track.gates.size()), g.x, g.y, g.z,
                  g.heading);
}

void toggle_recording() {
    if (!g_recording) {
        g_recording = true;
        unload();
        g_finished = false;
        fpvlog::write("race: recording");
        return;
    }
    g_recording = false;
    if (g_track.gates.size() < 2) {
        unload();
        fpvlog::write("race: recording dropped, it needs two gates");
        return;
    }
    char file[64], name[64];
    std::time_t t = std::time(nullptr);
    std::strftime(file, sizeof file, "race_%Y-%m-%d_%H%M%S.race", std::localtime(&t));
    std::strftime(name, sizeof name, "Track %Y-%m-%d %H:%M", std::localtime(&t));
    Track track = g_track;
    track.name = name;
    track.author = g_settings.author;
    const fs::path path = races_dir() / file;
    if (write_file(path, format(track)))
        fpvlog::write("race: saved %d gates to %s", int(track.gates.size()), path.string().c_str());
    else
        fpvlog::write("race: cannot write %s", path.string().c_str());
    use(std::move(track), file);
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
    if (!g_track.point_to_point && (g_best_race <= 0 || total < g_best_race)) {
        g_best_race = total;
        g_bests[race_key()] = total;
        write_bests();
    }
    fpvlog::write("race: finished in %.2f s", total);
}

// A lap (point to point: a run) is done: a personal best keeps its ghost.
void lap_done(double t) {
    g_last_lap = t - g_lap_start;
    ++g_laps;
    if (g_best_lap <= 0 || g_last_lap < g_best_lap) {
        g_best_lap = g_last_lap;
        g_new_pb_until = t + 4;
        g_bests["lap " + g_id] = g_last_lap;
        write_bests();
        g_ghost = g_rec;
        save_ghost();
        fpvlog::write("race: new personal best %.2f s", g_last_lap);
    }
    g_rec.clear();
    g_rec_last = -1;
}

// Through g_track.gates[g_next].
void pass_gate(double t) {
    const int n = int(g_track.gates.size());
    if (g_track.point_to_point && g_running && g_next == n - 1) {
        lap_done(t);
        finish(g_last_lap);
        return;
    }
    if (g_next == 0) {  // the start, and on a circuit the finish of each lap
        if (g_running) {
            lap_done(t);
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
        g_rec.clear();
        g_rec_last = -1;
        g_ghost_cursor = 0;
    }
    g_next = (g_next + 1) % n;
}

void check_gates(const fpv_frame &f) {
    if (g_track.gates.size() < 2 || g_recording) return;
    const float *b = f.position;
    if (g_have_prev) {
        float dx = b[0] - g_prev[0], dy = b[1] - g_prev[1], dz = b[2] - g_prev[2];
        // a respawn or a teleport is not a path
        if (dx * dx + dy * dy + dz * dz < 900 && crossed(g_track.gates[g_next], g_prev, b)) pass_gate(f.sim_time);
    }
    std::copy(b, b + 3, g_prev);
    g_have_prev = true;
}

void record(const fpv_frame &f) {
    if (!g_running) return;
    const double t = f.sim_time - g_lap_start;
    if (g_rec_last >= 0 && t - g_rec_last < kGhostStep) return;
    g_rec_last = t;
    Sample s{};
    s.t = float(t);
    std::copy(f.position, f.position + 3, s.p);
    std::copy(f.rotation, f.rotation + 4, s.q);
    g_rec.push_back(s);
}

void delete_ghost() {
    if (g_ghost_entity && shv::call<bool>(nat::DOES_ENTITY_EXIST, g_ghost_entity)) {
        shv::call(nat::SET_ENTITY_AS_MISSION_ENTITY, g_ghost_entity, true, true);
        shv::call(nat::DELETE_ENTITY, &g_ghost_entity);
    }
    g_ghost_entity = 0;
}

// q * r for {x, y, z, w} quaternions.
void quat_mul(const float q[4], const float r[4], float out[4]) {
    out[0] = q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1];
    out[1] = q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0];
    out[2] = q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3];
    out[3] = q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2];
}

// The best lap's ghost flies along while a lap runs: the same model, see-through.
void play_ghost() {
    if (!g_settings.ghost || !g_running || g_ghost.size() < 2) {
        delete_ghost();
        return;
    }
    float yaw[4];
    const uint32_t model = drone::prop_model(yaw);
    if (!model) return;
    if (g_ghost_entity && shv::call<uint32_t>(nat::GET_ENTITY_MODEL, g_ghost_entity) != model) delete_ghost();
    if (!g_ghost_entity) {
        shv::call(nat::REQUEST_MODEL, model);
        if (!shv::call<bool>(nat::HAS_MODEL_LOADED, model)) return;  // next tick
        const Sample &s0 = g_ghost.front();
        g_ghost_entity = shv::call<int>(nat::CREATE_OBJECT_NO_OFFSET, model, s0.p[0], s0.p[1], s0.p[2], false, false,
                                        false, 0);
        shv::call(nat::SET_MODEL_AS_NO_LONGER_NEEDED, model);
        if (!g_ghost_entity) return;
        shv::call(nat::SET_ENTITY_COLLISION, g_ghost_entity, false, false);
        shv::call(nat::FREEZE_ENTITY_POSITION, g_ghost_entity, true);
        shv::call(nat::SET_ENTITY_ALPHA, g_ghost_entity, 110, false);
    }
    const float t = float(g_sim_time - g_lap_start);
    if (t > g_ghost.back().t) {  // the ghost is home
        shv::call(nat::SET_ENTITY_VISIBLE, g_ghost_entity, false, false);
        return;
    }
    if (g_ghost_cursor >= g_ghost.size() || g_ghost[g_ghost_cursor].t > t) g_ghost_cursor = 0;
    while (g_ghost_cursor + 2 < g_ghost.size() && g_ghost[g_ghost_cursor + 1].t <= t) ++g_ghost_cursor;
    const Sample &a = g_ghost[g_ghost_cursor], &b = g_ghost[g_ghost_cursor + 1];
    const float k = b.t > a.t ? std::clamp((t - a.t) / (b.t - a.t), 0.0f, 1.0f) : 0.0f;
    float p[3], q[4], sign = 0, len = 0;
    for (int i = 0; i < 3; ++i) p[i] = a.p[i] + (b.p[i] - a.p[i]) * k;
    for (int i = 0; i < 4; ++i) sign += a.q[i] * b.q[i];
    for (int i = 0; i < 4; ++i) q[i] = a.q[i] + ((sign < 0 ? -b.q[i] : b.q[i]) - a.q[i]) * k, len += q[i] * q[i];
    len = std::sqrt(len);
    for (float &v : q) v /= len > 0 ? len : 1;
    float out[4];
    quat_mul(q, yaw, out);
    shv::call(nat::SET_ENTITY_VISIBLE, g_ghost_entity, true, false);
    shv::call(nat::SET_ENTITY_COORDS_NO_OFFSET, g_ghost_entity, p[0], p[1], p[2], false, false, false);
    shv::call(nat::SET_ENTITY_QUATERNION, g_ghost_entity, out[0], out[1], out[2], out[3]);
}

// Rings: the next gate bright (green at the start, white at a point to point
// finish), the one after it dim.
void draw_gates() {
    const int n = int(g_track.gates.size());
    for (int i = 0; i < n; ++i) {
        const Gate &g = g_track.gates[i];
        int r = 255, gr = 255, b = 255, a = 40;
        if (g_recording) {
            r = 255, gr = 80, b = 80, a = 140;
        } else if (i == g_next) {
            a = 170;
            if (i == 0) r = 60, gr = 255, b = 90;
            else if (g_track.point_to_point && i == n - 1) a = 220;
            else r = 255, gr = 200, b = 0;
        } else if (i == (g_next + 1) % n && !(g_track.point_to_point && i == 0)) {
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

// A file name from the track's name and id.
std::string file_name(const Track &t, const std::string &id) {
    std::string s;
    for (char c : t.name) s += std::isalnum(uint8_t(c)) ? c : '_';
    return (s.empty() ? "track" : s.substr(0, 40)) + "_" + id.substr(0, 8) + ".race";
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
        if (g_recording && key_pressed('E') && !g_track.gates.empty()) {  // the finish: a point to point race
            mark(frame);
            g_track.point_to_point = true;
            toggle_recording();
        }
        if (g_recording && key_pressed('U') && !g_track.gates.empty()) g_track.gates.pop_back();
    }
    const bool on = frame.channels[3 + g_settings.mark_aux] > 0.5f;  // AETR, then AUX1..
    if (g_recording && on && !g_switch_was_on) mark(frame);
    g_switch_was_on = on;
    check_gates(frame);
    record(frame);
    play_ghost();
    draw_gates();
}

void restart() {
    std::lock_guard<std::mutex> lock(g_mutex);
    reset_lap();
}

void stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    delete_ghost();
    reset_lap();
}

bool hud(char *line, size_t size) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_recording) {
        std::snprintf(line, size, "RECORDING  %zu gates   AUX%d / M mark   U undo   N save lap race   E finish here",
                      g_track.gates.size(), g_settings.mark_aux);
        return true;
    }
    if (g_track.gates.empty()) return false;
    const int n = int(g_track.gates.size());
    const char *pb = g_sim_time < g_new_pb_until ? "NEW PB" : "PB";
    if (!g_running) {
        if (!g_finished)
            std::snprintf(line, size, "%s   PB %s   fly through the green gate to start", g_track.name.c_str(),
                          clock(g_best_lap).c_str());
        else if (g_track.point_to_point)
            std::snprintf(line, size, "FINISHED  %s   %s %s   back to the green gate to go again",
                          clock(g_total).c_str(), pb, clock(g_best_lap).c_str());
        else
            std::snprintf(line, size, "FINISHED  %d LAPS  %s (PB %s)   %s LAP %s   green gate to race again", g_laps,
                          clock(g_total).c_str(), clock(g_best_race).c_str(), pb, clock(g_best_lap).c_str());
        return true;
    }
    const bool at_finish = g_track.point_to_point ? g_next == n - 1 : g_next == 0;
    char gate[32];
    if (at_finish) std::snprintf(gate, sizeof gate, "FINISH");
    else std::snprintf(gate, sizeof gate, "GATE %d/%d", g_next + 1, n);
    if (g_track.point_to_point) {
        std::snprintf(line, size, "%s   %s   %s %s", clock(g_sim_time - g_lap_start).c_str(), gate, pb,
                      clock(g_best_lap).c_str());
    } else {
        char lap[16];
        if (g_settings.laps > 0) std::snprintf(lap, sizeof lap, "%d/%d", g_laps + 1, g_settings.laps);
        else std::snprintf(lap, sizeof lap, "%d", g_laps + 1);
        std::snprintf(line, size, "LAP %s  %s   %s   LAST %s   %s %s", lap, clock(g_sim_time - g_lap_start).c_str(),
                      gate, clock(g_last_lap).c_str(), pb, clock(g_best_lap).c_str());
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
    if (!g_id.empty()) g_best_race = best(race_key());
}

bool ghost() {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    return g_settings.ghost;
}

void set_ghost(bool on) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    g_settings.ghost = on;
    WritePrivateProfileStringA("Race", "Ghost", on ? "1" : "0", ini_path().c_str());
}

TrackInfo track() {
    std::lock_guard<std::mutex> lock(g_mutex);
    TrackInfo info;
    if (g_file.empty() || g_recording) return info;
    info.loaded = true;
    std::snprintf(info.name, sizeof info.name, "%s", g_track.name.c_str());
    std::snprintf(info.author, sizeof info.author, "%s", g_track.author.c_str());
    std::snprintf(info.id, sizeof info.id, "%.8s", g_id.c_str());
    info.point_to_point = g_track.point_to_point;
    info.gates = int(g_track.gates.size());
    info.best_lap = g_best_lap;
    info.has_ghost = g_ghost.size() >= 2;
    return info;
}

void rename_track(const char *name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.empty()) return;
    g_track.name = rest(name, 0);
    write_file(races_dir() / g_file, format(g_track));
}

std::string author() {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    return g_settings.author;
}

void set_author(const char *name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_settings();
    g_settings.author = rest(name, 0);
    WritePrivateProfileStringA("Race", "Author", g_settings.author.c_str(), ini_path().c_str());
}

bool copy_track() {
    std::string text;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file.empty()) return false;
        text = format(g_track);
    }
    for (size_t i = 0; (i = text.find('\n', i)) != std::string::npos; i += 2) text.insert(i, "\r");
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    bool ok = false;
    if (mem) {
        std::memcpy(GlobalLock(mem), text.c_str(), text.size() + 1);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_TEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

std::string paste_track() {
    std::string text;
    if (!OpenClipboard(nullptr)) return "cannot open the clipboard";
    if (HANDLE h = GetClipboardData(CF_TEXT)) {
        if (const char *p = static_cast<const char *>(GlobalLock(h))) text.assign(p, strnlen(p, 1 << 20));
        GlobalUnlock(h);
    }
    CloseClipboard();
    Track t;
    if (!parse(text, t)) return "no track on the clipboard (it needs two gate lines or more)";
    if (t.name.empty()) t.name = "Shared track";
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_recording) return "finish recording first";
    const std::string id = track_id(t);
    for (const fs::path &file : saved_races()) {  // already saved: load that one
        std::string existing;
        Track e;
        if (read_file(file, existing) && parse(existing, e) && track_id(e) == id) {
            if (e.name.empty()) e.name = file.stem().string();
            use(std::move(e), file.filename().string());
            return "you already have this track: loaded " + g_track.name;
        }
    }
    const std::string file = file_name(t, id);
    if (!write_file(races_dir() / file, format(t))) return "cannot write " + file;
    use(std::move(t), file);
    fpvlog::write("race: pasted %s as %s", g_track.name.c_str(), file.c_str());
    return "saved and loaded " + g_track.name;
}

}  // namespace race
