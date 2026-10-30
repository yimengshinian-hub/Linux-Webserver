#ifndef SYSUTIL_H
#define SYSUTIL_H

/* ============================================================
 * sysutil.h —— 系统信息读取工具（header-only，直接 include 即可）
 *
 * 设计原则：任何一个环节失败都返回 false，绝不返回垃圾值。
 *   open 失败 / 文件为空 / 内容非数字 / 非有限数 / 超出物理合理区间
 *   —— 全部判为无效，由调用方决定降级方式。
 * ============================================================ */

#include <fstream>
#include <string>
#include <cmath>

namespace sysutil {

/* 安全读取 sysfs/proc 中的单个数值 */
inline bool ReadNumber(const std::string& path, double& out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;                       // 节点不存在（驱动未加载）
    }
    std::string line;
    if (!std::getline(in, line) || line.empty()) {
        return false;                       // 空文件
    }
    try {
        out = std::stod(line);              // 非数字会抛异常
    } catch (...) {
        return false;
    }
    return std::isfinite(out);              // 过滤 inf / nan
}

/* CPU 当前主频，单位 MHz
 * 优先 scaling_cur_freq（调频器目标频率，读取快）
 * 退回 cpuinfo_cur_freq（实际频率，部分驱动不支持） */
inline bool ReadCpuFreqMHz(double& mhz) {
    static const char* kPaths[] = {
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq",
    };
    for (const char* p : kPaths) {
        double raw = 0;
        if (!ReadNumber(p, raw)) {
            continue;
        }
        double v = raw / 1000.0;            // 节点单位是 kHz
        if (v > 50.0 && v < 5000.0) {       // 合理区间：50MHz ~ 5GHz
            mhz = v;
            return true;
        }
    }
    return false;
}

/* CPU 温度，单位摄氏度。板卡未开放传感器时返回 false */
inline bool ReadCpuTemp(double& celsius) {
    static const char* kPaths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
    };
    for (const char* p : kPaths) {
        double raw = 0;
        if (!ReadNumber(p, raw)) {
            continue;
        }
        /* 多数平台给毫摄氏度(45000)，少数直接给摄氏度(45) */
        double v = (raw > 1000.0) ? raw / 1000.0 : raw;
        if (v > 0.0 && v < 150.0) {         // 合理区间：0~150℃
            celsius = v;
            return true;
        }
    }
    return false;
}

/* 内存使用率，单位 % */
inline bool ReadMemUsage(double& percent) {
    std::ifstream in("/proc/meminfo");
    if (!in.is_open()) {
        return false;
    }
    std::string key, unit;
    long long val = 0;
    long long total = 0, available = 0, freeMem = 0;

    while (in >> key >> val) {
        std::getline(in, unit);             // 吃掉行尾的 " kB"
        if (key == "MemTotal:")          { total = val; }
        else if (key == "MemAvailable:") { available = val; }
        else if (key == "MemFree:")      { freeMem = val; }
        if (total > 0 && available > 0)  { break; }
    }

    if (total <= 0) {
        return false;
    }
    /* 老内核没有 MemAvailable，退回 MemFree（偏保守，会略高估占用） */
    long long avail = (available > 0) ? available : freeMem;
    if (avail < 0) {
        return false;
    }
    double used = static_cast<double>(total - avail) / static_cast<double>(total) * 100.0;
    if (used < 0.0 || used > 100.0) {
        return false;
    }
    percent = used;
    return true;
}

/* 系统运行时长，单位秒 */
inline bool ReadUptime(long long& seconds) {
    std::ifstream in("/proc/uptime");
    if (!in.is_open()) {
        return false;
    }
    double up = 0;
    if (!(in >> up)) {
        return false;
    }
    if (!std::isfinite(up) || up < 0.0) {
        return false;
    }
    seconds = static_cast<long long>(up);
    return true;
}

}  // namespace sysutil

#endif  // SYSUTIL_H
