#ifndef ENGINE_EVENT_HPP
#define ENGINE_EVENT_HPP

#include "Common.hpp"
#include <memory>

enum class EventType { NEW_ORDER,CANCEL_ORDER };

// 撮合引擎事件结构体
struct EngineEvent {
    EventType type;
    std::shared_ptr<Order> order;        // 下单时使用
    uint64_t cancel_order_id = 0;       // 撤单时使用
    uint64_t cancel_user_id = 0;        // 撤单时使用
    uint32_t ticker_id = 0;
};

#endif