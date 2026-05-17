#include "hardware/simulator/simulator_antenna_azimuth_source.h"

#include "core/antenna_motion_planner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace siriusscope::hardware {

namespace {

constexpr double kFullCircleDeg = core::DomainConstraints::maxAzimuthDeg;
constexpr double kBlindStartDeg = core::AntennaMotionPlanner::blindZoneStartDeg;
constexpr double kBlindEndDeg = core::AntennaMotionPlanner::blindZoneEndDeg;

double normalizeAzimuth(double value)
{
    return core::AntennaMotionPlanner::normalizeAzimuth(value);
}

double clockwiseDistance(double fromDeg, double toDeg)
{
    return normalizeAzimuth(toDeg - fromDeg);
}

bool clockwiseArcContains(double fromDeg, double toDeg, double valueDeg)
{
    const auto distanceToTarget = clockwiseDistance(fromDeg, toDeg);
    const auto distanceToValue = clockwiseDistance(fromDeg, valueDeg);
    return distanceToValue > 0.0 && distanceToValue < distanceToTarget;
}

bool pathCrossesBlindZone(double fromDeg, double toDeg, bool clockwise)
{
    if (clockwise) {
        return clockwiseArcContains(fromDeg, toDeg, (kBlindStartDeg + kBlindEndDeg) / 2.0);
    }

    return clockwiseArcContains(toDeg, fromDeg, (kBlindStartDeg + kBlindEndDeg) / 2.0);
}

double movementStep(double speedDegPerSecond, std::chrono::milliseconds period)
{
    const auto periodSeconds = std::chrono::duration<double>(period).count();
    return std::max(0.0, speedDegPerSecond) * periodSeconds;
}

} // namespace

SimulatorAntennaAzimuthSource::SimulatorAntennaAzimuthSource(
    SimulatorAntennaState* state,
    SimulatorAntennaAzimuthSourceConfig config,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_state(state)
    , m_config(std::move(config))
    , m_diagnosticsSink(diagnosticsSink)
{
    if (m_state) {
        m_state->setCurrentAzimuthDeg(m_config.initialAzimuthDeg);
        m_state->setTargetAzimuthDeg(m_config.initialAzimuthDeg);
        m_state->setMovementSpeedDegPerSecond(m_config.movementSpeedDegPerSecond);
        m_state->setMoving(false);
    }
}

SimulatorAntennaAzimuthSource::~SimulatorAntennaAzimuthSource()
{
    stop();
}

core::OperationResult SimulatorAntennaAzimuthSource::start(AzimuthCallback callback)
{
    if (!callback) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator azimuth source start rejected: callback is empty");
        return core::OperationResult::failure("azimuth callback must not be empty");
    }

    if (!m_state) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator azimuth source start rejected: state is not available");
        return core::OperationResult::failure("antenna simulator state is not available");
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_running) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "antenna simulator azimuth source start rejected: source is already running");
            return core::OperationResult::failure(
                "antenna simulator azimuth source is already running");
        }

        m_stopRequested = false;
        m_running = true;
    }

    m_worker = std::thread(&SimulatorAntennaAzimuthSource::generationLoop,
                           this,
                           std::move(callback));
    return core::OperationResult::ok();
}

core::OperationResult SimulatorAntennaAzimuthSource::stop()
{
    std::thread worker;
    {
        std::lock_guard lock(m_mutex);
        if (!m_running && !m_worker.joinable()) {
            return core::OperationResult::ok();
        }

        m_stopRequested = true;
        worker = std::move(m_worker);
    }

    m_condition.notify_all();

    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
        worker.join();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_stopRequested = false;
    }

    return core::OperationResult::ok();
}

void SimulatorAntennaAzimuthSource::updateAzimuth()
{
    if (!m_state || !m_state->isMoving()) {
        return;
    }

    if (const auto direction = m_state->manualMoveDirection()) {
        const auto current = m_state->currentAzimuthDeg();
        const auto currentCoord = core::AntennaMotionPlanner::toSafeCoord(current);
        const auto step = movementStep(m_state->movementSpeedDegPerSecond(),
                                       m_config.updatePeriod);
        const auto nextCoord =
            *direction == AntennaManualMoveCommand::Direction::Left
            ? std::max(0.0, currentCoord - step)
            : std::min(core::AntennaMotionPlanner::maxSafeCoordDeg, currentCoord + step);

        m_state->setCurrentAzimuthDeg(core::AntennaMotionPlanner::fromSafeCoord(nextCoord));
        if (nextCoord <= 0.0 || nextCoord >= core::AntennaMotionPlanner::maxSafeCoordDeg) {
            m_state->stop();
        }
        return;
    }

    if (const auto command = m_state->activeScanCommand()) {
        const auto currentCoord =
            core::AntennaMotionPlanner::toSafeCoord(m_state->currentAzimuthDeg());
        const auto targetCoord = command->safeEndCoordDeg;
        const auto directionSign =
            command->direction == core::ScanDirection::IncreasingSafeCoord ? 1.0 : -1.0;
        const auto distance = (targetCoord - currentCoord) * directionSign;
        const auto step = movementStep(command->speedDegPerSec, m_config.updatePeriod);

        if (step <= 0.0 || distance <= step) {
            m_state->setCurrentAzimuthDeg(command->endAzimuthDeg);
            m_state->setMoving(false);
            return;
        }

        m_state->setCurrentAzimuthDeg(
            core::AntennaMotionPlanner::fromSafeCoord(
                std::clamp(currentCoord + directionSign * step,
                           0.0,
                           core::AntennaMotionPlanner::maxSafeCoordDeg)));
        return;
    }

    const auto current = m_state->currentAzimuthDeg();
    const auto target = m_state->targetAzimuthDeg();
    const auto clockwise = clockwiseDistance(current, target);
    const auto counterClockwise = kFullCircleDeg - clockwise;
    if (clockwise == 0.0) {
        m_state->setMoving(false);
        return;
    }

    bool moveClockwise = clockwise <= counterClockwise;
    if (pathCrossesBlindZone(current, target, moveClockwise)
        && !pathCrossesBlindZone(current, target, !moveClockwise)) {
        moveClockwise = !moveClockwise;
    }

    const auto distance = moveClockwise ? clockwise : counterClockwise;
    const auto step = movementStep(m_state->movementSpeedDegPerSecond(), m_config.updatePeriod);

    if (step <= 0.0 || step >= distance) {
        m_state->setCurrentAzimuthDeg(target);
        m_state->setMoving(false);
        return;
    }

    const auto next = moveClockwise ? current + step : current - step;
    m_state->setCurrentAzimuthDeg(normalizeAzimuth(next));
}

void SimulatorAntennaAzimuthSource::generationLoop(AzimuthCallback callback)
{
    for (;;) {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopRequested) {
                break;
            }
        }

        updateAzimuth();
        callback(AntennaAzimuthSample{
            m_state ? m_state->currentAzimuthDeg() : 0.0,
            std::chrono::system_clock::now(),
        });

        std::unique_lock lock(m_mutex);
        if (m_condition.wait_for(lock, m_config.updatePeriod, [this] { return m_stopRequested; })) {
            break;
        }
    }
}

void SimulatorAntennaAzimuthSource::publish(infrastructure::DiagnosticSeverity severity,
                                            const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SimulatorAntennaAzimuthSource",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
