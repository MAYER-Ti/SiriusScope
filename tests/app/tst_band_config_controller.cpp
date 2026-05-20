#include "app/bandconfigcontroller.h"

#include "hardware/interfaces/bco_control.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using siriusscope::app::BandConfigController;
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

class RecordingBcoControl final : public siriusscope::hardware::IBcoControl
{
public:
    siriusscope::core::OperationResult nextResult = siriusscope::core::OperationResult::ok();
    int applySingleCount = 0;
    std::vector<siriusscope::core::BandConfig> appliedConfigs;

    siriusscope::core::OperationResult applyBandConfig(
        const siriusscope::core::BandConfig& config) override
    {
        ++applySingleCount;
        appliedConfigs.push_back(config);
        return nextResult;
    }

    siriusscope::core::OperationResult applyBandConfigs(
        const std::vector<siriusscope::core::BandConfig>& configs) override
    {
        for (const auto& config : configs) {
            applyBandConfig(config);
        }
        return nextResult;
    }

    siriusscope::core::OperationResult startProcessing(
        const siriusscope::hardware::BcoProcessingStartCommand&) override
    {
        return siriusscope::core::OperationResult::ok();
    }

    siriusscope::core::OperationResult stopProcessing() override
    {
        return siriusscope::core::OperationResult::ok();
    }
};

class RecordingDiagnosticsSink final : public siriusscope::infrastructure::IDiagnosticsSink
{
public:
    std::vector<siriusscope::infrastructure::DiagnosticEvent> events;

    void publish(const siriusscope::infrastructure::DiagnosticEvent& event) override
    {
        events.push_back(event);
    }
};

void testApplyValidSettings(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    int applied = 0;
    QObject::connect(&controller,
                     &BandConfigController::bandSettingsApplied,
                     [&applied](int bandId) {
                         if (bandId == 1) {
                             ++applied;
                         }
                     });

    const bool ok = controller.applyBandSettings(1,
                                                 6'000'000'000.0,
                                                 400'000'000.0,
                                                 55.0,
                                                 10,
                                                 20,
                                                 QStringLiteral("vertical"));

    const QVariantMap band = model.getByBandId(1);
    test.require(ok, "valid settings are accepted");
    test.require(applied == 1, "applied signal is emitted");
    test.require(bco.applySingleCount == 1, "BCO control is called once");
    test.require(!bco.appliedConfigs.empty()
                     && bco.appliedConfigs.back().centerFrequencyHz == 6'000'000'000LL,
                 "BCO receives center frequency");
    test.require(band.value(QStringLiteral("widthHz")).toDouble() == 400'000'000.0,
                 "model stores applied width");
    test.require(band.value(QStringLiteral("inputAttenuatorDb")).toInt() == 10,
                 "model stores input attenuator");
    test.require(band.value(QStringLiteral("outputAttenuatorDb")).toInt() == 20,
                 "model stores output attenuator");
    test.require(band.value(QStringLiteral("polarization")).toString() == QStringLiteral("vertical"),
                 "model stores polarization");
    test.require(!diagnostics.events.empty(), "diagnostics are published");
}

void testRejectsTooWideBand(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalWidth = model.getByBandId(0).value(QStringLiteral("widthHz")).toDouble();
    int rejected = 0;
    QObject::connect(&controller,
                     &BandConfigController::bandSettingsRejected,
                     [&rejected](int, const QString&) { ++rejected; });

    const bool ok = controller.applyBandSettings(0,
                                                 3'000'000'000.0,
                                                 500'000'001.0,
                                                 50.0,
                                                 0,
                                                 0,
                                                 QStringLiteral("horizontal"));

    test.require(!ok, "band wider than 500 MHz is rejected");
    test.require(rejected == 1, "rejected signal is emitted");
    test.require(bco.applySingleCount == 0, "invalid settings are not sent to BCO");
    test.require(model.getByBandId(0).value(QStringLiteral("widthHz")).toDouble() == originalWidth,
                 "model keeps previous width after rejection");
    test.require(!model.getByBandId(0).value(QStringLiteral("valid")).toBool(),
                 "model exposes invalid diagnostics state");
}

void testRejectsOutOfRangeBand(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalCenter = model.getByBandId(0).value(QStringLiteral("centerHz")).toDouble();

    const bool ok = controller.applyBandSettings(0,
                                                 400'000'000.0,
                                                 500'000'000.0,
                                                 50.0,
                                                 0,
                                                 0,
                                                 QStringLiteral("horizontal"));

    test.require(!ok, "band outside 0.3..18 GHz is rejected");
    test.require(bco.applySingleCount == 0, "out-of-range settings are not sent to BCO");
    test.require(model.getByBandId(0).value(QStringLiteral("centerHz")).toDouble() == originalCenter,
                 "model keeps previous center after out-of-range rejection");
}

void testPreviewAndCancel(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalCenter = model.getByBandId(2).value(QStringLiteral("centerHz")).toDouble();
    const double originalWidth = model.getByBandId(2).value(QStringLiteral("widthHz")).toDouble();
    int previewChanged = 0;
    int previewCanceled = 0;
    QObject::connect(&controller,
                     &BandConfigController::bandPreviewChanged,
                     [&previewChanged](int bandId) {
                         if (bandId == 2) {
                             ++previewChanged;
                         }
                     });
    QObject::connect(&controller,
                     &BandConfigController::bandPreviewCanceled,
                     [&previewCanceled](int bandId) {
                         if (bandId == 2) {
                             ++previewCanceled;
                         }
                     });

    const bool previewOk = controller.previewBandSettings(2,
                                                          9'000'000'000.0,
                                                          300'000'000.0);
    test.require(previewOk, "preview accepts valid band");
    test.require(previewChanged == 1, "preview signal is emitted");
    test.require(bco.applySingleCount == 0, "preview does not call BCO");
    test.require(model.getByBandId(2).value(QStringLiteral("centerHz")).toDouble()
                     == 9'000'000'000.0,
                 "preview updates model center");

    controller.cancelBandSettingsPreview(2);
    test.require(previewCanceled == 1, "cancel signal is emitted");
    test.require(model.getByBandId(2).value(QStringLiteral("centerHz")).toDouble()
                     == originalCenter,
                 "cancel restores committed center");
    test.require(model.getByBandId(2).value(QStringLiteral("widthHz")).toDouble() == originalWidth,
                 "cancel restores committed width");
}

void testThresholdPreview(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalCenter = model.getByBandId(3).value(QStringLiteral("centerHz")).toDouble();
    const bool ok = controller.setBandThresholdPreview(3, 70.0);

    test.require(ok, "threshold preview is accepted");
    test.require(bco.applySingleCount == 0, "threshold preview does not call BCO");
    test.require(model.getByBandId(3).value(QStringLiteral("thresholdAmplitude")).toDouble() == 70.0,
                 "threshold preview updates threshold");
    test.require(model.getByBandId(3).value(QStringLiteral("centerHz")).toDouble() == originalCenter,
                 "threshold preview keeps center frequency");
}

void testThresholdRange(TestRunner& test)
{
    {
        BandListModel model;
        RecordingBcoControl bco;
        RecordingDiagnosticsSink diagnostics;
        BandConfigController controller(&model, &bco, &diagnostics);

        const bool ok = controller.applyBandSettings(0,
                                                     3'000'000'000.0,
                                                     500'000'000.0,
                                                     0.0,
                                                     0,
                                                     0,
                                                     QStringLiteral("horizontal"));
        test.require(!ok, "threshold 0 is rejected");
        test.require(bco.applySingleCount == 0, "threshold 0 is not sent to BCO");
    }

    {
        BandListModel model;
        RecordingBcoControl bco;
        RecordingDiagnosticsSink diagnostics;
        BandConfigController controller(&model, &bco, &diagnostics);

        const bool ok = controller.applyBandSettings(0,
                                                     3'000'000'000.0,
                                                     500'000'000.0,
                                                     1.0,
                                                     0,
                                                     0,
                                                     QStringLiteral("horizontal"));
        test.require(ok, "threshold 1 is accepted");
        test.require(bco.applySingleCount == 1, "threshold 1 is sent to BCO");
    }

    {
        BandListModel model;
        RecordingBcoControl bco;
        RecordingDiagnosticsSink diagnostics;
        BandConfigController controller(&model, &bco, &diagnostics);

        const bool ok = controller.applyBandSettings(0,
                                                     3'000'000'000.0,
                                                     500'000'000.0,
                                                     127.0,
                                                     0,
                                                     0,
                                                     QStringLiteral("horizontal"));
        test.require(ok, "threshold 127 is accepted");
        test.require(bco.applySingleCount == 1, "threshold 127 is sent to BCO");
    }

    {
        BandListModel model;
        RecordingBcoControl bco;
        RecordingDiagnosticsSink diagnostics;
        BandConfigController controller(&model, &bco, &diagnostics);

        const bool ok = controller.applyBandSettings(0,
                                                     3'000'000'000.0,
                                                     500'000'000.0,
                                                     128.0,
                                                     0,
                                                     0,
                                                     QStringLiteral("horizontal"));
        test.require(!ok, "threshold 128 is rejected");
        test.require(bco.applySingleCount == 0, "threshold 128 is not sent to BCO");
    }
}

void testEditingLockRejectsChanges(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalCenter = model.getByBandId(1).value(QStringLiteral("centerHz")).toDouble();
    const double originalWidth = model.getByBandId(1).value(QStringLiteral("widthHz")).toDouble();
    const double originalThreshold =
        model.getByBandId(1).value(QStringLiteral("thresholdAmplitude")).toDouble();

    int rejected = 0;
    QObject::connect(&controller,
                     &BandConfigController::bandSettingsRejected,
                     [&rejected](int bandId, const QString&) {
                         if (bandId == 1) {
                             ++rejected;
                         }
                     });

    controller.setEditingLocked(true);
    const bool previewOk = controller.previewBandSettings(1,
                                                          6'100'000'000.0,
                                                          300'000'000.0);
    const bool thresholdOk = controller.setBandThresholdPreview(1, originalThreshold + 50.0);
    const bool applyOk = controller.applyBandSettings(1,
                                                      6'100'000'000.0,
                                                      300'000'000.0,
                                                      originalThreshold + 50.0,
                                                      10,
                                                      10,
                                                      QStringLiteral("horizontal"));

    const QVariantMap band = model.getByBandId(1);
    test.require(controller.editingLocked(), "editing lock is enabled");
    test.require(!previewOk, "locked controller rejects frequency preview");
    test.require(!thresholdOk, "locked controller rejects threshold preview");
    test.require(!applyOk, "locked controller rejects apply");
    test.require(rejected == 1, "locked apply emits one rejection");
    test.require(bco.applySingleCount == 0, "locked apply does not call BCO");
    test.require(band.value(QStringLiteral("centerHz")).toDouble() == originalCenter,
                 "locked controller keeps committed center");
    test.require(band.value(QStringLiteral("widthHz")).toDouble() == originalWidth,
                 "locked controller keeps committed width");
    test.require(band.value(QStringLiteral("thresholdAmplitude")).toDouble() == originalThreshold,
                 "locked controller keeps committed threshold");
}

void testEditingLockRestoresActivePreview(TestRunner& test)
{
    BandListModel model;
    RecordingBcoControl bco;
    RecordingDiagnosticsSink diagnostics;
    BandConfigController controller(&model, &bco, &diagnostics);

    const double originalCenter = model.getByBandId(2).value(QStringLiteral("centerHz")).toDouble();
    const double originalThreshold =
        model.getByBandId(2).value(QStringLiteral("thresholdAmplitude")).toDouble();

    const bool previewOk = controller.previewBandSettings(2,
                                                          9'000'000'000.0,
                                                          300'000'000.0);
    const bool thresholdOk = controller.setBandThresholdPreview(2, originalThreshold + 40.0);

    controller.setEditingLocked(true);

    const QVariantMap band = model.getByBandId(2);
    test.require(previewOk, "preview is accepted before lock");
    test.require(thresholdOk, "threshold preview is accepted before lock");
    test.require(band.value(QStringLiteral("centerHz")).toDouble() == originalCenter,
                 "locking restores previewed center to committed state");
    test.require(band.value(QStringLiteral("thresholdAmplitude")).toDouble() == originalThreshold,
                 "locking restores previewed threshold to committed state");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testApplyValidSettings(test);
    testRejectsTooWideBand(test);
    testRejectsOutOfRangeBand(test);
    testPreviewAndCancel(test);
    testThresholdPreview(test);
    testThresholdRange(test);
    testEditingLockRejectsChanges(test);
    testEditingLockRestoresActivePreview(test);

    return test.result();
}
