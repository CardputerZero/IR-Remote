cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroIRRemote VERSION 0.1.1 LANGUAGES C CXX ASM)

option(IR_REMOTE_USE_SDL "Use LVGL's SDL backend for desktop testing" ON)
option(IR_REMOTE_CONVERT_ASSETS "Convert image assets during CMake configure" ON)
option(IR_REMOTE_BUILD_TESTS "Build IR Remote unit tests" OFF)
set(IR_REMOTE_BIN_NAME "M5CardputerZero-IR-Remote" CACHE STRING "Output executable name")

if(APPLE AND NOT IR_REMOTE_USE_SDL)
    message(FATAL_ERROR "macOS builds only support IR_REMOTE_USE_SDL=ON.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(IR_REMOTE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(IR_REMOTE_SRC_DIR "${IR_REMOTE_ROOT}/src")
set(IR_REMOTE_DEPS_DIR "${IR_REMOTE_ROOT}/dependencies")
set(IR_REMOTE_LVGL_DIR "${IR_REMOTE_DEPS_DIR}/lvgl")
set(IR_REMOTE_SPDLOG_DIR "${IR_REMOTE_DEPS_DIR}/spdlog")
set(IR_REMOTE_SMOOTH_UI_DIR "${IR_REMOTE_DEPS_DIR}/smooth_ui_toolkit")
set(IR_REMOTE_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND IR_REMOTE_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

if(NOT DEFINED Python3_EXECUTABLE)
    foreach(candidate IN ITEMS "${IR_REMOTE_ROOT}/.venv/bin/python" "${IR_REMOTE_ROOT}/../.venv/bin/python")
        if(EXISTS "${candidate}")
            execute_process(
                COMMAND "${candidate}" -m pip --version
                RESULT_VARIABLE IR_REMOTE_PYTHON_PIP_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(IR_REMOTE_PYTHON_PIP_RESULT EQUAL 0)
                set(Python3_EXECUTABLE "${candidate}")
                break()
            endif()
        endif()
    endforeach()
endif()

foreach(required_dir IN LISTS IR_REMOTE_LVGL_DIR IR_REMOTE_SPDLOG_DIR IR_REMOTE_SMOOTH_UI_DIR)
    if(NOT EXISTS "${required_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first.")
    endif()
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)

if(IR_REMOTE_CONVERT_ASSETS)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${IR_REMOTE_SRC_DIR}/assets/convert_images.py"
        WORKING_DIRECTORY "${IR_REMOTE_ROOT}"
        RESULT_VARIABLE IR_REMOTE_ASSET_RESULT
    )
    if(NOT IR_REMOTE_ASSET_RESULT EQUAL 0)
        message(FATAL_ERROR "Image asset conversion failed")
    endif()
endif()

set(LV_BUILD_CONF_PATH "${IR_REMOTE_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

add_subdirectory("${IR_REMOTE_LVGL_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${IR_REMOTE_SPDLOG_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${IR_REMOTE_SMOOTH_UI_DIR}" EXCLUDE_FROM_ALL)

function(ir_remote_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

set(IR_REMOTE_LVGL_POSIX_PATH "./")
if(NOT IR_REMOTE_USE_SDL)
    set(IR_REMOTE_LVGL_POSIX_PATH "/usr/share/IR-Remote/")
endif()

target_compile_definitions(lvgl PUBLIC
    IR_REMOTE_LVGL_USE_SDL=$<IF:$<BOOL:${IR_REMOTE_USE_SDL}>,1,0>
    IR_REMOTE_LVGL_FS_POSIX_PATH="${IR_REMOTE_LVGL_POSIX_PATH}"
)

if(IR_REMOTE_USE_SDL)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        ir_remote_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(IR_REMOTE_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            ir_remote_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(IR_REMOTE_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(IR_REMOTE_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

file(GLOB IR_REMOTE_CPP CONFIGURE_DEPENDS
    "${IR_REMOTE_SRC_DIR}/main.cpp"
    "${IR_REMOTE_SRC_DIR}/core/*.cpp"
    "${IR_REMOTE_SRC_DIR}/hal/*.cpp"
    "${IR_REMOTE_SRC_DIR}/input/*.cpp"
    "${IR_REMOTE_SRC_DIR}/models/*.cpp"
    "${IR_REMOTE_SRC_DIR}/view_models/*.cpp"
    "${IR_REMOTE_SRC_DIR}/views/*.cpp"
)
file(GLOB_RECURSE IR_REMOTE_ASSET_C CONFIGURE_DEPENDS
    "${IR_REMOTE_SRC_DIR}/assets/res_c/*.c"
)

add_executable(${IR_REMOTE_BIN_NAME}
    ${IR_REMOTE_CPP}
    ${IR_REMOTE_ASSET_C}
)

target_include_directories(${IR_REMOTE_BIN_NAME} PRIVATE
    "${IR_REMOTE_SRC_DIR}"
    "${IR_REMOTE_SRC_DIR}/assets"
)

target_compile_definitions(${IR_REMOTE_BIN_NAME} PRIVATE
    $<$<BOOL:${IR_REMOTE_USE_SDL}>:IR_REMOTE_USE_DUMMY_IR=1>
    $<$<NOT:$<BOOL:${IR_REMOTE_USE_SDL}>>:IR_REMOTE_USE_LIRC_IR=1>
)

target_link_libraries(${IR_REMOTE_BIN_NAME} PRIVATE
    lvgl::lvgl
    smooth_ui_toolkit
    spdlog::spdlog
    Threads::Threads
    ${IR_REMOTE_SDL_LIBS}
    ${IR_REMOTE_SYSTEM_LIBS}
)

set_target_properties(${IR_REMOTE_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${IR_REMOTE_ROOT}/dist"
)

if(IR_REMOTE_BUILD_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "IR_REMOTE_BUILD_TESTS requires Linux LIRC headers")
    endif()

    enable_testing()
    add_executable(ir_remote_lirc_device_discovery_test
        "${IR_REMOTE_SRC_DIR}/models/lirc_device_discovery.cpp"
        "${IR_REMOTE_ROOT}/tests/lirc_device_discovery_test.cpp"
    )
    target_include_directories(ir_remote_lirc_device_discovery_test PRIVATE "${IR_REMOTE_SRC_DIR}")
    target_compile_definitions(ir_remote_lirc_device_discovery_test PRIVATE IR_REMOTE_USE_LIRC_IR=1)
    add_test(NAME ir_remote_lirc_device_discovery COMMAND ir_remote_lirc_device_discovery_test)
endif()
