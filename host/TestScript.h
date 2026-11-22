// Fleet Combo test mode: a script of inputs and game commands read from <install>/fleet-test/commands.txt.
//
// Only active while that folder exists. Each line of commands.txt is one step:
//   wait N                 do nothing for N frames
//   hold A+B N             hold buttons for N frames (A B Z L R START DU DD DL DR CU CD CL CR)
//   press A                hold for 3 frames, then release for 3
//   stick X Y N            hold the stick at X,Y (-80..80) for N frames
//   notes CD CL CU         ocarina notes (A CD CR CL CU), each held 6 frames with 6 frames between
//   log TEXT               write TEXT to results.txt
//   anything else          a game command, run by the active game (see FleetTest.cpp in each game)
// The host appends what happens to fleet-test/results.txt. commands.txt is deleted once read.
#pragma once

#include <cstdint>
#include <filesystem>

namespace FleetCombo {

void TestInit(const std::filesystem::path& root);
// Once per frame of the active game.
void TestTick(const char* activeGame);
// Host services ("TestPad", "TestCommand", "TestReport"); nullptr for other names.
void* TestFunction(const char* name);

} // namespace FleetCombo
