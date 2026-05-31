#include "hardware/simulator/simulated_bco_payload_accounting.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
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

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

hardware::SimulatedBcoPacketModel defaultPacketModel()
{
    hardware::SimulatedBcoPacketModel model;
    model.packetHeaderBytes = 32;
    model.sampleRecordBytes = 16;
    model.packetFooterBytes = 0;
    model.samplesPerPacket = 256;
    model.alignmentBytes = 0;
    return model;
}

hardware::ThroughputTarget defaultTarget()
{
    hardware::ThroughputTarget target;
    target.targetBytesPerSecond = 90'000'000;
    target.batchPeriod = std::chrono::milliseconds{10};
    target.mode = hardware::PayloadAccountingMode::RawBcoBytes;
    target.packetModel = defaultPacketModel();
    return target;
}

void testRawBytesPerPacket(TestRunner& test)
{
    test.require(hardware::rawBytesPerPacket(defaultPacketModel()) == 4128,
                 "raw packet bytes include header and sample records");
}

void testRawBytesPerSample(TestRunner& test)
{
    const auto bytesPerSample = hardware::rawBytesPerSample(defaultPacketModel());
    test.require(std::abs(bytesPerSample - 16.125) < 0.000001,
                 "raw bytes per sample uses packet model average");
}

void testSamplesPerBatchForTarget(TestRunner& test)
{
    const auto samplesPerBatch = hardware::samplesPerBatchForTarget(defaultTarget());
    test.require(samplesPerBatch == 55'813,
                 "target samples per batch floors raw byte budget");
}

void testRawBytesForSamplesUsesFullPackets(TestRunner& test)
{
    const auto model = defaultPacketModel();
    test.require(hardware::rawBytesForSamples(256, model) == 4128,
                 "one full packet accounts to one raw packet");
    test.require(hardware::rawBytesForSamples(257, model) == 8256,
                 "partial second packet accounts as a full raw packet");
}

void testInvalidConfigUsesSafeMinimums(TestRunner& test)
{
    hardware::SimulatedBcoPacketModel model;
    model.packetHeaderBytes = 0;
    model.sampleRecordBytes = 0;
    model.packetFooterBytes = 0;
    model.samplesPerPacket = 0;
    model.alignmentBytes = 0;

    test.require(hardware::rawBytesPerPacket(model) == 1,
                 "invalid packet model keeps at least one raw byte per packet");
    test.require(hardware::rawBytesPerSample(model) == 1.0,
                 "invalid packet model keeps at least one raw byte per sample");

    hardware::ThroughputTarget target;
    target.targetBytesPerSecond = 0;
    target.batchPeriod = std::chrono::milliseconds{0};
    target.packetModel = model;

    test.require(hardware::samplesPerBatchForTarget(target) == 0,
                 "invalid low throughput target is safe and produces no batch samples");
    test.require(hardware::rawBytesForSamples(1, model) == 1,
                 "invalid packet model still accounts sample bytes safely");
}

} // namespace

int main()
{
    TestRunner test;

    testRawBytesPerPacket(test);
    testRawBytesPerSample(test);
    testSamplesPerBatchForTarget(test);
    testRawBytesForSamplesUsesFullPackets(test);
    testInvalidConfigUsesSafeMinimums(test);

    return test.result();
}
