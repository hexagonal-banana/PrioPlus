./ns3 

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Get all files in this directory
for f in ${SCRIPT_DIR}/*; do
    # Check if file end with .json
    if [[ $f == *.json ]]; then
        # Run the simulation
        echo "Run $f"
        ./ns3 run "rdma-simulator $f"&
    fi
done

# # 排除的文件名列表
# exclude_list=("hpcc-target2" "hpcc" "poseidon" "powertcp" "theta-powertcp")

# # Get all files in this directory
# # for f in ${SCRIPT_DIR}/*.json; do
# #     # 获取文件名（去除路径）
# #     filename=$(basename "$f" .json)

# #     # 检查文件名是否在排除列表中
# #     if [[ ! " ${exclude_list[@]} " =~ " ${filename} " ]]; then
# #         # 如果不在排除列表中，运行模拟
# #         echo "Run $f"
# #         ./ns3 run "rdma-simulator $f" &
# #     else
# #         echo "Skip $f"
# #     fi
# # done

# only_list=("dcqcn-limit" "timely-limit")

# # Get all files in this directory
# for f in ${SCRIPT_DIR}/*.json; do
#     # 获取文件名（去除路径）
#     filename=$(basename "$f" .json)

#     if [[ " ${only_list[@]} " =~ " ${filename} " ]]; then
#         echo "Run $f"
#         ./ns3 run "rdma-simulator $f" &
#     else
#         echo "Skip $f"
#     fi
# done