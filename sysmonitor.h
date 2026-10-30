#ifndef SYSMONITOR_H
#define SYSMONITOR_H

#include <string>
//让服务器能去读取 Linux 系统底层的文件（CPU温度、内存、运行时间），并把这些数据打包成 JSON 格式发给网页。
class SysMonitor {
public:
    static float GetCpuTemp();    // 获取CPU温度
    static float GetCpuFreq();   //新增：读取 CPU 当前主频
    static float GetMemUsage();   // 获取内存使用率
    static long GetUptime();      // 获取系统运行秒数
    static std::string ToJson();  // 把上面三个数据拼成 JSON 字符串
};

#endif