# ============================================================
#  STM32MP157 / x86 双架构 Makefile (SQLite 版)
#
#  用法:
#    make              -> 编译 x86 版本, 产物 build_x86/webserver
#    make ARCH=arm     -> 交叉编译 ARM 版本, 产物 build_arm/webserver
#    make check        -> 打印当前变量, 排查用
#    make clean        -> 删除当前架构的编译目录
#    make cleanall     -> 删除所有架构的编译目录
#
#  ARM 分支的 SQLite 有两种接法(自动判断):
#    1) 存在 ./arm_libs/libsqlite3.a  -> 静态链接(推荐)
#       用交叉编译器从 sqlite3.c 源码编出, 不依赖板上 .so,
#       彻底规避 "undefined reference to fcntl64@GLIBC_2.28" 问题
#    2) 只有 ./arm_libs/libsqlite3.so -> 动态链接 + rpath
#       要求交叉工具链 glibc >= 板子 glibc
# ============================================================

ARCH ?= x86
TARGET = webserver

# 存放中间文件的路径(按架构隔离, 避免 x86/arm 的 .o 混用)
BUILD_DIR = build_$(ARCH)

# 源文件 / 头文件目录
SRC_DIR = .
# arm_libs 里有 sqlite3.h, 加进搜索路径后可直接 #include <sqlite3.h>
INC_DIR = includes . arm_libs

# ---------- 源文件收集: 排除 MySQL 连接池 ----------
SRCS = $(filter-out $(SRC_DIR)/sqlconnpool.cpp, $(wildcard $(SRC_DIR)/*.cpp))
# 目标文件(统一平铺到 BUILD_DIR 下)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))
# 头文件依赖(任意头文件变动触发全量重编, 项目规模小, 够用)
DEPS = $(wildcard $(INC_DIR)/*.h)

# ---------- 编译器与链接参数: 按架构切换 ----------
ifeq ($(ARCH),x86)
    CXX      = g++
    LIB_DIR  =
    LIBS     = -lsqlite3
else
    # 换工具链只改这一行, 例如 OpenSTLinux SDK:
    # CROSS_COMPILE = arm-ostl-linux-gnueabi-
    CROSS_COMPILE ?= arm-linux-gnueabihf-
    CXX      = $(CROSS_COMPILE)g++
    # 优先静态库: 不依赖板上 glibc 版本, 板上也不需要拷 .so
    ifeq ($(wildcard ./arm_libs/libsqlite3.a),)
        # 没有 .a, 退回动态链接(工具链 glibc 必须 >= 板子 glibc)
        LIB_DIR  = -L./arm_libs
        LIBS     = -lsqlite3 -Wl,-rpath,\$$ORIGIN/../arm_libs
    else
        LIB_DIR  =
        LIBS     = ./arm_libs/libsqlite3.a -ldl -lm
    endif
endif

CFLAGS  = $(patsubst %,-I%,$(INC_DIR)) -std=c++14 -O2 -Wall -g -pthread
LDFLAGS = -pthread $(LIB_DIR) $(LIBS)

# ---------- 目标 ----------
all: $(BUILD_DIR)/$(TARGET)

# 链接: 只链接 OBJS, 不再带 -lmysqlclient
$(BUILD_DIR)/$(TARGET): $(OBJS) | create_build
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# *.o 生成规则
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(DEPS) | create_build
	$(CXX) -c -o $@ $< $(CFLAGS)

# ---------- 伪目标 ----------
.PHONY: all clean cleanall check create_build sqlite-static

check:
	@echo "ARCH      = $(ARCH)"
	@echo "CXX       = $(CXX)"
	@echo "CFLAGS    = $(CFLAGS)"
	@echo "LDFLAGS   = $(LDFLAGS)"
	@echo "CURDIR    = $(CURDIR)"
	@echo "SRCS      = $(SRCS)"
	@echo "OBJS      = $(OBJS)"
	@echo "静态库    = $(wildcard ./arm_libs/libsqlite3.a)"

# 可选: 自动从 sqlite3.c 生成静态库(需先把 sqlite3.c 放在项目根)
sqlite-static:
	$(CROSS_COMPILE)gcc -O2 -fPIC -DSQLITE_THREADSAFE=1 \
		-DSQLITE_OMIT_LOAD_EXTENSION -c sqlite3.c -o sqlite3.o
	ar rcs arm_libs/libsqlite3.a sqlite3.o
	@echo "生成 arm_libs/libsqlite3.a 完成"

clean:
	rm -rf $(BUILD_DIR)

cleanall:
	rm -rf build_x86 build_arm

create_build:
	@mkdir -p $(BUILD_DIR)
