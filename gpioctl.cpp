#include "gpioctl.h"
#include <fstream>
#include <string>
#include <unistd.h>   // usleep

// Linux 操作 GPIO 的底层标准流程
bool GpioCtl::Init(int gpioNum) {
    // 1. 把引脚号写入 export 节点，相当于向系统申请这个引脚
    std::ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open()) return false;
    exportFile << gpioNum;
    exportFile.close();

    usleep(100000);  // 等待100ms，让系统准备好对应的文件夹

    // 2. 把引脚设置为输出模式 (out)
    std::string dirPath = "/sys/class/gpio/gpio" + std::to_string(gpioNum) + "/direction";
    std::ofstream dirFile(dirPath);
    if (!dirFile.is_open()) return false;
    dirFile << "out";
    dirFile.close();
    return true;
}

// 往 value 节点写 1 或 0，实现亮灭
bool GpioCtl::SetLed(int gpioNum, bool on) {
    std::string valPath = "/sys/class/gpio/gpio" + std::to_string(gpioNum) + "/value";
    std::ofstream valFile(valPath);
    if (!valFile.is_open()) return false;
    valFile << (on ? "1" : "0");
    valFile.close();
    return true;
}

bool GpioCtl::Uninit(int gpioNum) {
    std::ofstream unexportFile("/sys/class/gpio/unexport");
    if (!unexportFile.is_open()) return false;
    unexportFile << gpioNum;
    unexportFile.close();
    return true;
}