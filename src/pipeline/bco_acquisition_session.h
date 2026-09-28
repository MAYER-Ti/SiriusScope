#pragma once

#include "pipeline/acquisition_control.h"
#include "pipeline/source_to_pipeline_bridge.h"
#include <mutex>

namespace siriusscope::pipeline {

//! Qt-free соединение источника с bounded RX queue.
//! source, pipeline и diagnostics должны пережить сессию. stop() источника
//! обязан завершить callbacks перед возвратом; деструктор останавливает источник
//! раньше очереди. Ожидания start/stop/flush — операции control plane.
class BcoAcquisitionSession final : public IAcquisitionControl
{
public:
    BcoAcquisitionSession(hardware::IBcoStreamSource* source, DataIngestPipeline* pipeline,
                          infrastructure::IDiagnosticsSink* diagnostics = nullptr,
                          SourceToPipelineBridgeConfig config = {32, RxOverflowPolicy::DropNewest});
    ~BcoAcquisitionSession() override;
    //! Перепривязывает источник только после остановки источника и закрытия входа.
    //! nullptr отключает приём; новый источник должен пережить сессию/следующую замену.
    core::OperationResult setSource(hardware::IBcoStreamSource* source);
    core::OperationResult startSource() override;
    core::OperationResult stopSource() override;
    bool sourceActive() const noexcept override { return m_active; }
    core::OperationResult openInput() override;
    void closeInput(bool drain) override;
    void setAccepting(bool accepting) override;
    void clearInput() override;
    core::OperationResult flushInput(std::chrono::milliseconds timeout) override;
    SourceToPipelineBridgeMetrics metrics() const;

private:
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;
    hardware::IBcoStreamSource* m_source;
    DataIngestPipeline* m_pipeline;
    infrastructure::IDiagnosticsSink* m_diagnostics;
    SourceToPipelineBridge m_bridge;
    // Gate synchronizes disabling input with any callback already in submit().
    std::mutex m_deliveryMutex;
    bool m_accepting = false;
    bool m_active = false;
};

} // namespace siriusscope::pipeline
