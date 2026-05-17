#include "app/inmemoryscanacquisitionrecorder.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

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

app::ScanAcquisitionMetadata makeMetadata(std::uint64_t sessionId)
{
    app::ScanAcquisitionMetadata metadata;
    metadata.scanSessionId = sessionId;
    metadata.requestedSector = core::ScanSector{10.0, 60.0};
    metadata.startedAt = std::chrono::system_clock::now();
    metadata.finishedAt = metadata.startedAt;
    metadata.startAzimuthDeg = 10.0;
    metadata.endAzimuthDeg = 10.0;
    metadata.speedDegPerSec = 12.0;
    return metadata;
}

processing::BearingFrameObservation makeObservation(std::uint64_t sampleIndex,
                                                    double azimuthDeg)
{
    processing::BearingInputFrame frame;
    frame.bandIndex = 0;
    frame.sampleIndexStart = sampleIndex;
    frame.sampleIndexEnd = sampleIndex;

    processing::BearingCandidate candidate;
    candidate.bandIndex = 0;
    candidate.sampleIndexStart = sampleIndex;
    candidate.sampleIndexEnd = sampleIndex;
    candidate.frequencyBin = 0;
    candidate.frequencyRange = core::FrequencyRange{1'000'000'000LL, 1'001'000'000LL};
    candidate.beamAmplitudes = {100, 80};
    candidate.beamPresent = {true, true};
    frame.candidates.push_back(candidate);

    return processing::BearingFrameObservation{std::move(frame), azimuthDeg, 123};
}

void testBeginAppendClose(TestRunner& test)
{
    app::InMemoryScanAcquisitionRecorder recorder;
    auto metadata = makeMetadata(1);

    const auto beginResult = recorder.begin(metadata);
    const auto appendResult = recorder.append(makeObservation(10, 25.0));
    metadata.finishedAt = std::chrono::system_clock::now();
    metadata.endAzimuthDeg = 60.0;
    const auto closeResult = recorder.close(metadata);
    const auto observations = recorder.observations(1);

    test.require(beginResult.success, "begin opens acquisition session");
    test.require(appendResult.success, "append stores an observation");
    test.require(closeResult.success, "close closes acquisition session");
    test.require(!recorder.active(), "close clears active state");
    test.require(observations.size() == 1, "observations returns stored session data");
    test.require(observations.front().antennaAzimuthDeg == 25.0,
                 "stored observation keeps antenna azimuth");
}

void testAppendAfterCloseFails(TestRunner& test)
{
    app::InMemoryScanAcquisitionRecorder recorder;
    auto metadata = makeMetadata(1);

    recorder.begin(metadata);
    recorder.close(metadata);
    const auto appendResult = recorder.append(makeObservation(11, 30.0));

    test.require(!appendResult.success, "append after close returns failure");
}

void testObservationsAreSessionScoped(TestRunner& test)
{
    app::InMemoryScanAcquisitionRecorder recorder;
    auto first = makeMetadata(1);
    auto second = makeMetadata(2);

    recorder.begin(first);
    recorder.append(makeObservation(1, 10.0));
    recorder.close(first);
    recorder.begin(second);
    recorder.append(makeObservation(2, 20.0));
    recorder.close(second);

    const auto firstObservations = recorder.observations(1);
    const auto secondObservations = recorder.observations(2);

    test.require(firstObservations.size() == 1, "first session has one observation");
    test.require(secondObservations.size() == 1, "second session has one observation");
    test.require(firstObservations.front().frame.sampleIndexStart == 1,
                 "first session returns only first observation");
    test.require(secondObservations.front().frame.sampleIndexStart == 2,
                 "second session returns only second observation");
}

void testDuplicateBeginFails(TestRunner& test)
{
    app::InMemoryScanAcquisitionRecorder recorder;
    const auto firstBegin = recorder.begin(makeMetadata(1));
    const auto secondBegin = recorder.begin(makeMetadata(2));

    test.require(firstBegin.success, "first begin succeeds");
    test.require(!secondBegin.success, "second begin while active fails");
    test.require(recorder.active(), "failed duplicate begin keeps active session");
}

} // namespace

int main()
{
    TestRunner test;

    testBeginAppendClose(test);
    testAppendAfterCloseFails(test);
    testObservationsAreSessionScoped(test);
    testDuplicateBeginFails(test);

    return test.result();
}
