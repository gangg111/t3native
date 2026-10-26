cmake_minimum_required(VERSION 3.22)
project(t3native C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# -fwrapv: signed overflow wraps, as int64 arithmetic does in Godot's GDScript VM.
add_compile_options(-fwrapv -Wall -Wextra)
# The folder holding t3native, Tekken3Remake and _tools: the host's and the tests' default paths.
if(ANDROID)
  # Nothing on the phone reads them; keep the PC's folder out of the library.
  add_compile_definitions(T3_WORKSPACE=".")
else()
  get_filename_component(T3_WORKSPACE_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
  set(T3_WORKSPACE ${T3_WORKSPACE_DEFAULT} CACHE PATH "Folder holding Tekken3Remake and _tools")
  add_compile_definitions(T3_WORKSPACE="${T3_WORKSPACE}")
endif()
if(ANDROID)
  # Android 15+ devices with 16 KB pages refuse ELF segments aligned to 4 KB.
  add_link_options(-Wl,-z,max-page-size=16384)
endif()

# Everything but the platform entry points: the core, content, gfx, views and the app.
file(GLOB_RECURSE T3_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER T3_CORE_SOURCES EXCLUDE REGEX "/src/platform/(host|android)/")
# The 3D shaders (shaders/**, the remake's .gdshader files) as one generated source.
file(GLOB_RECURSE T3_SHADERS CONFIGURE_DEPENDS shaders/*.gdshader shaders/*.gdshaderinc)
set(T3_SHADER_CPP ${CMAKE_CURRENT_BINARY_DIR}/shader_sources.cpp)
add_custom_command(
  OUTPUT ${T3_SHADER_CPP}
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders -DOUT=${T3_SHADER_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake
  DEPENDS ${T3_SHADERS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake
  COMMENT "Embedding the 3D shaders")
list(APPEND T3_CORE_SOURCES ${T3_SHADER_CPP})
# The UI font (Godot's default theme face, gfx::Font::fallback) built into the program.
set(T3_FONT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/OpenSans-SemiBold.ttf)
set(T3_FONT_CPP ${CMAKE_CURRENT_BINARY_DIR}/font_open_sans.cpp)
add_custom_command(
  OUTPUT ${T3_FONT_CPP}
  COMMAND ${CMAKE_COMMAND} -DFILE=${T3_FONT} -DNAME=open_sans_semibold -DOUT=${T3_FONT_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  DEPENDS ${T3_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  COMMENT "Embedding the UI font")
list(APPEND T3_CORE_SOURCES ${T3_FONT_CPP})
# The native KEY CONFIGURATION's pad picture built into the program: the private photo
# (art/fit_pad_photo.py, never committed) when present, else the drawing (art/draw_dualsense.py).
set(T3_PAD ${CMAKE_CURRENT_SOURCE_DIR}/art/private/pad_dualsense_photo.png)
if(NOT EXISTS ${T3_PAD})
  set(T3_PAD ${CMAKE_CURRENT_SOURCE_DIR}/art/pad_dualsense.png)
endif()
set(T3_PAD_CPP ${CMAKE_CURRENT_BINARY_DIR}/pad_dualsense.cpp)
add_custom_command(
  OUTPUT ${T3_PAD_CPP}
  COMMAND ${CMAKE_COMMAND} -DFILE=${T3_PAD} -DNAME=pad_dualsense -DOUT=${T3_PAD_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  DEPENDS ${T3_PAD} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  COMMENT "Embedding the pad picture")
list(APPEND T3_CORE_SOURCES ${T3_PAD_CPP})
add_library(t3core STATIC ${T3_CORE_SOURCES})
target_include_directories(t3core PUBLIC src third_party)
if(ANDROID)
  target_link_libraries(t3core PUBLIC log GLESv3)
else()
  # The host's GLES3 declarations (pointers loaded at run time) come from the NDK's Khronos headers.
  target_include_directories(t3core PUBLIC third_party/khronos)
endif()
if(WIN32)
  # The local network play's sockets (net/udp.cpp).
  target_link_libraries(t3core PUBLIC ws2_32 iphlpapi)
endif()

file(GLOB_RECURSE T3_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(t3tests ${T3_TEST_SOURCES})
target_include_directories(t3tests PRIVATE tests)
target_link_libraries(t3tests PRIVATE t3core)
if(ANDROID)
  # Runs on the device through adb (static C++ runtime: one self-contained file to push).
  target_link_options(t3tests PRIVATE -static-libstdc++)
endif()

if(ANDROID)
  # The game as a NativeActivity library (android_native_app_glue from the NDK).
  set(GLUE ${ANDROID_NDK}/sources/android/native_app_glue)
  add_library(t3game SHARED src/platform/android/android_main.cpp ${GLUE}/android_native_app_glue.c)
  target_include_directories(t3game PRIVATE ${GLUE})
  set_source_files_properties(${GLUE}/android_native_app_glue.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
  target_link_libraries(t3game PRIVATE t3core android EGL GLESv3 aaudio log)
  # The glue's entry point must stay exported although nothing in the library calls it.
  target_link_options(t3game PRIVATE -u ANativeActivity_onCreate)
endif()

if(WIN32)
  file(GLOB T3_HOST_SOURCES CONFIGURE_DEPENDS src/platform/host/*.cpp)
  add_executable(t3host ${T3_HOST_SOURCES})
  target_link_libraries(t3host PRIVATE t3core opengl32 gdi32 user32 winmm)
endif()
