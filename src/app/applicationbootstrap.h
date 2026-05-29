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
#include "processing/bearing_service.h"
#include "recordingcontroller.h"
#include "resulttablecontroller.h"
#include "resulttablemodel.h"
#include "scancontroller.h"
#include "signalsamplebus.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"
#include "spectrumenvelopecontroller.h"
#include "spectrumsnapshotadapter.h"
#include "statusmodel.h"
#include "waterfallcontroller.h"
#include "waterfallscanrecordingadapter.h"
#include "waterfallstorage.h"

#include "hardware/data_source_factory.h"
#include "hardware/hardware_profile.h"
#include "hardware/interfaces/antenna_azimuth_source.h"
#include "hardware/interfaces/antenna_control.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "hardware/interfaces/bco_control.h"
#include "hardware/simulator/simulator_antenna_azimuth_source.h"
#include "hardware/simulator/simulator_antenna_control.h"
#include "hardware/simulator/simulator_antenna_state.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/result_table_storage.h"
#include "infrastructure/interfaces/waterfall_storage.h"
#include "infrastructure/logging/diagnostic_log_writer.h"
#include "pipeline/data_ingest_pipeline.h"

#include <memory>

namespace siriusscope::app {

class ApplicationBootstrap
{
public:
    ApplicationBootstrap();
    ~ApplicationBootstrap();

    void registerQmlSingletons();

    FrequencyViewportModel* frequencyViewportModel() noexcept { return &m_viewportModel; }
    FrequencyGridModel* frequencyGridModel() noexcept { return &m_frequencyGridModel; }
    SpectrumControllerStub* spectrumController() noexcept { return &m_spectrumController; }
    SpectrumDecimator* spectrumDecimator() noexcept { return &m_spectrumDecimator; }
    SpectrumEnvelopeController* spectrumEnvelopeController() noexcept
    {
        return &m_spectrumEnvelopeController;
    }
    SpectrumSnapshotAdapter* spectrumSnapshotAdapter() noexcept
    {
        return m_spectrumSnapshotAdapter.get();
    }
    WaterfallController* waterfallController() noexcept { return m_waterfallController.get(); }
    AntennaControllerStub* antennaController() noexcept { return &m_antennaController; }
    BandListModel* bandListModel() noexcept { return &m_bandListModel; }
    BandConfigController* bandConfigController() noexcept { return m_bandConfigController.get(); }
    DiagnosticsService* diagnosticsService() noexcept { return m_diagnosticsService.get(); }
    StatusModel* statusModel() noexcept { return m_statusModel.get(); }
    RecordingController* recordingController() noexcept { return m_recordingController.get(); }
    ScanController* scanController() noexcept { return m_scanController.get(); }
    ResultTableModel* resultTableModel() noexcept { return m_resultTableModel.get(); }
    ResultTableController* resultTableController() noexcept
    {
        return m_resultTableController.get();
    }
    BearingFrameBus* bearingFrameBus() noexcept { return m_bearingFrameBus.get(); }
    SignalSampleBus* signalSampleBus() noexcept { return m_signalSampleBus.get(); }
    pipeline::DataIngestPipeline* dataIngestPipeline() noexcept
    {
        return m_dataIngestPipeline.get();
    }
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
        return m_resultTableController.get();
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

    hardware::IBcoStreamSource* bcoStreamSource() noexcept
    {
        return m_bcoStreamSource.get();
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
    hardware::BcoStreamConfig makeBcoStreamConfig() const;
    hardware::HardwareProfile makeDefaultHardwareProfile() const;
    void createBcoStreamSource();
    void configureBcoStreamSource();

    FrequencyViewportModel m_viewportModel;
    FrequencyGridModel m_frequencyGridModel;
    SpectrumControllerStub m_spectrumController;
    SpectrumDecimator m_spectrumDecimator;
    SpectrumEnvelopeController m_spectrumEnvelopeController;
    std::unique_ptr<SpectrumSnapshotAdapter> m_spectrumSnapshotAdapter;
    AntennaControllerStub m_antennaController;
    std::unique_ptr<infrastructure::DiagnosticLogWriter> m_diagnosticLogWriter;
    std::unique_ptr<DiagnosticsService> m_diagnosticsService;
    std::unique_ptr<infrastructure::IWaterfallStorage> m_waterfallStorage;
    std::unique_ptr<IWaterfallSessionStorage> m_waterfallSessionStorage;
    std::unique_ptr<hardware::SimulatorAntennaState> m_antennaState;
    hardware::HardwareProfile m_hardwareProfile;
    std::unique_ptr<hardware::IBcoStreamSource> m_bcoStreamSource;
    std::unique_ptr<hardware::SimulatorAntennaAzimuthSource> m_antennaAzimuthSource;
    std::unique_ptr<hardware::IBcoControl> m_bcoControl;
    std::unique_ptr<hardware::IAntennaControl> m_antennaControl;
    std::unique_ptr<BearingFrameBus> m_bearingFrameBus;
    std::unique_ptr<SignalSampleBus> m_signalSampleBus;
    std::unique_ptr<pipeline::DataIngestPipeline> m_dataIngestPipeline;
    std::unique_ptr<processing::BearingService> m_bearingService;
    std::unique_ptr<IScanAcquisitionRecorder> m_scanAcquisitionRecorder;
    std::unique_ptr<IScanRecordingControl> m_scanRecordingControl;
    std::unique_ptr<infrastructure::IResultTableStorage> m_resultTableStorage;
    std::unique_ptr<ResultTableModel> m_resultTableModel;
    std::unique_ptr<ResultTableController> m_resultTableController;
    BandListModel m_bandListModel;
    std::unique_ptr<BandConfigController> m_bandConfigController;
    std::unique_ptr<WaterfallController> m_waterfallController;
    std::unique_ptr<RecordingController> m_recordingController;
    std::unique_ptr<ScanController> m_scanController;
    std::unique_ptr<StatusModel> m_statusModel;
};

} // namespace siriusscope::app
