#ifndef SOCKET_SERVER_HPP
#define SOCKET_SERVER_HPP

#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <map>

/**
 * TCP 服务端，基于 epoll 事件驱动，支持高并发连接
 * 
 * 核心功能：
 *   - 非阻塞 socket + epoll 边缘触发
 *   - 自定义协议处理（粘包拆包）
 *   - 每个连接的数据通过回调函数交给上层处理
 */
class SocketServer {
public:
    // 回调函数类型：当收到完整消息时调用，参数为 socket fd 和消息内容
    using MessageCallback = std::function<void(int, const std::string&)>;

    SocketServer(int port, int maxConnections = 1024);
    ~SocketServer();

    // 启动服务端（非阻塞，内部开启事件循环线程）
    bool start();

    // 停止服务端
    void stop();

    // 设置消息处理回调
    void setMessageCallback(MessageCallback cb) { m_messageCb = std::move(cb); }

    // 向指定客户端发送数据
    bool sendTo(int clientFd, const std::string& data);

private:
    // 事件循环（在独立线程中运行）
    void eventLoop();

    // 处理新连接
    void handleNewConnection();

    // 处理客户端数据（读事件）
    void handleClientData(int clientFd, uint32_t events);

    // 关闭客户端连接
    void closeClient(int clientFd);

    // 处理粘包拆包（从接收缓冲区解析出完整消息）
    void parseMessages(int clientFd, const std::string& data);

private:
    int m_port;
    int m_maxConnections;
    int m_listenFd = -1;
    int m_epollFd = -1;
    std::atomic<bool> m_running{false};
    std::thread m_loopThread;

    // 每个客户端未处理完的粘包数据缓冲区
    struct ClientBuffer {
        std::string buffer;      // 未处理的数据
        size_t expectedLen = 0;  // 当前期望的消息长度
    };
    std::map<int, ClientBuffer> m_clientBuffers;

    MessageCallback m_messageCb;

    static constexpr int MAX_EVENTS = 64;
};

#endif // SOCKET_SERVER_HPP
