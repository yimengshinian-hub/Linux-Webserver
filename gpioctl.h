#ifndef GPIOCTL_H
#define GPIOCTL_H

#include <string>
//让网页上的按钮，能够通过代码去操作 Linux 底层的硬件引脚（点亮板子上的灯）
class GpioCtl {
public:
    static bool Init(int gpioNum);               // 初始化引脚
    static bool SetLed(int gpioNum, bool on);    // 开灯/关灯
    static bool Uninit(int gpioNum);             // 释放引脚
};

#endif