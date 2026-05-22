#include "signalsamplebus.h"

#include <algorithm>
#include <utility>

namespace siriusscope::app {

int SignalSampleBus::subscribe(Callback callback)
{
    if (!callback) {
        return 0;
    }

    std::lock_guard lock(m_mutex);
    const int id = m_nextSubscriptionId++;
    m_subscriptions.push_back(Subscription{id, std::move(callback)});
    return id;
}

void SignalSampleBus::unsubscribe(int subscriptionId)
{
    std::lock_guard lock(m_mutex);
    const auto removed =
        std::remove_if(m_subscriptions.begin(),
                       m_subscriptions.end(),
                       [subscriptionId](const auto& subscription) {
                           return subscription.id == subscriptionId;
                       });
    m_subscriptions.erase(removed, m_subscriptions.end());
}

void SignalSampleBus::publish(const std::vector<core::SignalSample>& samples)
{
    if (samples.empty()) {
        return;
    }

    std::vector<Callback> callbacks;
    {
        std::lock_guard lock(m_mutex);
        callbacks.reserve(m_subscriptions.size());
        for (const auto& subscription : m_subscriptions) {
            callbacks.push_back(subscription.callback);
        }
    }

    for (const auto& callback : callbacks) {
        callback(samples);
    }
}

} // namespace siriusscope::app
