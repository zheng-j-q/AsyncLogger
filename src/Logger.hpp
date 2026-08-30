#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

// 日志级别枚举
enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

// 日志配置结构体
struct LogConfig {
    std::string logFilePath = "./logs/app.log";  // 日志文件路径
    LogLevel minLevel = LogLevel::DEBUG;          // 最低记录级别
    int maxFileSizeMB = 100;                      // 单文件最大大小(MB)
    int flushIntervalMs = 1000;                   // 刷盘间隔(毫秒)
};

// 全局日志接口
class Logger {
public:
    // 初始化日志系统
    static bool init(const LogConfig& config);
    
    // 关闭日志系统（等待队列刷完）
    static void shutdown();
    
    // 写入日志（供宏使用）
    static void log(LogLevel level, const char* file, int line, const std::string& message);
    
    // 设置日志级别
    static void setLevel(LogLevel level);
    
    static size_t queueSize();
    
private:
    Logger() = default;
};

#endif // LOGGER_HPP
