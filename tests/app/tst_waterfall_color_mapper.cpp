#include "app/waterfallcolormapper.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

WaterfallColorParams defaultParams()
{
    WaterfallColorParams params;
    params.gamma = 1.0;
    params.directionDeadZone = 0.10;
    params.directionalAlpha = 0.50;
    return params;
}

void testZeroIsDarkNeutral(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({0, 0}, defaultParams());
    test.require(color.r == 0 && color.g == 0 && color.b == 0, "zero bin is dark neutral");
    test.require(color.a == 0, "zero bin is transparent");
}

void testEqualBeamsNeutral(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({64, 64}, defaultParams());
    test.require(color.r == color.g && color.g == color.b, "equal beams are neutral gray");
    test.require(color.a > 0, "non-zero neutral bin is visible");
}

void testLeftDominantIsRed(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({127, 10}, defaultParams());
    test.require(color.r > color.g, "left-dominant bin is redder than green");
}

void testRightDominantIsGreen(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({10, 127}, defaultParams());
    test.require(color.g > color.r, "right-dominant bin is greener than red");
}

void testDeadZoneNeutralizesSmallDifference(TestRunner& test)
{
    WaterfallColorParams params = defaultParams();
    params.directionDeadZone = 0.20;

    const Rgba8 color = WaterfallColorMapper::map({60, 66}, params);
    test.require(color.r == color.g && color.g == color.b,
                 "dead zone neutralizes small beam difference");
}

void testStandardModeDependsOnlyOnMaxAmplitude(TestRunner& test)
{
    WaterfallColorParams params = defaultParams();
    params.directionalEnabled = false;

    const Rgba8 leftDominant = WaterfallColorMapper::map({127, 10}, params);
    const Rgba8 rightDominant = WaterfallColorMapper::map({10, 127}, params);
    test.require(leftDominant.r == rightDominant.r
                     && leftDominant.g == rightDominant.g
                     && leftDominant.b == rightDominant.b,
                 "standard mode depends only on max amplitude");
}

void testBelowDisplayThresholdIsTransparent(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({1, 0}, defaultParams());
    test.require(color.a == 0, "amplitude below display threshold is transparent");
    test.require(color.r == 0 && color.g == 0 && color.b == 0,
                 "amplitude below display threshold is dark");
}

void testThresholdAmplitudeIsVisible(TestRunner& test)
{
    const Rgba8 color = WaterfallColorMapper::map({4, 0}, defaultParams());
    test.require(color.a == 255, "amplitude at display threshold is visible");
    test.require(color.r > 0 || color.g > 0 || color.b > 0,
                 "amplitude at display threshold has a render level");
}

void testMinimumDomainAmplitudeCanBeEnabled(TestRunner& test)
{
    WaterfallColorParams params = defaultParams();
    params.displayAmplitudeThreshold = 1;

    const Rgba8 color = WaterfallColorMapper::map({1, 0}, params);
    test.require(color.a == 255, "minimum domain amplitude is visible when threshold is one");
    test.require(color.r > 0 || color.g > 0 || color.b > 0,
                 "minimum domain amplitude has a visible render level when enabled");
}

} // namespace

int main()
{
    TestRunner test;

    testZeroIsDarkNeutral(test);
    testEqualBeamsNeutral(test);
    testLeftDominantIsRed(test);
    testRightDominantIsGreen(test);
    testDeadZoneNeutralizesSmallDifference(test);
    testStandardModeDependsOnlyOnMaxAmplitude(test);
    testBelowDisplayThresholdIsTransparent(test);
    testThresholdAmplitudeIsVisible(test);
    testMinimumDomainAmplitudeCanBeEnabled(test);

    return test.result();
}
