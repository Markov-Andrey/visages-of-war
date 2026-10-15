# Vendored dependencies: each target owns its include path and license packaging.
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include"
)
install(FILES nlohmann_json/LICENSE.MIT DESTINATION licenses RENAME nlohmann-json-MIT.txt)

# Header-only ECS, pinned source release; no upstream build scripts or downloads run.
add_library(entt INTERFACE)
add_library(EnTT::EnTT ALIAS entt)
target_include_directories(entt SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/entt/include")
install(FILES entt/LICENSE.MIT DESTINATION licenses RENAME EnTT-MIT.txt)
