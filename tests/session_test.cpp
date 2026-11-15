#include "/home/claude/fc-repo/host/Session.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
using json=nlohmann::json;
template<class F> F S(const char* n){ return reinterpret_cast<F>(FleetCombo::SessionFunction(n)); }
int main(){
  std::filesystem::remove_all("/tmp/sesstest/root");
  FleetCombo::SessionInit("/tmp/sesstest/root");
  // fake a generated seed by writing a session file and selecting it
  std::filesystem::create_directories("/tmp/sesstest/root/sessions");
  json seed={{"masterSeed",7},{"oot",{{"placements",{{"KF Kokiri Sword Chest","Kokiri Sword"},{"LW Boomerang","Boomerang"},{"X","skijer.byrna item"}}}}},
    {"mm",{{"placements",{{"Clock","Deku Mask"}}}}},
    {"fleetCombo",{{"pairs",{{"oot",{{"Boomerang","oot.boomerang"},{"skijer.byrna item","skijer.byrna"}}},{"mm",{{"Deku Mask","mm.mask.deku"}}}}},
                   {"byKey",{{"mm",{{"oot.boomerang","MM Boomerang"},{"skijer.byrna","Cane of Byrna"}}},{"oot",{{"mm.mask.deku","Deku Mask (MM)"}}}}}}}};
  { std::ofstream o("/tmp/sesstest/root/sessions/slot0.json"); o<<json{{"slot",0},{"seed",seed}}.dump(); }
  S<bool(*)(int)>("SelectFile")(0);
  assert(S<const char*(*)()>("GetSession")()!=nullptr);
  auto rep=S<bool(*)(const char*,const char*)>("ReportSharedTiers");
  auto get=S<const char*(*)(const char*)>("GetSharedTiers");
  auto obt=S<bool(*)(const char*,const char*)>("ReportObtained");
  auto del=S<const char*(*)(const char*)>("GetDeliveries");
  // both start at 3 hearts
  rep("oot",R"({"hearts":12,"bows":0})"); printf("oot get: %s\n",get("oot"));
  rep("mm",R"({"hearts":12,"bows":0})"); printf("mm get: %s\n",get("mm"));
  // oot finds a heart container (+4) and a bow
  rep("oot",R"({"hearts":16,"bows":1})"); printf("oot get: %s\n",get("oot"));
  printf("mm get: %s\n",get("mm"));
  rep("mm",R"({"hearts":16,"bows":1})"); printf("mm after applying: %s\n",get("mm"));
  // mm finds a piece of heart (+1)
  rep("mm",R"({"hearts":17,"bows":1})"); printf("oot get: %s\n",get("oot"));
  rep("oot",R"({"hearts":17,"bows":1})"); printf("oot after applying: %s  mm: %s\n",get("oot"),get("mm"));
  obt("oot","LW Boomerang"); obt("oot","X"); obt("oot","KF Kokiri Sword Chest"); obt("oot","LW Boomerang");
  printf("mm deliveries: %s\n",del("mm"));
  obt("mm","Clock"); printf("oot deliveries: %s\n",del("oot"));
}
