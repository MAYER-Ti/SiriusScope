add_executable(tst_core_domain
    domain/tst_core_domain.cpp
)

target_link_libraries(tst_core_domain
    PRIVATE
        siriusscope_core
)

add_test(NAME tst_core_domain COMMAND tst_core_domain)

add_executable(tst_antenna_motion_planner
    core/tst_antenna_motion_planner.cpp
)

target_link_libraries(tst_antenna_motion_planner
    PRIVATE
        siriusscope_core
)

add_test(NAME tst_antenna_motion_planner COMMAND tst_antenna_motion_planner)

add_executable(tst_processing
    processing/tst_processing.cpp
)

target_link_libraries(tst_processing
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_processing COMMAND tst_processing)

add_executable(tst_bearing_service
    processing/tst_bearing_service.cpp
)

target_link_libraries(tst_bearing_service
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_bearing_service COMMAND tst_bearing_service)

add_executable(tst_signal_parameter_estimator
    processing/tst_signal_parameter_estimator.cpp
)

target_link_libraries(tst_signal_parameter_estimator
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_signal_parameter_estimator COMMAND tst_signal_parameter_estimator)

add_executable(tst_signal_parameter_accumulator
    processing/tst_signal_parameter_accumulator.cpp
)

target_link_libraries(tst_signal_parameter_accumulator
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_signal_parameter_accumulator COMMAND tst_signal_parameter_accumulator)

add_executable(tst_pipeline
    pipeline/tst_pipeline.cpp
)

target_link_libraries(tst_pipeline
    PRIVATE
        siriusscope_pipeline
        siriusscope_hardware
)

target_include_directories(tst_pipeline
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_pipeline COMMAND tst_pipeline)

add_executable(tst_pipeline_metrics
    pipeline/tst_pipeline_metrics.cpp
)

target_link_libraries(tst_pipeline_metrics
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_pipeline_metrics
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_pipeline_metrics COMMAND tst_pipeline_metrics)

add_executable(tst_waterfall_aggregator
    pipeline/tst_waterfall_aggregator.cpp
)

target_link_libraries(tst_waterfall_aggregator
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_waterfall_aggregator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_aggregator COMMAND tst_waterfall_aggregator)

add_executable(tst_waterfall_row_queue
    pipeline/tst_waterfall_row_queue.cpp
)

target_link_libraries(tst_waterfall_row_queue
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_waterfall_row_queue
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_row_queue COMMAND tst_waterfall_row_queue)

add_executable(tst_spectrum_aggregator
    pipeline/tst_spectrum_aggregator.cpp
)

target_link_libraries(tst_spectrum_aggregator
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_spectrum_aggregator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_spectrum_aggregator COMMAND tst_spectrum_aggregator)

add_executable(tst_bearing_aggregator
    pipeline/tst_bearing_aggregator.cpp
)

target_link_libraries(tst_bearing_aggregator
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_bearing_aggregator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_bearing_aggregator COMMAND tst_bearing_aggregator)

add_executable(tst_signal_parameter_aggregator
    pipeline/tst_signal_parameter_aggregator.cpp
)

target_link_libraries(tst_signal_parameter_aggregator
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_signal_parameter_aggregator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_signal_parameter_aggregator COMMAND tst_signal_parameter_aggregator)

add_executable(tst_data_ingest_pipeline
    pipeline/tst_data_ingest_pipeline.cpp
)

target_link_libraries(tst_data_ingest_pipeline
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_data_ingest_pipeline
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_data_ingest_pipeline COMMAND tst_data_ingest_pipeline)

add_executable(tst_source_to_pipeline_bridge
    pipeline/tst_source_to_pipeline_bridge.cpp
)

target_link_libraries(tst_source_to_pipeline_bridge
    PRIVATE
        siriusscope_pipeline
)

target_include_directories(tst_source_to_pipeline_bridge
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_source_to_pipeline_bridge COMMAND tst_source_to_pipeline_bridge)

add_executable(tst_high_load_data_plane
    perf/tst_high_load_data_plane.cpp
)

target_link_libraries(tst_high_load_data_plane
    PRIVATE
        siriusscope_pipeline
        siriusscope_hardware
)

target_include_directories(tst_high_load_data_plane
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_high_load_data_plane COMMAND tst_high_load_data_plane)
set_tests_properties(tst_high_load_data_plane PROPERTIES LABELS "perf")

add_executable(tst_simulated_bco_payload_accounting
    hardware/tst_simulated_bco_payload_accounting.cpp
)

target_link_libraries(tst_simulated_bco_payload_accounting
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_simulated_bco_payload_accounting COMMAND tst_simulated_bco_payload_accounting)

add_executable(tst_simulator_bco_sample_source
    hardware/tst_simulator_bco_sample_source.cpp
)

target_link_libraries(tst_simulator_bco_sample_source
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_simulator_bco_sample_source COMMAND tst_simulator_bco_sample_source)

add_executable(tst_high_load_simulator_bco_stream_source
    hardware/tst_high_load_simulator_bco_stream_source.cpp
)

target_link_libraries(tst_high_load_simulator_bco_stream_source
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_high_load_simulator_bco_stream_source COMMAND tst_high_load_simulator_bco_stream_source)

add_executable(tst_high_load_simulator_bco_control
    hardware/tst_high_load_simulator_bco_control.cpp
)

target_link_libraries(tst_high_load_simulator_bco_control
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_high_load_simulator_bco_control COMMAND tst_high_load_simulator_bco_control)

add_executable(tst_legacy_bco_sample_source_adapter
    hardware/tst_legacy_bco_sample_source_adapter.cpp
)

target_link_libraries(tst_legacy_bco_sample_source_adapter
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_legacy_bco_sample_source_adapter COMMAND tst_legacy_bco_sample_source_adapter)

add_executable(tst_data_source_factory
    hardware/tst_data_source_factory.cpp
)

target_link_libraries(tst_data_source_factory
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_data_source_factory COMMAND tst_data_source_factory)

add_executable(tst_simulator_bco_control
    hardware/tst_simulator_bco_control.cpp
)

target_link_libraries(tst_simulator_bco_control
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_simulator_bco_control COMMAND tst_simulator_bco_control)

add_executable(tst_simulator_antenna
    hardware/tst_simulator_antenna.cpp
)

target_link_libraries(tst_simulator_antenna
    PRIVATE
        siriusscope_hardware
)

add_test(NAME tst_simulator_antenna COMMAND tst_simulator_antenna)

add_executable(tst_architecture_baseline
    infrastructure/tst_architecture_baseline.cpp
)

target_link_libraries(tst_architecture_baseline
    PRIVATE
        siriusscope_infrastructure_iface
)

add_test(NAME tst_architecture_baseline COMMAND tst_architecture_baseline)

add_executable(tst_binary_waterfall_session_storage
    infrastructure/tst_binary_waterfall_session_storage.cpp
)

target_link_libraries(tst_binary_waterfall_session_storage
    PRIVATE
        Qt6::Core
        siriusscope_infrastructure
)

target_include_directories(tst_binary_waterfall_session_storage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_binary_waterfall_session_storage COMMAND tst_binary_waterfall_session_storage)

add_executable(tst_diagnostic_log_writer
    infrastructure/tst_diagnostic_log_writer.cpp
)

target_link_libraries(tst_diagnostic_log_writer
    PRIVATE
        Qt6::Core
        siriusscope_infrastructure
)

target_include_directories(tst_diagnostic_log_writer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_diagnostic_log_writer COMMAND tst_diagnostic_log_writer)

add_executable(tst_binary_result_table_storage
    infrastructure/tst_binary_result_table_storage.cpp
)

target_link_libraries(tst_binary_result_table_storage
    PRIVATE
        Qt6::Core
        siriusscope_infrastructure
)

target_include_directories(tst_binary_result_table_storage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_binary_result_table_storage COMMAND tst_binary_result_table_storage)

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

add_executable(tst_waterfall_timeline
    app/tst_waterfall_timeline.cpp
    ../src/app/waterfalltimeline.cpp
    ../src/app/waterfallstorage.cpp
)

target_link_libraries(tst_waterfall_timeline
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_waterfall_timeline
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_timeline COMMAND tst_waterfall_timeline)

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

add_executable(tst_waterfall_render_buffer_adapter
    app/tst_waterfall_render_buffer_adapter.cpp
)

target_link_libraries(tst_waterfall_render_buffer_adapter
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_waterfall_render_buffer_adapter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_render_buffer_adapter COMMAND tst_waterfall_render_buffer_adapter)

add_executable(tst_realtime_signal_pipeline
    app/tst_realtime_signal_pipeline.cpp
)

target_link_libraries(tst_realtime_signal_pipeline
    PRIVATE
        siriusscope_application
)

target_include_directories(tst_realtime_signal_pipeline
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_realtime_signal_pipeline COMMAND tst_realtime_signal_pipeline)

add_executable(tst_waterfall_controller
    app/tst_waterfall_controller.cpp
)

target_link_libraries(tst_waterfall_controller
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_waterfall_controller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_controller COMMAND tst_waterfall_controller)

add_executable(tst_appstate
    app/tst_appstate.cpp
    ../src/app/appstate.cpp
)

set_target_properties(tst_appstate PROPERTIES
    AUTOMOC ON
)

target_link_libraries(tst_appstate
    PRIVATE
        Qt6::Core
)

target_include_directories(tst_appstate
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_appstate COMMAND tst_appstate)

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

add_executable(tst_antenna_controller_stub
    app/tst_antenna_controller_stub.cpp
    ../src/app/antennacontrollerstub.cpp
)

set_target_properties(tst_antenna_controller_stub PROPERTIES
    AUTOMOC ON
)

target_link_libraries(tst_antenna_controller_stub
    PRIVATE
        Qt6::Core
        siriusscope_core
)

target_include_directories(tst_antenna_controller_stub
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_antenna_controller_stub COMMAND tst_antenna_controller_stub)

add_executable(tst_band_list_model
    app/tst_band_list_model.cpp
)

target_link_libraries(tst_band_list_model
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_band_list_model
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_band_list_model COMMAND tst_band_list_model)

add_executable(tst_band_config_controller
    app/tst_band_config_controller.cpp
)

target_link_libraries(tst_band_config_controller
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_band_config_controller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_band_config_controller COMMAND tst_band_config_controller)

add_executable(tst_spectrum_envelope_controller
    app/tst_spectrum_envelope_controller.cpp
)

target_link_libraries(tst_spectrum_envelope_controller
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_spectrum_envelope_controller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_spectrum_envelope_controller COMMAND tst_spectrum_envelope_controller)

add_executable(tst_spectrum_snapshot_adapter
    app/tst_spectrum_snapshot_adapter.cpp
)

target_link_libraries(tst_spectrum_snapshot_adapter
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_spectrum_snapshot_adapter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_spectrum_snapshot_adapter COMMAND tst_spectrum_snapshot_adapter)

add_executable(tst_bearing_snapshot_adapter
    app/tst_bearing_snapshot_adapter.cpp
)

target_link_libraries(tst_bearing_snapshot_adapter
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_bearing_snapshot_adapter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_bearing_snapshot_adapter COMMAND tst_bearing_snapshot_adapter)

add_executable(tst_signal_parameter_snapshot_adapter
    app/tst_signal_parameter_snapshot_adapter.cpp
)

target_link_libraries(tst_signal_parameter_snapshot_adapter
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_signal_parameter_snapshot_adapter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_signal_parameter_snapshot_adapter COMMAND tst_signal_parameter_snapshot_adapter)

add_executable(tst_diagnostics_service
    app/tst_diagnostics_service.cpp
)

target_link_libraries(tst_diagnostics_service
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_diagnostics_service
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_diagnostics_service COMMAND tst_diagnostics_service)

add_executable(tst_scan_band_result_aggregator
    app/tst_scan_band_result_aggregator.cpp
)

target_link_libraries(tst_scan_band_result_aggregator
    PRIVATE
        siriusscope_application
)

target_include_directories(tst_scan_band_result_aggregator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_scan_band_result_aggregator COMMAND tst_scan_band_result_aggregator)

add_executable(tst_result_table_model
    app/tst_result_table_model.cpp
)

target_link_libraries(tst_result_table_model
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_result_table_model
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_result_table_model COMMAND tst_result_table_model)

add_executable(tst_result_table_controller
    app/tst_result_table_controller.cpp
)

target_link_libraries(tst_result_table_controller
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_result_table_controller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_result_table_controller COMMAND tst_result_table_controller)

add_executable(tst_generator_pulse_result_table_pipeline
    app/tst_generator_pulse_result_table_pipeline.cpp
)

target_link_libraries(tst_generator_pulse_result_table_pipeline
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_generator_pulse_result_table_pipeline
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_generator_pulse_result_table_pipeline COMMAND tst_generator_pulse_result_table_pipeline)

add_executable(tst_scan_acquisition_recorder
    app/tst_scan_acquisition_recorder.cpp
)

target_link_libraries(tst_scan_acquisition_recorder
    PRIVATE
        siriusscope_application
)

target_include_directories(tst_scan_acquisition_recorder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_scan_acquisition_recorder COMMAND tst_scan_acquisition_recorder)

add_executable(tst_signal_sample_bus
    app/tst_signal_sample_bus.cpp
)

target_link_libraries(tst_signal_sample_bus
    PRIVATE
        siriusscope_application
)

target_include_directories(tst_signal_sample_bus
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_signal_sample_bus COMMAND tst_signal_sample_bus)

add_executable(tst_scan_controller
    app/tst_scan_controller.cpp
)

target_link_libraries(tst_scan_controller
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_scan_controller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_scan_controller COMMAND tst_scan_controller)

add_executable(tst_scan_controller_simulator
    app/tst_scan_controller_simulator.cpp
)

target_link_libraries(tst_scan_controller_simulator
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_scan_controller_simulator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_scan_controller_simulator COMMAND tst_scan_controller_simulator)

add_executable(tst_waterfall_scan_recording_adapter
    app/tst_waterfall_scan_recording_adapter.cpp
)

target_link_libraries(tst_waterfall_scan_recording_adapter
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_waterfall_scan_recording_adapter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_waterfall_scan_recording_adapter COMMAND tst_waterfall_scan_recording_adapter)

add_executable(tst_status_model
    app/tst_status_model.cpp
)

target_link_libraries(tst_status_model
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_status_model
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_status_model COMMAND tst_status_model)

add_executable(tst_application_bootstrap
    app/tst_application_bootstrap.cpp
)

target_link_libraries(tst_application_bootstrap
    PRIVATE
        Qt6::Core
        siriusscope_application
)

target_include_directories(tst_application_bootstrap
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME tst_application_bootstrap COMMAND tst_application_bootstrap)

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
        set_tests_properties(tst_waterfall_timeline PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_binary_waterfall_session_storage PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_diagnostic_log_writer PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_binary_result_table_storage PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_architecture_baseline PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_row_resampler PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_color_mapper PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_render_buffer_adapter PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_realtime_signal_pipeline PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_controller PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_appstate PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_synthetic_waterfall_datasource PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_antenna_controller_stub PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_band_list_model PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_band_config_controller PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_spectrum_envelope_controller PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_spectrum_snapshot_adapter PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_bearing_snapshot_adapter PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_signal_parameter_snapshot_adapter PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_diagnostics_service PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_scan_band_result_aggregator PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_result_table_model PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_result_table_controller PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_generator_pulse_result_table_pipeline PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_scan_acquisition_recorder PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_signal_sample_bus PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_scan_controller PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_scan_controller_simulator PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_waterfall_scan_recording_adapter PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_status_model PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
        set_tests_properties(tst_application_bootstrap PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
        )
    endif()
endif()
