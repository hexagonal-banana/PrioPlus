#!/usr/bin/env python3
"""
Plot throughput scatter plot for each queue in JSON file
Y-axis unit: Gbps
Each queue has its own figure with vertical lines for flow start/end times
"""

import json
import matplotlib.pyplot as plt
import argparse
import os
import numpy as np

def plot_throughput_scatter(json_file_path, target_switch_id=None, show_flow_lines=False):
    """
    Plot queue throughput scatter plot - one figure per queue
    Add vertical lines for flow start and end times
    
    Args:
        json_file_path (str): JSON file path
        target_switch_id (int): Optional switch ID to filter by
        show_flow_lines (bool): Whether to show vertical lines for flow start/end times
    """
    # Read JSON file
    try:
        with open(json_file_path, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"Error: File {json_file_path} does not exist")
        return
    except json.JSONDecodeError:
        print(f"Error: File {json_file_path} is not a valid JSON format")
        return
    
    # Check if switchStatistics data exists
    if 'switchStatistics' not in data:
        print("Error: No switchStatistics data found in JSON file")
        return
    
    # Get the directory of the input JSON file
    json_dir = os.path.dirname(json_file_path)
    if not json_dir:  # If empty, use current directory
        json_dir = "."
    
    # Extract flow start and end times
    flow_times = []
    if 'flowStatistics' in data:
        for flow_stat in data['flowStatistics']:
            flow_id = flow_stat['flowId']
            start_ns = flow_stat['startNs']
            finish_ns = flow_stat['finishNs']
            flow_times.append({
                'flowId': flow_id,
                'startNs': start_ns,
                'finishNs': finish_ns
            })
        print(f"Found {len(flow_times)} flows with timing information")
    else:
        print("Warning: No flowStatistics found in JSON file")
    
    # Track if we have any data to plot
    has_any_data = False
    
    # Iterate through all switches
    for switch_stat in data['switchStatistics']:
        switch_id = switch_stat['switchId']
        
        # If target_switch_id is specified and doesn't match current switch_id, skip
        if target_switch_id is not None and switch_id != target_switch_id:
            continue
        
        # Iterate through all ports
        for port_stat in switch_stat['portStats']:
            port_id = port_stat['portId']
            
            # Check if queueStats exists
            if 'queueStats' in port_stat:
                # Iterate through all queues
                for queue_stat in port_stat['queueStats']:
                    queue_id = queue_stat['queueId']
                    
                    # Check if deviceThroughput data exists in this queue
                    if 'deviceThroughput' in queue_stat:
                        throughput_data = queue_stat['deviceThroughput']
                        
                        if not throughput_data:
                            print(f"Warning: Switch {switch_id} Port {port_id} Queue {queue_id} has no throughput data")
                            continue
                        
                        # Create a new figure for this queue
                        plt.figure(figsize=(12, 8))
                        
                        # Extract time and throughput data
                        times = [item['timeNs'] for item in throughput_data]
                        throughputs_bps = [item['throughputBitps'] for item in throughput_data]
                        
                        # Convert to Gbps
                        throughputs_gbps = [bps / 1e9 for bps in throughputs_bps]
                        
                        # Convert to relative time (starting from first time point)
                        start_time = min(times)
                        relative_times = [(t - start_time) / 1e9 for t in times]  # Convert to seconds
                        
                        # Plot scatter plot for this queue
                        plt.scatter(relative_times, throughputs_gbps, 
                                   color='blue', alpha=0.7, s=10, label='Throughput')
                        
                        # Add vertical lines for flow start and end times if enabled
                        if flow_times and show_flow_lines:
                            # Define colors for different flows
                            colors = ['red', 'green', 'orange', 'purple', 'brown', 'pink', 'gray', 'olive']
                            
                            for i, flow_time in enumerate(flow_times):
                                color = colors[i % len(colors)]
                                
                                # Convert flow times to relative time
                                flow_start_rel = (flow_time['startNs'] - start_time) / 1e9
                                flow_finish_rel = (flow_time['finishNs'] - start_time) / 1e9
                                
                                # Add vertical lines for flow start and end
                                plt.axvline(x=flow_start_rel, color=color, linestyle='--', 
                                           alpha=0.7, linewidth=1.5, 
                                           label=f'Flow {flow_time["flowId"]} Start')
                                plt.axvline(x=flow_finish_rel, color=color, linestyle=':', 
                                           alpha=0.7, linewidth=1.5,
                                           label=f'Flow {flow_time["flowId"]} End')
                        
                        # Set figure properties
                        plt.xlabel('Time (seconds)')
                        plt.ylabel('Throughput (Gbps)')
                        plt.title(f'Switch {switch_id} Port {port_id} Queue {queue_id} Throughput')
                        plt.grid(True, alpha=0.3)
                        
                        # Show legend
                        plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
                        
                        # Auto-adjust layout
                        plt.tight_layout()
                        
                        # Save image for this queue in the same directory as the JSON file
                        output_filename = f"{os.path.splitext(os.path.basename(json_file_path))[0]}_switch{switch_id}_port{port_id}_queue{queue_id}_throughput.png"
                        output_path = os.path.join(json_dir, output_filename)
                        plt.savefig(output_path, dpi=300, bbox_inches='tight')
                        print(f"Image saved as: {output_path}")
                        
                        # Show plot
                        plt.show()
                        
                        has_any_data = True
            else:
                print(f"Warning: Switch {switch_id} Port {port_id} has no queueStats")
    
    # Check if we have any data to plot overall
    if not has_any_data:
        print("Warning: No throughput data found in any port/queue")

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='Plot throughput scatter plot for each queue in JSON file with flow timing lines')
    parser.add_argument('json_file', help='JSON file path')
    parser.add_argument('switch_id', nargs='?', type=int, help='Optional switch ID to filter by')
    parser.add_argument('--show-flow-lines', action='store_true', default=False, 
                        help='Show vertical lines for flow start/end times (default: False)')
    parser.add_argument('--hide-flow-lines', action='store_false', dest='show_flow_lines', 
                        help='Hide vertical lines for flow start/end times (default behavior)')
    
    args = parser.parse_args()
    
    # Check if file exists
    if not os.path.exists(args.json_file):
        print(f"Error: File {args.json_file} does not exist")
        return
    
    # Plot graph
    plot_throughput_scatter(args.json_file, args.switch_id, args.show_flow_lines)

if __name__ == "__main__":
    main()