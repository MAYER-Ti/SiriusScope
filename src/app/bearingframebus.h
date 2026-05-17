#pragma once

#include "processing/sample_processor.h"

#include <functional>
#include <mutex>
#include <vector>

namespace siriusscope::app {

class BearingFrameBus
{
public:
    using Callback = std::function<void(std::vector<processing::BearingInputFrame>)>;

    int subscribe(Callback callback);
    void unsubscribe(int subscriptionId);
    void publish(const std::vector<processing::BearingInputFrame>& frames);

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
