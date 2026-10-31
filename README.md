# WebServer

> C++ 高性能 HTTP 服务器 · **x86 Linux 开发验证版**，最终部署目标 **STM32MP157**

一个从零实现的 C++ Web 服务器，采用 **Reactor + 线程池** 的事件驱动架构。它不是只能返回静态页的示例工程：在完整的 HTTP/1.1 协议栈之上，它还能直接读取 Linux 的 `/sys`、`/proc` 节点，把**板载 CPU 温度、内存占用、开机时长**以 JSON 接口暴露给浏览器，并让网页上的按钮**直接点亮开发板上的 LED**。

项目采用"**主机开发、板端落地**"的工作方式：协议栈、事件循环、线程模型这些与 CPU 架构无关的部分，先在 x86 上编译、调试、压测跑通；确认逻辑正确后，只切换工具链重新编译，同一套源码即可运行在 STM32MP157 上。

## 核心特性

- **Reactor + 线程池**：主线程只做 `epoll_wait` 与事件分发，读写与业务处理全部下沉到工作线程
- **ET / LT 双触发模式**：`trigMode` 一键切换，默认全 ET；配合 `EPOLLONESHOT` 实现连接的无锁归属
- **最小堆定时器**：`O(log n)` 管理万级连接的空闲超时，活跃连接自动续期
- **`mmap` + `writev` 零拷贝**：响应头与文件体一次分散写完成，静态文件不落用户态缓冲区
- **异步日志**：阻塞队列 + 独立落盘线程，按天 / 按行数切分，4 级日志过滤
- **MySQL 连接池**：信号量限流 + `RAII` 自动归还，为后续业务接口预留
- **板端硬件能力**：温度 / 内存 / 运行时长监控、LED / GPIO 控制，通过 HTTP API 直接驱动
- **单套源码双架构**：`make ARCH=x86` 与 `make ARCH=arm` 分别产出主机与开发板可执行文件

---

## 一、整体架构

主线程只做一件事：**等待并分发事件**；真正的读、解析、写都交给工作线程。这是典型的 **半同步 / 半异步（Reactor + Worker Pool）** 模型。

```
                         ┌──────────────────────────────────────────┐
                         │            主线程（事件循环）             │
                         │  Start() → epoll_wait → 事件分发          │
                         └──────────────────┬───────────────────────┘
                                            │
              ┌─────────────────────────────┼─────────────────────────────┐
              │                             │                             │
        listenFd 可读                connFd EPOLLIN                connFd EPOLLOUT
              │                             │                             │
        DealListen_()                  DealRead_()                   DealWrite_()
        ├─ accept()                    ├─ ExtentTime_()  续命定时器    ├─ ExtentTime_()
        ├─ userCount 限流              └─ AddTask(OnRead_)            └─ AddTask(OnWrite_)
        ├─ AddClient_()                       │                             │
        │   ├─ HeapTimer::add                 ▼                             ▼
        │   ├─ Epoller::AddFd          ┌────────────────────────────────────────┐
        │   └─ SetFdNonblock           │        线程池 ThreadPool（6 线程）      │
        └─ 循环 accept（ET）            │  条件变量 + 任务队列，EPOLLONESHOT 保证 │
                                       │  同一连接同一时刻只被一个线程处理        │
                                       └────────────────┬───────────────────────┘
                                                        │
                                          ┌─────────────┴─────────────┐
                                          ▼                           ▼
                                    OnRead_()                    OnWrite_()
                                    readv → Buffer               writev(iov[0] 头 + iov[1] 文件)
                                          │                           │
                                    HttpRequest::parse                │
                                    （状态机解析报文）                 │
                                          │                           │
                                    HttpResponse::MakeResponse        │
                                    ├─ 静态文件 → mmap 零拷贝 ────────┘
                                    └─ /api/*  → JSON（SysMonitor / LED）
                                          │
                                    MofFd(EPOLLOUT)  ← 请求处理完，通知内核可写
```

**关键点：`EPOLLONESHOT` + 事件重注册**

每个连接 fd 注册时都带 `EPOLLONESHOT`，一旦触发就自动从监听集合中摘除，必须由处理线程显式调用 `MofFd()` 重新注册。这样彻底避免了"多个工作线程同时读写同一个 socket"的竞态，也让"读 → 处理 → 写 → 回到读"的状态流转变得清晰可控。

---

## 二、分层与模块职责

| 层次 | 模块 | 文件 | 职责 |
|---|---|---|---|
| 事件驱动层 | `Epoller` | `epoller.h/cpp` | 封装 `epoll_create1` / `epoll_ctl` / `epoll_wait`，提供 `AddFd` / `MofFd` / `DelFd` / `Wait` |
| | `WebServer` | `webserver.h/cpp` | 服务器主体：socket 初始化、事件注册、accept、事件分发、连接生命周期管理 |
| 并发层 | `ThreadPool` | `threadpool.h` | 固定大小线程池，`std::function` 任务队列 + 条件变量，`shared_ptr<Pool>` 支持移动语义 |
| 定时器层 | `HeapTimer` | `heaptimer.h/cpp` | **最小堆**定时器 + `unordered_map<fd, 堆下标>` 索引，O(log n) 增删改；`GetNextTick()` 供 `epoll_wait` 用作超时时间 |
| 协议层 | `HttpConn` | `httpconn.h/cpp` | 一条连接的载体：持有 fd、读写缓冲区、`iovec[2]`、请求与响应对象 |
| | `HttpRequest` | `httprequest.h/cpp` | 基于**状态机**（REQUEST_LINE → HEADERS → BODY → FINISH）解析报文，解析 URL 编码、POST 表单、keep-alive |
| | `HttpResponse` | `httpresponse.h/cpp` | 构造状态行/响应头/响应体，静态文件 `mmap` 映射，`/api/` 路由分发 |
| 基础组件 | `Buffer` | `buffer.h/cpp` | `vector<char>` + 原子读写指针，支持 `readv` 分散读、自动扩容与前移复用 |
| | `Log` | `log.h/cpp` | 单例异步日志：`BlockDeque` 阻塞队列 + 独立写线程，按天/按行数切分文件，4 级日志 |
| | `BlockDeque` | `blockqueue.h` | 有界阻塞队列，生产者/消费者双条件变量，支持带超时的 `pop` |
| | `SqlConnPool` | `sqlconnpool.h/cpp` | MySQL 连接池单例，`sem_t` 信号量控制并发，`SqlConnRAII` 自动归还 |
| 应用扩展层 | `SysMonitor` | `sysmonitor.h/cpp` | 读 `/sys/class/thermal`、`/proc/meminfo`、`/proc/uptime`，拼装 JSON |
| | `GpioCtl` | `gpioctl.h/cpp` | 通过 `/sys/class/gpio` 的 `export` / `direction` / `value` 节点控制引脚 |
| 资源层 | `resources/` | `*.html` | 静态页面：登录、注册、欢迎页、仪表盘、错误页 |

---

## 三、一次请求的完整生命周期

1. **建立连接**：`listenFd` 可读 → `DealListen_()` 循环 `accept()`（ET 模式下必须循环到返回 -1），`userCount` 达到 `MAX_FD`(65536) 时回写 `Server busy!` 并关闭。
2. **接入管理**：`AddClient_()` 完成三件事——把 fd 交给 `HeapTimer` 注册 60s 超时回调、加入 `Epoller`、设为非阻塞。
3. **读事件**：`EPOLLIN` → `DealRead_()` 先 `ExtentTime_()` 续命（活跃连接不会被误杀），再把 `OnRead_` 投进线程池。
4. **收数据**：`HttpConn::read()` 用 `readv` 读进 `Buffer`，ET 模式下 `do-while` 一直读到 `EAGAIN` 为止。
5. **解析**：`HttpRequest::parse()` 逐行驱动状态机，产出 method / path / version / headers / body。
6. **生成响应**：
   - 普通请求 → `stat()` 校验文件（不存在 404、无读权限 403），`mmap` 映射文件，`Content-length` 取 `st_size`；
   - `/api/` 开头 → 解析 query string，走 `MakeApiResponse_()` 返回 `application/json`。
7. **回写**：`iov_[0]` 放响应头，`iov_[1]` 直接指向 `mmap` 的文件内容，一次 `writev` 完成"头 + 体"的分散写；没写完（`EAGAIN`）就保持 `EPOLLOUT` 等下次可写。
8. **收尾**：写完且 `keep-alive` → 改回 `EPOLLIN` 等待下一个请求；否则 `CloseConn_()` 摘除 epoll、关闭 fd、`munmap`、`userCount--`。
9. **超时清理**：`epoll_wait` 的超时时间来自 `GetNextTick()`，堆顶到期的连接由回调直接关闭，空闲连接不会长期占用 fd。

---

## 四、核心设计取舍

**为什么用 `mmap` + `writev`**
静态文件不做"读进用户态再写出"的搬运，`mmap` 让文件页直接进入页缓存，`writev` 让内核一次拷贝"响应头 + 文件体"，省掉一次内存拷贝和一次缓冲区拼接。

**为什么定时器用最小堆而不是时间轮**
连接数在万级、超时时间统一为 60s 的场景下，最小堆实现简单、`O(log n)` 的增删改足够快；配合 `unordered_map` 存堆下标，`adjust()` 可以 O(1) 定位节点再上浮/下沉，避免遍历。

**为什么日志要异步**
`write()` 落在请求处理的关键路径上，同步写盘会直接拖慢 QPS。日志先进 `BlockDeque`，由独立线程批量落盘，业务线程只承担一次入队开销。

**为什么缓冲区读写指针用 `std::atomic`**
`Buffer` 会被读线程与写线程在不同阶段触碰，原子下标 + `Retrieve` 前移复用，避免频繁 `vector` 扩容，也避免为每个连接加锁。

**触发模式**
`trigMode = 3` 表示 listen 与 conn 全部 `ET`。ET 性能更好，但要求"必须读到 `EAGAIN`""必须一次写完或保留剩余状态"，代码中 `HttpConn::read/write` 的 `do-while(isET)` 就是为此服务。

---

## 五、HTTP 能力与路由

- 协议：HTTP/1.1，支持 `GET` / `POST`，`keep-alive: max=6, timeout=120`
- 静态资源：按后缀查表返回 MIME（html / css / js / png / jpg / gif / pdf / mp4 系列等），未知后缀回落 `text/plain`
- 错误页：`400 / 401 / 403 / 404` 分别映射到 `resources/` 下的对应页面
- 认证：登录/注册走 `HttpRequest::UserVerify()`，校验失败时置 401 并复用 `error.html`
- 缓存策略：响应头固定 `Cache-Control: no-store`，保证仪表盘数据每次都是最新

### API 接口

| 方法 | 路径 | 说明 | 返回示例 |
|---|---|---|---|
| GET | `/api/sysinfo` | CPU 温度、内存使用率、开机时长 | `{"cpuTemp":45.0,"memUsage":38.2,"uptime":{"seconds":86400,"text":"1天0小时0分钟"}}` |
| GET | `/api/gpio?action=on` | 点亮板载 LED（写 `/sys/class/leds/red/brightness`） | `{"status":"ok","led":"on"}` |
| GET | `/api/gpio?action=off` | 熄灭 LED | `{"status":"ok","led":"off"}` |

**关于硬件相关接口**：这两个接口是"板端能力"的入口，行为随运行环境自然分层——在 x86 开发机上缺少对应硬件节点时优雅降级，在 STM32MP157 上则返回真实数据。

| 运行环境 | `/api/sysinfo` | `/api/gpio` |
|---|---|---|
| x86 开发机 | 无 `thermal_zone0`，`cpuTemp` 返回 `-1.0`；内存与运行时长正常 | 通常无 `/sys/class/leds/red` 节点，写失败并返回错误 JSON |
| STM32MP157 | 返回板载真实温度 | 正常点亮 / 熄灭板载 LED |

因为要写 `/sys` 节点，`/api/gpio` 需要以 **root 权限** 启动；只访问静态页面时不需要 `sudo`。

---

## 六、目录结构

```
webserver_x86/
├── main.cpp              # 入口：构造 WebServer 并 Start()
├── webserver.h/cpp       # 服务器主体：事件循环、accept、事件分发
├── epoller.h/cpp         # epoll 封装
├── threadpool.h          # 线程池（header-only）
├── heaptimer.h/cpp       # 最小堆定时器
├── httpconn.h/cpp        # 连接对象：readv/writev、iovec
├── httprequest.h/cpp     # 报文解析状态机
├── httpresponse.h/cpp    # 响应构造、mmap、/api 路由
├── buffer.h/cpp          # 自动扩容缓冲区
├── log.h/cpp             # 异步日志
├── blockqueue.h          # 阻塞队列（header-only）
├── sqlconnpool.h/cpp     # MySQL 连接池 + RAII
├── sysmonitor.h/cpp      # 系统监控 → JSON
├── gpioctl.h/cpp         # GPIO / LED 控制
├── makefile              # 支持 ARCH=x86 / arm 双目标
├── resources/            # 静态页面（login / register / welcome / dashboard / 错误页）
├── log/                  # 运行期日志输出
├── build_x86/            # x86 中间产物与可执行文件
└── build_arm/            # ARM 交叉编译产物（上板用）
```

---

## 七、编译与运行

### 依赖

```bash
# 主机编译（x86）
sudo apt install build-essential libmysqlclient-dev

# 交叉编译（ARM，上板用）
sudo apt install g++-arm-linux-gnueabihf
```

### 编译

```bash
make ARCH=x86          # 使用 g++，产物 build_x86/webserver
make ARCH=arm          # 交叉编译，使用 arm-linux-gnueabihf-g++，产物 build_arm/webserver
make clean             # 清理当前架构的中间文件
make cleanall          # 同时清理 x86 与 arm
```

### 运行

```bash
cd build_x86
sudo ./webserver       # GPIO/LED 需要 root；仅静态页面可不用 sudo
```

启动后访问 `http://<ip>:1316/login.html`。

> 资源路径做了自适应：程序会检测当前工作目录是否包含 `build_x86`，是则回退到 `../resources/`，否则用 `./resources/`。因此**在 `build_x86` 目录内启动即可**，无需手工拷贝页面。

### 关键参数（`main.cpp` 中构造）

| 参数 | 当前值 | 说明 |
|---|---|---|
| port | 1316 | 监听端口（限制在 1024~65535） |
| trigMode | 3 | 0=LT/LT，1=conn ET，2=listen ET，3=全部 ET |
| timeoutMs | 60000 | 连接空闲超时，0 表示关闭定时器 |
| OptLinger | false | 是否开启 `SO_LINGER` 优雅关闭 |
| sqlPort / user / pwd / db | 3306 / root / root / webserver | MySQL 连接信息 |
| connPoolNum | 12 | 数据库连接池大小 |
| threadNum | 6 | 工作线程数 |
| openLog / logLevel / logQueSize | true / 1 / 1024 | 日志开关、级别（0-3）、异步队列容量 |

---

## 八、移植到 STM32MP157

### 为什么先在 x86 上跑通

编译秒级完成、GDB / ASan / perf 工具齐全、`ab` 压测方便；而板子的串口和网络调试成本高得多。事件循环、协议解析、线程模型这些逻辑与指令集无关，先在主机上把正确性验证充分，换到板端就只剩"重编译 + 换依赖库"两件事，风险可控。

### 目标平台

STM32MP157 是双核 **Cortex-A7**（32 位 ARM，带硬浮点）+ Cortex-M4 的异构 SoC，运行 Linux。因此工具链选 `arm-linux-gnueabihf-g++`（`hf` 即 hard-float），与 `makefile` 中 `ARCH=arm` 的分支一致。

### 移植三件事

1. **换工具链**：`make ARCH=arm`，产物在 `build_arm/webserver`；可用 `file build_arm/webserver` 确认输出为 `ELF 32-bit LSB ... ARM`。
2. **交叉编译依赖库**：`libmysqlclient` 必须编译进目标 sysroot。`makefile` 中 `-L/usr/lib64/mysql -L/usr/lib64` 是 **x86 主机路径**，交叉编译时需指向 sysroot 下的实际库目录，否则链接阶段会报 `cannot find -lmysqlclient`。
3. **带齐运行资源**：`resources/` 与 `log/` 要和可执行文件保持相对位置一起拷贝。注意程序探测资源目录时只识别路径中的 `build_x86` 字样——命中则用 `../resources/`，否则用 `./resources/`。所以**直接在板端的 `build_arm/` 里运行会找不到页面**，把 `webserver` 放到与 `resources/` 同级的目录下启动，或把 `resources/` 一并拷进 `build_arm/` 即可。

### 板端能力激活

上板后无需改动任何代码，硬件模块会自动"活"过来：

| 能力 | 依赖节点 | 说明 |
|---|---|---|
| CPU 温度 | `/sys/class/thermal/thermal_zone0/temp` | 读出毫度值，代码内除以 1000 转摄氏度 |
| 内存使用率 | `/proc/meminfo` | 解析 `MemTotal` 与 `MemAvailable` 计算占比 |
| 开机时长 | `/proc/uptime` | 换算成"X天Y小时Z分钟" |
| LED 控制 | `/sys/class/leds/red/brightness` | 写 `1` / `0` 控制亮灭 |
| GPIO | `/sys/class/gpio/{export,direction,value}` | `GpioCtl` 已实现申请 → 配置方向 → 读写 → 释放的完整流程，可直接复用到其他外设（当前尚未挂 HTTP 路由） |

> 若板载 LED 的节点名不是 `red`，只需修改 `httpresponse.cpp` 中 `/api/gpio` 分支里的路径，其余逻辑不变。

### 上板验证清单

- `curl http://127.0.0.1:1316/api/sysinfo`，确认 `cpuTemp` 是正常正数（不再是 `-1.0`）
- 打开 `dashboard.html` 点击按钮，观察板载 LED 亮灭
- 板端执行 `ab` 并与 x86 数据对比：Cortex-A7 的主频与内存带宽远低于 x86，QPS 下降属正常，重点关注**失败率是否仍为 0**、长连接是否稳定

---

## 九、性能测试

工具：Apache Bench

```bash
ab -n 10000 -c 100 http://192.168.58.103:1316/welcome.html
```

| 指标 | 结果 |
|---|---|
| QPS | 5136 req/s |
| 平均响应时间 | 19.47 ms |
| 失败率 | 0% |
| 90% 请求 | 2 ms 内返回 |

> 以上为 **x86 开发机（虚拟机）** 上的静态资源压测结果，用于验证事件循环与线程模型在高并发下的稳定性；STM32MP157 板端数据待上板后补充。

---

## 十、后续演进方向

以下不是"待修的坑"，而是围绕板端落地继续打磨的方向：

- **接口注册表化**：`MakeApiResponse_()` 目前是逐条 `if-else` 匹配路径，接口增多后可改为 `unordered_map<string, handler>`，新增接口不必改动分发逻辑。
- **自适应线程数**：当前线程数固定为 6，适合资源受限的板端；若后续要跑在更强的主机上，可加入队列水位监控，按负载动态扩缩。
- **`sendfile` 快路径**：板端内存更紧张，大文件场景下 `sendfile` 比 `mmap` 更省一次页表映射，可作为静态资源的可选分支。
- **API 连接复用**：`/api/` 响应固定 `Connection: close`，若仪表盘改为高频轮询，可切到 keep-alive 减少握手开销。
- **业务接口接入**：`SqlConnPool` 与 `UserVerify` 已就绪，下一步是把登录注册真正落到数据表上，让认证链路闭环。
- **板端外设扩展**：`GpioCtl` 的 `export / direction / value` 流程已完整实现，可直接复用到继电器、传感器等更多外设。
