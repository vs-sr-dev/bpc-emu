cmake_minimum_required(VERSION 3.16)
project(bpcemu VERSION 0.1.0 LANGUAGES C)

set(BPC_VERSION_SUFFIX "-dev" CACHE STRING "Appended to the version number")
set(BPC_VERSION "${PROJECT_VERSION}${BPC_VERSION_SUFFIX}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# the machine
add_library(bpccore STATIC
  src/v30mz.c src/aswan.c src/aswan_video.c src/aswan_audio.c src/pcv2.c src/png.c)
target_compile_definitions(bpccore PUBLIC BPC_VERSION="${BPC_VERSION}")

# headless: scripted buttons, pictures, the state at every frame
add_executable(bpc-cli src/headless.c)
target_link_libraries(bpc-cli PRIVATE bpccore)

# the window (SDL2)
option(BPC_SDL "Build the SDL2 frontend" ON)
if(BPC_SDL)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
  endif()
  if(SDL2_FOUND)
    add_executable(bpc-emu src/main.c)
    target_link_libraries(bpc-emu PRIVATE bpccore)
    target_compile_definitions(bpc-emu PRIVATE SDL_MAIN_HANDLED)
    target_include_directories(bpc-emu PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(bpc-emu PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(bpc-emu PRIVATE SDL2)
    if(WIN32)
      set_target_properties(bpc-emu PROPERTIES WIN32_EXECUTABLE TRUE)
      if(EXISTS "${SDL2_PREFIX}/bin/SDL2.dll")
        # so that the build folder runs with a double click
        add_custom_command(TARGET bpc-emu POST_BUILD COMMAND ${CMAKE_COMMAND} -E
          copy_if_different "${SDL2_PREFIX}/bin/SDL2.dll" "$<TARGET_FILE_DIR:bpc-emu>")
      endif()
    endif()
  else()
    message(STATUS "SDL2 not found: headless-only build")
  endif()
endif()
