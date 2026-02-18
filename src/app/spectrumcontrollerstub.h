#ifndef SPECTRUMCONTROLLERSTUB_H
#define SPECTRUMCONTROLLERSTUB_H

#include <QObject>
#include <QVariantList>

class SpectrumControllerStub : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumControllerStub(QObject *parent = nullptr);

public slots:
    void requestSpectrum(double viewMinHz, double viewMaxHz);
    void setBand(int bandId, double centerHz, double widthHz, bool isFinal);
    void setBandThreshold(int bandId, double thresholdDb, bool isFinal);
    void setBandEnabled(int bandId, bool enabled);

signals:
    void spectrumReady(double viewMinHz, double viewMaxHz, const QVariantList &samples, float minDb, float maxDb);
    void bandStateChanged(int bandId, double centerHz, double widthHz, double thresholdDb, bool enabled);

private:
    QVariantList generateSpectrum(double viewMinHz, double viewMaxHz, float &outMinDb, float &outMaxDb) const;
};

#endif // SPECTRUMCONTROLLERSTUB_H
