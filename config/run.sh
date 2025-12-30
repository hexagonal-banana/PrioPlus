#!/bin/bash

# 脚本：start_fat320_configs.sh
# 功能：使用screen启动config目录下所有fat320子目录中的ExpressPass和CreditSpray配置文件

# 定义项目根目录
PROJECT_DIR="./"
CONFIG_DIR="$PROJECT_DIR/config"
OUTPUT_DIR="$PROJECT_DIR/output"

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 进入项目目录
cd $PROJECT_DIR

# 查找所有以fat320开头的目录
for dir in $CONFIG_DIR/fat320*; do
    if [ -d "$dir" ]; then
        dir_name=$(basename "$dir")
        echo "Processing directory: $dir_name"
        
        # 启动ExpressPass.json（如果存在）
        if [ -f "$dir/ExpressPass.json" ]; then
            screen_name="${dir_name}-ExpressPass"
            output_file="$OUTPUT_DIR/ExpressPass-output.txt"
            echo "Starting screen session: $screen_name"
            screen -dmS "$screen_name" bash -c "./ns3 run 'rdma-simulator $dir/ExpressPass.json' > '$output_file' 2>&1; exec bash"
            echo "Started: ./ns3 run 'rdma-simulator $dir/ExpressPass.json' > '$output_file' 2>&1"
        fi
        
        # 启动CreditSpray.json（如果存在）
        if [ -f "$dir/CreditSpray.json" ]; then
            screen_name="${dir_name}-CreditSpray"
            output_file="$OUTPUT_DIR/CreditSpray-output.txt"
            echo "Starting screen session: $screen_name"
            screen -dmS "$screen_name" bash -c "./ns3 run 'rdma-simulator $dir/CreditSpray.json' > '$output_file' 2>&1; exec bash"
            echo "Started: ./ns3 run 'rdma-simulator $dir/CreditSpray.json' > '$output_file' 2>&1"
        fi

        if [ -f "$dir/dcqcn.json" ]; then
            screen_name="${dir_name}-dcqcn"
            output_file="$OUTPUT_DIR/dcqcn-output.txt"
            echo "Starting screen session: $screen_name"
            screen -dmS "$screen_name" bash -c "./ns3 run 'rdma-simulator $dir/dcqcn.json' > '$output_file' 2>&1; exec bash"
            echo "Started: ./ns3 run 'rdma-simulator $dir/dcqcn.json' > '$output_file' 2>&1"
        fi        
    fi
done

echo "All screen sessions have been started."
echo "You can check running sessions with: screen -ls"
echo "To attach to a session: screen -r session_name"