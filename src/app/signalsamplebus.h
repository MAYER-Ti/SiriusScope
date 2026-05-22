#pragma once

#include "core/domain_models.h"

#include <functional>
#include <mutex>
#include <vector>

namespace siriusscope::app {

class SignalSampleBus
{
public:
    using Callback = std::function<void(std::vector<core::SignalSample>)>;

    int subscribe(Callback callback);
    void unsubscribe(int subscriptionId);
    void publish(const std::vector<core::SignalSample>& samples);

private:
    struct Subscription
    {
        int id = 0;
        Callback callback;
    };

    std::mutex m_mutex;
    std::vector<Subscription> m_subscriptions;
    int m_nextSubscriptionId = 1;
};

} // namespace siriusscope::app
