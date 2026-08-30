#include "Logger.hpp"
#include "AsyncLogger.hpp"  // 下一节课会实现

#include <iostream>
#include <mutex>

static AsyncLogger* g_asyncLogger = nullptr;
static LogLevel g_minLevel = LogLevel::DEBUG;
static std::mutex g_mutex;

bool Logger::init(const LogConfig& config) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_asyncLogger != nullptr) {
        return false;  // 已经初始化过了
    }
    
    g_minLevel = config.minLevel;
    g_asyncLogger = new AsyncLogger(config);
    return g_asyncLogger->start();
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_asyncLogger != nullptr) {
        g_asyncLogger->stop();
        delete g_asyncLogger;
        g_asyncLogger = nullptr;
    }
}

void Logger::log(LogLevel level, const char* file, int line, const std::string& message) {
    // 🔍 添加调试输出
    std::cout << "[Logger::log] 被调用, level=" << static_cast<int>(level) 
              << ", g_asyncLogger=" << (g_asyncLogger ? "非空" : "空指针") << std::endl;

    if (level < g_minLevel) {
        std::cout << "[Logger::log] 级别被过滤" << std::endl;
        return;
    }

    std::string formatted = std::string("[") + 
                            std::to_string(static_cast<int>(level)) + "] " +
                            std::string(file) + ":" + std::to_string(line) + " - " +
                            message;

    if (g_asyncLogger != nullptr) {
        std::cout << "[Logger::log] 调用 push" << std::endl;
        g_asyncLogger->push(formatted);
    } else {
        std::cout << "[Logger::log] 降级输出: " << formatted << std::endl;
    }
}

void Logger::setLevel(LogLevel level) {
    g_minLevel = level;
}

size_t Logger::queueSize() {
    if (g_asyncLogger) {
        return g_asyncLogger->queueSize();
    }
    return 0;
}

