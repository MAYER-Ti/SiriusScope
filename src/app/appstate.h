#ifndef APPSTATE_H
#define APPSTATE_H

#include <QObject>

class AppState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)

public:
    enum class Mode : int {
        Test = 0,
        Combat = 1,
        Control = 2
    };
    Q_ENUM(Mode)


    inline Mode mode() const noexcept {
        return m_mode;
    };

    static AppState& instance();

public slots:
    void setMode(AppState::Mode newMode);

signals:
    void modeChanged(AppState::Mode mode);

private:
    explicit AppState(QObject *parent = nullptr);
    void load();
    void save();

private:
    Mode m_mode = Mode::Test;

};

#endif // APPSTATE_H
