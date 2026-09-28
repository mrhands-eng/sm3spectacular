cmake_minimum_required(VERSION 3.16)
project(sm3spectacular VERSION 1.2.0 LANGUAGES CXX)

# Spider-Man 3 is a 32-bit exe — this DLL must be Win32 / x86.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "Build as Win32/x86 only (Game.exe is 32-bit). "
    "In Visual Studio: switch Configuration to x86 / win32-debug, not x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Native Spectacular d3d9 (fallback / UseReShade=0). Hybrid deploy uses Remastered as d3d9.dll.
add_library(d3d9 SHARED
  src/dllmain.cpp
  src/config.cpp
  src/hooks.cpp
  src/log.cpp
  src/fps.cpp
  src/menu.cpp
  src/osd.cpp
  src/postfx.cpp
  src/depth.cpp
  src/citylife.cpp
  src/world.cpp
  src/exeid.cpp
  src/steam_compat.cpp
  src/igct_fps.cpp
)

set_target_properties(d3d9 PROPERTIES
  OUTPUT_NAME "d3d9"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(d3d9 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(d3d9 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
  foreach(_flag_var CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
    string(REGEX REPLACE "/RTC[1scu]+" "" ${_flag_var} "${${_flag_var}}")
  endforeach()
  target_compile_options(d3d9 PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(d3d9 PRIVATE user32 gdi32 ole32 dxguid)
  target_link_options(d3d9 PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy.def")
else()
  target_compile_options(d3d9 PRIVATE -Wall -Wextra)
  target_link_libraries(d3d9 PRIVATE user32 gdi32 ole32 dxguid)
  target_link_options(d3d9 PRIVATE
    "-Wl,--kill-at"
    "-Wl,--enable-stdcall-fixup"
    "-static-libgcc"
    "-static-libstdc++"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy.def"
  )
endif()

set(_post_cmds
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/sm3spectacular.ini"
    "$<TARGET_FILE_DIR:d3d9>/sm3spectacular.ini"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:d3d9>/textures"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:d3d9>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/textures"
    "$<TARGET_FILE_DIR:d3d9>/textures"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:d3d9>/shaders"
)
if(EXISTS "C:/msys64/mingw32/bin/libwinpthread-1.dll")
  list(APPEND _post_cmds
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "C:/msys64/mingw32/bin/libwinpthread-1.dll"
      "$<TARGET_FILE_DIR:d3d9>/libwinpthread-1.dll"
  )
endif()
add_custom_command(TARGET d3d9 POST_BUILD ${_post_cmds})

# Hybrid companion — Game.exe loads local binkw32.dll
add_library(binkw32 SHARED
  src/binkw32_main.cpp
  src/companion.cpp
  src/config.cpp
  src/log.cpp
  src/fps.cpp
  src/menu.cpp
  src/vsync.cpp
  src/citylife.cpp
  src/world.cpp
  src/exeid.cpp
  src/steam_compat.cpp
  src/igct_fps.cpp
)
set_target_properties(binkw32 PROPERTIES
  OUTPUT_NAME "binkw32"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_include_directories(binkw32 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(binkw32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(binkw32 PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(binkw32 PRIVATE user32)
  target_link_options(binkw32 PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/binkw32.def")
else()
  target_compile_options(binkw32 PRIVATE -Wall -Wextra)
  target_link_libraries(binkw32 PRIVATE user32 dxguid)
  target_link_options(binkw32 PRIVATE
    "-Wl,--enable-stdcall-fixup"
    "-static-libgcc"
    "-static-libstdc++"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/binkw32.def"
  )
endif()
if(EXISTS "C:/msys64/mingw32/bin/libwinpthread-1.dll")
  add_custom_command(TARGET binkw32 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "C:/msys64/mingw32/bin/libwinpthread-1.dll"
      "$<TARGET_FILE_DIR:binkw32>/libwinpthread-1.dll"
  )
endif()
