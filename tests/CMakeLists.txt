add_executable(tst_core_domain
    domain/tst_core_domain.cpp
)

target_link_libraries(tst_core_domain
    PRIVATE
        siriusscope_core
)

add_test(NAME tst_core_domain COMMAND tst_core_domain)

add_executable(tst_processing
    processing/tst_processing.cpp
)

target_link_libraries(tst_processing
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_processing COMMAND tst_processing)

add_executable(tst_waterfall_history
    app/tst_waterfall_history.cpp
    ../src/app/waterfallhistorymodel.cpp
    ../src/app/waterfallringbuffer.cpp
    ../src/app/waterfallstorage.cpp
)

target_link_libraries(tst_waterfall_history
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_waterfall_history
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_history COMMAND tst_waterfall_history)

if(WIN32)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION)
    if(NOT _qt_core_location)
        get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_DEBUG)
    endif()
    if(_qt_core_location)
        get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
        set_tests_properties(tst_waterfall_history PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
    endif()
endif()
