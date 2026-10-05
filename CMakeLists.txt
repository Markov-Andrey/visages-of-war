cmake_minimum_required(VERSION 3.25)
project(RTS VERSION 0.0.1 LANGUAGES CXX RC)
set(RTS_APP_NAME "Visages of War")

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "RTS targets Windows x64 with the MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use the x64 preset.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(rts_options INTERFACE)
target_compile_options(rts_options INTERFACE /W4 /EHsc /permissive- /utf-8 /Zc:__cplusplus)
target_compile_definitions(rts_options INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

add_subdirectory(libs)

add_library(rts_core
    src/core/Map.cpp
    src/core/Definitions.cpp
    src/core/WeaponDefinitions.cpp
    src/core/Environment.cpp
    src/core/WorldAssets.cpp
    src/core/ScenarioIO.cpp
    src/render/TerrainPaint.cpp
    src/core/FogOfWar.cpp
    src/render/FogMask.cpp
    src/render/NightLighting.cpp
    src/render/MinimapRaster.cpp
    src/render/DirectionalSprite.cpp
    src/core/Navigation.cpp
    src/core/Formation.cpp
    src/core/Simulation.cpp
    src/core/Construction.cpp
    src/core/Gathering.cpp
    src/core/Combat.cpp
    src/core/CombatTargeting.cpp
    src/core/CombatDamage.cpp
    src/core/Remains.cpp
    src/core/HeroProgression.cpp
    src/core/Projectiles.cpp
    src/core/UnitMovement.cpp
    src/core/UnitFacing.cpp
    src/core/UnitOrders.cpp
    src/core/TacticalOrders.cpp
    src/core/CommandUi.cpp
    src/core/IconAsset.cpp
    src/core/Selection.cpp
    src/core/SelectionGroups.cpp
    src/core/ControlGroups.cpp
    src/platform/Paths.cpp
)
target_include_directories(rts_core PUBLIC include)
target_include_directories(rts_core PRIVATE src)
target_link_libraries(rts_core PUBLIC rts_options PRIVATE nlohmann_json::nlohmann_json shell32 ole32)

# The game consumes maps; only Forge and tests link the authoring model.
add_library(rts_editor_core src/core/WorldEditor.cpp)
target_link_libraries(rts_editor_core PUBLIC rts_core)
add_library(rts_render
    src/render/Renderer.cpp
    src/render/RenderResources.cpp
    src/render/RenderPrimitives.cpp
    src/render/RenderSnapshot.cpp
    src/render/TerrainRenderer.cpp
    src/render/WorldRenderer.cpp
    src/render/UnitSpriteRenderer.cpp
    src/render/RemainsRenderer.cpp
    src/render/ResourceRenderer.cpp
    src/render/DirectionalSpriteAssets.cpp
    src/render/UnitDirectionPreview.cpp
    src/render/BuildingSpriteRenderer.cpp
    src/render/RallyPointRenderer.cpp
    src/render/LightingRenderer.cpp
    src/render/SpriteLighting.cpp
    src/render/ProjectileRenderer.cpp
    src/render/EntityRenderer.cpp
    src/render/HealthBarRenderer.cpp
    src/render/SceneRenderer.cpp
    src/render/HudRenderer.cpp
    src/render/SelectionRenderer.cpp
    src/render/SelectionRingRenderer.cpp
    src/render/PortraitRenderer.cpp
    src/render/MinimapRenderer.cpp
    src/render/MenuRenderer.cpp
    src/render/ColorSelectRenderer.cpp
    src/render/LibraryRenderer.cpp
    src/platform/GameCursor.cpp
)
target_link_libraries(rts_render PUBLIC rts_core d2d1 dwrite windowscodecs ole32 shell32 user32 gdi32)
target_include_directories(rts_render PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(RTS_ICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/project.ico")
add_custom_command(OUTPUT "${RTS_ICON_FILE}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make-icon.ps1"
        -InputPng "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/project-icon.png" -OutputIco "${RTS_ICON_FILE}"
    DEPENDS assets/ui/project-icon.png scripts/make-icon.ps1
    COMMENT "Preparing project icon" VERBATIM)
add_custom_target(rts_icon DEPENDS "${RTS_ICON_FILE}")
configure_file(include/rts/Version.hpp.in generated/rts/Version.hpp @ONLY)
function(add_visages_app target RTS_APP_NAME RTS_APP_ID)
    configure_file(src/platform/RTS.rc.in "generated/${target}.rc" @ONLY)
    configure_file(src/platform/RTS.manifest.in "generated/${target}.manifest" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc" PROPERTIES OBJECT_DEPENDS "${RTS_ICON_FILE}")
    add_executable(${target} WIN32 ${ARGN} "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc")
    add_dependencies(${target} rts_icon)
    target_include_directories(${target} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(${target} PRIVATE rts_render)
    target_link_options(${target} PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.manifest")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${RTS_APP_NAME}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()
add_visages_app(RTS "Visages of War" "Visages.Game"
    src/platform/Main.cpp
    src/game/GameApplication.cpp
    src/game/GameCamera.cpp
    src/game/CameraController.cpp
    src/game/GameCommands.cpp
    src/game/GameUnitCommands.cpp
    src/game/GameLibrary.cpp
    src/game/GameInput.cpp
    src/game/GameSmoke.cpp
    src/game/RangedSmoke.cpp
    src/game/UnitCommandSmoke.cpp
)
add_visages_app(VisagesForge "Visages Forge" "Visages.Forge"
    src/platform/ForgeMain.cpp
    src/forge/ForgeApplication.cpp
    src/forge/ForgeDocument.cpp
    src/forge/ForgeInput.cpp
    src/forge/ForgePlaytest.cpp
    src/forge/ForgeSmoke.cpp
    src/render/ForgeRenderer.cpp
)
target_link_libraries(VisagesForge PRIVATE rts_editor_core comdlg32)

# A target (not POST_BUILD) also synchronizes assets when only data changes.
add_custom_target(rts_assets ALL
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "-DDESTINATION=$<TARGET_FILE_DIR:RTS>/assets" "-DOUTPUT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/out"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync-assets.cmake"
    COMMENT "Copying RTS assets next to executable"
    VERBATIM
)
add_dependencies(RTS rts_assets)
add_dependencies(VisagesForge rts_assets)

include(CTest)
if(BUILD_TESTING)
    # Opt-in diagnostic; timings are not correctness gates and do not run in CTest.
    add_executable(rts_benchmarks EXCLUDE_FROM_ALL tests/Performance.cpp)
    target_link_libraries(rts_benchmarks PRIVATE rts_core)
    add_executable(rts_tests
        tests/CoreTests.cpp
        tests/NavigationTests.cpp
        tests/EconomyTests.cpp
        tests/CombatTests.cpp
        tests/ProjectileTests.cpp
        tests/DataTests.cpp
        tests/SpriteTests.cpp
        tests/VisionTests.cpp
        tests/LightingTests.cpp
        tests/SelectionTests.cpp
        tests/CameraTests.cpp
        src/game/CameraController.cpp
        tests/FormationTests.cpp
        tests/MovementTests.cpp
        tests/EditorTests.cpp
    )
    target_link_libraries(rts_tests PRIVATE rts_editor_core rts_render nlohmann_json::nlohmann_json)
    target_include_directories(rts_tests PRIVATE src)
    add_test(NAME rts_core COMMAND rts_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_test(NAME rts_asset_sync COMMAND ${CMAKE_COMMAND}
        "-DSYNC=${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync-assets.cmake"
        "-DWORK=${CMAKE_BINARY_DIR}/asset-sync-test" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/AssetSync.cmake")
    # Deliberately use a working directory unrelated to the executable/assets.
    add_test(NAME rts_assets_from_other_cwd COMMAND RTS --verify-assets)
    set_tests_properties(rts_assets_from_other_cwd PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rts_render_snapshot COMMAND RTS --snapshot "${CMAKE_BINARY_DIR}/preview.png")
    add_test(NAME rts_grid_snapshot COMMAND RTS --snapshot-grid "${CMAKE_BINARY_DIR}/grid-preview.png")
    add_test(NAME rts_menu_snapshot COMMAND RTS --snapshot-menu "${CMAKE_BINARY_DIR}/menu-preview.png")
    add_test(NAME rts_title_snapshot COMMAND RTS --snapshot-title "${CMAKE_BINARY_DIR}/title-preview.png")
    add_test(NAME rts_native_gameplay_ui COMMAND RTS --smoke-test)
    # Includes software snapshots with cold sprite/lighting caches; this is not a performance gate.
    set_tests_properties(rts_native_gameplay_ui PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME forge_assets_from_other_cwd COMMAND VisagesForge --verify-assets)
    set_tests_properties(forge_assets_from_other_cwd PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME forge_render_snapshot COMMAND VisagesForge --snapshot "${CMAKE_BINARY_DIR}/forge-preview.png")
    add_test(NAME forge_native_editor_ui COMMAND VisagesForge --smoke-test)
    set_tests_properties(forge_native_editor_ui PROPERTIES TIMEOUT 40 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rts_relocated_unicode_paths COMMAND ${CMAKE_COMMAND}
        "-DFORGE=$<TARGET_FILE:VisagesForge>" "-DAPP=$<TARGET_FILE:RTS>" "-DWORK=${CMAKE_BINARY_DIR}/relocation-test"
        "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Relocation.cmake")
endif()

install(TARGETS RTS VisagesForge RUNTIME DESTINATION .)
install(CODE "
    execute_process(COMMAND \"${CMAKE_COMMAND}\" \"-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/assets\"
        \"-DDESTINATION=\${CMAKE_INSTALL_PREFIX}/assets\" \"-DOUTPUT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/out\"
        -P \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync-assets.cmake\" COMMAND_ERROR_IS_FATAL ANY)
")
install(FILES README_TECHNICAL.md DESTINATION . RENAME README.md)
install(DIRECTORY docs DESTINATION .)
