#!/usr/bin/env python3
"""
统计JSON结果文件中每个交换机的每个端口的吞吐量，以GB为单位输出
"""

import json
import sys
import os
import argparse

def calculate_throughput_gb(json_file_path):
    """
    计算JSON文件中每个交换机每个端口的吞吐量（GB）
    
    Args:
        json_file_path (str): JSON文件路径
        
    Returns:
        dict: 包含交换机端口吞吐量统计的字典
    """
    try:
        with open(json_file_path, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"错误: 文件 {json_file_path} 不存在")
        return None
    except json.JSONDecodeError:
        print(f"错误: 文件 {json_file_path} 不是有效的JSON格式")
        return None
    
    # 检查是否包含switchStatistics
    if 'switchStatistics' not in data:
        print("错误: JSON文件中没有找到switchStatistics字段")
        return None
    
    results = {}
    
    for switch in data['switchStatistics']:
        switch_id = switch['switchId']
        results[switch_id] = {'ports': {}}
        
        for port in switch['portStats']:
            port_id = port['portId']
            total_throughput_bits = 0
            
            # 遍历每个队列的deviceThroughput数据
            for queue in port['queueStats']:
                if 'deviceThroughput' in queue:
                    for throughput_entry in queue['deviceThroughput']:
                        # 累加每个时间点的吞吐量（单位：bits）
                        total_throughput_bits += throughput_entry['throughputBitps']
            
            # 转换为GB (1 GB = 8e9 bits)
            total_throughput_gb = total_throughput_bits / 8e9
            
            results[switch_id]['ports'][port_id] = {
                'total_throughput_bits': total_throughput_bits,
                'total_throughput_gb': total_throughput_gb
            }
    
    return results

def print_results(results, json_file_path):
    """
    打印吞吐量统计结果
    
    Args:
        results (dict): 吞吐量统计结果
        json_file_path (str): JSON文件路径
    """
    print(f"文件: {json_file_path}")
    print("=" * 60)
    
    if not results:
        print("没有找到有效的吞吐量数据")
        return
    
    for switch_id, switch_data in sorted(results.items()):
        print(f"\n交换机 {switch_id}:")
        print("-" * 40)
        
        for port_id, port_data in sorted(switch_data['ports'].items()):
            throughput_gb = port_data['total_throughput_gb']
            throughput_bits = port_data['total_throughput_bits']
            
            print(f"  端口 {port_id}: {throughput_gb:.6f} GB ({throughput_bits:.0f} bits)")
        
        # 计算交换机的总吞吐量
        switch_total_gb = sum(port['total_throughput_gb'] for port in switch_data['ports'].values())
        switch_total_bits = sum(port['total_throughput_bits'] for port in switch_data['ports'].values())
        print(f"  交换机总计: {switch_total_gb:.6f} GB ({switch_total_bits:.0f} bits)")
    
    # 计算所有交换机的总吞吐量
    all_switch_total_gb = 0
    all_switch_total_bits = 0
    for switch_data in results.values():
        for port_data in switch_data['ports'].values():
            all_switch_total_gb += port_data['total_throughput_gb']
            all_switch_total_bits += port_data['total_throughput_bits']
    
    print(f"\n所有交换机总计: {all_switch_total_gb:.6f} GB ({all_switch_total_bits:.0f} bits)")
    print("=" * 60)

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='统计JSON结果文件中每个交换机的每个端口的吞吐量')
    parser.add_argument('json_file', help='JSON结果文件路径')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.json_file):
        print(f"错误: 文件 {args.json_file} 不存在")
        sys.exit(1)
    
    results = calculate_throughput_gb(args.json_file)
    
    if results is not None:
        print_results(results, args.json_file)

if __name__ == "__main__":
    main()