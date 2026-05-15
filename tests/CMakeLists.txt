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

add_executable(tst_waterfall_row_resampler
    app/tst_waterfall_row_resampler.cpp
    ../src/app/waterfallrowresampler.cpp
)

target_link_libraries(tst_waterfall_row_resampler
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_waterfall_row_resampler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_row_resampler COMMAND tst_waterfall_row_resampler)

add_executable(tst_waterfall_color_mapper
    app/tst_waterfall_color_mapper.cpp
    ../src/app/waterfallcolormapper.cpp
)

target_link_libraries(tst_waterfall_color_mapper
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_waterfall_color_mapper
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_color_mapper COMMAND tst_waterfall_color_mapper)

add_executable(tst_synthetic_waterfall_datasource
    app/tst_synthetic_waterfall_datasource.cpp
    ../src/app/syntheticwaterfalldatasource.cpp
)

target_link_libraries(tst_synthetic_waterfall_datasource
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_synthetic_waterfall_datasource
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_synthetic_waterfall_datasource COMMAND tst_synthetic_waterfall_datasource)

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
        set_tests_properties(tst_waterfall_row_resampler PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_color_mapper PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_synthetic_waterfall_datasource PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
    endif()
endif()
