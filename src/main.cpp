#include "Logger.hpp"
#include "SocketServer.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

// 全局 Server 指针，用于信号处理
static SocketServer* g_server = nullptr;

// ---------- 信号处理（优雅退出） ----------
void signalHandler(int sig) {
    std::cout << "\n接收到退出信号，正在关闭..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    Logger::shutdown();
    exit(0);
}

// ---------- 消息回调函数（收到日志消息时的处理） ----------
void onLogMessage(int clientFd, const std::string& message) {
    // 将收到的消息通过日志系统写入文件
    std::cout << "[onLogMessage] 收到消息，fd=" << clientFd 
              << ", msg=" << message << std::endl;
    // 这里简单地将消息作为日志记录，并加上客户端标识
    Logger::log(LogLevel::INFO, __FILE__, __LINE__, 
                "Client " + std::to_string(clientFd) + ": " + message);

    // 可选：回复客户端确认
    // g_server->sendTo(clientFd, "ACK\n");
}

// ---------- 主函数 ----------
int main() {
    std::cout << "===== 高并发异步日志采集系统 =====" << std::endl;

    // ---- 1. 初始化日志系统 ----
    LogConfig config;
    config.logFilePath = "./logs/app.log";
    config.minLevel = LogLevel::DEBUG;
    config.maxFileSizeMB = 10;      // 10MB 滚动
    config.flushIntervalMs = 1000;  // 1秒刷盘

    if (!Logger::init(config)) {
        std::cerr << "日志系统初始化失败" << std::endl;
        return 1;
    }
    std::cout << "日志系统初始化成功" << std::endl;

    // ---- 2. 启动 TCP 服务端 ----
    SocketServer server(8888);
    server.setMessageCallback(onLogMessage);

    if (!server.start()) {
        std::cerr << "服务端启动失败" << std::endl;
        return 1;
    }
    std::cout << "服务端启动成功，监听端口 8888" << std::endl;
    g_server = &server;

    // ---- 3. 注册信号处理（Ctrl+C 优雅退出） ----
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "等待客户端连接... (按 Ctrl+C 退出)" << std::endl;

    // ---- 4. 模拟主线程工作（或直接等待） ----
    // 这里我们让主线程循环打印统计信息（可选）
    int count = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        count++;
        std::cout << "[心跳] 运行 " << count * 5 << " 秒，日志队列大小: " 
                  << Logger::queueSize() << std::endl;
    }

    return 0;
}
