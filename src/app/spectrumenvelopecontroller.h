#pragma once

#include "hardware/interfaces/bco_sample_source.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::app {

struct SpectrumEnvelopeControllerConfig
{
    int binCount = 1024;
    int decayIntervalMs = 33;
    double decayPerSecond = 80.0;
};

class SpectrumEnvelopeController final : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumEnvelopeController(QObject* parent = nullptr);
    explicit SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig config,
                                        QObject* parent = nullptr);

    void setViewport(double minHz, double maxHz);
    void ingestBatch(const hardware::BcoSampleBatch& batch);

    Q_INVOKABLE QVariantList envelopeSamples() const;

signals:
    void envelopeChanged(double minHz, double maxHz, const QVariantList& samples);

private slots:
    void applyDecay();

private:
    std::optional<std::size_t> frequencyBinFor(std::int64_t frequencyHz) const;
    QVariantList buildSamples() const;
    void publishEnvelope();
    void clearEnvelope();
    void ensureDecayTimerRunning();

    SpectrumEnvelopeControllerConfig m_config;
    QTimer m_decayTimer;
    QElapsedTimer m_decayClock;
    std::vector<double> m_envelope;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
};

} // namespace siriusscope::app
