#include "app/appstate.h"

#include <QCoreApplication>

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

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

void testModeChangeLockBlocksModeMutation(TestRunner& test)
{
    auto& appState = AppState::instance();
    appState.setModeChangeLocked(false);
    appState.setMode(AppState::Mode::Test);

    int modeChanged = 0;
    QObject::connect(&appState,
                     &AppState::modeChanged,
                     [&modeChanged](AppState::Mode) {
                         ++modeChanged;
                     });

    appState.setModeChangeLocked(true);
    appState.setMode(AppState::Mode::Combat);

    test.require(appState.modeChangeLocked(), "mode lock is enabled");
    test.require(appState.mode() == AppState::Mode::Test,
                 "locked AppState keeps the previous mode");
    test.require(modeChanged == 0, "locked AppState does not emit modeChanged");

    appState.setModeChangeLocked(false);
    appState.setMode(AppState::Mode::Combat);

    test.require(appState.mode() == AppState::Mode::Combat,
                 "unlocked AppState accepts mode changes");
    test.require(modeChanged == 1, "unlocked AppState emits one modeChanged signal");

    appState.setMode(AppState::Mode::Test);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testModeChangeLockBlocksModeMutation(test);

    return test.result();
}
