foreach(example money documents boleto reconcile)
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE celerpay::celerpay celerpay_warnings)
endforeach()

if(CELERPAY_BUILD_QR)
    add_executable(example_pix_qr pix_qr.cpp)
    target_link_libraries(example_pix_qr PRIVATE celerpay::qr celerpay_warnings)
endif()
