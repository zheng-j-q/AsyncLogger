#include "SocketServer.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <cstring>
#include <errno.h>

static int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

SocketServer::SocketServer(int port, int maxConnections)
    : m_port(port), m_maxConnections(maxConnections) {}

SocketServer::~SocketServer() { stop(); }

bool SocketServer::start() {
    if (m_running.load()) return false;

    m_listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (m_listenFd < 0) {
        std::cerr << "[SocketServer] 创建 socket 失败: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);

    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[SocketServer] 绑定端口失败: " << strerror(errno) << std::endl;
        close(m_listenFd);
        return false;
    }

    if (listen(m_listenFd, SOMAXCONN) < 0) {
        std::cerr << "[SocketServer] 监听失败: " << strerror(errno) << std::endl;
        close(m_listenFd);
        return false;
    }

    m_epollFd = epoll_create1(0);
    if (m_epollFd < 0) {
        std::cerr << "[SocketServer] 创建 epoll 失败: " << strerror(errno) << std::endl;
        close(m_listenFd);
        return false;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = m_listenFd;
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_listenFd, &ev) < 0) {
        std::cerr << "[SocketServer] epoll 添加监听失败" << std::endl;
        close(m_listenFd);
        close(m_epollFd);
        return false;
    }

    m_running.store(true);
    m_loopThread = std::thread(&SocketServer::eventLoop, this);

    std::cout << "[SocketServer] 启动成功，监听端口 " << m_port << std::endl;
    return true;
}

void SocketServer::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_loopThread.joinable()) {
        if (m_epollFd >= 0) {
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_listenFd, nullptr);
            close(m_epollFd);
        }
        m_loopThread.join();
    }
    for (auto& pair : m_clientBuffers) close(pair.first);
    m_clientBuffers.clear();
    if (m_listenFd >= 0) { close(m_listenFd); m_listenFd = -1; }
    std::cout << "[SocketServer] 已停止" << std::endl;
}

bool SocketServer::sendTo(int clientFd, const std::string& data) {
    ssize_t sent = send(clientFd, data.c_str(), data.size(), 0);
    return sent == static_cast<ssize_t>(data.size());
}

void SocketServer::eventLoop() {
    struct epoll_event events[MAX_EVENTS];
    while (m_running.load()) {
        int nfds = epoll_wait(m_epollFd, events, MAX_EVENTS, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[SocketServer] epoll_wait 错误: " << strerror(errno) << std::endl;
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                if (fd == m_listenFd) continue;
                closeClient(fd);
                continue;
            }
            if (fd == m_listenFd) {
                handleNewConnection();
                continue;
            }
            if (events[i].events & EPOLLIN) {
                handleClientData(fd, events[i].events);
            }
        }
    }
}

void SocketServer::handleNewConnection() {
    while (m_running.load()) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept4(m_listenFd, (struct sockaddr*)&clientAddr, &addrLen, SOCK_NONBLOCK);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[SocketServer] accept 失败: " << strerror(errno) << std::endl;
            break;
        }
        if (m_clientBuffers.size() >= static_cast<size_t>(m_maxConnections)) {
            close(clientFd);
            continue;
        }
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = clientFd;
        if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, clientFd, &ev) < 0) {
            close(clientFd);
            continue;
        }
        m_clientBuffers[clientFd] = ClientBuffer();
        std::cout << "[SocketServer] 新连接: fd=" << clientFd << std::endl;
    }
}

void SocketServer::handleClientData(int clientFd, uint32_t events) {
    char buffer[4096];
    ssize_t bytesRead;
    while (m_running.load()) {
        bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[SocketServer] recv 错误: " << strerror(errno) << std::endl;
            closeClient(clientFd);
            return;
        } else if (bytesRead == 0) {
            std::cout << "[SocketServer] 客户端断开: fd=" << clientFd << std::endl;
            closeClient(clientFd);
            return;
        }
        std::cout << "[SocketServer] 收到数据: fd=" << clientFd 
                  << ", bytes=" << bytesRead << std::endl;
        std::cout << "[SocketServer] 数据内容: " << std::string(buffer, bytesRead) << std::endl;
        auto& clientBuf = m_clientBuffers[clientFd];
        clientBuf.buffer.append(buffer, bytesRead);
        parseMessages(clientFd, clientBuf.buffer);
    }
}

void SocketServer::parseMessages(int clientFd, const std::string& data) {
    auto& clientBuf = m_clientBuffers[clientFd];
    size_t processed = 0;

    while (true) {
        if (clientBuf.expectedLen == 0) {
            if (clientBuf.buffer.size() - processed < 4) break;
            uint32_t netLen;
            memcpy(&netLen, clientBuf.buffer.data() + processed, 4);
            clientBuf.expectedLen = ntohl(netLen);
            if (clientBuf.expectedLen > 1024 * 1024) {
                std::cerr << "[SocketServer] 非法消息长度: " << clientBuf.expectedLen 
                          << "，关闭连接 fd=" << clientFd << std::endl;
                closeClient(clientFd);
                return;
            }
            processed += 4;
        }
        size_t remaining = clientBuf.buffer.size() - processed;
        if (remaining < clientBuf.expectedLen) break;
        if (processed + clientBuf.expectedLen > clientBuf.buffer.size()) {
            std::cerr << "[SocketServer] 数据越界，关闭连接 fd=" << clientFd << std::endl;
            closeClient(clientFd);
            return;
        }
        std::string message = clientBuf.buffer.substr(processed, clientBuf.expectedLen);
        processed += clientBuf.expectedLen;
        clientBuf.expectedLen = 0;
        if (m_messageCb) {
            m_messageCb(clientFd, message);
        }
    }
    if (processed > 0) {
        if (processed <= clientBuf.buffer.size()) {
            clientBuf.buffer.erase(0, processed);
        } else {
            clientBuf.buffer.clear();
        }
    }
}

void SocketServer::closeClient(int clientFd) {
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
    m_clientBuffers.erase(clientFd);
    std::cout << "[SocketServer] 关闭连接: fd=" << clientFd << std::endl;
}
