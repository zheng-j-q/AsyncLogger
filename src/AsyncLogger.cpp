#include "AsyncLogger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ---------- 构造函数 ----------
AsyncLogger::AsyncLogger(const LogConfig& config)
    : m_config(config)
    , m_maxBufferSize(1024 * 100)  // 10万行缓冲区
{
    // 确保日志目录存在
    fs::path logDir = fs::path(m_config.logFilePath).parent_path();
    if (!logDir.empty() && !fs::exists(logDir)) {
        fs::create_directories(logDir);
    }
    
    // 预分配缓冲区，避免频繁扩容
    m_buffer.reserve(m_maxBufferSize);
    m_swapBuffer.reserve(m_maxBufferSize);
}

// ---------- 析构函数 ----------
AsyncLogger::~AsyncLogger() {
    stop();
}

// ---------- 启动后端线程 ----------
bool AsyncLogger::start() {
    if (m_running.load()) {
        return false;  // 已经启动
    }
    
    // 打开日志文件
    m_currentFilePath = m_config.logFilePath;
    m_logFile.open(m_currentFilePath, std::ios::app);
    if (!m_logFile.is_open()) {
        std::cerr << "[AsyncLogger] 无法打开日志文件: " << m_currentFilePath << std::endl;
        return false;
    }
    
    // 获取当前文件大小
    if (fs::exists(m_currentFilePath)) {
        m_currentFileSize = fs::file_size(m_currentFilePath);
    }
    
    // 启动后端线程
    m_running.store(true);
    m_backendThread = std::thread(&AsyncLogger::backendLoop, this);
    
    return true;
}

// ---------- 停止后端线程 ----------
void AsyncLogger::stop() {
    if (!m_running.load()) {
        return;
    }
    
    // 通知后端线程退出
    m_running.store(false);
    m_cv.notify_one();
    
    // 等待后端线程结束
    if (m_backendThread.joinable()) {
        m_backendThread.join();
    }
    
    // 关闭文件
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

// ---------- 推送日志到队列（前端调用） ----------
void AsyncLogger::push(const std::string& logEntry) {
    // 🔍 打印入口
    std::cout << "[AsyncLogger::push] 进入，m_running=" << m_running.load() 
              << ", 消息: " << logEntry << std::endl;

    if (!m_running.load()) {
        m_droppedLogs++;
        std::cout << "[AsyncLogger::push] ❌ m_running=false，丢弃日志" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[AsyncLogger::push] 已加锁，当前缓冲区大小: " << m_buffer.size() << std::endl;

    if (m_buffer.size() >= m_maxBufferSize) {
        m_droppedLogs++;
        std::cout << "[AsyncLogger::push] ❌ 缓冲区已满，丢弃日志" << std::endl;
        return;
    }

    m_buffer.push_back(logEntry);
    m_totalLogs++;
    std::cout << "[AsyncLogger::push] ✅ 成功添加，缓冲区大小: " << m_buffer.size() 
              << ", 总日志数: " << m_totalLogs << std::endl;

    if (m_buffer.size() >= m_maxBufferSize / 2) {
        m_cv.notify_one();
        std::cout << "[AsyncLogger::push] 通知后端线程刷盘" << std::endl;
    }
}
// ---------- 后端线程主循环 ----------
void AsyncLogger::backendLoop() {
    // 记录上次刷盘时间
    auto lastFlushTime = std::chrono::steady_clock::now();
    
    while (m_running.load()) {
        // ---- 等待条件触发 ----
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            
            // 等待以下条件之一：
            // 1. 缓冲区达到阈值
            // 2. 超过刷盘间隔
            // 3. 被通知退出
            m_cv.wait_for(lock, std::chrono::milliseconds(m_config.flushIntervalMs), [this]() {
                return !m_running.load() || 
                       m_buffer.size() >= m_maxBufferSize / 2;
            });
            
            // 交换缓冲区（减少锁持有时间）
            m_swapBuffer.swap(m_buffer);
        }
        
        // ---- 写入文件（不持有锁） ----
        if (!m_swapBuffer.empty()) {
            for (const auto& line : m_swapBuffer) {
                m_logFile << line << std::endl;
                m_currentFileSize += line.size() + 1;  // +1 for newline
            }
            m_logFile.flush();
            m_swapBuffer.clear();
            lastFlushTime = std::chrono::steady_clock::now();
        } else if (m_running.load()) {
            // 即使没有日志，也定期刷新（保证文件写入落盘）
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime);
            if (elapsed.count() >= m_config.flushIntervalMs) {
                m_logFile.flush();
                lastFlushTime = now;
            }
        }
        
        // ---- 检查是否需要滚动日志 ----
        checkRollover();
    }
    
    // ---- 退出前最后一次刷盘（保证所有日志都写入） ----
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_swapBuffer.swap(m_buffer);
    }
    
    if (!m_swapBuffer.empty()) {
        for (const auto& line : m_swapBuffer) {
            m_logFile << line << std::endl;
        }
        m_logFile.flush();
    }
}

// ---------- 检查是否需要滚动日志文件 ----------
void AsyncLogger::checkRollover() {
    if (m_config.maxFileSizeMB <= 0) {
        return;  // 不限制大小
    }
    
    size_t maxBytes = static_cast<size_t>(m_config.maxFileSizeMB) * 1024 * 1024;
    if (m_currentFileSize < maxBytes) {
        return;
    }
    
    // ---- 滚动日志 ----
    // 关闭当前文件
    m_logFile.close();
    
    // 重命名现有文件（添加时间戳）
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << m_config.logFilePath << "." << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S");
    std::string backupPath = ss.str();
    
    // 如果备份文件已存在，追加序号
    int idx = 1;
    while (fs::exists(backupPath)) {
        ss.str("");
        ss << m_config.logFilePath << "." << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S")
           << "_" << idx++;
        backupPath = ss.str();
    }
    
    fs::rename(m_currentFilePath, backupPath);
    
    // 重新打开文件
    m_logFile.open(m_currentFilePath, std::ios::app);
    m_currentFileSize = 0;
    
    std::cout << "[AsyncLogger] 日志滚动: " << backupPath << std::endl;
}

// ---------- 获取队列大小 ----------
size_t AsyncLogger::queueSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size();
}
