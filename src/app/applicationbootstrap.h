#pragma once

#include "antennacontrollerstub.h"
#include "bandconfigcontroller.h"
#include "bandlistmodel.h"
#include "frequencygridmodel.h"
#include "frequencyviewportmodel.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"
#include "waterfallcontrollerstub.h"

#include "hardware/interfaces/antenna_control.h"
#include "hardware/interfaces/bco_control.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/waterfall_storage.h"

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
    WaterfallControllerStub* waterfallController() noexcept { return &m_waterfallController; }
    AntennaControllerStub* antennaController() noexcept { return &m_antennaController; }
    BandListModel* bandListModel() noexcept { return &m_bandListModel; }
    BandConfigController* bandConfigController() noexcept { return &m_bandConfigController; }

    infrastructure::IDiagnosticsSink* diagnosticsSink() noexcept
    {
        return m_diagnosticsSink.get();
    }

    infrastructure::IWaterfallStorage* waterfallStorage() noexcept
    {
        return m_waterfallStorage.get();
    }

    hardware::IBcoControl* bcoControl() noexcept
    {
        return m_bcoControl.get();
    }

    hardware::IAntennaControl* antennaControl() noexcept
    {
        return m_antennaControl.get();
    }

private:
    FrequencyViewportModel m_viewportModel;
    FrequencyGridModel m_frequencyGridModel;
    SpectrumControllerStub m_spectrumController;
    SpectrumDecimator m_spectrumDecimator;
    WaterfallControllerStub m_waterfallController;
    AntennaControllerStub m_antennaController;
    std::unique_ptr<infrastructure::IDiagnosticsSink> m_diagnosticsSink;
    std::unique_ptr<infrastructure::IWaterfallStorage> m_waterfallStorage;
    std::unique_ptr<hardware::IBcoControl> m_bcoControl;
    std::unique_ptr<hardware::IAntennaControl> m_antennaControl;
    BandListModel m_bandListModel;
    BandConfigController m_bandConfigController;
};

} // namespace siriusscope::app
