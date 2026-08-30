#ifndef ASYNC_LOGGER_HPP
#define ASYNC_LOGGER_HPP

#include "Logger.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <fstream>

/**
 * 异步日志核心引擎
 * 
 * 架构：
 *   [前端调用] -> push() -> [无锁队列] -> 后端线程 -> [文件写入]
 * 
 * 特性：
 *   - 双线程解耦：前端推送不阻塞，后端独立刷盘
 *   - 支持批量写入：减少磁盘IO次数
 *   - 支持定时刷盘：按时间或缓冲区大小触发
 *   - 优雅关闭：确保所有日志在退出前全部落盘
 */
class AsyncLogger {
public:
    explicit AsyncLogger(const LogConfig& config);
    ~AsyncLogger();

    // 启动后端线程
    bool start();

    // 停止后端线程（等待所有日志写入完成）
    void stop();

    // 推送日志到队列（前端调用，非阻塞）
    void push(const std::string& logEntry);

    // 获取队列大小（用于监控）
    size_t queueSize() const;

private:
    // 后端线程主循环
    void backendLoop();

    // 将缓冲区内容写入文件
    void flushBuffer();

    // 检查是否需要滚动日志文件
    void checkRollover();

private:
    LogConfig m_config;
    
    // --- 线程与同步 ---
    std::thread m_backendThread;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    
    // --- 日志队列（多生产者单消费者） ---
    std::vector<std::string> m_buffer;          // 当前累积的日志行
    std::vector<std::string> m_swapBuffer;      // 交换缓冲区（减少锁竞争）
    size_t m_maxBufferSize = 1024 * 100;        // 最大缓冲行数（100KB行）
    
    // --- 文件相关 ---
    std::ofstream m_logFile;
    std::string m_currentFilePath;
    size_t m_currentFileSize = 0;
    
    // --- 统计信息（便于监控） ---
    std::atomic<size_t> m_totalLogs{0};
    std::atomic<size_t> m_droppedLogs{0};
};

#endif // ASYNC_LOGGER_HPP
