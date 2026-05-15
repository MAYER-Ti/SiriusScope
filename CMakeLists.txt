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
qt_standard_project_setup(REQUIRES 6.8)

set(SIRIUSSCOPE_APPLICATION_SOURCES
    src/app/syntheticwaterfalldatasource.h
    src/app/syntheticwaterfalldatasource.cpp
    src/app/waterfallcolormapper.h
    src/app/waterfallcolormapper.cpp
    src/app/waterfallrowresampler.h
    src/app/waterfallrowresampler.cpp
    src/app/waterfallhistorymodel.h
    src/app/waterfallhistorymodel.cpp
    src/app/waterfallringbuffer.h
    src/app/waterfallringbuffer.cpp
    src/app/waterfallstorage.h
    src/app/waterfallstorage.cpp
)

set(SIRIUSSCOPE_CORE_SOURCES
    src/core/domain_constraints.h
    src/core/domain_constraints.cpp
    src/core/domain_models.h
    src/core/domain_models.cpp
    src/core/domain_validation.h
    src/core/layer_placeholder.cpp
)

set(SIRIUSSCOPE_PROCESSING_SOURCES
    src/processing/sample_processor.h
    src/processing/sample_processor.cpp
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

add_library(siriusscope_processing_iface INTERFACE)
target_link_libraries(siriusscope_processing_iface
    INTERFACE
        siriusscope_processing
)
add_library(siriusscope_infrastructure_iface INTERFACE)
add_library(siriusscope_hardware_iface INTERFACE)

add_library(siriusscope_application STATIC
    ${SIRIUSSCOPE_APPLICATION_SOURCES}
)

target_link_libraries(siriusscope_application
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        siriusscope_core
        siriusscope_processing_iface
        siriusscope_infrastructure_iface
        siriusscope_hardware_iface
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
        src/app/appstate.h
        src/app/appstate.cpp
        src/app/frequencygridmodel.h
        src/app/frequencygridmodel.cpp
        src/app/frequencyviewportmodel.h
        src/app/frequencyviewportmodel.cpp
        src/app/qmlsingletons.h
        src/app/spectrumcontrollerstub.h
        src/app/spectrumcontrollerstub.cpp
        src/app/spectrumdecimator.h
        src/app/spectrumdecimator.cpp
        src/app/waterfallcontrollerstub.h
        src/app/waterfallcontrollerstub.cpp
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
        src/ui/components/AntennaIndicator/Indicator.qml
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
