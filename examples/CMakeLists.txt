add_executable(cdl_example load_math.c)
target_link_libraries(cdl_example PRIVATE cdl::cdl)
cdl_enable_warnings(cdl_example)

if(WIN32 AND CDL_BUILD_SHARED)
    add_custom_command(TARGET cdl_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:cdl>
            $<TARGET_FILE_DIR:cdl_example>)
endif()
