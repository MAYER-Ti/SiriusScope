#pragma once

#include "antennacontrollerstub.h"
#include "bandconfigcontroller.h"
#include "bandlistmodel.h"
#include "bearingframebus.h"
#include "diagnosticsservice.h"
#include "frequencygridmodel.h"
#include "frequencyviewportmodel.h"
#include "inmemoryscanacquisitionrecorder.h"
#include "interfaces/processing_flush_control.h"
#include "interfaces/result_table_sink.h"
#include "interfaces/scan_acquisition_recorder.h"
#include "interfaces/scan_recording_control.h"
#include "nullresulttablesink.h"
#include "processing/bearing_service.h"
#include "scancontroller.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"
#include "statusmodel.h"
#include "waterfallcontroller.h"
#include "waterfallscanrecordingadapter.h"
#include "waterfallstorage.h"

#include "hardware/interfaces/antenna_azimuth_source.h"
#include "hardware/interfaces/antenna_control.h"
#include "hardware/interfaces/bco_sample_source.h"
#include "hardware/interfaces/bco_control.h"
#include "hardware/simulator/simulator_antenna_azimuth_source.h"
#include "hardware/simulator/simulator_antenna_control.h"
#include "hardware/simulator/simulator_antenna_state.h"
#include "hardware/simulator/simulator_bco_control.h"
#include "hardware/simulator/simulator_bco_sample_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/waterfall_storage.h"
#include "infrastructure/logging/diagnostic_log_writer.h"

#include <memory>

namespace siriusscope::app {

class ApplicationBootstrap
{
public:
    ApplicationBootstrap();

    void registerQmlSingletons();

    FrequencyViewportModel* frequencyViewportModel() noexcept { return &m_viewportModel; }
    FrequencyGridModel* frequencyGridModel() noexcept { return &m_frequencyGridModel; }
    SpectrumControllerStub* spectrumController() noexcept { return &m_spectrumController; }
    SpectrumDecimator* spectrumDecimator() noexcept { return &m_spectrumDecimator; }
    WaterfallController* waterfallController() noexcept { return m_waterfallController.get(); }
    AntennaControllerStub* antennaController() noexcept { return &m_antennaController; }
    BandListModel* bandListModel() noexcept { return &m_bandListModel; }
    BandConfigController* bandConfigController() noexcept { return &m_bandConfigController; }
    DiagnosticsService* diagnosticsService() noexcept { return m_diagnosticsService.get(); }
    StatusModel* statusModel() noexcept { return m_statusModel.get(); }
    ScanController* scanController() noexcept { return m_scanController.get(); }
    BearingFrameBus* bearingFrameBus() noexcept { return m_bearingFrameBus.get(); }
    IScanAcquisitionRecorder* scanAcquisitionRecorder() noexcept
    {
        return m_scanAcquisitionRecorder.get();
    }
    IProcessingFlushControl* processingFlushControl() noexcept
    {
        return m_waterfallController.get();
    }
    IScanRecordingControl* scanRecordingControl() noexcept
    {
        return m_scanRecordingControl.get();
    }
    IResultTableSink* resultTableSink() noexcept
    {
        return m_resultTableSink.get();
    }

    infrastructure::IDiagnosticsSink* diagnosticsSink() noexcept
    {
        return m_diagnosticsService.get();
    }

    infrastructure::IWaterfallStorage* waterfallStorage() noexcept
    {
        return m_waterfallStorage.get();
    }

    hardware::IBcoControl* bcoControl() noexcept
    {
        return m_bcoControl.get();
    }

    hardware::IBcoSampleSource* bcoSampleSource() noexcept
    {
        return m_bcoSampleSource.get();
    }

    hardware::IAntennaControl* antennaControl() noexcept
    {
        return m_antennaControl.get();
    }

    hardware::IAntennaAzimuthSource* antennaAzimuthSource() noexcept
    {
        return m_antennaAzimuthSource.get();
    }

private:
    FrequencyViewportModel m_viewportModel;
    FrequencyGridModel m_frequencyGridModel;
    SpectrumControllerStub m_spectrumController;
    SpectrumDecimator m_spectrumDecimator;
    AntennaControllerStub m_antennaController;
    std::unique_ptr<infrastructure::DiagnosticLogWriter> m_diagnosticLogWriter;
    std::unique_ptr<DiagnosticsService> m_diagnosticsService;
    std::unique_ptr<infrastructure::IWaterfallStorage> m_waterfallStorage;
    std::unique_ptr<IWaterfallSessionStorage> m_waterfallSessionStorage;
    std::unique_ptr<hardware::SimulatorBcoSampleSource> m_bcoSampleSource;
    std::unique_ptr<hardware::SimulatorAntennaState> m_antennaState;
    std::unique_ptr<hardware::SimulatorAntennaAzimuthSource> m_antennaAzimuthSource;
    std::unique_ptr<hardware::IBcoControl> m_bcoControl;
    std::unique_ptr<hardware::IAntennaControl> m_antennaControl;
    std::unique_ptr<BearingFrameBus> m_bearingFrameBus;
    std::unique_ptr<processing::BearingService> m_bearingService;
    std::unique_ptr<IScanAcquisitionRecorder> m_scanAcquisitionRecorder;
    std::unique_ptr<IScanRecordingControl> m_scanRecordingControl;
    std::unique_ptr<IResultTableSink> m_resultTableSink;
    BandListModel m_bandListModel;
    BandConfigController m_bandConfigController;
    std::unique_ptr<WaterfallController> m_waterfallController;
    std::unique_ptr<ScanController> m_scanController;
    std::unique_ptr<StatusModel> m_statusModel;
};

} // namespace siriusscope::app
