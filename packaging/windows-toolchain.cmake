# Native MinGW toolchain selected by build.bat. Paths come from the current
# project/tool configuration, never from the original checkout's location.
if(NOT DEFINED ENV{MINGW_DIR} OR "$ENV{MINGW_DIR}" STREQUAL "")
    message(FATAL_ERROR "MINGW_DIR must select an installed MinGW toolchain")
endif()
file(TO_CMAKE_PATH "$ENV{MINGW_DIR}" maxchat_mingw_root)
set(CMAKE_C_COMPILER "${maxchat_mingw_root}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${maxchat_mingw_root}/bin/g++.exe")
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -dumpfullversion
    OUTPUT_VARIABLE maxchat_mingw_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE maxchat_mingw_triple OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(maxchat_mingw_search
    "-B\"${maxchat_mingw_root}/lib/gcc/${maxchat_mingw_triple}/${maxchat_mingw_version}/\" -B\"${maxchat_mingw_root}/${maxchat_mingw_triple}/lib/\" -B\"${maxchat_mingw_root}/${maxchat_mingw_triple}/bin/\"")
set(CMAKE_C_FLAGS_INIT "${maxchat_mingw_search} $ENV{CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${maxchat_mingw_search} $ENV{CXXFLAGS}")
