#include "sysmonitor.h"
#include <fstream>
#include <sstream>
#include <cstdio>

// 读取 /sys/class/thermal/thermal_zone0/temp (这是Linux底层提供硬件温度的节点)
float SysMonitor::GetCpuTemp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open()) return -1.0f; // 如果电脑上没有这个节点（比如虚拟机），返回 -1
    int temp;
    file >> temp;
    return temp / 1000.0f;  // 底层读出来是毫度（如45000），除以1000变45度
}

// 读取 /proc/meminfo，解析 MemTotal 和 MemAvailable
float SysMonitor::GetMemUsage() {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) return -1.0f;

    long memTotal = 0, memAvailable = 0;//总内存和可用内存
    std::string key;
    long value;
    std::string unit;

    while (file >> key >> value >> unit) {
        if (key == "MemTotal:")        memTotal = value;
        if (key == "MemAvailable:")    memAvailable = value;
        if (memTotal > 0 && memAvailable > 0) break; 
    }

    if (memTotal == 0) return -1.0f;
    return (float)(memTotal - memAvailable) / memTotal * 100.0f;//(总内存-可用内存)/总内存*100就是当前系统使用率
}

// 读取 /proc/uptime，第一个数字就是系统开机的秒数
long SysMonitor::GetUptime() {
    std::ifstream file("/proc/uptime");
    if (!file.is_open()) return -1;
    double uptime;
    file >> uptime;
    return (long)uptime;
}

// 把上面获取的数据拼接成前端能看懂的 JSON 字符串
std::string SysMonitor::ToJson() {
    float temp = GetCpuTemp();
    float mem = GetMemUsage();
    long uptime = GetUptime();

    // 把秒换算成 "X天Y小时Z分"
    long days    = uptime / 86400;
    long hours   = (uptime % 86400) / 3600;
    long minutes = (uptime % 3600) / 60;

    char json[512];
    snprintf(json, sizeof(json),
        "{\"cpuTemp\":%.1f,\"memUsage\":%.1f,\"uptime\":{\"seconds\":%ld,\"text\":\"%ld天%ld小时%ld分钟\"}}",
        temp, mem, uptime, days, hours, minutes);
    return std::string(json);
}