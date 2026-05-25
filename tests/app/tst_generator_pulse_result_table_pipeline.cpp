#include "app/bandconfigcontroller.h"
#include "app/bandlistmodel.h"
#include "app/resulttablecontroller.h"
#include "app/resulttablemodel.h"
#include "hardware/simulator/simulator_antenna_state.h"
#include "hardware/simulator/simulator_bco_sample_source.h"
#include "infrastructure/interfaces/result_table_storage.h"
#include "processing/signal_parameter_accumulator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

class FakeResultTableStorage final : public infrastructure::IResultTableStorage
{
public:
    core::OperationResult append(const core::ResultTableRow& row) override
    {
        std::lock_guard lock(m_mutex);
        m_rows.push_back(row);
        return core::OperationResult::ok();
    }

    std::vector<core::ResultTableRow> readAll() override
    {
        std::lock_guard lock(m_mutex);
        return m_rows;
    }

private:
    std::mutex m_mutex;
    std::vector<core::ResultTableRow> m_rows;
};

std::vector<hardware::SimulatorPulseBandConfig> pulseConfigsFromBands(
    const app::BandListModel& model)
{
    std::vector<hardware::SimulatorPulseBandConfig> configs;
    configs.reserve(static_cast<std::size_t>(model.count()));

    for (int row = 0; row < model.count(); ++row) {
        const auto* band = model.bandAt(row);
        if (!band) {
            continue;
        }

        configs.push_back(hardware::SimulatorPulseBandConfig{
            band->config.bandIndex,
            band->config.enabled,
            band->generatorPulsePeriodUs,
            band->generatorPulseWidthUs,
        });
    }

    return configs;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

bool waitForSamplesThroughIndex(hardware::SimulatorBcoSampleSource& source,
                                std::vector<core::SignalSample>& collectedSamples,
                                std::uint64_t targetSampleIndex,
                                std::chrono::milliseconds timeout)
{
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t maxSampleIndex = 0;
    bool reachedTarget = false;

    const auto startResult = source.start([&](const hardware::BcoSampleBatch& batch) {
        std::lock_guard lock(mutex);
        collectedSamples.insert(collectedSamples.end(), batch.samples.begin(), batch.samples.end());
        for (const auto& sample : batch.samples) {
            maxSampleIndex = std::max(maxSampleIndex, sample.sampleIndex);
        }
        reachedTarget = maxSampleIndex >= targetSampleIndex;
        condition.notify_one();
    });

    if (!startResult) {
        return false;
    }

    std::unique_lock lock(mutex);
    return condition.wait_for(lock, timeout, [&] {
        return reachedTarget;
    });
}

bool nearly(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

core::BearingResult makeBearingResult(const processing::SignalParameters& parameters)
{
    const std::vector<std::int64_t> frequencies =
        parameters.frequenciesHz.empty()
            ? std::vector<std::int64_t>{3'000'000'000LL}
            : parameters.frequenciesHz;
    const auto created = core::BearingResult::create(1,
                                                     1'700'000'000'000'000'000LL,
                                                     parameters.bandIndex,
                                                     45.0,
                                                     frequencies,
                                                     0.95);
    return *created.value();
}

void testGeneratorPulseParametersReachResultTableThroughSamples(TestRunner& test)
{
    constexpr int bandIndex = 0;
    constexpr double expectedPriUs = 100'000.0;
    constexpr double expectedPwUs = 10'000.0;
    constexpr double toleranceUs = 1'000.0;
    constexpr std::uint64_t periodSamples =
        static_cast<std::uint64_t>(expectedPriUs * 1000.0
                                   / core::DomainConstraints::defaultSamplePeriodNs);
    constexpr std::uint64_t widthSamples =
        static_cast<std::uint64_t>(expectedPwUs * 1000.0
                                   / core::DomainConstraints::defaultSamplePeriodNs);
    constexpr std::uint64_t secondPulseLastSampleIndex = periodSamples + widthSamples - 1;

    app::BandListModel bandModel;
    app::BandConfigController bandController(&bandModel, nullptr, nullptr);

    const bool applied =
        bandController.applyGeneratorPulseSettings(bandIndex, expectedPriUs, expectedPwUs);
    test.require(applied, "generator pulse settings are applied through controller");

    hardware::SimulatorAntennaState antennaState(45.0);
    hardware::SimulatorBcoSampleSource sampleSource(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(1), 12'500, 0, 1},
        &antennaState);
    sampleSource.setBandConfigs(bandModel.bandConfigs());
    sampleSource.setPulseBandConfigs(pulseConfigsFromBands(bandModel));

    std::vector<core::SignalSample> samples;
    const bool collected = waitForSamplesThroughIndex(sampleSource,
                                                      samples,
                                                      secondPulseLastSampleIndex,
                                                      std::chrono::milliseconds(2000));
    sampleSource.stop();

    test.require(collected, "simulator produces samples through the second pulse window");
    test.require(!samples.empty(), "simulator samples are collected before estimation");

    std::vector<core::SignalSample> firstTwoPulses;
    for (const auto& sample : samples) {
        if (sample.bandIndex == bandIndex && sample.sampleIndex <= secondPulseLastSampleIndex) {
            firstTwoPulses.push_back(sample);
        }
    }

    processing::SignalParameterEstimatorConfig accumulatorConfig;
    accumulatorConfig.samplePeriodNs = core::DomainConstraints::defaultSamplePeriodNs;
    processing::SignalParameterAccumulator accumulator(accumulatorConfig);
    accumulator.ingest(firstTwoPulses);
    const auto estimates = accumulator.finalize();
    const auto parameters = std::find_if(estimates.begin(), estimates.end(), [](const auto& item) {
        return item.bandIndex == bandIndex;
    });

    test.require(parameters != estimates.end(), "accumulator produces band 0 signal parameters");
    if (parameters == estimates.end()) {
        return;
    }

    test.require(parameters->pulseCount >= 2, "accumulator sees at least two pulse windows");
    test.require(parameters->pulseRepetitionPeriodUs
                     && nearly(*parameters->pulseRepetitionPeriodUs,
                               expectedPriUs,
                               toleranceUs),
                 "accumulator calculates PRI from generated samples");
    test.require(nearly(parameters->pulseWidthUs, expectedPwUs, toleranceUs),
                 "accumulator calculates PW from generated samples");

    app::ResultTableModel resultModel;
    FakeResultTableStorage storage;
    app::ResultTableController resultController(&resultModel, &storage, nullptr);

    app::ResultTableAppendContext context;
    context.scanSessionId = 1;
    context.antennaAzimuthDeg = 45.0;
    context.signalParameters = {*parameters};

    const auto appendResult =
        resultController.appendBearingResults(context, {makeBearingResult(*parameters)});
    test.require(appendResult.success, "result table accepts bearing result with estimated parameters");
    test.require(resultController.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "result table worker finishes");
    test.require(waitUntil([&resultModel] {
                     return resultModel.count() == 1;
                 }),
                 "result table model receives appended row");

    if (resultModel.rows().empty()) {
        return;
    }

    const auto& row = resultModel.rows().front();
    test.require(row.bandIndex == bandIndex, "result table row belongs to band 0");
    test.require(row.pulseRepetitionPeriodUs.has_value(),
                 "result table row contains estimated PRI");
    test.require(row.pulseRepetitionPeriodUs
                     && nearly(*row.pulseRepetitionPeriodUs, expectedPriUs, toleranceUs),
                 "result table row PRI follows estimator output");
    test.require(row.pulseWidthUs.has_value(), "result table row contains estimated PW");
    test.require(row.pulseWidthUs && nearly(*row.pulseWidthUs, expectedPwUs, toleranceUs),
                 "result table row PW follows estimator output");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testGeneratorPulseParametersReachResultTableThroughSamples(test);

    return test.result();
}
