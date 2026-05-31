cmake_minimum_required(VERSION 3.16)

project(SiriusScope VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    QuickControls2
    ShaderTools
)
find_package(Threads REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

set(SIRIUSSCOPE_APPLICATION_SOURCES
    src/app/applicationbootstrap.h
    src/app/applicationbootstrap.cpp
    src/app/antennacontrolinterface.h
    src/app/antennacontrollerstub.h
    src/app/antennacontrollerstub.cpp
    src/app/appstate.h
    src/app/appstate.cpp
    src/app/bearingsnapshotadapter.h
    src/app/bearingsnapshotadapter.cpp
    src/app/bearingframebus.h
    src/app/bearingframebus.cpp
    src/app/bandconfigcontroller.h
    src/app/bandconfigcontroller.cpp
    src/app/bandlistmodel.h
    src/app/bandlistmodel.cpp
    src/app/diagnosticsservice.h
    src/app/diagnosticsservice.cpp
    src/app/frequencygridmodel.h
    src/app/frequencygridmodel.cpp
    src/app/frequencyviewportmodel.h
    src/app/frequencyviewportmodel.cpp
    src/app/inmemoryscanacquisitionrecorder.h
    src/app/inmemoryscanacquisitionrecorder.cpp
    src/app/interfaces/processing_flush_control.h
    src/app/interfaces/result_table_sink.h
    src/app/interfaces/scan_acquisition_recorder.h
    src/app/interfaces/scan_recording_control.h
    src/app/nullresulttablesink.h
    src/app/nullresulttablesink.cpp
    src/app/recordingcontroller.h
    src/app/recordingcontroller.cpp
    src/app/realtimesignalpipeline.h
    src/app/realtimesignalpipeline.cpp
    src/app/resulttablecontroller.h
    src/app/resulttablecontroller.cpp
    src/app/resulttablemodel.h
    src/app/resulttablemodel.cpp
    src/app/scanbandresultaggregator.h
    src/app/scanbandresultaggregator.cpp
    src/app/scanacquisitionsession.h
    src/app/scanacquisitionsession.cpp
    src/app/scancontroller.h
    src/app/scancontroller.cpp
    src/app/signalsamplebus.h
    src/app/signalsamplebus.cpp
    src/app/signalparametersnapshotadapter.h
    src/app/signalparametersnapshotadapter.cpp
    src/app/spectrumcontrollerstub.h
    src/app/spectrumcontrollerstub.cpp
    src/app/spectrumdecimator.h
    src/app/spectrumdecimator.cpp
    src/app/spectrumenvelopecontroller.h
    src/app/spectrumenvelopecontroller.cpp
    src/app/spectrumenvelopeworker.h
    src/app/spectrumenvelopeworker.cpp
    src/app/spectrumsnapshotadapter.h
    src/app/spectrumsnapshotadapter.cpp
    src/app/statusmodel.h
    src/app/statusmodel.cpp
    src/app/syntheticwaterfalldatasource.h
    src/app/syntheticwaterfalldatasource.cpp
    src/app/waterfallcontroller.h
    src/app/waterfallcontroller.cpp
    src/app/waterfallscanrecordingadapter.h
    src/app/waterfallscanrecordingadapter.cpp
    src/app/waterfallcontrollerstub.h
    src/app/waterfallcontrollerstub.cpp
    src/app/waterfallcolormapper.h
    src/app/waterfallcolormapper.cpp
    src/app/waterfallrenderbufferadapter.h
    src/app/waterfallrenderbufferadapter.cpp
    src/app/waterfallrowresampler.h
    src/app/waterfallrowresampler.cpp
    src/app/waterfallhistorymodel.h
    src/app/waterfallhistorymodel.cpp
    src/app/waterfalltimeline.h
    src/app/waterfalltimeline.cpp
    src/app/waterfallringbuffer.h
    src/app/waterfallringbuffer.cpp
    src/app/waterfallstorage.h
    src/app/waterfallstorage.cpp
)

set(SIRIUSSCOPE_CORE_SOURCES
    src/core/antenna_motion_planner.h
    src/core/antenna_motion_planner.cpp
    src/core/domain_constraints.h
    src/core/domain_constraints.cpp
    src/core/domain_models.h
    src/core/domain_models.cpp
    src/core/domain_validation.h
    src/core/layer_placeholder.cpp
    src/core/operation_result.h
)

set(SIRIUSSCOPE_PROCESSING_SOURCES
    src/processing/bearing_service.h
    src/processing/bearing_service.cpp
    src/processing/sample_processor.h
    src/processing/sample_processor.cpp
    src/processing/signal_parameter_accumulator.h
    src/processing/signal_parameter_accumulator.cpp
    src/processing/signal_parameter_estimator.h
    src/processing/signal_parameter_estimator.cpp
    src/processing/spectrum_envelope_processor.h
    src/processing/spectrum_envelope_processor.cpp
)

set(SIRIUSSCOPE_PIPELINE_SOURCES
    src/pipeline/README.md
    src/pipeline/bearing_aggregator.h
    src/pipeline/bearing_aggregator.cpp
    src/pipeline/bearing_snapshot.h
    src/pipeline/bearing_snapshot.cpp
    src/pipeline/bounded_block_queue.h
    src/pipeline/data_ingest_pipeline.h
    src/pipeline/data_ingest_pipeline.cpp
    src/pipeline/pipeline_diagnostics.h
    src/pipeline/pipeline_diagnostics.cpp
    src/pipeline/pipeline_metrics.h
    src/pipeline/pipeline_metrics.cpp
    src/pipeline/processing_engine.h
    src/pipeline/processing_engine.cpp
    src/pipeline/signal_block.h
    src/pipeline/signal_block.cpp
    src/pipeline/signal_block_pool.h
    src/pipeline/signal_block_pool.cpp
    src/pipeline/signal_parameter_aggregator.h
    src/pipeline/signal_parameter_aggregator.cpp
    src/pipeline/signal_parameter_snapshot.h
    src/pipeline/signal_parameter_snapshot.cpp
    src/pipeline/snapshot_exchange.h
    src/pipeline/spectrum_aggregator.h
    src/pipeline/spectrum_aggregator.cpp
    src/pipeline/spectrum_snapshot.h
    src/pipeline/spectrum_snapshot.cpp
    src/pipeline/waterfall_aggregator.h
    src/pipeline/waterfall_aggregator.cpp
    src/pipeline/waterfall_row_queue.h
    src/pipeline/waterfall_row_queue.cpp
    src/pipeline/waterfall_snapshot.h
    src/pipeline/waterfall_snapshot.cpp
)

set(SIRIUSSCOPE_HARDWARE_INTERFACE_SOURCES
    src/hardware/hardware_profile.h
    src/hardware/interfaces/antenna_azimuth_source.h
    src/hardware/interfaces/antenna_azimuth_provider.h
    src/hardware/interfaces/antenna_control.h
    src/hardware/interfaces/bco_control.h
    src/hardware/interfaces/bco_sample_source.h
    src/hardware/interfaces/bco_stream_source.h
)

set(SIRIUSSCOPE_HARDWARE_ADAPTER_SOURCES
    src/hardware/adapters/legacy_bco_sample_source_adapter.h
    src/hardware/adapters/legacy_bco_sample_source_adapter.cpp
)

set(SIRIUSSCOPE_HARDWARE_FACTORY_SOURCES
    src/hardware/data_source_factory.h
    src/hardware/data_source_factory.cpp
)

set(SIRIUSSCOPE_HARDWARE_STUB_SOURCES
    src/hardware/stubs/real_bco_stream_source_stub.h
    src/hardware/stubs/real_bco_stream_source_stub.cpp
)

set(SIRIUSSCOPE_HARDWARE_SIMULATOR_SOURCES
    src/hardware/simulator/high_load_simulator_bco_control.h
    src/hardware/simulator/high_load_simulator_bco_control.cpp
    src/hardware/simulator/high_load_simulator_bco_stream_source.h
    src/hardware/simulator/high_load_simulator_bco_stream_source.cpp
    src/hardware/simulator/simulated_bco_payload_accounting.h
    src/hardware/simulator/simulated_bco_payload_accounting.cpp
    src/hardware/simulator/simulator_pulse_config.h
    src/hardware/simulator/simulator_antenna_azimuth_source.h
    src/hardware/simulator/simulator_antenna_azimuth_source.cpp
    src/hardware/simulator/simulator_antenna_control.h
    src/hardware/simulator/simulator_antenna_control.cpp
    src/hardware/simulator/simulator_antenna_state.h
    src/hardware/simulator/simulator_antenna_state.cpp
    src/hardware/simulator/simulator_bco_control.h
    src/hardware/simulator/simulator_bco_control.cpp
    src/hardware/simulator/simulator_radio_scene.h
    src/hardware/simulator/simulator_radio_scene.cpp
    src/hardware/simulator/simulator_bco_sample_source.h
    src/hardware/simulator/simulator_bco_sample_source.cpp
)

set(SIRIUSSCOPE_INFRASTRUCTURE_INTERFACE_SOURCES
    src/infrastructure/interfaces/diagnostics_sink.h
    src/infrastructure/interfaces/result_table_storage.h
    src/infrastructure/interfaces/settings_storage.h
    src/infrastructure/interfaces/waterfall_storage.h
)

set(SIRIUSSCOPE_INFRASTRUCTURE_SOURCES
    src/infrastructure/logging/diagnostic_log_writer.h
    src/infrastructure/logging/diagnostic_log_writer.cpp
    src/infrastructure/storage/binary_result_table_storage.h
    src/infrastructure/storage/binary_result_table_storage.cpp
    src/infrastructure/storage/result_table_storage_format.h
    src/infrastructure/storage/binary_waterfall_session_storage.h
    src/infrastructure/storage/binary_waterfall_session_storage.cpp
    src/infrastructure/storage/waterfall_storage_format.h
    src/infrastructure/storage/waterfall_storage_paths.h
    src/infrastructure/storage/waterfall_storage_paths.cpp
)

add_library(siriusscope_core STATIC
    ${SIRIUSSCOPE_CORE_SOURCES}
)

target_compile_features(siriusscope_core
    PUBLIC cxx_std_20
)

target_include_directories(siriusscope_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(siriusscope_processing STATIC
    ${SIRIUSSCOPE_PROCESSING_SOURCES}
)

target_compile_features(siriusscope_processing
    PUBLIC cxx_std_20
)

target_include_directories(siriusscope_processing
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(siriusscope_processing
    PUBLIC
        siriusscope_core
)

add_library(siriusscope_pipeline STATIC
    ${SIRIUSSCOPE_PIPELINE_SOURCES}
)

target_compile_features(siriusscope_pipeline
    PUBLIC cxx_std_20
)

target_include_directories(siriusscope_pipeline
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(siriusscope_processing_iface INTERFACE)
target_link_libraries(siriusscope_processing_iface
    INTERFACE
        siriusscope_processing
)
add_library(siriusscope_infrastructure_iface INTERFACE)
add_library(siriusscope_hardware_iface INTERFACE)

target_sources(siriusscope_hardware_iface
    INTERFACE
        ${SIRIUSSCOPE_HARDWARE_INTERFACE_SOURCES}
)

target_include_directories(siriusscope_hardware_iface
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(siriusscope_hardware_iface
    INTERFACE
        siriusscope_core
)

target_sources(siriusscope_infrastructure_iface
    INTERFACE
        ${SIRIUSSCOPE_INFRASTRUCTURE_INTERFACE_SOURCES}
)

target_include_directories(siriusscope_infrastructure_iface
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(siriusscope_infrastructure_iface
    INTERFACE
        siriusscope_core
        siriusscope_processing_iface
)

target_link_libraries(siriusscope_pipeline
    PUBLIC
        Threads::Threads
        siriusscope_core
        siriusscope_processing_iface
        siriusscope_infrastructure_iface
)

add_library(siriusscope_infrastructure STATIC
    ${SIRIUSSCOPE_INFRASTRUCTURE_SOURCES}
)

target_compile_features(siriusscope_infrastructure
    PUBLIC cxx_std_20
)

target_include_directories(siriusscope_infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(siriusscope_infrastructure
    PUBLIC
        Threads::Threads
        Qt6::Core
        siriusscope_infrastructure_iface
)

add_library(siriusscope_hardware STATIC
    ${SIRIUSSCOPE_HARDWARE_ADAPTER_SOURCES}
    ${SIRIUSSCOPE_HARDWARE_FACTORY_SOURCES}
    ${SIRIUSSCOPE_HARDWARE_STUB_SOURCES}
    ${SIRIUSSCOPE_HARDWARE_SIMULATOR_SOURCES}
)

target_compile_features(siriusscope_hardware
    PUBLIC cxx_std_20
)

target_include_directories(siriusscope_hardware
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(siriusscope_hardware
    PUBLIC
        Threads::Threads
        siriusscope_hardware_iface
        siriusscope_infrastructure_iface
)

add_library(siriusscope_application STATIC
    ${SIRIUSSCOPE_APPLICATION_SOURCES}
)

target_link_libraries(siriusscope_application
    PUBLIC
        Threads::Threads
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        siriusscope_core
        siriusscope_processing_iface
        siriusscope_pipeline
        siriusscope_infrastructure_iface
        siriusscope_infrastructure
        siriusscope_hardware
)

target_compile_features(siriusscope_application
    PUBLIC cxx_std_20
)

qt_add_executable(appSiriusScope
    src/app/main.cpp
)

target_link_libraries(appSiriusScope
    PRIVATE
        siriusscope_application
)

target_include_directories(appSiriusScope
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

set_source_files_properties(
    src/ui/BandModel.qml
    src/ui/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt6_add_qml_module(appSiriusScope
    URI SiriusScope
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    SOURCES
        src/app/qmlsingletons.h
        src/app/waterfallitem.h
        src/app/waterfallitem.cpp
    QML_FILES
        src/ui/Main.qml
        src/ui/BandModel.qml
        src/ui/Theme.qml
        src/ui/components/WaterfallView.qml
        src/ui/components/MenuBarApp.qml
        src/ui/components/FooterDataView.qml
        src/ui/components/Panel.qml
        src/ui/components/ResultTablePanel.qml
        src/ui/components/StatusChip.qml
        src/ui/components/TopToolbar.qml
        src/ui/components/SpectrumView/SpectrumView.qml
        src/ui/components/SpectrumView/BandItem.qml
        src/ui/components/SpectrumView/BandSettingsDialog.qml
        src/ui/components/AntennaIndicator/ArcBand.qml
        src/ui/components/AntennaIndicator/Indicator.qml
        src/ui/components/AntennaIndicator/ScanProgressIndicator.qml
        src/ui/components/AntennaIndicator/ScanSpeedControl.qml
        src/ui/components/AntennaIndicator/TargetTracker.qml
        src/ui/components/AntennaIndicator/AntennaIndicator.qml
)

qt6_add_shaders(appSiriusScope "waterfall_shaders"
    PREFIX "/SiriusScope/shaders"
    BASE "src/ui/shaders"
    BATCHABLE
    HLSL 50
    GLSL "100es,120,150"
    MSL 12
    FILES
        src/ui/shaders/waterfall.vert
        src/ui/shaders/waterfall.frag
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appSiriusScope PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appSiriusScope
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS appSiriusScope
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
