cmake_minimum_required(VERSION 3.16)
project(VoxForgeSynth101 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------- 核心库（纯标准库，全平台） ----------
set(VF_CORE_SOURCES
    third_party/klatt-tts/src/klatt.cc
    third_party/klatt-tts/src/phonemes.cc
    third_party/klatt-tts/src/config.cc
    third_party/klatt-tts/src/synth_internal.cc
    third_party/klatt-tts/src/synth_stream.cc
    third_party/klatt-tts/src/g2p.cc
    third_party/klatt-tts/src/g2p_dict.cc
    third_party/klatt-tts/src/g2p_numbers.cc
    third_party/klatt-tts/src/g2p_rules.cc
    third_party/klatt-tts/src/ipa_tokens.cc
)

add_library(vfcore STATIC ${VF_CORE_SOURCES})
target_include_directories(vfcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klatt-tts/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klatt-tts/include/tts
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klatt-tts/include/g2p
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tsf
)

# ---------- 依赖查找 ----------
find_package(LuaJIT QUIET)
if(NOT LuaJIT_FOUND)
  find_path(LUAJIT_INCLUDE_DIR lua.hpp HINTS /usr/include/luajit-2.1 $ENV{PREFIX}/include/luajit-2.1)
  find_library(LUAJIT_LIBRARY NAMES luajit-5.1 luajit HINTS /usr/lib/aarch64-linux-gnu $ENV{PREFIX}/lib)
endif()

# ---------- 命令行工具（全平台，纯可执行） ----------
set(VF_TOOLS
    bench kt_bend kt_dsl kt_f0 kt_full kt_lint kt_multi kt_phrase
    kt_sf2 kt_sing kt_siren kt_whisper compare rh_test rt_test

    stretch_test ease_test humanity_test kt_vowel_test
)

foreach(tool ${VF_TOOLS})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/${tool}.cpp)
    add_executable(${tool} tools/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE vfcore)
  endif()
endforeach()

# Lua 驱动（需要 LuaJIT）
if(LUAJIT_INCLUDE_DIR AND LUAJIT_LIBRARY)
  add_executable(kt_lua tools/kt_lua.cpp)
  target_include_directories(kt_lua PRIVATE ${LUAJIT_INCLUDE_DIR})
  target_link_libraries(kt_lua PRIVATE vfcore ${LUAJIT_LIBRARY})
else()
  message(STATUS "LuaJIT 未找到，跳过 kt_lua")
endif()

# ---------- 双进程 IPC（仅类 Unix，Windows 走命名管道后续适配） ----------
if(UNIX)
  add_executable(voxforge-backend src/backend/main.cpp)
  target_link_libraries(voxforge-backend PRIVATE vfcore)

  add_executable(voxforge-frontend src/frontend/main.cpp)
  target_link_libraries(voxforge-frontend PRIVATE vfcore)
else()
  message(STATUS "Windows: IPC 层待适配（计划用命名管道 + 共享内存映射文件）")
endif()

install(TARGETS bench kt_dsl kt_lint kt_multi kt_phrase kt_sing kt_sf2
        RUNTIME DESTINATION bin)
