#pragma once
#include <string>
namespace Ship { struct GameRegistry { static void SetActive(const std::string&){} };
struct GameScope { explicit GameScope(const std::string&){} }; }
