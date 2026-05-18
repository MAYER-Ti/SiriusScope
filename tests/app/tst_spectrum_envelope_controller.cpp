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
    test.require(sampleAt(controller, bin) == 100.0, "sample amplitude maps to expected bin");
}

void testMaxAmplitudeAcrossBeams(TestRunner& test)
{
    app::SpectrumEnvelopeController controller(staticConfig());
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);

    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 40, 0),
                                                     makeSample(0, 100, 1)}});

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    test.require(sampleAt(controller, bin) == 100.0,
                 "bin stores maximum amplitude across beams");
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

void testDecayReducesAndClearsEnvelope(TestRunner& test)
{
    app::SpectrumEnvelopeControllerConfig config;
    config.decayIntervalMs = 5;
    config.decayPerSecond = 1000.0;
    app::SpectrumEnvelopeController controller(config);
    controller.setViewport(2'750'000'000.0, 3'250'000'000.0);
    controller.ingestBatch(hardware::BcoSampleBatch{{makeSample(0, 100)}});

    const auto bin = expectedBin(2'750'000'000.0, 3'250'000'000.0, 3'000'000'000LL);
    const bool decreased = waitUntil([&] {
        return sampleAt(controller, bin) < 100.0;
    }, 200);
    const bool cleared = waitUntil([&] {
        return sampleAt(controller, bin) == 0.0;
    }, 1000);

    test.require(decreased, "decay decreases envelope amplitude");
    test.require(cleared, "decay eventually clears envelope amplitude");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testSampleMapsToFrequencyBin(test);
    testMaxAmplitudeAcrossBeams(test);
    testOutOfViewportSamplesIgnored(test);
    testViewportChangeClearsEnvelope(test);
    testDecayReducesAndClearsEnvelope(test);

    return test.result();
}
