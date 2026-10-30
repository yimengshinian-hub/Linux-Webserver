#include "sysmonitor.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include "sysutil.h"

/* 读取 CPU 当前主频，单位 MHz。读不到返回 -1 */
float SysMonitor::GetCpuFreq() {
    double mhz = 0.0;
    if (!sysutil::ReadCpuFreqMHz(mhz)) {
        return -1.0f;
    }
    return (float)mhz;
}

/* 读取 CPU 温度，单位摄氏度。读不到返回 -1
 * 板卡未开放热敏驱动时，这里会稳定返回 -1，而不是随机垃圾值 */
float SysMonitor::GetCpuTemp() {
    double c = 0.0;
    if (!sysutil::ReadCpuTemp(c)) {
        return -1.0f;
    }
    return (float)c;
}

/* 内存使用率，单位 %。读不到返回 -1 */
float SysMonitor::GetMemUsage() {
    double pct = 0.0;
    if (!sysutil::ReadMemUsage(pct)) {
        return -1.0f;
    }
    return (float)pct;
}

/* 系统运行时长，单位秒。读不到返回 -1 */
long SysMonitor::GetUptime() {
    long long sec = 0;
    if (!sysutil::ReadUptime(sec)) {
        return -1;
    }
    return (long)sec;
}

/* 把数据拼接成前端能解析的 JSON 字符串 */
std::string SysMonitor::ToJson() {
    float freq   = GetCpuFreq();
    float temp   = GetCpuTemp();
    float mem    = GetMemUsage();
    long  uptime = GetUptime();

    if (uptime < 0) uptime = 0;

    long days    = uptime / 86400;
    long hours   = (uptime % 86400) / 3600;
    long minutes = (uptime % 3600) / 60;

    /* 无效值统一输出成 null，前端收到 null 会显示 N/A */
    char freqField[32];
    if (freq > 0.0f) {
        snprintf(freqField, sizeof(freqField), "%.0f", freq);
    } else {
        snprintf(freqField, sizeof(freqField), "null");
    }

    char tempField[32];
    if (temp > 0.0f && temp < 150.0f) {
        snprintf(tempField, sizeof(tempField), "%.1f", temp);
    } else {
        snprintf(tempField, sizeof(tempField), "null");
    }

    char memField[32];
    if (mem >= 0.0f && mem <= 100.0f) {
        snprintf(memField, sizeof(memField), "%.1f", mem);
    } else {
        snprintf(memField, sizeof(memField), "null");
    }

    char json[512];
    snprintf(json, sizeof(json),
        "{\"cpuFreq\":%s,\"cpuTemp\":%s,\"memUsage\":%s,"
        "\"uptime\":{\"seconds\":%ld,\"text\":\"%ld天%ld小时%ld分钟\"}}",
        freqField, tempField, memField, uptime, days, hours, minutes);

    return std::string(json);
}
