cmake_minimum_required(VERSION 3.16)
project(saftybox_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Protocol library (Arduino-portable core) ─────────────────────────────────
# These files go verbatim into the Arduino sketch folder.
add_library(saftybox_lib STATIC
    lib/packet.cpp
    lib/mac.cpp
    lib/nonce_buf.cpp
    lib/platform_node.cpp
    lib/home_node.cpp
)
target_include_directories(saftybox_lib PUBLIC lib)

# ── Platform simulator ────────────────────────────────────────────────────────
add_executable(platform_sim
    platform_sim/main.cpp
    platform_sim/sim_platform_hal.cpp
)
target_link_libraries(platform_sim PRIVATE saftybox_lib)
find_package(Threads REQUIRED)
target_link_libraries(platform_sim PRIVATE Threads::Threads)

# ── Key generator ────────────────────────────────────────────────────────────
add_executable(keygen tools/keygen.cpp)

# ── Home app (terminal stub; replace main.cpp with ImGui app later) ───────────
add_executable(home_app
    home_app/main.cpp
    home_app/sim_home_hal.cpp
)
target_link_libraries(home_app PRIVATE saftybox_lib)
target_link_libraries(home_app PRIVATE Threads::Threads)
