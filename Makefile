
# --- Makefile for modelauncher ---

# 编译器
CC = gcc

# 编译选项
# CFLAGS: 用于编译 .c 文件到 .o 文件
# -Wall, -Wextra: 开启所有常用的警告
# -g: 包含调试信息
# -std=c99: 使用 C99 标准
#  test
CFLAGS = -Wall -Wextra -g -std=c99

# LDFLAGS: 用于链接 .o 文件到最终的可执行文件
# -pthread: 链接 POSIX 线程库 (这是关键的修复！)
LDFLAGS = -pthread

# vcpkg 路径
VCPKG_UV_INC = /home/clearzero22/vcpkg/packages/libuv_x64-linux/include
VCPKG_UV_LIB = /home/clearzero22/vcpkg/packages/libuv_x64-linux/lib

VCPKG_CJSON_INC = /home/clearzero22/vcpkg/packages/cjson_x64-linux/include
VCPKG_CJSON_LIB = /home/clearzero22/vcpkg/packages/cjson_x64-linux/lib

# 头文件搜索路径
INCLUDES = -I$(VCPKG_UV_INC) -I$(VCPKG_CJSON_INC)

# 库文件搜索路径
LIBPATHS = -L$(VCPKG_UV_LIB) -L$(VCPKG_CJSON_LIB)

# 需要链接的库
# -luv: 链接 libuv
# -lcjson: 链接 libcjson
LIBS = -luv -lcjson

# 目标可执行文件名
TARGET = modelauncher

# 源文件
SRC = main.c

# 生成的目标文件
OBJ = $(SRC:.c=.o)

# 默认目标
all: $(TARGET)

# 链接规则：从 .o 文件生成可执行文件
$(TARGET): $(OBJ)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJ) -o $(TARGET) $(LIBPATHS) $(LIBS) $(LDFLAGS)
	@echo "Link complete."

# 编译规则：从 .c 文件生成 .o 文件
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	@echo "Compilation complete for $<."

# 运行目标
run: $(TARGET)
	@echo "Running launcher in 'work' mode..."
	./$(TARGET) work

# 清理目标
clean:
	@echo "Cleaning project..."
	rm -f $(OBJ) $(TARGET)
	@echo "Clean complete."

# 声明伪目标，避免与同名文件冲突
.PHONY: all run clean
