#include "app/signalsamplebus.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

siriusscope::core::SignalSample makeSample(std::uint64_t sampleIndex)
{
    siriusscope::core::SignalSample sample;
    sample.sampleIndex = sampleIndex;
    sample.bandIndex = 0;
    sample.frequencyOffsetHz = 1'000'000;
    sample.absoluteFrequencyHz = 1'001'000'000LL;
    sample.amplitude = 80;
    sample.beamIndex = 0;
    return sample;
}

void testPublishDeliversSamples(TestRunner& test)
{
    siriusscope::app::SignalSampleBus bus;
    std::vector<siriusscope::core::SignalSample> received;
    bus.subscribe([&received](std::vector<siriusscope::core::SignalSample> samples) {
        received = std::move(samples);
    });

    bus.publish({makeSample(1), makeSample(2)});

    test.require(received.size() == 2, "publish delivers two samples to subscriber");
    test.require(received[0].sampleIndex == 1 && received[1].sampleIndex == 2,
                 "publish preserves sample payload");
}

void testUnsubscribeStopsDelivery(TestRunner& test)
{
    siriusscope::app::SignalSampleBus bus;
    int deliveryCount = 0;
    const int subscriptionId =
        bus.subscribe([&deliveryCount](std::vector<siriusscope::core::SignalSample>) {
            ++deliveryCount;
        });

    bus.publish({makeSample(1)});
    bus.unsubscribe(subscriptionId);
    bus.publish({makeSample(2)});

    test.require(deliveryCount == 1, "unsubscribe stops delivery");
}

void testEmptyPublicationIgnored(TestRunner& test)
{
    siriusscope::app::SignalSampleBus bus;
    int deliveryCount = 0;
    bus.subscribe([&deliveryCount](std::vector<siriusscope::core::SignalSample>) {
        ++deliveryCount;
    });

    bus.publish({});

    test.require(deliveryCount == 0, "empty sample vector is not delivered");
}

} // namespace

int main()
{
    TestRunner test;

    testPublishDeliversSamples(test);
    testUnsubscribeStopsDelivery(test);
    testEmptyPublicationIgnored(test);

    return test.result();
}
