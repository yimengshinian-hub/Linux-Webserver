# 嵌入式 Linux 板端 Web 服务器

基于 [TinyWebServer](https://github.com/qinguoyi/TinyWebServer) 二次开发，从 x86 移植至 **STM32MP157（Cortex-A7）** 开发板，新增 SQLite 存储、sysfs 硬件控制与系统状态监控，实现浏览器远程查看板卡状态、控制板载 LED。

## 📸 效果展示

| 网页端监控与控制 (实机) | 物理硬件响应 |

<img width="426" height="240" alt="演示" src="https://github.com/user-attachments/assets/8d45bddc-e3cd-412d-aa4d-5dcfac00ec9a" />

## 硬件环境

| 项目 | 配置 |
| --- | --- |
| 开发板 | STM32MP157（Cortex-A7） |
| 交叉工具链 | arm-linux-gnueabihf-g++ |
| 第三方库 | SQLite（源码静态编译） |
| 编译标准 | C++14 |

## 功能特性

- **HTTP/1.1**：有限状态机解析，支持 GET/POST、Keep-Alive，19 种 MIME 映射
- **静态资源**：mmap + writev 零拷贝发送，配套 400/403/404/500 错误页
- **连接管理**：最小堆定时器 + 惰性删除，60s 空闲断开，最大 65536 连接
- **线程模型**：固定线程池，EPOLLONESHOT 保证同一连接同一时刻只被一个线程处理
- **用户系统**：登录/注册，SQLite 预编译语句防注入，首次运行自动建表
- **系统监控**：/api/sysinfo 返回 CPU 主频、温度、内存使用率、运行时长
- **硬件控制**：/api/gpio 通过 sysfs 控制板载 LED
- **异步日志**：阻塞队列 + 独立写线程，按天分文件

## 快速开始

### x86 本地调试

```bash
make
./build_x86/webserver
```

### ARM 交叉编译与部署

```bash
# 生成 SQLite 静态库（只需执行一次，需先下载 sqlite3.c 至项目根目录）
make sqlite-static

# 交叉编译，产物 build_arm/webserver
make ARCH=arm

# 部署到开发板
scp build_arm/webserver root@BOARD_IP:/root/webserver/
scp -r resources root@BOARD_IP:/root/webserver/

# 板上运行（LED 控制需 root 权限）
ssh root@BOARD_IP
cd /root/webserver && ./webserver
```

浏览器访问 `http://BOARD_IP:1316/dashboard.html`

> 将 `BOARD_IP` 替换为开发板的实际 IP 地址。

## API

### 获取板卡状态

```bash
curl http://BOARD_IP:1316/api/sysinfo
```

```json
{
  "cpuFreq": 650,
  "cpuTemp": null,
  "memUsage": 31.2,
  "uptime": { "seconds": 3725, "text": "0天1小时2分钟" }
}
```

节点不存在、内容非数字或超出物理合理区间时，字段返回 `null`，前端显示 `N/A`。

### 控制板载 LED

```bash
curl "http://BOARD_IP:1316/api/gpio?action=on"
curl "http://BOARD_IP:1316/api/gpio?action=off"
```

| 状态码 | 含义 |
| --- | --- |
| 200 | 操作成功 |
| 400 | action 参数非法 |
| 500 | 权限不足，需 root 运行 |

## 移植关键问题

### glibc 符号冲突

板端 glibc 版本低于交叉工具链，动态链接报 `undefined reference to fcntl64@GLIBC_2.28`。
改为从 sqlite3.c 源码编译静态库，消除运行时库依赖，实现二进制单文件部署：

```bash
arm-linux-gnueabihf-gcc -O2 -fPIC -DSQLITE_THREADSAFE=1 \
    -DSQLITE_OMIT_LOAD_EXTENSION -c sqlite3.c -o sqlite3.o
ar rcs arm_libs/libsqlite3.a sqlite3.o
```

Makefile 会自动探测：存在 libsqlite3.a 则静态链接，否则回退动态链接并设置 rpath。

### 板端无 MySQL 服务

原项目依赖 MySQL 连接池，板端未部署 MySQL，且交叉编译 libmysqlclient 依赖链过重。
改用 SQLite 单文件数据库，零外部服务依赖，二进制可直接拷贝运行。

## 修复的缺陷

| # | 问题 | 后果 |
| --- | --- | --- |
| 1 | Content-Type 头名大小写敏感匹配 | 浏览器发送小写头名时匹配失败，任意密码均可登录 |
| 2 | 查询串未剥离即做路由匹配 | 带查询参数的登录请求恒返回 404 |
| 3 | 百分号编码解码未处理数字字符 | 特殊字符密码永远无法登录 |
| 4 | 注册页以 GET 提交，后端仅处理 POST | 用户永远无法注册 |
| 5 | 鉴权失败仍返回 200 | 前端无法区分成功与失败 |
| 6 | mmap 返回值误判 MAP_FAILED | 解引用导致段错误 |
| 7 | 空文件调用 mmap 长度为零 | 必定失败，整机崩溃 |
| 8 | 状态行缺少空格 | 输出 HTTP/1.1200 OK，协议不合法 |

## 性能测试

### x86 平台

```bash
ab -n 10000 -c 100 http://127.0.0.1:1316/index.html
```

| 指标 | 数值 |
| --- | --- |
| QPS | 5136 req/s |
| 平均响应时间 | 19.47 ms |
| 失败率 | 0% |
| 90% 分位耗时 | 不超过 2 ms |

### ARM 板端（STM32MP157）

板卡 Cortex-A7 算力有限，且压测工具与服务器同机运行（SSH 终端下执行 curl 脚本），
工具本身的进程开销会与服务器抢占 CPU 并导致满载，因此本平台的吞吐量数据受限于测试环境，未作 QPS 统计。
改以资源占用与稳定性作为主要评估指标：

| 指标 | 数值 |
| --- | --- |
| 常驻内存 RSS | 2.6 MB |
| 二进制体积 | 约 3.9 MB（交叉编译产物） |
| 运行稳定性 | 连续运行无崩溃，失败率 0% |

> 资源占用测量：`ps -o rss= -p $(pidof webserver)`
>
> 说明：嵌入式场景下，板端更关注内存占用与长期运行稳定性；吞吐能力已在 x86 平台单独验证，
> 用于评估网络层实现本身（Reactor 模型、线程池、零拷贝发送）的性能表现。

## 已知限制

- 单进程单 Reactor，未实现多线程 accept 与 SO_REUSEPORT
- 未支持 HTTPS、gzip 压缩、Range 请求
- 密码明文存储，未加盐哈希
- LED 路径硬编码，未做板型抽象

## 参考

本项目基于 TinyWebServer 上游项目二次开发并移植至 ARM 平台。
