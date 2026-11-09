add_library(cdl_test_module SHARED test_module.c)
set_target_properties(cdl_test_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cdl_test_module")
cdl_enable_warnings(cdl_test_module)

add_executable(cdl_tests test_cdl.c)
target_link_libraries(cdl_tests PRIVATE cdl::cdl)
if(WIN32)
    # CommandLineToArgvW() is used to recover a UTF-16 command line.
    target_link_libraries(cdl_tests PRIVATE shell32)
endif()
cdl_enable_warnings(cdl_tests)

if(WIN32 AND CDL_BUILD_SHARED)
    add_custom_command(TARGET cdl_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:cdl>
            $<TARGET_FILE_DIR:cdl_tests>)
endif()

if(NOT ANDROID)
    add_test(NAME cdl_tests
        COMMAND cdl_tests $<TARGET_FILE:cdl_test_module>)
endif()
