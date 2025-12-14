#!/usr/bin/env python3
"""
统计JSON结果文件中每个交换机的每个端口的吞吐量，以Bytes为单位输出
包括每个端口每个队列的详细吞吐量统计
基于速率和固定时间间隔计算实际数据量
"""

import json
import sys
import os
import argparse
import re

def parse_time_interval(interval_str):
    """
    解析时间间隔字符串，转换为秒
    
    Args:
        interval_str (str): 时间间隔字符串，如 "10us", "1ms", "100ns"
        
    Returns:
        float: 时间间隔（秒）
    """
    # 匹配数字和单位
    match = re.match(r'^(\d+(?:\.\d+)?)([a-zA-Z]+)$', interval_str)
    if not match:
        raise ValueError(f"无法解析时间间隔: {interval_str}")
    
    value = float(match.group(1))
    unit = match.group(2).lower()
    
    # 转换为秒
    if unit == 'ns':
        return value / 1e9
    elif unit == 'us':
        return value / 1e6
    elif unit == 'ms':
        return value / 1e3
    elif unit == 's':
        return value
    else:
        raise ValueError(f"不支持的时间单位: {unit}")

def calculate_data_volume_bytes(json_file_path):
    """
    根据速率和固定时间间隔计算JSON文件中每个交换机每个端口的数据量（Bytes）
    包括每个队列的详细统计
    
    Args:
        json_file_path (str): JSON文件路径
        
    Returns:
        dict: 包含交换机端口数据量统计的字典
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
    
    # 获取时间间隔配置
    try:
        interval_str = data['config']['globalConfig']['deviceThroughputRecordInterval']
        time_interval_seconds = parse_time_interval(interval_str)
        print(f"使用时间间隔: {interval_str} ({time_interval_seconds:.9f} 秒)")
    except (KeyError, ValueError) as e:
        print(f"警告: 无法获取时间间隔配置，使用默认值10us: {e}")
        time_interval_seconds = 10e-6  # 默认10微秒
    
    results = {}
    
    for switch in data['switchStatistics']:
        switch_id = switch['switchId']
        results[switch_id] = {'ports': {}}
        
        for port in switch['portStats']:
            port_id = port['portId']
            total_data_bytes = 0
            queue_stats = {}
            
            # 遍历每个队列的deviceThroughput数据
            for queue in port['queueStats']:
                queue_id = queue['queueId']
                queue_data_bytes = 0
                
                if 'deviceThroughput' in queue:
                    for throughput_entry in queue['deviceThroughput']:
                        # 计算每个时间点的数据量
                        # 数据量 = 速率 × 时间间隔
                        data_in_interval_bits = throughput_entry['throughputBitps'] * time_interval_seconds
                        # 转换为Bytes (1 Byte = 8 bits)
                        data_in_interval_bytes = data_in_interval_bits / 8
                        queue_data_bytes += data_in_interval_bytes
                
                # 存储队列统计
                queue_stats[queue_id] = {
                    'data_bytes': queue_data_bytes
                }
                
                # 累加到端口总数据量
                total_data_bytes += queue_data_bytes
            
            results[switch_id]['ports'][port_id] = {
                'total_data_bytes': total_data_bytes,
                'queues': queue_stats
            }
    
    return results

def print_results(results, json_file_path):
    """
    打印数据量统计结果（Bytes为单位）
    包括每个端口每个队列的详细统计，跳过数据量为0的队列
    
    Args:
        results (dict): 数据量统计结果
        json_file_path (str): JSON文件路径
    """
    print(f"文件: {json_file_path}")
    print("=" * 80)
    print("说明: 数据量基于速率和固定时间间隔计算得出，单位为Bytes")
    print("=" * 80)
    
    if not results:
        print("没有找到有效的吞吐量数据")
        return
    
    for switch_id, switch_data in sorted(results.items()):
        print(f"\n交换机 {switch_id}:")
        print("-" * 60)
        
        for port_id, port_data in sorted(switch_data['ports'].items()):
            data_bytes = port_data['total_data_bytes']
            
            print(f"  端口 {port_id}: {data_bytes:.0f} Bytes")
            
            # 打印每个队列的数据量（跳过为0的队列）
            queue_printed = False
            for queue_id, queue_data in sorted(port_data['queues'].items()):
                queue_data_bytes = queue_data['data_bytes']
                
                # 跳过数据量为0的队列
                if queue_data_bytes > 0:
                    if not queue_printed:
                        print("    队列详情:")
                        queue_printed = True
                    print(f"      队列 {queue_id}: {queue_data_bytes:.0f} Bytes")
            
            if not queue_printed:
                print("    所有队列数据量均为0")
        
        # 计算交换机的总数据量
        switch_total_bytes = sum(port['total_data_bytes'] for port in switch_data['ports'].values())
        print(f"  交换机总计: {switch_total_bytes:.0f} Bytes")
    
    # 计算所有交换机的总数据量
    all_switch_total_bytes = 0
    for switch_data in results.values():
        for port_data in switch_data['ports'].values():
            all_switch_total_bytes += port_data['total_data_bytes']
    
    print(f"\n所有交换机总计: {all_switch_total_bytes:.0f} Bytes")
    print("=" * 80)

def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='统计JSON结果文件中每个交换机的每个端口的数据量，基于速率和固定时间间隔计算，包括队列级别统计，单位为Bytes'
    )
    parser.add_argument('json_file', help='JSON结果文件路径')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.json_file):
        print(f"错误: 文件 {args.json_file} 不存在")
        sys.exit(1)
    
    results = calculate_data_volume_bytes(args.json_file)
    
    if results is not None:
        print_results(results, args.json_file)

if __name__ == "__main__":
    main()