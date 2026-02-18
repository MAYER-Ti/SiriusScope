#ifndef FREQUENCYVIEWPORTMODEL_H
#define FREQUENCYVIEWPORTMODEL_H

#include <QObject>
#include <QString>

class FrequencyViewportModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double viewMinHz READ viewMinHz WRITE setViewMinHz NOTIFY viewportChanged FINAL)
    Q_PROPERTY(double viewMaxHz READ viewMaxHz WRITE setViewMaxHz NOTIFY viewportChanged FINAL)
    Q_PROPERTY(double globalMinHz READ globalMinHz CONSTANT)
    Q_PROPERTY(double globalMaxHz READ globalMaxHz CONSTANT)

public:
    explicit FrequencyViewportModel(QObject *parent = nullptr);

    double viewMinHz() const noexcept { return m_viewMinHz; }
    double viewMaxHz() const noexcept { return m_viewMaxHz; }
    double globalMinHz() const noexcept { return m_globalMinHz; }
    double globalMaxHz() const noexcept { return m_globalMaxHz; }

    Q_INVOKABLE void setViewport(double minHz, double maxHz, const QString &sourceTag = QString());

public slots:
    void setViewMinHz(double minHz);
    void setViewMaxHz(double maxHz);

signals:
    void viewportChanged(double minHz, double maxHz, const QString &sourceTag);

private:
    void applyViewport(double minHz, double maxHz, const QString &sourceTag);

    double m_viewMinHz = 300e6;
    double m_viewMaxHz = 18e9;
    const double m_globalMinHz = 300e6;
    const double m_globalMaxHz = 18e9;
};

#endif // FREQUENCYVIEWPORTMODEL_H
