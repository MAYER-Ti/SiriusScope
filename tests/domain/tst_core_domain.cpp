#include "core/domain_models.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace siriusscope::core;

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

void testAmplitudeValidation(TestRunner& test)
{
    test.require(validateAmplitude(1).isValid(), "amplitude 1 is valid");
    test.require(validateAmplitude(127).isValid(), "amplitude 127 is valid");
    test.require(validateAmplitude(0).contains(ValidationCode::InvalidAmplitude), "amplitude 0 is invalid");
    test.require(validateAmplitude(-1).contains(ValidationCode::InvalidAmplitude), "amplitude -1 is invalid");
    test.require(validateAmplitude(128).contains(ValidationCode::InvalidAmplitude), "amplitude 128 is invalid");
}

void testBeamValidation(TestRunner& test)
{
    const auto capabilities = defaultRuntimeCapabilities();
    test.require(validateBeamIndex(0, capabilities).isValid(), "beam 0 is valid");
    test.require(validateBeamIndex(1, capabilities).isValid(), "beam 1 is valid");
    test.require(validateBeamIndex(2, capabilities).contains(ValidationCode::InvalidBeamIndex),
                 "beam 2 is invalid for current capabilities");
}

void testFrequencyAndBandConstraints(TestRunner& test)
{
    test.require(validateSystemFrequency(300'000'000LL).isValid(), "0.3 GHz is valid");
    test.require(validateSystemFrequency(18'000'000'000LL).isValid(), "18 GHz is valid");
    test.require(validateSystemFrequency(299'999'999LL).contains(ValidationCode::InvalidFrequency),
                 "below 0.3 GHz is invalid");
    test.require(validateSystemFrequency(18'000'000'001LL).contains(ValidationCode::InvalidFrequency),
                 "above 18 GHz is invalid");

    const auto lowerBand = BandConfig::create(0, 550'000'000LL, 500'000'000LL);
    test.require(lowerBand.hasValue(), "500 MHz band touching lower system boundary is valid");
    test.require((*lowerBand.value()).frequencyRange().minHz == 300'000'000LL,
                 "lower boundary is preserved");

    const auto upperBand = BandConfig::create(4, 17'750'000'000LL, 500'000'000LL);
    test.require(upperBand.hasValue(), "500 MHz band touching upper system boundary is valid");
    test.require((*upperBand.value()).frequencyRange().maxHz == 18'000'000'000LL,
                 "upper boundary is preserved");

    const auto tooWide = BandConfig::create(0, 1'000'000'000LL, 500'000'001LL);
    test.require(tooWide.validation().contains(ValidationCode::InvalidBandWidth),
                 "band wider than 500 MHz is invalid");

    const auto outOfRange = BandConfig::create(0, 400'000'000LL, 500'000'000LL);
    test.require(outOfRange.validation().contains(ValidationCode::BandOutOfRange),
                 "band extending below system range is invalid");

    const auto validBand = *lowerBand.value();
    const auto validSample = SignalSample::create(BeamSample{10, 10'000'000LL, 1, 0}, validBand);
    test.require(validSample.hasValue(), "signal sample inside band is valid");

    const auto invalidOffset = SignalSample::create(BeamSample{10, 250'000'001LL, 1, 0}, validBand);
    test.require(invalidOffset.validation().contains(ValidationCode::InvalidFrequencyOffset),
                 "signal sample outside band offset is invalid");
}

void testScanSectorAndAzimuth(TestRunner& test)
{
    test.require(validateAzimuth(0.0).isValid(), "azimuth 0 is valid");
    test.require(validateAzimuth(359.9).isValid(), "azimuth below 360 is valid");
    test.require(validateAzimuth(-0.1).contains(ValidationCode::InvalidAzimuth), "negative azimuth is invalid");
    test.require(validateAzimuth(360.0).contains(ValidationCode::InvalidAzimuth), "azimuth 360 is invalid");

    const auto sector = ScanSector::create(10.0, 40.0);
    test.require(sector.hasValue(), "normal sector is valid");
    test.require((*sector.value()).contains(20.0), "normal sector contains inner azimuth");
    test.require(!(*sector.value()).contains(50.0), "normal sector excludes outer azimuth");

    const auto wrap = ScanSector::create(350.0, 10.0);
    test.require(wrap.hasValue(), "wrap-around sector is valid");
    test.require((*wrap.value()).isWrapAround(), "wrap-around sector is detected");
    test.require((*wrap.value()).contains(355.0), "wrap-around sector contains high azimuth");
    test.require((*wrap.value()).contains(5.0), "wrap-around sector contains low azimuth");
    test.require(!(*wrap.value()).contains(180.0), "wrap-around sector excludes middle azimuth");

    const auto zeroSpan = ScanSector::create(10.0, 10.0);
    test.require(zeroSpan.validation().contains(ValidationCode::InvalidScanSector),
                 "zero-width sector is invalid");
}

void testTimeBase(TestRunner& test)
{
    const auto created = TimeBase::create(1'000'000LL, 100, DomainConstraints::defaultSamplePeriodNs);
    test.require(created.hasValue(), "valid time base is created");
    const auto timeBase = *created.value();

    const auto localFirst = timeBase.localTimeNsForSample(100);
    const auto localNext = timeBase.localTimeNsForSample(101);
    test.require(localFirst.hasValue() && *localFirst.value() == 0, "first sample has zero local time");
    test.require(localNext.hasValue() && *localNext.value() == 320, "next sample is one period later");
    test.require(*localNext.value() > *localFirst.value(), "sample to time mapping is monotonic");

    const auto globalA = timeBase.globalTimeUtcNsForSample(150);
    const auto globalB = timeBase.globalTimeUtcNsForSample(150);
    test.require(globalA.hasValue() && globalB.hasValue() && *globalA.value() == *globalB.value(),
                 "time conversion is deterministic");

    const auto beforeFirst = timeBase.localTimeNsForSample(99);
    test.require(beforeFirst.validation().contains(ValidationCode::InvalidSampleIndex),
                 "sample before first sample is invalid");

    const auto invalidPeriod = TimeBase::create(0, 0, 0);
    test.require(invalidPeriod.validation().contains(ValidationCode::InvalidTimeBase),
                 "zero sample period is invalid");

    const auto explicitBase =
        TimeBase::create(1'700'000'000'000'000'000LL, 1000, 1'000'000);
    test.require(explicitBase.hasValue(), "explicit UTC time base is valid");
    if (explicitBase) {
        const auto mapped = (*explicitBase.value()).globalTimeUtcNsForSample(1040);
        test.require(mapped.hasValue()
                         && *mapped.value() == 1'700'000'000'040'000'000LL,
                     "sampleIndex maps to UTC through recording start and sample period");
    }
}

void testResultModels(TestRunner& test)
{
    const auto bearing = BearingResult::create(12,
                                               1'000'000LL,
                                               1,
                                               45.0,
                                               std::vector<std::int64_t>{1'000'000'000LL},
                                               0.75);
    test.require(bearing.hasValue(), "valid bearing result is created");

    const auto row = ResultTableRow::fromBearingResult(*bearing.value(), 46.0);
    test.require(row.hasValue(), "valid result table row is created from bearing result");
    test.require((*row.value()).bearingAzimuthDeg == 45.0,
                 "result row preserves bearing azimuth");
    test.require((*row.value()).antennaAzimuthDeg == 46.0,
                 "result row preserves antenna azimuth");
    test.require((*row.value()).bandIndex == 1, "result row preserves band index");
    test.require((*row.value()).frequenciesHz.size() == 1, "result row preserves frequency set");

    const auto rowWithSignalParameters =
        ResultTableRow::fromBearingResult(*bearing.value(), 46.0, 100.0, 10.0);
    test.require(rowWithSignalParameters.hasValue(),
                 "valid result table row accepts signal parameters");
    test.require((*rowWithSignalParameters.value()).pulseRepetitionPeriodUs
                     && *(*rowWithSignalParameters.value()).pulseRepetitionPeriodUs == 100.0,
                 "result row preserves pulse repetition period");
    test.require((*rowWithSignalParameters.value()).pulseWidthUs
                     && *(*rowWithSignalParameters.value()).pulseWidthUs == 10.0,
                 "result row preserves pulse width");

    const auto invalidPri =
        ResultTableRow::fromBearingResult(*bearing.value(), 46.0, 0.0, 10.0);
    test.require(invalidPri.validation().contains(ValidationCode::InvalidTimeBase),
                 "zero pulse repetition period is invalid");

    const auto invalidWidth =
        ResultTableRow::fromBearingResult(*bearing.value(), 46.0, 100.0, -1.0);
    test.require(invalidWidth.validation().contains(ValidationCode::InvalidTimeBase),
                 "negative pulse width is invalid");

    const auto invalidRatio =
        ResultTableRow::fromBearingResult(*bearing.value(), 46.0, 100.0, 100.0);
    test.require(invalidRatio.validation().contains(ValidationCode::InvalidTimeBase),
                 "pulse width must be less than pulse repetition period");

    const auto invalidNaN = ResultTableRow::fromBearingResult(
        *bearing.value(),
        46.0,
        std::numeric_limits<double>::quiet_NaN(),
        10.0);
    test.require(invalidNaN.validation().contains(ValidationCode::InvalidTimeBase),
                 "NaN pulse repetition period is invalid");

    const auto invalidInf = ResultTableRow::fromBearingResult(
        *bearing.value(),
        46.0,
        100.0,
        std::numeric_limits<double>::infinity());
    test.require(invalidInf.validation().contains(ValidationCode::InvalidTimeBase),
                 "infinite pulse width is invalid");

    const auto invalidBearingAzimuth = BearingResult::create(12,
                                                             1'000'000LL,
                                                             1,
                                                             360.0,
                                                             std::vector<std::int64_t>{1'000'000'000LL},
                                                             0.75);
    test.require(invalidBearingAzimuth.validation().contains(ValidationCode::InvalidAzimuth),
                 "bearing azimuth 360 is invalid");

    const auto invalidBand = BearingResult::create(12,
                                                   1'000'000LL,
                                                   5,
                                                   45.0,
                                                   std::vector<std::int64_t>{1'000'000'000LL},
                                                   0.75);
    test.require(invalidBand.validation().contains(ValidationCode::InvalidBandIndex),
                 "bearing band outside 0..4 is invalid");

    const auto invalidFrequency = BearingResult::create(12,
                                                        1'000'000LL,
                                                        1,
                                                        45.0,
                                                        std::vector<std::int64_t>{299'999'999LL},
                                                        0.75);
    test.require(invalidFrequency.validation().contains(ValidationCode::InvalidFrequency),
                 "bearing frequency outside system range is invalid");

    const auto invalidQuality = BearingResult::create(12,
                                                      1'000'000LL,
                                                      1,
                                                      45.0,
                                                      std::vector<std::int64_t>{1'000'000'000LL},
                                                      1.01);
    test.require(invalidQuality.validation().contains(ValidationCode::InvalidQuality),
                 "bearing quality above 1 is invalid");

    const auto invalidTime = BearingResult::create(12,
                                                   -1,
                                                   1,
                                                   45.0,
                                                   std::vector<std::int64_t>{1'000'000'000LL},
                                                   std::nullopt);
    test.require(invalidTime.validation().contains(ValidationCode::InvalidTimeBase),
                 "negative result time is invalid");

    ResultTableRow invalidRow{12,
                              1'000'000LL,
                              45.0,
                              -1.0,
                              1,
                              {1'000'000'000LL},
                              std::nullopt,
                              std::nullopt,
                              std::nullopt,
                              {}};
    test.require(invalidRow.validate().contains(ValidationCode::InvalidAzimuth),
                 "result row with invalid antenna azimuth is invalid");

    ResultTableRow invalidBearingRow{12,
                                     1'000'000LL,
                                     360.0,
                                     46.0,
                                     1,
                                     {1'000'000'000LL},
                                     std::nullopt,
                                     std::nullopt,
                                     std::nullopt,
                                     {}};
    test.require(invalidBearingRow.validate().contains(ValidationCode::InvalidAzimuth),
                 "result row with invalid bearing azimuth is invalid");
}

} // namespace

int main()
{
    TestRunner test;

    testAmplitudeValidation(test);
    testBeamValidation(test);
    testFrequencyAndBandConstraints(test);
    testScanSectorAndAzimuth(test);
    testTimeBase(test);
    testResultModels(test);

    return test.result();
}
