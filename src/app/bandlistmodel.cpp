#include "bandlistmodel.h"

#include <QtGlobal>
#include <QVariant>

#include <cassert>
#include <utility>

namespace siriusscope::app {

namespace {

struct DefaultBand
{
    int bandId = 0;
    std::int64_t centerHz = 0;
    std::int64_t widthHz = 0;
    double thresholdAmplitude = 0.0;
    const char* color = "";
    const char* borderColor = "";
    const char* textColor = "";
};

constexpr DefaultBand kDefaultBands[] = {
    {0, 3'000'000'000LL, 500'000'000LL, 30.0, "#4BB4FF", "#7FCBFF", "#DDF4FF"},
    {1, 5'795'000'000LL, 410'000'000LL, 30.0, "#35D07F", "#7CF3A9", "#E3FFF0"},
    {2, 8'250'000'000LL, 500'000'000LL, 30.0, "#E5B84B", "#FFD671", "#FFF2C5"},
    {3, 9'550'000'000LL, 500'000'000LL, 30.0, "#E46BD4", "#FF9AF0", "#FFE0FA"},
    {4, 14'250'000'000LL, 500'000'000LL, 30.0, "#8A7CFF", "#B6AEFF", "#ECE9FF"},
};

QVariant frequencyVariant(std::int64_t value)
{
    return QVariant::fromValue(static_cast<double>(value));
}

} // namespace

BandListModel::BandListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_bands(makeDefaultBands())
{
}

int BandListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(m_bands.size());
}

QVariant BandListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const auto& band = m_bands.at(static_cast<std::size_t>(index.row()));
    const auto range = band.config.frequencyRange();

    switch (role) {
    case BandIdRole:
    case BandIndexRole:
        return band.config.bandIndex;
    case CenterHzRole:
        return frequencyVariant(band.config.centerFrequencyHz);
    case WidthHzRole:
        return frequencyVariant(band.config.widthHz);
    case MinHzRole:
        return frequencyVariant(range.minHz);
    case MaxHzRole:
        return frequencyVariant(range.maxHz);
    case ThresholdAmplitudeRole:
        return band.thresholdAmplitude;
    case InputAttenuatorDbRole:
        return band.inputAttenuatorDb;
    case OutputAttenuatorDbRole:
        return band.outputAttenuatorDb;
    case PolarizationRole:
        return band.polarization;
    case ColorRole:
        return band.color;
    case BorderColorRole:
        return band.borderColor;
    case TextColorRole:
        return band.textColor;
    case SettingsWindowOpenRole:
        return band.settingsWindowOpen;
    case GeneratorPulsePeriodUsRole:
        return band.generatorPulsePeriodUs;
    case GeneratorPulseWidthUsRole:
        return band.generatorPulseWidthUs;
    case ValidRole:
        return band.valid;
    case DiagnosticsRole:
        return band.diagnostics;
    default:
        return {};
    }
}

QHash<int, QByteArray> BandListModel::roleNames() const
{
    return {
        {BandIdRole, QByteArrayLiteral("bandId")},
        {BandIndexRole, QByteArrayLiteral("bandIndex")},
        {CenterHzRole, QByteArrayLiteral("centerHz")},
        {WidthHzRole, QByteArrayLiteral("widthHz")},
        {MinHzRole, QByteArrayLiteral("minHz")},
        {MaxHzRole, QByteArrayLiteral("maxHz")},
        {ThresholdAmplitudeRole, QByteArrayLiteral("thresholdAmplitude")},
        {InputAttenuatorDbRole, QByteArrayLiteral("inputAttenuatorDb")},
        {OutputAttenuatorDbRole, QByteArrayLiteral("outputAttenuatorDb")},
        {PolarizationRole, QByteArrayLiteral("polarization")},
        {ColorRole, QByteArrayLiteral("color")},
        {BorderColorRole, QByteArrayLiteral("borderColor")},
        {TextColorRole, QByteArrayLiteral("textColor")},
        {SettingsWindowOpenRole, QByteArrayLiteral("settingsWindowOpen")},
        {GeneratorPulsePeriodUsRole, QByteArrayLiteral("generatorPulsePeriodUs")},
        {GeneratorPulseWidthUsRole, QByteArrayLiteral("generatorPulseWidthUs")},
        {ValidRole, QByteArrayLiteral("valid")},
        {DiagnosticsRole, QByteArrayLiteral("diagnostics")},
    };
}

int BandListModel::count() const
{
    return rowCount();
}

QVariantMap BandListModel::get(int index) const
{
    const auto* band = bandAt(index);
    return band ? toMap(*band) : QVariantMap{};
}

int BandListModel::indexForBandId(int bandId) const
{
    for (std::size_t i = 0; i < m_bands.size(); ++i) {
        if (m_bands.at(i).config.bandIndex == bandId) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

QVariantMap BandListModel::getByBandId(int bandId) const
{
    const auto* band = stateForBandId(bandId);
    return band ? toMap(*band) : QVariantMap{};
}

void BandListModel::setSettingsWindowOpen(int bandId, bool open)
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return;
    }

    auto& band = m_bands.at(static_cast<std::size_t>(row));
    if (band.settingsWindowOpen == open) {
        return;
    }

    band.settingsWindowOpen = open;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {SettingsWindowOpenRole});
}

const core::BandConfig* BandListModel::bandConfig(int bandId) const
{
    const auto* band = stateForBandId(bandId);
    return band ? &band->config : nullptr;
}

const BandListModel::BandPresentationState* BandListModel::bandState(int bandId) const
{
    return stateForBandId(bandId);
}

const BandListModel::BandPresentationState* BandListModel::bandAt(int index) const
{
    if (index < 0 || index >= rowCount()) {
        return nullptr;
    }

    return &m_bands.at(static_cast<std::size_t>(index));
}

std::vector<core::BandConfig> BandListModel::bandConfigs() const
{
    std::vector<core::BandConfig> configs;
    configs.reserve(m_bands.size());

    for (const auto& band : m_bands) {
        configs.push_back(band.config);
    }

    return configs;
}

bool BandListModel::updateBandConfig(const core::BandConfig& config,
                                     double thresholdAmplitude,
                                     int inputAttenuatorDb,
                                     int outputAttenuatorDb,
                                     const QString& polarization,
                                     bool valid,
                                     const QString& diagnostics)
{
    const int row = indexForBandId(config.bandIndex);
    if (row < 0) {
        return false;
    }

    auto& band = m_bands.at(static_cast<std::size_t>(row));
    band.config = config;
    band.thresholdAmplitude = thresholdAmplitude;
    band.inputAttenuatorDb = inputAttenuatorDb;
    band.outputAttenuatorDb = outputAttenuatorDb;
    band.polarization = polarization;
    band.valid = valid;
    band.diagnostics = diagnostics;

    const auto modelIndex = index(row);
    emit dataChanged(modelIndex,
                     modelIndex,
                     {CenterHzRole,
                      WidthHzRole,
                      MinHzRole,
                      MaxHzRole,
                      ThresholdAmplitudeRole,
                      InputAttenuatorDbRole,
                      OutputAttenuatorDbRole,
                      PolarizationRole,
                      ValidRole,
                      DiagnosticsRole});
    return true;
}

bool BandListModel::setThresholdAmplitude(int bandId,
                                          double thresholdAmplitude,
                                          bool valid,
                                          const QString& diagnostics)
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return false;
    }

    auto& band = m_bands.at(static_cast<std::size_t>(row));
    band.thresholdAmplitude = thresholdAmplitude;
    band.valid = valid;
    band.diagnostics = diagnostics;

    const auto modelIndex = index(row);
    emit dataChanged(modelIndex,
                     modelIndex,
                     {ThresholdAmplitudeRole, ValidRole, DiagnosticsRole});
    return true;
}

bool BandListModel::updateGeneratorPulseSettings(int bandId,
                                                 double pulsePeriodUs,
                                                 double pulseWidthUs,
                                                 bool valid,
                                                 const QString& diagnostics)
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return false;
    }

    auto& band = m_bands.at(static_cast<std::size_t>(row));
    band.generatorPulsePeriodUs = pulsePeriodUs;
    band.generatorPulseWidthUs = pulseWidthUs;
    band.valid = valid;
    band.diagnostics = diagnostics;

    const auto modelIndex = index(row);
    emit dataChanged(modelIndex,
                     modelIndex,
                     {GeneratorPulsePeriodUsRole,
                      GeneratorPulseWidthUsRole,
                      ValidRole,
                      DiagnosticsRole});
    return true;
}

bool BandListModel::setBandDiagnostics(int bandId, bool valid, const QString& diagnostics)
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return false;
    }

    auto& band = m_bands.at(static_cast<std::size_t>(row));
    band.valid = valid;
    band.diagnostics = diagnostics;

    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {ValidRole, DiagnosticsRole});
    return true;
}

QVariantMap BandListModel::toMap(const BandPresentationState& band) const
{
    const auto range = band.config.frequencyRange();
    return {
        {QStringLiteral("bandId"), band.config.bandIndex},
        {QStringLiteral("bandIndex"), band.config.bandIndex},
        {QStringLiteral("centerHz"), frequencyVariant(band.config.centerFrequencyHz)},
        {QStringLiteral("widthHz"), frequencyVariant(band.config.widthHz)},
        {QStringLiteral("minHz"), frequencyVariant(range.minHz)},
        {QStringLiteral("maxHz"), frequencyVariant(range.maxHz)},
        {QStringLiteral("thresholdAmplitude"), band.thresholdAmplitude},
        {QStringLiteral("inputAttenuatorDb"), band.inputAttenuatorDb},
        {QStringLiteral("outputAttenuatorDb"), band.outputAttenuatorDb},
        {QStringLiteral("polarization"), band.polarization},
        {QStringLiteral("color"), band.color},
        {QStringLiteral("borderColor"), band.borderColor},
        {QStringLiteral("textColor"), band.textColor},
        {QStringLiteral("settingsWindowOpen"), band.settingsWindowOpen},
        {QStringLiteral("generatorPulsePeriodUs"), band.generatorPulsePeriodUs},
        {QStringLiteral("generatorPulseWidthUs"), band.generatorPulseWidthUs},
        {QStringLiteral("valid"), band.valid},
        {QStringLiteral("diagnostics"), band.diagnostics},
    };
}

const BandListModel::BandPresentationState* BandListModel::stateForBandId(int bandId) const
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return nullptr;
    }

    return &m_bands.at(static_cast<std::size_t>(row));
}

BandListModel::BandPresentationState* BandListModel::stateForBandId(int bandId)
{
    const int row = indexForBandId(bandId);
    if (row < 0) {
        return nullptr;
    }

    return &m_bands.at(static_cast<std::size_t>(row));
}

std::vector<BandListModel::BandPresentationState> BandListModel::makeDefaultBands()
{
    std::vector<BandPresentationState> bands;
    bands.reserve(core::DomainConstraints::currentBandCount);

    for (const auto& defaultBand : kDefaultBands) {
        BandPresentationState state;
        const auto config = core::BandConfig::create(defaultBand.bandId,
                                                     defaultBand.centerHz,
                                                     defaultBand.widthHz);
        Q_ASSERT(config);
        if (config) {
            state.config = *config.value();
        } else {
            state.config = core::BandConfig{defaultBand.bandId,
                                            defaultBand.centerHz,
                                            defaultBand.widthHz,
                                            true};
            state.valid = false;
            state.diagnostics = QStringLiteral("Invalid default band configuration");
        }
        state.thresholdAmplitude = defaultBand.thresholdAmplitude;
        state.color = QColor(QString::fromLatin1(defaultBand.color));
        state.borderColor = QColor(QString::fromLatin1(defaultBand.borderColor));
        state.textColor = QColor(QString::fromLatin1(defaultBand.textColor));
        bands.push_back(std::move(state));
    }

    assert(static_cast<int>(bands.size()) == core::DomainConstraints::currentBandCount);
    return bands;
}

} // namespace siriusscope::app
