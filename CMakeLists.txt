cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Filum - Federated Learning for MCU-class Devices
# Dual-target build: host (GCC/Clang/MSVC) and STM32 (arm-none-eabi-gcc)
# ---------------------------------------------------------------------------
#
# Build host - Linux/macOS:
#   cmake -B build && cmake --build build
#
# Build host - Windows (VS Code / Visual Studio):
#   cmake -B build -DFILUM_TARGET=host
#   cmake --build build
#
# Build STM32 (requires arm-none-eabi-gcc in PATH):
#   cmake -B build_stm32 -DFILUM_TARGET=STM32F411
#   cmake --build build_stm32
#
# Run tests:
#   cd build && ctest --output-on-failure
# ---------------------------------------------------------------------------

# IMPORTANT: use set() not option() for FILUM_TARGET.
# option() converts string defaults to bool OFF, which breaks VS Code CMake
# Tools (it passes -DFILUM_TARGET=OFF when no kit value is set).
if(NOT DEFINED FILUM_TARGET OR FILUM_TARGET STREQUAL "" OR FILUM_TARGET STREQUAL "OFF")
    set(FILUM_TARGET "host")
endif()

option(FILUM_ENABLE_TESTS "Build unit tests (host only)" ON)
option(FILUM_ENABLE_ASAN  "Enable AddressSanitizer - GCC/Clang only" OFF)
set(FILUM_LORA_SF "7" CACHE STRING "LoRa spreading factor (7-12)")

# ---------------------------------------------------------------------------
# Toolchain - must be set BEFORE project() for cross-compilation
# ---------------------------------------------------------------------------
if(FILUM_TARGET STREQUAL "STM32F411" OR FILUM_TARGET STREQUAL "STM32F103")
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_PROCESSOR arm)
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_AR arm-none-eabi-ar)
    set(CMAKE_RANLIB arm-none-eabi-ranlib)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    project(filum C ASM)
    set(IS_HOST FALSE)
    message(STATUS "[Filum] Target: ${FILUM_TARGET} (arm-none-eabi-gcc)")
else()
    # host - GCC, Clang, or MSVC
    if(NOT FILUM_TARGET STREQUAL "host")
        message(WARNING "[Filum] Unknown FILUM_TARGET='${FILUM_TARGET}', falling back to host")
        set(FILUM_TARGET "host")
    endif()
    project(filum C)
    set(IS_HOST TRUE)
    message(STATUS "[Filum] Target: host (${CMAKE_C_COMPILER_ID})")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Configurable memory presets (passed to fl_config.h via compile definitions)
# ---------------------------------------------------------------------------
set(FL_CONFIG_PRESET "" CACHE STRING
    "Memory preset: TINY (STM32F103), SMALL (STM32F411), MEDIUM (default), LARGE (Herald)")

if(FL_CONFIG_PRESET STREQUAL "TINY")
    add_compile_definitions(FL_CONFIG_PRESET_TINY)
    message(STATUS "[Filum] Config preset: TINY (STM32F103, 20KB)")
elseif(FL_CONFIG_PRESET STREQUAL "SMALL")
    add_compile_definitions(FL_CONFIG_PRESET_SMALL)
    message(STATUS "[Filum] Config preset: SMALL (STM32F411, 50KB)")
elseif(FL_CONFIG_PRESET STREQUAL "LARGE")
    add_compile_definitions(FL_CONFIG_PRESET_LARGE)
    message(STATUS "[Filum] Config preset: LARGE (Herald Linux)")
elseif(NOT FL_CONFIG_PRESET STREQUAL "")
    message(WARNING "[Filum] Unknown FL_CONFIG_PRESET='${FL_CONFIG_PRESET}', using defaults")
endif()

# Individual overrides
if(DEFINED FL_MODEL_MAX_PARAMS)
    add_compile_definitions(FL_MODEL_MAX_PARAMS=${FL_MODEL_MAX_PARAMS})
endif()
if(DEFINED FL_SPARSE_MAX_ENTRIES)
    add_compile_definitions(FL_SPARSE_MAX_ENTRIES=${FL_SPARSE_MAX_ENTRIES})
endif()

add_compile_definitions(
    FILUM_LORA_SF=${FILUM_LORA_SF}
    FILUM_VERSION_MAJOR=0
    FILUM_VERSION_MINOR=2
    FILUM_VERSION_PATCH=0
)

# ---------------------------------------------------------------------------
# Host compiler flags
# ---------------------------------------------------------------------------
if(IS_HOST)
    if(MSVC)
        # MSVC (Visual Studio / VS Code on Windows)
        add_compile_options(/W3 /WX- /O2)
        # Suppress POSIX deprecation warnings for open/read/write used in herald
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    else()
        # GCC / Clang
        add_compile_options(-Wall -Wextra -Wpedantic -O2)
        if(FILUM_ENABLE_ASAN)
            add_compile_options(-fsanitize=address -fsanitize=undefined)
            add_link_options(-fsanitize=address -fsanitize=undefined)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# STM32 compiler flags
# ---------------------------------------------------------------------------
if(NOT IS_HOST)
    if(FILUM_TARGET STREQUAL "STM32F411")
        set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")
        add_compile_definitions(STM32F411xx USE_HAL_DRIVER)
    elseif(FILUM_TARGET STREQUAL "STM32F103")
        set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
        add_compile_definitions(STM32F103xB USE_HAL_DRIVER)
    endif()

    separate_arguments(MCU_FLAGS_LIST UNIX_COMMAND "${MCU_FLAGS}")
    add_compile_options(
        ${MCU_FLAGS_LIST}
        -Os -ffunction-sections -fdata-sections -fno-exceptions -Wall -Wextra
    )
    add_link_options(
        ${MCU_FLAGS_LIST}
        -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs
    )
endif()

# ---------------------------------------------------------------------------
# Platform compatibility shim
# On Windows/MSVC, POSIX functions (open/read/write/close) used by the Herald
# are available via io.h / fcntl.h but need _O_RDWR etc. renamed.
# The common/ and shard/ code is pure C99 with no POSIX - builds clean on MSVC.
# Herald serial transport is POSIX-only; exclude on MSVC.
# ---------------------------------------------------------------------------
if(MSVC)
    set(HERALD_TRANSPORT_SOURCES "")   # no serial transport on Windows
else()
    set(HERALD_TRANSPORT_SOURCES "herald/transport/lora_serial.c")
endif()

# ---------------------------------------------------------------------------
# fl_common - wire protocol, CRC, quantization, sparse encoding
# Builds on all targets (host + STM32 + MSVC).
# ---------------------------------------------------------------------------
add_library(fl_common STATIC
    common/src/fl_frame.c
    common/src/fl_quant.c
    common/src/fl_sparse.c
    common/src/fl_crc.c
    common/src/fl_dp.c
    common/src/fl_crypto.c
    common/src/fl_error.c
    common/src/fl_version.c
)
target_include_directories(fl_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/shard/include>
    $<INSTALL_INTERFACE:include/filum>
)

# Allow individual config overrides via CMake -D flags
foreach(PARAM FL_MODEL_MAX_PARAMS FL_MODEL_MAX_LAYERS FL_SPARSE_MAX_ENTRIES
              FL_AGG_MEDIAN_MAX_SHARDS FL_POOL_MAX_SHARDS FL_POOL_MAX_FRAGS)
    if(DEFINED ${PARAM})
        target_compile_definitions(fl_common PUBLIC ${PARAM}=${${PARAM}})
        message(STATUS "[Filum] Override: ${PARAM}=${${PARAM}}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# fl_shard - state machine, training, model
# ---------------------------------------------------------------------------
add_library(fl_shard STATIC
    shard/src/fl_shard.c
    shard/src/fl_train.c
    shard/src/fl_model.c
)
target_include_directories(fl_shard PUBLIC shard/include shard/hal)
target_link_libraries(fl_shard PUBLIC fl_common)
if(NOT MSVC)
    target_link_libraries(fl_shard PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Host-only targets (Herald + tests)
# ---------------------------------------------------------------------------
if(IS_HOST)

    add_library(fl_herald STATIC
        herald/src/fl_herald.c
        herald/src/fl_round.c
        herald/src/fl_aggregator.c
        herald/src/fl_fragment_pool.c
        ${HERALD_TRANSPORT_SOURCES}
    )
    target_include_directories(fl_herald PUBLIC herald/include)
    target_link_libraries(fl_herald PUBLIC fl_common)

    if(NOT MSVC)
        # Herald daemon (uses POSIX serial - Linux/macOS only)
        add_executable(filum_herald examples/herald_linux/main.c)
        target_link_libraries(filum_herald fl_herald fl_shard m)
        # End-to-end demo: 1 Herald + 1 Shard + synthetic data, no hardware
        add_executable(filum_demo examples/demo/demo.c)
        target_link_libraries(filum_demo fl_shard fl_herald m)

        # Software shard simulator (uses pthreads - Linux/macOS only)
        add_executable(filum_shard_sim
            examples/herald_linux/shard_sim.c
            shard/hal/host/hal_host.c
        )
        target_include_directories(filum_shard_sim PRIVATE
            shard/hal shard/include common/include herald/include)
        target_link_libraries(filum_shard_sim fl_shard fl_herald pthread m)
    endif()

    # Tests - pure C99, build on all host platforms including MSVC
    if(FILUM_ENABLE_TESTS)
        enable_testing()
        foreach(TEST quant sparse frame round aggregator dp crypto median integration)
            add_executable(test_${TEST} tests/test_${TEST}.c)
            target_link_libraries(test_${TEST} fl_common fl_herald fl_shard)
            if(NOT MSVC)
                target_link_libraries(test_${TEST} m)
            endif()
            add_test(NAME test_${TEST} COMMAND test_${TEST})
        endforeach()
    endif()

endif()

# ---------------------------------------------------------------------------
# Install targets (host only)
# ---------------------------------------------------------------------------
if(IS_HOST)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install libraries
    install(TARGETS fl_common fl_shard fl_herald
        EXPORT FilumTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Install public headers
    install(DIRECTORY common/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY shard/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY herald/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY shard/hal/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum/hal
        FILES_MATCHING PATTERN "hal.h"
    )

    # CMake package config
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/FilumConfigVersion.cmake"
        VERSION ${FL_VERSION_MAJOR}.${FL_VERSION_MINOR}.${FL_VERSION_PATCH}
        COMPATIBILITY SameMajorVersion
    )

    install(EXPORT FilumTargets
        FILE FilumTargets.cmake
        NAMESPACE Filum::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Filum
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FilumConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/FilumConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Filum
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/FilumConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/FilumConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Filum
    )

    # pkg-config
    set(FL_VERSION_MAJOR 0)
    set(FL_VERSION_MINOR 2)
    set(FL_VERSION_PATCH 0)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/filum.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/filum.pc"
        @ONLY
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/filum.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

endif()

# ---------------------------------------------------------------------------
# STM32 firmware
# ---------------------------------------------------------------------------
if(NOT IS_HOST)
    add_executable(filum_shard.elf
        examples/shard_stm32f4/main.c
        shard/hal/stm32/hal_lora.c
        shard/hal/stm32/hal_rtc.c
        shard/hal/stm32/hal_power.c
        shard/hal/stm32/hal_nvs.c
    )
    target_include_directories(filum_shard.elf PRIVATE
        shard/hal shard/hal/stm32 examples/shard_stm32f4)
    target_link_libraries(filum_shard.elf fl_shard fl_common)

    add_custom_command(TARGET filum_shard.elf POST_BUILD
        COMMAND arm-none-eabi-objcopy -O binary filum_shard.elf filum_shard.bin
        COMMAND arm-none-eabi-objcopy -O ihex   filum_shard.elf filum_shard.hex
        COMMAND arm-none-eabi-size filum_shard.elf
        COMMENT "Generating filum_shard.bin and filum_shard.hex"
    )
endif()

# ---------------------------------------------------------------------------
# Install targets (cmake --install build --prefix /usr/local)
# ---------------------------------------------------------------------------
if(IS_HOST)
    include(GNUInstallDirs)

    # Libraries
    install(TARGETS fl_common fl_shard fl_herald
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Headers — install into <prefix>/include/filum/
    install(DIRECTORY common/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY shard/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY shard/hal/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum/hal
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY herald/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filum
        FILES_MATCHING PATTERN "*.h"
    )

    # pkg-config
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/filum.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/filum.pc
        @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/filum.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

    # CMake package config (find_package(Filum))
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FilumConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/FilumConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Filum
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/FilumConfigVersion.cmake
        VERSION 0.2.0
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/FilumConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/FilumConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Filum
    )

    # Herald daemon binary
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/filum_herald
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        OPTIONAL
    )
endif()

# ---------------------------------------------------------------------------
# Doxygen documentation
# ---------------------------------------------------------------------------
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND AND IS_HOST)
    set(DOXYGEN_PROJECT_NAME    "Filum")
    set(DOXYGEN_PROJECT_BRIEF   "Federated Learning for MCU-class Edge Devices over LoRa")
    set(DOXYGEN_PROJECT_VERSION "0.2.0")
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_GENERATE_HTML   YES)
    set(DOXYGEN_GENERATE_MAN    NO)
    set(DOXYGEN_GENERATE_LATEX  NO)
    set(DOXYGEN_EXTRACT_ALL     YES)
    set(DOXYGEN_EXTRACT_STATIC  YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_ENABLE_PREPROCESSING YES)
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE README.md)
    set(DOXYGEN_HTML_TIMESTAMP NO)

    doxygen_add_docs(filum_docs
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/shard/include
        ${CMAKE_CURRENT_SOURCE_DIR}/shard/hal
        ${CMAKE_CURRENT_SOURCE_DIR}/herald/include
        ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        COMMENT "Generating Filum API documentation"
    )
    message(STATUS "[Filum] Doxygen found — run 'cmake --build build --target filum_docs'")
else()
    if(IS_HOST)
        message(STATUS "[Filum] Doxygen not found — documentation target disabled")
    endif()
endif()

