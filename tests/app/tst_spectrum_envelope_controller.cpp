#include "app/spectrumenvelopecontroller.h"

#include "core/domain_models.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

namespace {

constexpr int kExpectedSpreadRadius = 24;
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
    return static_cast<std::int64_t>(std::llround(minHz + (static_cast<double>(bin) + 0.5) * binWidthHz));
}

bool nearlyEqual(double left, double right, double epsilon = 1.0e-6)
{
    return std::abs(left - right) <= epsilon;
}

double expectedSpreadAmplitude(double amplitude, int offset)
{
    const double distance = static_cast<double>(std::abs(offset));
    return amplitude
        * 0.5
        * (1.0 + std::cos(kExpectedPi * distance / static_cast<double>(kExpectedSpreadRadius + 1)));
}

double sampleAt(const app::SpectrumEnvelopeController& controller, std::size_t index)
{
    const QVariantList samples = controller.envelopeSamples();
    if (index >= static_cast<std::size_t>(samples.size())) {
        return 0.0;
    }
    return samples.at(static_cast<qsizetype>(index)).toDouble();
}

double maxSample(const app::SpectrumEnvelopeController& controller)
{
    const QVariantList samples = controller.envelopeSamples();
    double maxValue = 0.0;
    for (const auto& sample : samples) {
        maxValue = std::max(maxValue, sample.toDouble());
    }
    return maxValue;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 1000)
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

app::SpectrumEnvelopeControllerConfig staticConfig()
{
    app::SpectrumEnvelopeControllerConfig config;
    config.decayPerSecond = 0.0;
    return config;
}

void testSampleMapsToFrequencyBin(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);

    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    test.require(controller.envelopeSamples().size() == 1024, "envelope has 1024 bins");
    test.require(nearlyEqual(sampleAt(controller, bin), 100.0),
                 "sample amplitude maps to expected bin");
}

void testSampleSpreadsToNeighborBins(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);

    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    test.require(nearlyEqual(sampleAt(controller, bin - 24), expectedSpreadAmplitude(100.0, -24)),
                 "spread radius -24 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin - 12), expectedSpreadAmplitude(100.0, -12)),
                 "spread radius -12 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin - 1), expectedSpreadAmplitude(100.0, -1)),
                 "spread radius -1 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin + 1), expectedSpreadAmplitude(100.0, 1)),
                 "spread radius +1 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin + 12), expectedSpreadAmplitude(100.0, 12)),
                 "spread radius +12 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin + 24), expectedSpreadAmplitude(100.0, 24)),
                 "spread radius +24 uses raised-cosine weight");
    test.require(nearlyEqual(sampleAt(controller, bin + kExpectedSpreadRadius + 1), 0.0),
                 "spread does not reach outside radius 24");
}

void testMaxAmplitudeAcrossBeams(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    const double minHz = 2'750'000'000.0;
    const double maxHz = 3'250'000'000.0;
    controller.setViewport(minHz, maxHz);

    const auto bin = expectedBin(minHz, maxHz, 3'000'000'000LL);
    const auto adjacentFrequency = frequencyForBinCenter(minHz, maxHz, bin + 1);

    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 40, 0),
                                                     makeSample(0, 100, 1),
                                                     makeSample(adjacentFrequency - 3'000'000'000LL,
                                                                100,
                                                                0)}});

    test.require(nearlyEqual(sampleAt(controller, bin), 100.0),
                 "bin stores maximum amplitude across beams");
    test.require(nearlyEqual(sampleAt(controller, bin + 1), 100.0),
                 "neighbor bin keeps maximum of direct sample and spread");
}

void testOutOfViewportSamplesIgnored(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    controller.setViewport(4'000'000'000.0, 4'500'000'000.0);

    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    test.require(maxSample(controller) == 0.0, "out-of-viewport sample is ignored");
}

void testViewportChangeClearsEnvelope(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);
    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    controller.setViewport(4'000'000'000.0, 4'500'000'000.0);

    test.require(maxSample(controller) == 0.0, "viewport change clears envelope");
}

void testRepeatedSignalStaysStableDuringHold(TestRunner& test)
{
    app::SpectrumEnvelopeControllerConfig config;
    config.decayIntervalMs = 5;
    config.decayPerSecond = 1000.0;
    app::SpectrumEnvelopeController controller(config);
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);

    const hardware::BcoSampleBatch batch{{makeSample(0, 100)}};
    controller.ingestBatch(batch);
    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);

    processEventsFor(120);
    test.require(nearlyEqual(sampleAt(controller, bin), 100.0),
                 "hold prevents decay before the refresh timeout");

    controller.ingestBatch(batch);
    processEventsFor(180);

    test.require(nearlyEqual(sampleAt(controller, bin), 100.0),
                 "repeated identical batch refreshes bin freshness without amplitude jitter");
    test.require(nearlyEqual(sampleAt(controller, bin - 1), expectedSpreadAmplitude(100.0, -1)),
                 "spread bin remains stable while signal is continuously refreshed");
}

void testDecayStartsAfterHoldAndClearsEnvelope(TestRunner& test)
{
    app::SpectrumEnvelopeControllerConfig config;
    config.decayIntervalMs = 5;
    config.decayPerSecond = 1000.0;
    app::SpectrumEnvelopeController controller(config);
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);
    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    processEventsFor(120);
    test.require(nearlyEqual(sampleAt(controller, bin), 100.0),
                 "envelope does not decay before hold expires");

    const bool decreased = waitUntil([&] {
        return sampleAt(controller, bin) < 100.0
            && sampleAt(controller, bin - 1) < expectedSpreadAmplitude(100.0, -1);
    }, 700);
    const bool cleared = waitUntil([&] {
        return maxSample(controller) == 0.0;
    }, 1000);

    test.require(decreased, "decay decreases envelope amplitude after hold expires");
    test.require(cleared, "decay eventually clears envelope amplitude");
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

    return test.result();
}
