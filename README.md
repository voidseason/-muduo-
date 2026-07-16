# Muduo-like Network Library

基于 Reactor 多线程模型的 C++ 网络库，参考 muduo 库设计实现，采用 **one loop per thread** 架构。

## 特性

- **Reactor 多线程模型**：主 Reactor 接收连接，从 Reactor 处理 IO 事件
- **epoll IO 多路复用**：基于边缘触发（ET）的事件监控
- **时间轮定时器**：基于 timerfd 的高效定时器，支持连接超时自动销毁
- **Buffer 缓冲区**：自动扩容的读写缓冲区
- **LoopThreadPool 线程池**：多线程事件循环，轮询分配连接
- **TcpServer 封装**：完整的服务端框架，支持自定义回调
- **HTTP 服务器**（`http/http.hpp`）：完整的 HTTP/1.1 协议实现，支持静态资源服务、路由分发
- **日志模块**（`log.hpp`）：轻量级日志，支持级别/时间戳/线程 ID/文件行号

## 快速开始

### 编译

项目使用 Makefile 构建，确保系统支持 epoll。

```bash
# Echo 服务器
cd echo && make

# HTTP 服务器
cd http && make
```

### Echo 服务器

```cpp
#include "echo/echo.hpp"

int main() {
    EchoServer server(8080);
    server.Start();  // one loop per thread 模型启动
    return 0;
}
```

### HTTP 服务器

```cpp
#include "http/http.hpp"

int main() {
    HttpServer server(8080);
    server.SetThreadCount(3);
    server.GET("/", [](const HttpRequest &req, HttpResponse &resp) {
        resp.SetContent("<h1>Hello World</h1>");
    });
    server.Start();
    return 0;
}
```

## 项目结构

```
├── server.hpp          # 核心网络库（Buffer / Channel / Poller / EventLoop / TcpServer ...）
├── log.hpp             # 日志模块
├── echo/               # Echo 服务器示例
│   ├── echo.hpp
│   └── main.cc
├── http/               # HTTP 服务器
│   ├── http.hpp        # HTTP 协议实现 + 路由分发 + 静态资源
│   ├── main.cc
│   └── wwwroot/        # 静态资源目录
└── Test/               # 测试用例
    ├── server.cc
    └── client1-5.cpp   # 多种场景客户端测试
```

## 核心组件

| 组件 | 说明 |
|------|------|
| `Buffer` | 基于 `std::vector<char>` 的读写缓冲区，自动扩容 |
| `Socket` | 套接字封装，支持非阻塞和地址复用 |
| `Channel` | 事件通道，管理 fd 的事件回调 |
| `Poller` | epoll 事件监控封装 |
| `TimerWheel` | 60 格时间轮定时器 |
| `EventLoop` | 核心事件循环，整合 Poller/TimerWheel/任务队列 |
| `LoopThreadPool` | 线程池，管理多个 EventLoop 线程 |
| `Connection` | TCP 连接管理，含输入/输出缓冲区、状态机、超时控制 |
| `Acceptor` | 监听套接字，接收新连接 |
| `TcpServer` | 服务端总封装，整合上述所有组件 |

## 环境要求

- Linux（依赖 epoll、eventfd、timerfd）
- C++11 及以上
- Make
