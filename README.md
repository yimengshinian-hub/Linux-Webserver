# WebServer

基于 C++ 的高性能 HTTP Web 服务器

## 技术栈

- C++17
- Epoll + 非阻塞 IO
- 线程池
- HTTP/1.1
- MySQL 连接池

## 性能测试

工具：Apache Bench (ab)
测试命令：ab -n 10000 -c 100 http://192.168.58.103:1316/index.html

结果：
QPS：5136 req/s
平均响应时间：19.47 ms
失败率：0%
90% 请求在 2ms 内返回