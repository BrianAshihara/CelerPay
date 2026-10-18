cmake_minimum_required(VERSION 3.25)

project(celerpay VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

option(CELERPAY_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(CELERPAY_BUILD_BENCH "Build benchmarks" OFF)
option(CELERPAY_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(CELERPAY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(CELERPAY_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(CELERPAY_BUILD_QR "Build the optional QR Code module" ON)
option(CELERPAY_BUILD_FUZZ "Build libFuzzer targets (MSVC or Clang)" OFF)

if(BUILD_SHARED_LIBS AND PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

set(CELERPAY_SIMD_SOURCES
    ${PROJECT_SOURCE_DIR}/src/simd/avx2.cpp
    ${PROJECT_SOURCE_DIR}/src/simd/dispatch.cpp
    ${PROJECT_SOURCE_DIR}/src/simd/neon.cpp
    ${PROJECT_SOURCE_DIR}/src/simd/scalar.cpp
)

function(celerpay_avx2_flags target)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$")
        if(MSVC)
            set_source_files_properties(${PROJECT_SOURCE_DIR}/src/simd/avx2.cpp TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS /arch:AVX2)
        else()
            set_source_files_properties(${PROJECT_SOURCE_DIR}/src/simd/avx2.cpp TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS -mavx2)
        endif()
    endif()
endfunction()

# Testes e benchmarks usam os kernels internos, que não são exportados numa biblioteca dinâmica.
function(celerpay_link_internals target)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    if(BUILD_SHARED_LIBS)
        target_sources(${target} PRIVATE ${CELERPAY_SIMD_SOURCES})
        celerpay_avx2_flags(${target})
    endif()
endfunction()

add_library(celerpay_warnings INTERFACE)
if(MSVC)
    target_compile_options(celerpay_warnings INTERFACE /W4 /permissive- /utf-8)
    if(CELERPAY_WARNINGS_AS_ERRORS)
        target_compile_options(celerpay_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(celerpay_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(CELERPAY_WARNINGS_AS_ERRORS)
        target_compile_options(celerpay_warnings INTERFACE -Werror)
    endif()
endif()

if(CELERPAY_BUILD_FUZZ)
    if(MSVC)
        add_compile_options(/fsanitize=address /fsanitize-coverage=inline-8bit-counters /fsanitize-coverage=edge /fsanitize-coverage=trace-cmp)
    else()
        add_compile_options(-fsanitize=address,fuzzer-no-link -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

if(CELERPAY_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(celerpay
    src/boleto.cpp
    src/document.cpp
    src/money.cpp
    src/pix.cpp
    src/reconcile.cpp
    src/rules.cpp
    src/statement/cnab240.cpp
    src/statement/mapped_file.cpp
    src/statement/ofx.cpp
    src/statement/pixjson.cpp
    src/tlv.cpp
    ${CELERPAY_SIMD_SOURCES}
)
add_library(celerpay::celerpay ALIAS celerpay)

generate_export_header(celerpay
    BASE_NAME CELERPAY
    EXPORT_MACRO_NAME CELERPAY_API
    EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/celerpay/export.hpp
)

target_include_directories(celerpay PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(celerpay PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(celerpay PUBLIC cxx_std_23)
celerpay_avx2_flags(celerpay)
target_link_libraries(celerpay PRIVATE $<BUILD_INTERFACE:celerpay_warnings>)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(celerpay PUBLIC CELERPAY_STATIC_DEFINE)
endif()
set_target_properties(celerpay PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME celerpay
)

set(CELERPAY_INSTALL_TARGETS celerpay)

if(CELERPAY_BUILD_QR)
    add_library(celerpay_qr src/qr/qr.cpp)
    add_library(celerpay::qr ALIAS celerpay_qr)
    generate_export_header(celerpay_qr
        BASE_NAME CELERPAY_QR
        EXPORT_MACRO_NAME CELERPAY_QR_API
        EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/celerpay/qr_export.hpp
    )
    target_link_libraries(celerpay_qr PUBLIC celerpay PRIVATE $<BUILD_INTERFACE:celerpay_warnings>)
    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(celerpay_qr PUBLIC CELERPAY_QR_STATIC_DEFINE)
    endif()
    set_target_properties(celerpay_qr PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME qr
    )
    list(APPEND CELERPAY_INSTALL_TARGETS celerpay_qr)
endif()

install(TARGETS ${CELERPAY_INSTALL_TARGETS} EXPORT celerpayTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(CELERPAY_BUILD_QR)
    install(DIRECTORY include/ ${PROJECT_BINARY_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else()
    install(DIRECTORY include/ ${PROJECT_BINARY_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "qr.hpp" EXCLUDE)
endif()
install(EXPORT celerpayTargets
    NAMESPACE celerpay::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/celerpay
)
configure_package_config_file(cmake/celerpayConfig.cmake.in
    ${PROJECT_BINARY_DIR}/celerpayConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/celerpay
)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/celerpayConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${PROJECT_BINARY_DIR}/celerpayConfig.cmake
    ${PROJECT_BINARY_DIR}/celerpayConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/celerpay
)

if(CELERPAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CELERPAY_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if(CELERPAY_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

if(CELERPAY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
