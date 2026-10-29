# 🚀 嵌入式 Linux 高并发 Web 监控网关

本项目是一个基于 C++ 与 Reactor 模式的高性能 Web 服务器，专为资源受限的嵌入式 Linux 环境（如 STM32MP157）设计。不仅具备高并发网络处理能力，还集成了底层系统监控与硬件控制接口。

## ✨ 核心特性

- **高并发网络引擎**：基于 `epoll` (ET模式) + 非阻塞 IO + 线程池，支持海量并发连接。
- **多环境工程架构**：采用 Git 多分支管理。`main` 分支为 PC 端完整版（含 MySQL）；`arm-board` 分支为 ARM 板端轻量版（已适配 SQLite，剥离重型依赖）。
- **系统级监控面板**：通过读取 Linux 底层的 `/proc/meminfo` 和 `/proc/uptime`，实时将 CPU/内存状态以 JSON 格式返回，并通过前端 Canvas 绘制动态折线图。
- **软硬结合**：预留 `/api/gpio` 接口，可通过 Web 页面下发指令，直接操作开发板底层的 GPIO 引脚。
- **严谨的鉴权机制**：严格遵循 HTTP 状态码规范（200 OK / 401 Unauthorized），前端 Fetch 按状态码精准拦截，杜绝无脑跳转。

## 🛠️ 技术栈

- **后端**：C++11/14, Epoll (ET), Reactor, 线程池, 小根堆定时器, 异步日志
- **数据存储**：MySQL (PC端) / SQLite (ARM端)
- **前端**：HTML5, 原生 JavaScript (Fetch API), Canvas 绘图
- **构建与部署**：Makefile, `arm-linux-gnueabihf` 交叉编译
- **系统交互**：Linux 虚拟文件系统 (`/sys`, `/proc`), 标准文件 I/O

## 📊 性能压测 (PC端)

- **测试工具**：Apache Bench (ab)
- **测试命令**：`ab -n 10000 -c 100 http://127.0.0.1:1316/login.html`
- **测试结果**：
  - **QPS**：**5136 req/s**
  - **平均响应时间**：19.47 ms
  - **失败率**：0%
  - **90% 请求延迟**：≤ 2 ms

## 📸 运行效果 (系统监控面板)

> *(在这里把你那张带折线图的网页截图拖进来)*

## 🔧 编译与运行

**1. 环境依赖**
```bash
sudo apt install g++ make libmysqlclient-dev
