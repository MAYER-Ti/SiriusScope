#include "app/spectrumenvelopecontroller.h"
#include "processing/spectrum_envelope_processor.h"

#include "core/domain_models.h"
#include "hardware/interfaces/bco_sample_source.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QVector>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kExpectedSpreadRadius = 8;
constexpr double kExpectedPi = 3.14159265358979323846;

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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

core::BandConfig makeBand()
{
    const auto created = core::BandConfig::create(0, 3'000'000'000LL, 500'000'000LL);
    return *created.value();
}

core::SignalSample makeSample(std::int64_t offsetHz,
                              int amplitude,
                              int beamIndex = 0,
                              std::uint64_t sampleIndex = 1)
{
    const auto created =
        core::SignalSample::create(core::BeamSample{sampleIndex, offsetHz, amplitude, beamIndex},
                                   makeBand());
    return *created.value();
}

std::size_t expectedBin(double minHz, double maxHz, std::int64_t frequencyHz, int binCount = 1024)
{
    const double ratio = (static_cast<double>(frequencyHz) - minHz) / (maxHz - minHz);
    auto bin = static_cast<std::size_t>(std::floor(ratio * static_cast<double>(binCount)));
    return std::min<std::size_t>(bin, static_cast<std::size_t>(binCount - 1));
}

std::int64_t frequencyForBinCenter(double minHz, double maxHz, std::size_t bin, int binCount = 1024)
{
    const double binWidthHz = (maxHz - minHz) / static_cast<double>(binCount);
    return static_cast<std::int64_t>(
        std::llround(minHz + (static_cast<double>(bin) + 0.5) * binWidthHz));
}

bool nearlyEqual(double left, double right, double epsilon = 1.0e-5)
{
    return std::abs(left - right) <= epsilon;
}

double expectedSpreadAmplitude(double amplitude, int offset)
{
    const double distance = static_cast<double>(std::abs(offset));
    return amplitude
        * 0.5
        * (1.0
           + std::cos(kExpectedPi * distance / static_cast<double>(kExpectedSpreadRadius + 1)));
}

double sampleAt(const processing::SpectrumEnvelopeProcessor& processor, std::size_t index)
{
    const auto& samples = processor.samples();
    if (index >= samples.size()) {
        return 0.0;
    }
    return samples[index];
}

double maxSample(const processing::SpectrumEnvelopeProcessor& processor)
{
    const auto& samples = processor.samples();
    return samples.empty() ? 0.0F : *std::max_element(samples.cbegin(), samples.cend());
}

double maxControllerSample(const app::SpectrumEnvelopeController& controller)
{
    const QVariantList samples = controller.envelopeSamples();
    double maxValue = 0.0;
    for (const auto& sample : samples) {
        maxValue = std::max(maxValue, sample.toDouble());
    }
    return maxValue;
}

void processEventsFor(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

processing::SpectrumEnvelopeProcessorConfig staticConfig()
{
    processing::SpectrumEnvelopeProcessorConfig config;
    config.decayPerSecond = 0.0;
    return config;
}

void testSampleMapsToFrequencyBin(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);

    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 100)}}.samples, 0);

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    test.require(processor.samples().size() == 1024, "envelope has 1024 bins");
    test.require(nearlyEqual(sampleAt(processor, bin), 100.0),
                 "sample amplitude maps to expected bin");
}

void testSampleSpreadsToNeighborBins(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);

    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 100)}}.samples, 0);

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    test.require(nearlyEqual(sampleAt(processor, bin - 8), expectedSpreadAmplitude(100.0, -8)),
                 "spread radius -8 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin - 4), expectedSpreadAmplitude(100.0, -4)),
                 "spread radius -4 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin - 1), expectedSpreadAmplitude(100.0, -1)),
                 "spread radius -1 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin + 1), expectedSpreadAmplitude(100.0, 1)),
                 "spread radius +1 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin + 4), expectedSpreadAmplitude(100.0, 4)),
                 "spread radius +4 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin + 8), expectedSpreadAmplitude(100.0, 8)),
                 "spread radius +8 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(processor, bin + kExpectedSpreadRadius + 1), 0.0),
                 "spread does not reach outside radius 8");
}

void testMaxAmplitudeAcrossBeams(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    const double minHz = 2'750'000'000.0;
    const double maxHz = 3'250'000'000.0;
    processor.setViewport(minHz, maxHz);

    const auto bin = expectedBin(minHz, maxHz, 3'000'000'000LL);
    const auto adjacentFrequency = frequencyForBinCenter(minHz, maxHz, bin + 1);

    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 40, 0),
                                                      makeSample(0, 100, 1),
                                                      makeSample(adjacentFrequency - 3'000'000'000LL,
                                                                 100,
                                                                 0)}}.samples,
                            0);

    test.require(nearlyEqual(sampleAt(processor, bin), 100.0),
                 "bin stores maximum amplitude across beams");
    test.require(nearlyEqual(sampleAt(processor, bin + 1), 100.0),
                 "neighbor bin keeps maximum of direct sample and spread");
}

void testOutOfViewportSamplesIgnored(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    processor.setViewport(4'000'000'000.0, 4'500'000'000.0);

    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 100)}}.samples, 0);

    test.require(maxSample(processor) == 0.0, "out-of-viewport sample is ignored");
}

void testViewportChangeClearsEnvelope(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);
    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 100)}}.samples, 0);

    processor.setViewport(4'000'000'000.0, 4'500'000'000.0);

    test.require(maxSample(processor) == 0.0, "viewport change clears envelope");
}

void testRepeatedSignalStaysStableDuringHold(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessorConfig config;
    config.decayPerSecond = 1000.0;
    processing::SpectrumEnvelopeProcessor processor(config);
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);

    const hardware::BcoSampleBatch batch{{makeSample(0, 100)}};
    processor.ingestSamples(batch.samples, 0);
    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);

    processor.applyDecay(120, 120);
    test.require(nearlyEqual(sampleAt(processor, bin), 100.0),
                 "hold prevents decay before the refresh timeout");

    processor.ingestSamples(batch.samples, 120);
    processor.applyDecay(180, 300);

    test.require(nearlyEqual(sampleAt(processor, bin), 100.0),
                 "repeated identical batch refreshes bin freshness without amplitude jitter");
    test.require(nearlyEqual(sampleAt(processor, bin - 1), expectedSpreadAmplitude(100.0, -1)),
                 "spread bin remains stable while signal is continuously refreshed");
}

void testDecayStartsAfterHoldAndClearsEnvelope(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessorConfig config;
    config.decayPerSecond = 100.0;
    processing::SpectrumEnvelopeProcessor processor(config);
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);
    processor.ingestSamples(hardware::BcoSampleBatch{{makeSample(0, 100)}}.samples, 0);

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    processor.applyDecay(120, 120);
    test.require(nearlyEqual(sampleAt(processor, bin), 100.0),
                 "envelope does not decay before hold expires");

    processor.applyDecay(260, 260);
    test.require(sampleAt(processor, bin) < 100.0 && sampleAt(processor, bin) > 0.0,
                 "decay decreases envelope amplitude after hold expires");

    for (int step = 0; step < 20 && maxSample(processor) > 0.0; ++step) {
        processor.applyDecay(100, 360 + step * 100);
    }

    test.require(maxSample(processor) == 0.0, "decay eventually clears envelope amplitude");
}

void testControllerPublishesThrottledSnapshots(TestRunner& test)
{
    app::SpectrumEnvelopeControllerConfig config;
    config.publishIntervalMs = 66;
    app::SpectrumEnvelopeController controller(config);

    int publishCount = 0;
    QObject::connect(&controller,
                     &app::SpectrumEnvelopeController::envelopeChanged,
                     [&](double, double, const QVariantList&) {
                         ++publishCount;
                     });

    QVector<float> samples(1024, 0.0F);
    for (int index = 0; index < 10; ++index) {
        samples[5] = static_cast<float>(index + 1);
        controller.acceptSnapshot(1.0, 2.0, samples);
        processEventsFor(10);
    }
    processEventsFor(120);

    test.require(publishCount >= 2, "controller publishes throttled snapshots eventually");
    test.require(publishCount <= 5, "controller does not publish every incoming snapshot");
    test.require(nearlyEqual(maxControllerSample(controller), 10.0),
                 "controller publishes the latest pending snapshot");
}

void testIngestBatchPerfSmoke(TestRunner& test)
{
    processing::SpectrumEnvelopeProcessor processor(staticConfig());
    processor.setViewport(2'750'000'000.0, 3'250'000'000.0);

    hardware::BcoSampleBatch batch;
    batch.samples.reserve(128);
    for (int index = 0; index < 128; ++index) {
        const auto offset = static_cast<std::int64_t>((index % 64) - 32) * 1'000'000LL;
        batch.samples.push_back(makeSample(offset, 80 + (index % 30), index % 2, index));
    }

    QElapsedTimer timer;
    timer.start();
    for (int iteration = 0; iteration < 100; ++iteration) {
        processor.ingestSamples(batch.samples, iteration * 10);
    }
    const double averageMs = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0 / 100.0;
    std::cout << "SpectrumEnvelopeProcessor average ingest time: "
              << averageMs << " ms\n";

    test.require(maxSample(processor) > 0.0, "perf smoke keeps a valid envelope");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testSampleMapsToFrequencyBin(test);
    testSampleSpreadsToNeighborBins(test);
    testMaxAmplitudeAcrossBeams(test);
    testOutOfViewportSamplesIgnored(test);
    testViewportChangeClearsEnvelope(test);
    testRepeatedSignalStaysStableDuringHold(test);
    testDecayStartsAfterHoldAndClearsEnvelope(test);
    testControllerPublishesThrottledSnapshots(test);
    testIngestBatchPerfSmoke(test);

    return test.result();
}
