cmake_minimum_required(VERSION 3.16)

project(SiriusScope VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick ShaderTools)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appSiriusScope
    src/app/main.cpp
    src/app/appstate.h
    src/app/appstate.cpp
    src/app/frequencyviewportmodel.h
    src/app/frequencyviewportmodel.cpp
    src/app/spectrumcontrollerstub.h
    src/app/spectrumcontrollerstub.cpp
    src/app/spectrumdecimator.h
    src/app/spectrumdecimator.cpp
    src/app/waterfallcontrollerstub.h
    src/app/waterfallcontrollerstub.cpp
    src/app/waterfallitem.h
    src/app/waterfallitem.cpp
    src/app/waterfallringbuffer.h
    src/app/waterfallringbuffer.cpp
)

qt_add_qml_module(appSiriusScope
    URI SiriusScope
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/BandModel.qml
        src/ui/components/WaterfallView.qml
        src/ui/components/MenuBarApp.qml
        src/ui/components/FooterDataView.qml
        src/ui/components/SpectrumView/SpectrumView.qml
        src/ui/components/SpectrumView/BandItem.qml
        src/ui/components/AntennaIndicator/Indicator.qml
        src/ui/components/AntennaIndicator/TargetTracker.qml
        src/ui/components/AntennaIndicator/AntennaIndicator.qml
)

qt_add_shaders(appSiriusScope "waterfall_shaders"
    PREFIX "/SiriusScope/shaders"
    BASE "src/ui/shaders"
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

target_link_libraries(appSiriusScope
    PRIVATE Qt6::Quick
)

target_compile_features(appSiriusScope
    PRIVATE cxx_std_20
)

include(GNUInstallDirs)
install(TARGETS appSiriusScope
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
