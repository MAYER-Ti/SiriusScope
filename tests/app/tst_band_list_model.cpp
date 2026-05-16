#include "app/bandlistmodel.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using siriusscope::app::BandListModel;

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

void testDefaultBands(TestRunner& test)
{
    BandListModel model;

    test.require(model.count() == 5, "BandListModel creates five default bands");
    test.require(model.rowCount() == 5, "rowCount matches current band count");

    for (int bandId = 0; bandId < model.count(); ++bandId) {
        const auto* config = model.bandConfig(bandId);
        test.require(config != nullptr, "band config exists");
        test.require(config && config->validate().isValid(), "default band config is valid");
    }
}

void testGetReturnsExpectedRoles(TestRunner& test)
{
    BandListModel model;
    const QVariantMap band = model.get(0);

    const std::vector<QString> keys = {
        QStringLiteral("bandId"),
        QStringLiteral("bandIndex"),
        QStringLiteral("centerHz"),
        QStringLiteral("widthHz"),
        QStringLiteral("minHz"),
        QStringLiteral("maxHz"),
        QStringLiteral("thresholdAmplitude"),
        QStringLiteral("inputAttenuatorDb"),
        QStringLiteral("outputAttenuatorDb"),
        QStringLiteral("polarization"),
        QStringLiteral("color"),
        QStringLiteral("borderColor"),
        QStringLiteral("textColor"),
        QStringLiteral("settingsWindowOpen"),
        QStringLiteral("valid"),
        QStringLiteral("diagnostics"),
    };

    for (const auto& key : keys) {
        test.require(band.contains(key), ("get(index) returns role " + key.toStdString()));
    }

    test.require(band.value(QStringLiteral("bandId")).toInt() == 0, "bandId role is preserved");
    test.require(band.value(QStringLiteral("centerHz")).toDouble() == 3'000'000'000.0,
                 "centerHz role has default value");
    test.require(band.value(QStringLiteral("widthHz")).toDouble() == 500'000'000.0,
                 "widthHz role has default value");
    test.require(band.value(QStringLiteral("polarization")).toString() == QStringLiteral("horizontal"),
                 "polarization defaults to horizontal");
}

void testLookupByBandId(TestRunner& test)
{
    BandListModel model;

    test.require(model.indexForBandId(0) == 0, "band 0 index is found");
    test.require(model.indexForBandId(4) == 4, "band 4 index is found");
    test.require(model.indexForBandId(5) == -1, "invalid band id is not found");

    const QVariantMap band = model.getByBandId(3);
    test.require(band.value(QStringLiteral("bandId")).toInt() == 3, "getByBandId returns band 3");
}

void testSettingsWindowState(TestRunner& test)
{
    BandListModel model;

    model.setSettingsWindowOpen(2, true);
    test.require(model.getByBandId(2).value(QStringLiteral("settingsWindowOpen")).toBool(),
                 "settings window state opens");

    model.setSettingsWindowOpen(2, false);
    test.require(!model.getByBandId(2).value(QStringLiteral("settingsWindowOpen")).toBool(),
                 "settings window state closes");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testDefaultBands(test);
    testGetReturnsExpectedRoles(test);
    testLookupByBandId(test);
    testSettingsWindowState(test);

    return test.result();
}
