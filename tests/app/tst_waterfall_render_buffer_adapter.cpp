#include "app/waterfallrenderbufferadapter.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {

namespace processing = siriusscope::processing;

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

processing::WaterfallCell makeCell(std::int64_t minHz,
                                   std::int64_t maxHz,
                                   int beam0,
                                   bool beam0Present,
                                   int beam1,
                                   bool beam1Present)
{
    processing::WaterfallCell cell;
    cell.frequencyRange = siriusscope::core::FrequencyRange{minHz, maxHz};
    cell.status = processing::WaterfallCellStatus::Valid;
    cell.beamAmplitudes = {beam0, beam1};
    cell.beamPresent = {beam0Present, beam1Present};
    return cell;
}

processing::WaterfallFrame makeFrame(processing::WaterfallCell cell)
{
    processing::WaterfallRow row;
    row.bandIndex = 0;
    row.sampleIndexStart = 10;
    row.sampleIndexEnd = 12;
    row.frequencyRange = siriusscope::core::FrequencyRange{0, 100};
    row.cells.push_back(std::move(cell));

    processing::WaterfallFrame frame;
    frame.rows.push_back(std::move(row));
    return frame;
}

WaterfallBeamBin centerBin(const WaterfallRow& row)
{
    return row.bins.at(row.bins.size() / 2);
}

void testFrameConvertsToRenderRow(TestRunner& test)
{
    const auto result = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 64, true, 32, true)),
        1234,
        0.0,
        100.0,
        11);

    test.require(result.hasVisibleCells, "adapter reports visible cells");
    test.require(result.row.utcMs == 1234, "adapter preserves display UTC time");
    test.require(result.row.firstSampleIndex == 10 && result.row.lastSampleIndex == 12,
                 "adapter preserves sample index range");
    test.require(result.row.bins.size() == 11, "adapter creates requested render bin count");
    test.require(centerBin(result.row) == WaterfallBeamBin{64, 32},
                 "adapter maps cell amplitudes to render bin");
}

void testAmplitudeRangeIsPreserved(TestRunner& test)
{
    const auto minResult = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 1, true, 0, false)),
        1,
        0.0,
        100.0,
        11);
    const auto maxResult = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 127, true, 0, false)),
        1,
        0.0,
        100.0,
        11);

    test.require(centerBin(minResult.row).left == 1,
                 "domain amplitude 1 is preserved as minimum useful render level");
    test.require(centerBin(maxResult.row).left == 127,
                 "domain amplitude 127 is preserved as maximum render level");
}

void testEmptyCellStaysEmpty(TestRunner& test)
{
    auto cell = makeCell(45, 55, 0, false, 0, false);
    cell.status = processing::WaterfallCellStatus::MissingData;

    const auto result = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(std::move(cell)),
        1,
        0.0,
        100.0,
        11);

    test.require(!result.hasVisibleCells, "missing cell does not create visible data");
    test.require(std::all_of(result.row.bins.cbegin(),
                             result.row.bins.cend(),
                             [](const WaterfallBeamBin& bin) {
                                 return bin.left == 0 && bin.right == 0;
                             }),
                 "missing cell leaves render bins empty");
}

void testDirectionalInputs(TestRunner& test)
{
    const auto beam0 = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 100, true, 20, true)),
        1,
        0.0,
        100.0,
        11);
    const auto beam1 = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 20, true, 100, true)),
        1,
        0.0,
        100.0,
        11);
    const auto neutral = siriusscope::app::WaterfallRenderBufferAdapter::adaptFrame(
        makeFrame(makeCell(45, 55, 64, true, 64, true)),
        1,
        0.0,
        100.0,
        11);

    test.require(centerBin(beam0.row).left > centerBin(beam0.row).right,
                 "A0 > A1 maps to beam 0 render dominance");
    test.require(centerBin(beam1.row).right > centerBin(beam1.row).left,
                 "A1 > A0 maps to beam 1 render dominance");
    test.require(centerBin(neutral.row).left == centerBin(neutral.row).right,
                 "A0 ~= A1 maps to neutral render input");
}

} // namespace

int main()
{
    TestRunner test;

    testFrameConvertsToRenderRow(test);
    testAmplitudeRangeIsPreserved(test);
    testEmptyCellStaysEmpty(test);
    testDirectionalInputs(test);

    return test.result();
}
