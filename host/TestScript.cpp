#include "TestScript.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace FleetCombo {
namespace {

fs::path sFolder;
bool sEnabled = false;
uint64_t sFrame = 0;
std::deque<std::string> sLines;
std::string sActiveGame;

// Input being held this frame.
uint32_t sButtons = 0;
int8_t sStickX = 0;
int8_t sStickY = 0;
bool sStickHeld = false;
int sHoldFrames = 0;   // frames the current buttons/stick stay held
int sWaitFrames = 0;   // frames before the next line runs

// Ocarina notes and pressed buttons expand into hold/release pairs.
struct Step {
    uint32_t buttons;
    int frames;
};
std::deque<Step> sSteps;

// A game command waiting for the active game to take it.
std::string sGameCommand;
int sGameCommandWait = 0;

void Report(const std::string& line) {
    std::ofstream out(sFolder / "results.txt", std::ios::app);
    out << "[" << sFrame << "] " << line << "\n";
}

uint32_t ParseButtons(const std::string& text, bool* ok) {
    static const std::map<std::string, uint32_t> kButtons = {
        { "A", 0x8000 },  { "B", 0x4000 },  { "Z", 0x2000 },  { "START", 0x1000 }, { "DU", 0x0800 },
        { "DD", 0x0400 }, { "DL", 0x0200 }, { "DR", 0x0100 }, { "L", 0x0020 },     { "R", 0x0010 },
        { "CU", 0x0008 }, { "CD", 0x0004 }, { "CL", 0x0002 }, { "CR", 0x0001 },    { "NONE", 0 },
    };
    uint32_t mask = 0;
    *ok = true;
    std::stringstream parts(text);
    std::string part;
    while (std::getline(parts, part, '+')) {
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return std::toupper(c); });
        auto button = kButtons.find(part);
        if (button == kButtons.end()) {
            *ok = false;
            return 0;
        }
        mask |= button->second;
    }
    return mask;
}

void ReadNewCommands() {
    const fs::path file = sFolder / "commands.txt";
    std::error_code error;
    if (!fs::exists(file, error)) {
        return;
    }
    std::ifstream in(file);
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            sLines.push_back(line);
            ++count;
        }
    }
    in.close();
    fs::remove(file, error);
    Report("read " + std::to_string(count) + " commands (" + sActiveGame + " active)");
}

// Runs lines until one takes frames; false when the script is empty.
void RunLines() {
    while (sWaitFrames == 0 && sHoldFrames == 0 && sSteps.empty() && sGameCommand.empty() && !sLines.empty()) {
        const std::string line = sLines.front();
        sLines.pop_front();
        std::stringstream words(line);
        std::string verb;
        words >> verb;
        if (verb == "wait") {
            words >> sWaitFrames;
        } else if (verb == "hold" || verb == "press") {
            std::string buttons;
            int frames = 3;
            words >> buttons;
            if (verb == "hold") {
                words >> frames;
            }
            bool ok = false;
            const uint32_t mask = ParseButtons(buttons, &ok);
            if (!ok) {
                Report("bad buttons: " + line);
                continue;
            }
            sSteps.push_back({ mask, std::max(frames, 1) });
            sSteps.push_back({ 0, 3 });
        } else if (verb == "notes") {
            std::string note;
            while (words >> note) {
                bool ok = false;
                const uint32_t mask = ParseButtons(note, &ok);
                if (!ok) {
                    Report("bad note: " + note);
                    continue;
                }
                sSteps.push_back({ mask, 6 });
                sSteps.push_back({ 0, 6 });
            }
        } else if (verb == "stick") {
            int x = 0;
            int y = 0;
            int frames = 1;
            words >> x >> y >> frames;
            sStickX = static_cast<int8_t>(std::clamp(x, -80, 80));
            sStickY = static_cast<int8_t>(std::clamp(y, -80, 80));
            sStickHeld = true;
            sHoldFrames = std::max(frames, 1);
        } else if (verb == "log") {
            Report(line.size() > 4 ? line.substr(4) : "");
        } else {
            sGameCommand = line;
            sGameCommandWait = 600;
        }
    }
}

bool HostTestPad(uint32_t* buttons, int8_t* stickX, int8_t* stickY) {
    if (!sEnabled || (sButtons == 0 && !sStickHeld)) {
        return false;
    }
    if (buttons != nullptr) {
        *buttons |= sButtons;
    }
    if (sStickHeld) {
        if (stickX != nullptr) {
            *stickX = sStickX;
        }
        if (stickY != nullptr) {
            *stickY = sStickY;
        }
    }
    return true;
}

const char* HostTestCommand() {
    static std::string sTaken;
    if (!sEnabled || sGameCommand.empty()) {
        return nullptr;
    }
    sTaken = sGameCommand;
    sGameCommand.clear();
    Report("> " + sTaken);
    return sTaken.c_str();
}

void HostTestReport(const char* line) {
    if (sEnabled && line != nullptr) {
        Report(line);
    }
}

} // namespace

void TestInit(const fs::path& root) {
    sFolder = root / "fleet-test";
    std::error_code error;
    sEnabled = fs::is_directory(sFolder, error);
    if (sEnabled) {
        Report("test mode on");
        SPDLOG_WARN("[FleetCombo] Test mode: reading {}", (sFolder / "commands.txt").string());
    }
}

void TestTick(const char* activeGame) {
    if (!sEnabled) {
        return;
    }
    ++sFrame;
    sActiveGame = activeGame != nullptr ? activeGame : "";
    if (sFrame % 15 == 0) {
        ReadNewCommands();
    }
    // Advance what is held or waited on.
    if (sHoldFrames > 0 && --sHoldFrames == 0) {
        sStickHeld = false;
        sStickX = sStickY = 0;
    }
    if (sWaitFrames > 0) {
        --sWaitFrames;
    }
    if (!sGameCommand.empty() && --sGameCommandWait <= 0) {
        Report("game did not take: " + sGameCommand);
        sGameCommand.clear();
    }
    if (!sSteps.empty()) {
        Step& step = sSteps.front();
        sButtons = step.buttons;
        if (--step.frames <= 0) {
            sSteps.pop_front();
        }
        return;
    }
    sButtons = 0;
    RunLines();
    if (!sSteps.empty()) {
        sButtons = sSteps.front().buttons;
    }
}

void* TestFunction(const char* name) {
    const std::string key = name != nullptr ? name : "";
    if (key == "TestPad") {
        return reinterpret_cast<void*>(HostTestPad);
    }
    if (key == "TestCommand") {
        return reinterpret_cast<void*>(HostTestCommand);
    }
    if (key == "TestReport") {
        return reinterpret_cast<void*>(HostTestReport);
    }
    return nullptr;
}

} // namespace FleetCombo
