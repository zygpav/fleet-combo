# Built inside the Shipwright (Unbound) tree so it links the very libultraship the games load:
# CI copies this folder to <Shipwright>/fleet-host and adds it with add_subdirectory().
add_executable(FleetCombo WIN32 main.cpp Fiber.cpp Session.cpp)
target_compile_features(FleetCombo PRIVATE cxx_std_20)
target_link_libraries(FleetCombo PRIVATE libultraship)
if(NOT WIN32)
    target_link_libraries(FleetCombo PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(FleetCombo PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
