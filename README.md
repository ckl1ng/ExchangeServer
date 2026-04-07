# 高性能内存撮合交易系统 (C++ Trading Engine)

一个基于 **Reactor + 多线程 + 无锁队列** 架构的低延迟交易引擎，支持订单撮合、资产清算、WAL 持久化及市场行情推送。  
适用于教学演示、性能测试或作为生产级交易系统的基础框架。

## ✨ 核心特性

- **内存撮合引擎**  
  每个交易标的独立 `OrderBook`，采用价格优先、时间优先的 FIFO 匹配算法。
- **金融级资产安全**  
  双重记账：下单时**冻结**资产（资金/股票），成交后**结算**，撤单时**解冻**。  
  数据库操作均使用 `WHERE balance >= amount` 防止超卖/透支。
- **高吞吐架构**  
  - Epoll 边缘触发 (ET) + 非阻塞 IO  
  - 线程池分离 IO 与业务逻辑  
  - 无锁并发队列 (`moodycamel::ConcurrentQueue`)  
  - 分段锁 `UserManager`，消除热点竞争
- **可靠性保障**  
  - **WAL (Write-Ahead Logging)**：订单与撤单请求先落盘，再处理  
  - 连接超时管理 (60s 自动清理)  
  - MySQL / Redis 连接池，支持自动重连
- **行情推送**  
  成交记录异步写入 Redis ZSET (`history:trade:<ticker_id>`)，供 K 线/走势图使用。
- **二进制协议**  
  紧凑的 `#pragma pack(1)` 结构体，减少网络开销。

## 🏗 系统架构

```text
Client (TCP) 
    ↓
TcpServer (Epoll + ThreadPool) 
    ↓
Router → Handler (业务逻辑)
    ↓
WALManager (落盘) → MatchingEngine (撮合)
    ↓
OrderBookProcessor (每个标的独立线程)
    ↓
MarketDataPublisher (Redis)  &  SettlementProcessor (MySQL资产变更)
```

## 📦 依赖组件

| 组件           | 版本/说明                        |
| -------------- | -------------------------------- |
| C++ 标准       | C++17                            |
| 编译器         | GCC 9+ / Clang 12+               |
| MySQL          | 8.0+ (需 `libmysqlclient-dev`)   |
| Redis          | 6.0+ (需 `hiredis`)              |
| 第三方库       | `moodycamel::ConcurrentQueue` (已内置) |
| 构建工具       | CMake 3.10+                      |

## 🔧 编译与运行

### 1. 安装依赖（Ubuntu/Debian 示例）

```bash
sudo apt update
sudo apt install libmysqlclient-dev libhiredis-dev cmake g++
```

### 2. 数据库初始化

创建数据库 `test_db` 并执行以下 SQL（示例表结构）：

```sql
CREATE TABLE accounts (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) UNIQUE NOT NULL,
    password VARCHAR(32) NOT NULL,
    balance BIGINT NOT NULL DEFAULT 0   -- 金额单位：分（*10000）
);

CREATE TABLE holdings (
    username VARCHAR(32) NOT NULL,
    symbol VARCHAR(16) NOT NULL,
    quantity INT NOT NULL,
    PRIMARY KEY (username, symbol)
);

CREATE TABLE stocks (
    id INT AUTO_INCREMENT PRIMARY KEY,
    symbol VARCHAR(16) UNIQUE NOT NULL
);
INSERT INTO stocks (symbol) VALUES ('AAPL'), ('NVDA'), ('TSLA');  -- 示例
```

### 3. 编译项目

```bash
mkdir build
cd build 
cmake .. 
cmake --build
```

生成两个可执行文件：`server` 和 `client`。

### 4. 启动服务端

```bash
./server
```

默认监听 `0.0.0.0:12345`，日志输出到 `server.log` 及终端。

### 5. 运行客户端测试

```bash
# 查看 root 账户持仓与市场股票列表
./client 0 0 2

# 压力测试（500 并发，每线程 2000 请求，混合模式）
./client 500 2000 0

# 纯交易压测（下单为主）
./client 500 2000 1
```

## 📡 二进制协议说明

所有网络包均遵循 **包头 + 包体** 结构：

```cpp
struct PacketHeader {
    uint32_t length;   // 包体字节数（网络序）
    MsgType  type;     // 消息类型（网络序）
};
```

**消息类型示例**（详见 `Protocol.hpp`）：

| 类型              | 值    | 方向          | 说明               |
| ----------------- | ----- | ------------- | ------------------ |
| `REGISTER_REQ`    | 1001  | C→S           | 注册请求           |
| `LOGIN_REQ`       | 1003  | C→S           | 登录请求           |
| `TRADE_REQ`       | 3001  | C→S           | 下单请求           |
| `CANCEL_REQ`      | 3003  | C→S           | 撤单请求           |
| `MARKET_REQ`      | 4003  | C→S           | 查询盘口行情       |
| `ALL_STOCKS_REQ`  | 4011  | C→S           | 查询全部股票代码   |

响应类型为请求类型 + 1（如 `LOGIN_RES` = 1004）。  
详细结构体定义请参考 `Protocol.hpp`。

## ⚙️ 核心模块详解

### `MatchingEngine` 与 `OrderBook`
- 每个股票独立 `OrderBook`，内部维护买盘 (`bids_`) 和卖盘 (`asks_`) 的 `std::map`。
- `OrderBookProcessor` 为每个标的启动一个专用线程，从无锁队列中消费事件，**避免锁争用**。

### `WALManager`
- 所有订单/撤单请求先写入 `wal.bin` 二进制文件，再转发给引擎。
- 崩溃恢复时可重放日志（当前版本未实现恢复逻辑，但保留了完整记录）。

### `SettlementProcessor`
- 异步消费成交记录，更新 MySQL 中的余额与持仓。
- 使用 `UPDATE ... WHERE balance >= ?` 保证资金安全。
- 同时更新内存中的 `UserManager` 缓存，避免后续查询穿透数据库。

### `UserManager`
- **分段锁** (`256` 个桶) 实现高并发用户状态读写。
- 缓存用户的可用/冻结资金、可用/冻结股票。
- 提供原子化的 `tryFreeze` / `commit` / `unfreeze` 接口。

### `MarketDataPublisher`
- 将成交记录批量写入 Redis ZSET，按时间戳排序，用于绘制分时图。
- 异步推送，不阻塞主撮合路径。

## 📊 性能调优建议

- **数据库连接池**：`DBPool` 大小建议设置为 CPU 核心数的 2~4 倍。
- **Redis 连接池**：同上。
- **线程池**：`TcpServer` 构造函数中的线程数建议 = CPU 核心数。
- **OrderBookProcessor 线程数**：每个股票一个线程，若标的数量过多可改为线程池复用（当前设计适合少量活跃标的）。
- **编译优化**：`-O3 -march=native -flto`。

## 🧪 压力测试结果参考

**测试环境**：Intel i9-13900 + 16GB 内存，WSL2 (Ubuntu) on Windows，通过 VSCode 连接。

在 **500 并发连接** 下，完成 **100 万次核心交易请求**：
- **总耗时**：5.27 秒
- **吞吐量**：189,717 QPS
- **平均延迟**：2.58 毫秒
- **成功率**：100%
- **内存泄漏**：零内存泄漏

## 🗂 项目文件清单

| 文件                     | 职责                                       |
| ------------------------ | ------------------------------------------ |
| `Common.hpp`             | 基础数据结构（Order, MatchRecord 等）      |
| `Protocol.hpp`           | 网络协议定义（二进制结构体）               |
| `TcpServer.{h,cpp}`      | Epoll + 线程池网络框架                     |
| `Router.hpp`             | 消息类型到业务 Handler 的路由              |
| `Handler.hpp`            | 所有具体业务处理器实现（登录、下单、查询等）|
| `MatchingEngine.hpp`     | 撮合引擎入口，管理多个 OrderBookProcessor  |
| `OrderBook.hpp`          | 单一标的的订单簿与匹配算法                 |
| `OrderBookProcessor.hpp` | 每个标的的异步处理线程                     |
| `WALManager.hpp`         | 预写日志，保证请求不丢失                   |
| `SettlementProcessor.hpp`| 成交清算，更新数据库与内存缓存             |
| `UserManager.hpp`        | 用户资产内存缓存（分段锁）                 |
| `TickerManager.hpp`      | 股票 ID ↔ Symbol 映射缓存                 |
| `DBPool.{h,cpp}`         | MySQL 连接池                               |
| `RedisPool.{h,cpp}`      | Redis 连接池                               |
| `Logger.hpp`             | 异步日志系统                               |
| `client.cpp`             | 压测与查询客户端                           |
| `CMakeLists.txt`         | 构建配置                                   |

## ⚠️ 注意事项

- **数据库配置**：请在 `main.cpp` 中修改 MySQL 和 Redis 的连接参数（目前硬编码为 `127.0.0.1/root/root`）。
- **WAL 文件**：运行目录下会生成 `wal.bin`，请定期归档或清理。
- **安全性**：示例代码中密码明文存储，生产环境应使用 `sha256` 等哈希加盐。
- **Redis 未设置密码**：若 Redis 有密码，请修改 `RedisPool::init` 中的 `password` 参数。

## 📄 License

MIT License

---

**欢迎贡献代码、提交 Issue 或 Star 本项目！**  
若有疑问，可查阅源码注释或联系作者。