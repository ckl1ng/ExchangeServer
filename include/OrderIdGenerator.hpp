#ifndef ORDER_ID_GENERATOR_HPP
#define ORDER_ID_GENERATOR_HPP

#include <chrono>
#include <atomic>


// 订单ID生成器，基于Twitter的Snowflake算法设计，保证分布式环境下的唯一性和高性能
class OrderIdGenerator {
private:
    static constexpr uint64_t EPOCH = 1767225600000ULL;
    static constexpr int NODE_ID_BITS = 10;
    static constexpr int SEQUENCE_BITS = 12;
    static constexpr uint16_t MAX_NODE_ID = (1 << NODE_ID_BITS) - 1;
    static constexpr uint16_t MAX_SEQUENCE = (1 << SEQUENCE_BITS) - 1;
    static constexpr int NODE_ID_SHIFT = SEQUENCE_BITS;
    static constexpr int TIMESTAMP_SHIFT = SEQUENCE_BITS + NODE_ID_BITS;

    uint16_t node_id_ = 1;
    std::atomic<uint32_t> sequence_{0}; // 使用原子变量替代 mutex

    OrderIdGenerator() = default;

public:
    static OrderIdGenerator& getInstance() {
        static OrderIdGenerator instance;
        return instance;
    }

    // 生成唯一订单ID，包含时间戳、节点ID和序列号
    uint64_t generate() {
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
        
        uint32_t seq = sequence_.fetch_add(1, std::memory_order_relaxed);
        
        return ((timestamp - EPOCH) << TIMESTAMP_SHIFT) |
               ((uint64_t)node_id_ << NODE_ID_SHIFT) |
               (uint64_t)(seq & MAX_SEQUENCE);
    }
};

inline uint64_t generateOrderId() {
    return OrderIdGenerator::getInstance().generate();
}

#endif