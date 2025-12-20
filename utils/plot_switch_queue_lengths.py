#!/usr/bin/env python3
"""
Script to plot queue lengths over time for all ports of a specified switch from JSON output file.
Creates individual time series plots for each port and combines them into a single image file.
Marks the maximum queue length with a vertical line on each subplot.
Only plots ports that have data.
"""

import json
import sys
import argparse
import matplotlib.pyplot as plt
import numpy as np
import os

def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='Plot queue lengths over time for a specified switch')
    parser.add_argument('output_file', help='Path to the JSON output file')
    parser.add_argument('switch_id', type=int, help='Switch ID to plot')
    return parser.parse_args()

def load_json_data(file_path):
    """Load JSON data from file."""
    try:
        with open(file_path, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"Error: File {file_path} not found.")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Failed to parse JSON file {file_path}: {e}")
        sys.exit(1)

def plot_switch_queue_lengths_over_time(data, file_path, switch_id):
    """Plot queue lengths over time for all ports of the specified switch and save as combined image."""
    # Find the switch data
    switch_stats = None
    if 'switchStatistics' in data:
        for switch in data['switchStatistics']:
            if switch.get('switchId') == switch_id:
                switch_stats = switch
                break
    
    if switch_stats is None:
        print(f"Error: Switch with ID {switch_id} not found in the data.")
        sys.exit(1)
    
    # Get port stats and filter out ports with no data
    port_stats_with_data = []
    
    for port_stat in switch_stats.get('portStats', []):
        # Check if this port has any queue with data
        has_data = False
        for queue_stat in port_stat.get('queueStats', []):
            if 'qLength' in queue_stat and queue_stat['qLength']:
                has_data = True
                break
        
        if has_data:
            port_stats_with_data.append(port_stat)
    
    if not port_stats_with_data:
        print(f"No port statistics with data found for switch {switch_id}.")
        return
    
    # Calculate number of subplots needed
    num_ports = len(port_stats_with_data)
    cols = 2  # Number of columns in the subplot grid
    rows = (num_ports + cols - 1) // cols  # Calculate rows needed
    
    # Create figure with subplots
    fig, axes = plt.subplots(rows, cols, figsize=(15, 5 * rows))
    
    # Handle case where there's only one subplot
    if num_ports == 1:
        axes = [axes]
    elif rows == 1 or cols == 1:
        axes = axes.flatten()
    else:
        axes = axes.flatten()
    
    # Iterate through ports with data and create individual time series plots
    for i, port_stat in enumerate(port_stats_with_data):
        ax = axes[i]
        port_id = port_stat.get('portId')
        
        # Plot queue length over time for each queue
        max_length = 0
        for queue_stat in port_stat.get('queueStats', []):
            # Check if queue length data exists
            if 'qLength' in queue_stat and queue_stat['qLength']:
                # Extract time and queue length data
                times = []
                lengths = []
                for record in queue_stat['qLength']:
                    times.append(record['timeNs'] / 1e9)  # Convert nanoseconds to seconds
                    lengths.append(record['lengthBytes'])
                
                # Update maximum length
                max_length = max(max_length, max(lengths)) if lengths else max_length
                
                # Plot the time series
                ax.plot(times, lengths, linestyle='-', marker='o', markersize=2, alpha=0.7, 
                       label=f"Queue {queue_stat.get('queueId', 'N/A')}")
        
        # Mark maximum queue length
        if max_length > 0:
            # Find the time of maximum length (simplified approach)
            ax.axhline(max_length, color='red', linestyle='--', linewidth=1)
            ax.text(0.05, max_length, f'Max: {max_length} bytes', 
                   verticalalignment='bottom', 
                   color='red', fontsize=9, transform=ax.get_yaxis_transform())
        
        # Configure subplot
        ax.set_xlabel('Time (seconds)')
        ax.set_ylabel('Queue Length (bytes)')
        ax.set_title(f'Port {port_id} Queue Lengths Over Time')
        ax.grid(True, alpha=0.3)
        ax.legend()
    
    # Hide any unused subplots
    for i in range(num_ports, len(axes)):
        axes[i].set_visible(False)
    
    # Set overall title
    fig.suptitle(f'Queue Lengths Over Time for Switch {switch_id}', fontsize=16)
    
    # Adjust layout
    plt.tight_layout()
    
    # Save the combined plot to the same directory as the input file
    input_dir = os.path.dirname(file_path)
    input_filename = os.path.splitext(os.path.basename(file_path))[0]
    output_image_path = os.path.join(input_dir, f"{input_filename}_switch_{switch_id}_queue_lengths_over_time.png")
    
    plt.savefig(output_image_path, dpi=300, bbox_inches='tight')
    print(f"Combined time series plot saved to: {output_image_path}")
    plt.close()

def main():
    """Main function."""
    args = parse_args()
    
    # Load data from JSON file
    data = load_json_data(args.output_file)
    
    # Plot queue lengths over time for the specified switch and save as combined image
    plot_switch_queue_lengths_over_time(data, args.output_file, args.switch_id)

if __name__ == "__main__":
    main()