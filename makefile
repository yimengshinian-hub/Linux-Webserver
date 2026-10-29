ARCH ?= x86
TARGET = webserver
#存放中间文件的路径
BUILD_DIR = build_$(ARCH)
#源文件文件夹
SRC_DIR =  .
#头文件文件夹
INC_DIR = includes .
#源文件 (自动收集当前目录下所有.cpp)
SRCS = $(wildcard $(SRC_DIR)/*.cpp) 
#目标文件(*.o)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))
#头文件
DEPS = $(wildcard $(INC_DIR)/*.h)
#指定头文件的路径、C++标准、多线程
CFLAGS = $(patsubst %,-I%,$(INC_DIR)) -std=c++14 -O2 -Wall -g -pthread

#根据输入的ARCH来选择编译器
ifeq ($(ARCH),x86)
CC = g++
else 
CC = arm-linux-gnueabihf-g++
endif

#目标文件链接 (注意这里把 -lmysqlclient 加回来了)
$(BUILD_DIR)/$(TARGET):$(OBJS) | create_build
	$(CC) -o $@ $^ -pthread -L/usr/lib64/mysql -L/usr/lib64 -lmysqlclient

#*.o生成规则
$(BUILD_DIR)/%.o:$(SRC_DIR)/%.cpp $(DEPS) | create_build
	$(CC) -c -o $@ $< $(CFLAGS)

#伪目标
.PHONY:clean cleanall check create_build

check:
	@echo $(CFLAGS)
	@echo $(CURDIR)
	@echo $(SRC_DIR)
	@echo $(SRCS)
	@echo $(OBJS)

clean: 
	rm -rf $(BUILD_DIR)

cleanall:
	rm -rf build_x86 build_arm

create_build:
	@mkdir -p $(BUILD_DIR)