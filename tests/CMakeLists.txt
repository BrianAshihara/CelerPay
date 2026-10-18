find_package(Catch2 3 CONFIG REQUIRED)

add_executable(celerpay_tests
    boleto_tests.cpp
    document_tests.cpp
    error_tests.cpp
    money_tests.cpp
    pix_tests.cpp
    primitives_tests.cpp
    reconcile_tests.cpp
    rules_tests.cpp
    simd_tests.cpp
    statement_tests.cpp
)
target_include_directories(celerpay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
celerpay_link_internals(celerpay_tests)
target_link_libraries(celerpay_tests PRIVATE celerpay::celerpay celerpay_warnings Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(celerpay_tests)

if(CELERPAY_BUILD_QR)
    find_package(ZXing CONFIG REQUIRED)
    add_executable(celerpay_qr_tests qr_tests.cpp)
    target_link_libraries(celerpay_qr_tests PRIVATE celerpay::qr celerpay_warnings ZXing::ZXing Catch2::Catch2WithMain)
    catch_discover_tests(celerpay_qr_tests)
endif()

add_executable(celerpay_stress stress/cnab_stress.cpp)
target_link_libraries(celerpay_stress PRIVATE celerpay::celerpay celerpay_warnings)
if(WIN32)
    target_link_libraries(celerpay_stress PRIVATE psapi)
endif()
