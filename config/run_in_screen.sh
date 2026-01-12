#!/bin/bash

# 脚本：run_in_screen
# 功能：接受一个配置文件路径作为输入，在screen中启动该配置
# 输出将重定向到output目录下的对应子目录中

# 检查是否提供了配置文件路径参数
if [ $# -ne 1 ]; then
    echo "用法: $0 <配置文件路径>"
    echo "例如: $0 config/fat320/ExpressPass.json"
    exit 1
fi

# 获取配置文件路径
CONFIG_PATH="$1"

# 检查配置文件是否存在
if [ ! -f "$CONFIG_PATH" ]; then
    echo "错误: 配置文件 '$CONFIG_PATH' 不存在"
    exit 1
fi

# 定义项目根目录
PROJECT_DIR="."
OUTPUT_DIR="$PROJECT_DIR/output"

# 创建output目录（如果不存在）
mkdir -p "$OUTPUT_DIR"

# 获取配置文件的目录和文件名
CONFIG_DIR=$(dirname "$CONFIG_PATH")
CONFIG_FILENAME=$(basename "$CONFIG_PATH")
CONFIG_NAME="${CONFIG_FILENAME%.*}"  # 去掉扩展名

# 获取配置文件所在目录的名称
SUBDIR_NAME=$(basename "$CONFIG_DIR")

# 创建对应的输出目录
output_subdir="$OUTPUT_DIR/$SUBDIR_NAME"
mkdir -p "$output_subdir"

# 构建screen会话名称和输出文件路径
screen_name="${SUBDIR_NAME}-${CONFIG_NAME}"
output_file="$output_subdir/${CONFIG_NAME}.out"

# 启动screen会话
echo "Starting screen session: $screen_name"
screen -dmS "$screen_name" bash -c "ulimit -c unlimited; ./ns3 run 'rdma-simulator $CONFIG_PATH' > '$output_file' 2>&1; exec bash"
echo "Started: ulimit -c unlimited; ./ns3 run 'rdma-simulator $CONFIG_PATH' > '$output_file' 2>&1"

echo "Screen会话已启动: $screen_name"
echo "输出文件: $output_file"
echo "检查运行会话: screen -ls"
echo "连接到会话: screen -r $screen_name"
