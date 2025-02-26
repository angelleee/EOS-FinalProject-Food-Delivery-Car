CFLAGS := -Wall -g
SRC_DIR := src
EXEC_DIR := exec

# 找到 SRC_DIR 中的所有 .c 文件
SRC_FILES := $(wildcard $(SRC_DIR)/*.c)

# 定义目标规则
all: $(SRC_FILES:$(SRC_DIR)/%.c=$(EXEC_DIR)/%)

# 编译规则，确保用 Tab 缩进
$(EXEC_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(EXEC_DIR) # 确保目标目录存在
	g++ $(CFLAGS) $< -o $@

# 清理目标文件
clean:
	rm -rf $(EXEC_DIR)

