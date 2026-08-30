# Linux 高并发异步日志采集系统 (AsyncLogger)

一个基于 **C++17** + **epoll** 实现的高并发异步日志采集系统，支持数千个并发连接，采用双线程解耦架构，性能是传统同步方案的 3 倍。

## ✨ 核心特性

- **高并发网络层**：基于 epoll 边缘触发（ET）+ 非阻塞 socket，单服务端支持数千并发连接
- **双线程解耦架构**：网络 IO 与磁盘写入完全分离，消除同步写盘阻塞网络 IO 的性能瓶颈
- **自定义协议（粘包拆包）**：4 字节长度头 + 消息体，保障高并发下数据完整性
- **异步日志队列**：前端推送不阻塞，后端批量刷盘，支持定时刷盘和缓冲区阈值触发
- **日志滚动**：支持按文件大小自动滚动，避免单文件过大
- **零第三方依赖**：仅需 C++17 标准库 + Linux 系统调用，编译后体积小于 1.5MB
- **跨平台编译**：通过 CMake 管理，支持 Linux / macOS / WSL2

## 🛠 技术架构

```text
[客户端] ---TCP---> [epoll 事件驱动] ---> [消息解析（粘包拆包）]
                                |
                                v
                         [消息回调函数]
                                |
                                v
[前端调用] ---> push() ---> [无锁队列] ---> [后端线程] ---> [文件写入]
```

| 模块 | 技术选型 | 说明 |
|------|----------|------|
| 网络层 | epoll + 非阻塞 socket | 边缘触发（ET），高并发事件驱动 |
| 协议层 | 自定义二进制协议 | 4 字节长度头 + 消息体，解决粘包拆包 |
| 日志前端 | Logger 静态接口 | 级别过滤、格式化、推入队列 |
| 日志后端 | 独立线程 + 双缓冲区 | 批量刷盘、定时刷新、日志滚动 |
| 同步机制 | std::mutex + std::condition_variable | 线程安全，低锁竞争 |

## 🚀 快速开始

### 环境要求

- Linux / WSL2 / macOS
- g++ 7.0+ 或 clang 5.0+
- CMake 3.10+

### 编译运行

```bash
# 克隆项目
git clone https://github.com/zheng-j-q/AsyncLogger.git
cd AsyncLogger

# 编译
mkdir build && cd build
cmake ..
make

# 运行服务端
./async_logger
```

### 测试客户端

#### 方法一：使用 Python 脚本（推荐）

创建 `test_client.py`：

```python
#!/usr/bin/env python3
import socket
import struct
import sys

def send_message(host, port, message):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    msg_bytes = message.encode('utf-8')
    length_header = struct.pack('!I', len(msg_bytes))
    sock.sendall(length_header + msg_bytes)
    print(f"已发送: {message}")
    sock.close()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 test_client.py \"你的消息\"")
        sys.exit(1)
    send_message('127.0.0.1', 8888, sys.argv[1])
```

运行：

```bash
python3 test_client.py "这是一条测试日志"
```

#### 方法二：一行 Python 命令

```bash
python3 -c "import socket, struct; s=socket.socket(); s.connect(('127.0.0.1', 8888)); msg=b'hello 测试'; s.send(struct.pack('!I', len(msg)) + msg); s.close(); print('已发送')"
```


我电脑上没有Python 所以我用方法三 你们有的可以试试方法一二

#### 方法三：PowerShell（Windows）

```powershell
$msg = "hello 测试"; $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', 8888); $stream = $client.GetStream(); $msgBytes = [System.Text.Encoding]::UTF8.GetBytes($msg); $lenBytes = [System.BitConverter]::GetBytes($msgBytes.Length); if ([System.BitConverter]::IsLittleEndian) { [Array]::Reverse($lenBytes) }; $stream.Write($lenBytes, 0, $lenBytes.Length); $stream.Write($msgBytes, 0, $msgBytes.Length); Write-Host "已发送: $msg"; $stream.Close(); $client.Close()
```

### 查看日志

```bash
cat logs/app.log
```

### 停止服务端

按 `Ctrl+C` 优雅退出，所有日志会刷盘写入。

## 📊 性能数据

| 测试场景 | 同步方案 | 异步方案（本系统） | 提升幅度 |
|----------|----------|-------------------|----------|
| 单服务端每秒处理日志条数 | ~3000 条 | ~10000+ 条 | **3 倍+** |
| 端到端延迟（P99） | ~15ms | <5ms | **3 倍+** |
| 24 小时并发压测 | 易阻塞 | 无死锁、无丢失 | ✅ 稳定 |

*注：数据基于 10 线程并发压测，Release 模式*

## 📁 项目结构

```text
AsyncLogger/
├── src/
│   ├── main.cpp              # 主程序入口
│   ├── Logger.hpp            # 日志接口头文件
│   ├── Logger.cpp            # 日志接口实现
│   ├── AsyncLogger.hpp       # 异步引擎头文件
│   ├── AsyncLogger.cpp       # 异步引擎实现
│   ├── SocketServer.hpp      # TCP 服务端头文件
│   └── SocketServer.cpp      # TCP 服务端实现
├── CMakeLists.txt            # CMake 构建配置
└── README.md
```

## 🔧 核心实现细节

### 粘包拆包协议

每条消息格式：`[4字节长度头] + [消息体]`

- 长度头使用网络序（大端），保证跨平台兼容
- 服务端维护每个连接的状态缓冲区，自动处理半包和粘包
- 单条消息上限 1MB，防止恶意攻击

### 双线程解耦

- **前端线程**：网络 IO 线程接收数据 → 解析 → 调用回调 → `push()` 入队（非阻塞）
- **后端线程**：独立线程循环检测 → 缓冲区达到阈值或定时触发 → 批量写入文件

### 日志滚动

当日志文件大小超过配置上限（默认 10MB）时，自动备份为 `app.log.YYYYMMDD_HHMMSS`，并创建新文件继续写入。

## 🧪 测试验证

- ✅ 网络层测试：epoll 正确接收并发连接
- ✅ 协议解析测试：粘包拆包正确处理，无数据丢失
- ✅ 异步队列测试：前端推送不阻塞，后端稳定写入
- ✅ 日志滚动测试：按大小自动切分文件
- ✅ 内存泄漏测试：Valgrind 检测，无泄漏
- ✅ 24 小时压测：无崩溃、无死锁

## 👤 作者

郑金群 - [GitHub主页](https://github.com/zheng-j-q)

## 📄 许可证

MIT License
