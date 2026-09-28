# Libraries

`libs/` contains vendored dependencies shared by the engine, applications and tests.
Project-owned code stays in `src/` and `include/rts/`. Each dependency has its own
directory; `libs/CMakeLists.txt` exposes named targets and packages their licenses.
Consumers link the target instead of adding library paths themselves.

## nlohmann/json 3.12.0

- Author: Niels Lohmann and contributors.
- Project: https://github.com/nlohmann/json
- Vendored single header: `nlohmann_json/include/nlohmann/json.hpp`.
- Source: https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
- License: MIT, full notice in `nlohmann_json/LICENSE.MIT`.
- License source: https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT
- SHA-256 of the unmodified header: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.

Used for loading and validating the UTF-8 game data catalog and reading/writing maps.
Link with `nlohmann_json::nlohmann_json`; source files include `<nlohmann/json.hpp>`.
No network access or extra DLL is
needed to build or run. CMake installs the complete MIT notice to
`licenses/nlohmann-json-MIT.txt` in the distributable package. Preserve this notice when distributing.
