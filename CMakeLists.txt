cmake_minimum_required(VERSION 3.18)

project(kcuckoounter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QT_MAJOR_VERSION 6)
set(QT_DEFAULT_MAJOR_VERSION 6)
set(qt_min_version "6.0.0")

option(KDE "Enable KDE Frameworks and KDEGames integration" OFF)
option(ENABLE_COVERAGE "Enable coverage instrumentation for gcc/clang" OFF)
option(BUILD_UNIT_TESTS "Build the unit tests target" ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
            -Og
            -g
            -fno-omit-frame-pointer
            -Werror
            -Wall
            -Wextra
            -Wpedantic
            -Wcast-align
            -Wcast-qual
            -Wconversion
            -Wctor-dtor-privacy
            -Wenum-compare
            -Wfloat-equal
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            -Wredundant-decls
            -Wsign-conversion
            -Wsign-promo
    )

    if (ENABLE_COVERAGE)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(--coverage)
            add_link_options(--coverage)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
            add_link_options(-fprofile-instr-generate -fcoverage-mapping)
        endif ()
    endif ()
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -march=native -mtune=native -DNDEBUG")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -s")
endif ()

if (NOT ANDROID AND BUILD_UNIT_TESTS)
    enable_testing()
endif ()

if (KDE)
    set(kf6_min_version "6.0.0")
    find_package(ECM 1.0.0 REQUIRED NO_MODULE)
    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    include(KDEInstallDirs)
    include(KDECMakeSettings)
    include(KDECompilerSettings NO_POLICY_SCOPE)
else ()
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    include(GNUInstallDirs)
endif ()

include(FeatureSummary)

find_package(Qt6 ${qt_min_version} CONFIG REQUIRED
        COMPONENTS
        Core
        Concurrent
        Svg
        Widgets
        Test
)

if (KDE)
    find_package(KF6 ${kf6_min_version} REQUIRED COMPONENTS
            CoreAddons
            I18n
            XmlGui
            ConfigWidgets
            WidgetsAddons
            KIO
    )
    find_package(KDEGames6 REQUIRED)
endif ()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(kcuckoounter_sources
        src/main_window.cpp
        src/table/table.cpp
        src/table/table_slot.cpp
        src/table/card_widget.cpp
        src/card_helpers/card_packer.cpp
        src/card_helpers/card_picker.cpp
        src/card_helpers/card_sheet.cpp
        src/arch/random_generator.cpp
        src/arch/base_clock.cpp
        src/arch/time_interface.cpp
        src/table/infinity_spinbox.cpp
        src/image/rasterization_runner.cpp
        src/image/image_cacher.cpp
        src/image/raster_cache.cpp
        src/image/card_preview_carousel.cpp
        src/table/slot_settings.cpp
        src/table/settings_template.cpp
        src/arch/icon_loader.cpp
        src/settings/strategy_data.cpp
        src/settings/theme_palette.cpp
        src/settings/theme_settings.cpp
        src/monitor/resource_monitor.cpp
        src/monitor/raster_cache_debug_strings.cpp
        src/monitor/monitor_visual_widgets.cpp
        src/monitor/main_window_monitor.cpp
        src/monitor/main_window_monitor_setup.cpp
)

set(kcuckoounter_headers
        include/main_window.hpp
        include/table/table.hpp
        include/table/table_slot.hpp
        include/table/card_widget.hpp
        include/card_helpers/card_packer.hpp
        include/card_helpers/card_picker.hpp
        include/card_helpers/card_sheet.hpp
        include/arch/random_generator.hpp
        include/image/image_cacher.hpp
        include/arch/base_clock.hpp
        include/arch/time_interface.hpp
        include/table/infinity_spinbox.hpp
        include/image/rasterization_runner.hpp
        include/image/raster_cache.hpp
        include/image/card_preview_carousel.hpp
        include/table/slot_settings.hpp
        include/table/settings_template.hpp
        include/arch/str_label.hpp
        include/arch/icon_loader.hpp
        include/settings/strategy_data.hpp
        include/settings/theme_palette.hpp
        include/settings/theme_settings.hpp
        include/monitor/resource_monitor.hpp
        include/monitor/geometry_debug_telemetry.hpp
        include/monitor/raster_cache_debug_strings.hpp
        include/monitor/monitor_visual_widgets.hpp
)

set(kcuckoounter_qt_libs
        Qt6::Core
        Qt6::Concurrent
        Qt6::Svg
        Qt6::Widgets
)

if (KDE)
    set(kcuckoounter_kde_libs
            KF6::CoreAddons
            KF6::I18n
            KF6::XmlGui
            KF6::ConfigWidgets
            KF6::WidgetsAddons
            KF6::KIOCore
            KDEGames6
    )
endif ()

qt_add_executable(kcuckoounter
        MANUAL_FINALIZATION
        src/main.cpp
        ${kcuckoounter_sources}
        ${kcuckoounter_headers}
)

target_include_directories(kcuckoounter
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kcuckoounter
        PRIVATE
        ${kcuckoounter_qt_libs}
        $<$<BOOL:${KDE}>:${kcuckoounter_kde_libs}>
)

target_compile_definitions(kcuckoounter
        PRIVATE
        $<$<BOOL:${KDE}>:KC_KDE>
)

if (NOT ANDROID AND BUILD_UNIT_TESTS)
    target_precompile_headers(kcuckoounter PRIVATE
            <QtCore>
            <QtWidgets>
            <vector>
            <tuple>
    )
endif ()

if (ANDROID)
    set_property(TARGET kcuckoounter PROPERTY QT_ANDROID_PACKAGE_NAME org.example.kcuckoounter)
endif ()

qt_finalize_executable(kcuckoounter)

add_custom_command(
        TARGET kcuckoounter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:kcuckoounter>/assets
)

if (KDE)
    set(install_args ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
else ()
    set(install_args
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif ()

if (NOT ANDROID)
    install(TARGETS kcuckoounter ${install_args})
endif ()

if (NOT ANDROID AND BUILD_UNIT_TESTS)
    set(kcuckoounter_test_headers
            tests/include/card_packer_tests.hpp
            tests/include/card_sheet_tests.hpp
            tests/include/card_widget_tests.hpp
            tests/include/preview_carousel_tests.hpp
            tests/include/table_tests.hpp
            tests/include/infinity_spinbox_tests.hpp
            tests/include/rasterization_runner_tests.hpp
            tests/include/raster_cache_tests.hpp
            tests/include/image_cacher_tests.hpp
            tests/include/resource_monitor_tests.hpp
    )

    set(kcuckoounter_test_sources
            tests/main_tests.cpp
            tests/card_packer_tests.cpp
            tests/card_sheet_tests.cpp
            tests/card_widget_tests.cpp
            tests/preview_carousel_tests.cpp
            tests/table_tests.cpp
            tests/infinity_spinbox_tests.cpp
            tests/rasterization_runner_tests.cpp
            tests/raster_cache_tests.cpp
            tests/image_cacher_tests.cpp
            tests/resource_monitor_tests.cpp
    )

    qt_add_executable(kcuckoounter_unittests
            ${kcuckoounter_test_sources}
            ${kcuckoounter_test_headers}
            ${kcuckoounter_sources}
            ${kcuckoounter_headers}
    )

    target_include_directories(kcuckoounter_unittests
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/include
    )

    target_link_libraries(kcuckoounter_unittests
            PRIVATE
            ${kcuckoounter_qt_libs}
            Qt6::Test
            $<$<BOOL:${KDE}>:${kcuckoounter_kde_libs}>
    )

    add_test(
            NAME kcuckoounter_unittests
            COMMAND kcuckoounter_unittests
    )

    add_custom_command(
            TARGET kcuckoounter_unittests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:kcuckoounter_unittests>/assets
    )
endif ()

if (ENABLE_COVERAGE AND NOT ANDROID)
    set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/../doc/cov")
    set(COVERAGE_IGNORE_REGEX ".*/tests/.*|.*/include/.*|.*/usr/lib/.*|.*/[^/]*_autogen/.*|.*\\.moc|.*/moc_.*")

    if (TARGET kcuckoounter_unittests)
        add_custom_command(
                TARGET kcuckoounter_unittests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
        )

        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            find_program(LLVM_PROFDATA llvm-profdata)
            find_program(LLVM_COV llvm-cov)

            if (LLVM_PROFDATA AND LLVM_COV)
                set(PROFRAW_FILE "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.profraw")
                set(PROFDATA_FILE "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.profdata")

                add_custom_target(coverage
                        COMMENT "Generating coverage with llvm-cov"
                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                        COMMAND ${CMAKE_COMMAND} --build . --target kcuckoounter_unittests
                        COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE}
                        $<TARGET_FILE:kcuckoounter_unittests>
                        COMMAND ${LLVM_PROFDATA} merge -sparse
                        -o ${PROFDATA_FILE} ${PROFRAW_FILE}
                        COMMAND ${LLVM_COV} show $<TARGET_FILE:kcuckoounter_unittests>
                        -instr-profile=${PROFDATA_FILE}
                        -format=html
                        -output-dir=${COVERAGE_DIR}
                        -ignore-filename-regex=${COVERAGE_IGNORE_REGEX}
                        -show-branches=count
                        -Xdemangler=c++filt
                        -show-line-counts
                        -show-regions
                        -show-instantiations
                        -show-expansions
                        -use-color
                        -coverage-watermark=90,60
                        COMMAND ${LLVM_COV} export $<TARGET_FILE:kcuckoounter_unittests>
                        -instr-profile=${PROFDATA_FILE}
                        -format=lcov
                        -ignore-filename-regex=${COVERAGE_IGNORE_REGEX}
                        > ${CMAKE_BINARY_DIR}/coverage.info
                        DEPENDS kcuckoounter_unittests
                        VERBATIM
                )
            else ()
                message(WARNING
                        "ENABLE_COVERAGE=ON but llvm-profdata/llvm-cov not found; "
                        "'coverage' target will be unavailable."
                )
            endif ()
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            find_program(GCOVR gcovr)

            if (GCOVR)
                add_custom_target(coverage
                        COMMENT "Generating coverage with gcovr"
                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                        COMMAND ${CMAKE_COMMAND} --build . --target kcuckoounter_unittests
                        COMMAND $<TARGET_FILE:kcuckoounter_unittests>
                        COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
                        COMMAND ${GCOVR}
                        --root ${CMAKE_SOURCE_DIR}
                        --exclude "${COVERAGE_IGNORE_REGEX}"
                        --html --html-details
                        -o ${COVERAGE_DIR}/index.html
                        DEPENDS kcuckoounter_unittests
                        VERBATIM
                )
            else ()
                message(WARNING
                        "ENABLE_COVERAGE=ON but gcovr not found; "
                        "'coverage' target will be unavailable."
                )
            endif ()
        endif ()
    else ()
        message(STATUS
                "ENABLE_COVERAGE=ON but tests are not built; 'coverage' target will be a no-op."
        )
    endif ()
endif ()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
