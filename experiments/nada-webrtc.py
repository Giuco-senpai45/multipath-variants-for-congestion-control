#!/usr/bin/env python3
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import re
import pandas as pd
from datetime import datetime
import seaborn as sns

# Configuration
OUTPUT_DIR = "results/webrtc-nada"
SIM_EXEC = "./ns3 run 'scratch/simple-nada-webrtc'"
PARAM_VARIATIONS = {
    "bottleneckBw": ["2Mbps", "5Mbps", "10Mbps", "20Mbps"],  # Different bottleneck bandwidths
    "delayMs": [25, 50, 100, 200],  # Different network delays
    "queueSize": [50, 100, 200],  # Different queue sizes
    "queueDisc": ["CoDel", "PIE", "FqCoDel"],  # Different queue disciplines
    "keyFrameInterval": [30, 60, 100],  # WebRTC key frame intervals
    "numCompetingSources": [0, 1, 3, 5]  # Number of competing sources
}

# Create output directory if it doesn't exist
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Regular expressions to parse simulation output
flow_re = re.compile(r"Flow (\d+) \(([0-9.]+) -> ([0-9.]+)\)(\s*\[WebRTC\])?")
tx_re = re.compile(r"Tx Packets: (\d+)")
rx_re = re.compile(r"Rx Packets: (\d+)")
tx_bytes_re = re.compile(r"Tx Bytes: (\d+)")
rx_bytes_re = re.compile(r"Rx Bytes: (\d+)")
throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
jitter_re = re.compile(r"Mean jitter: ([0-9.e-]+) seconds")
loss_re = re.compile(r"Packet loss: ([0-9.]+)%")
efficiency_re = re.compile(r"Average network efficiency: ([0-9.]+)%")

def run_simulation(params):
    """Run the simulation with the given parameters and return the results"""
    # Create a command for the simulation
    cmd = ["./ns3", "run", "scratch/simple-nada-webrtc", "--"]

    # Add parameters
    for key, value in params.items():
        cmd.append(f"--{key}={value}")

    # Log the command
    print(f"Running: {' '.join(cmd)}")

    # Execute the simulation
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)  # 10-min timeout

        if result.returncode != 0:
            print(f"Simulation failed with code {result.returncode}")
            print(f"Error: {result.stderr}")
            return None

        return result.stdout
    except subprocess.TimeoutExpired:
        print("Simulation timed out after 10 minutes")
        return None
    except Exception as e:
        print(f"Error running simulation: {e}")
        return None

def parse_output(output, params):
    """Parse the simulation output and extract statistics"""
    if not output:
        return None

    lines = output.split('\n')
    flows = []
    current_flow = None
    overall_stats = {
        'total_tx_packets': 0,
        'total_rx_packets': 0,
        'total_tx_bytes': 0,
        'total_rx_bytes': 0,
        'overall_packet_loss': 0,
        'network_efficiency': 0
    }

    for line in lines:
        # Check for flow header
        flow_match = flow_re.search(line)
        if flow_match:
            if current_flow:
                flows.append(current_flow)

            is_webrtc = bool(flow_match.group(4))
            current_flow = {
                'id': int(flow_match.group(1)),
                'src': flow_match.group(2),
                'dst': flow_match.group(3),
                'is_webrtc': is_webrtc,
                'tx_packets': 0,
                'rx_packets': 0,
                'tx_bytes': 0,
                'rx_bytes': 0,
                'throughput': 0,
                'delay': 0,
                'jitter': 0,
                'loss': 0
            }
            continue

        # Extract flow metrics
        if current_flow:
            tx_match = tx_re.search(line)
            if tx_match:
                current_flow['tx_packets'] = int(tx_match.group(1))
                continue

            rx_match = rx_re.search(line)
            if rx_match:
                current_flow['rx_packets'] = int(rx_match.group(1))
                continue

            tx_bytes_match = tx_bytes_re.search(line)
            if tx_bytes_match:
                current_flow['tx_bytes'] = int(tx_bytes_match.group(1))
                continue

            rx_bytes_match = rx_bytes_re.search(line)
            if rx_bytes_match:
                current_flow['rx_bytes'] = int(rx_bytes_match.group(1))
                continue

            throughput_match = throughput_re.search(line)
            if throughput_match:
                current_flow['throughput'] = float(throughput_match.group(1))
                continue

            delay_match = delay_re.search(line)
            if delay_match:
                current_flow['delay'] = float(delay_match.group(1))
                continue

            jitter_match = jitter_re.search(line)
            if jitter_match:
                current_flow['jitter'] = float(jitter_match.group(1))
                continue

            loss_match = loss_re.search(line)
            if loss_match:
                current_flow['loss'] = float(loss_match.group(1))
                continue

        # Extract overall statistics
        total_tx_match = re.search(r"Total Tx Packets: (\d+)", line)
        if total_tx_match:
            overall_stats['total_tx_packets'] = int(total_tx_match.group(1))
            continue

        total_rx_match = re.search(r"Total Rx Packets: (\d+)", line)
        if total_rx_match:
            overall_stats['total_rx_packets'] = int(total_rx_match.group(1))
            continue

        total_tx_bytes_match = re.search(r"Total Tx Bytes: (\d+)", line)
        if total_tx_bytes_match:
            overall_stats['total_tx_bytes'] = int(total_tx_bytes_match.group(1))
            continue

        total_rx_bytes_match = re.search(r"Total Rx Bytes: (\d+)", line)
        if total_rx_bytes_match:
            overall_stats['total_rx_bytes'] = int(total_rx_bytes_match.group(1))
            continue

        overall_loss_match = re.search(r"Overall packet loss: ([0-9.]+)%", line)
        if overall_loss_match:
            overall_stats['overall_packet_loss'] = float(overall_loss_match.group(1))
            continue

        efficiency_match = efficiency_re.search(line)
        if efficiency_match:
            overall_stats['network_efficiency'] = float(efficiency_match.group(1))
            continue

    # Add the last flow if it exists
    if current_flow:
        flows.append(current_flow)

    # Calculate WebRTC-specific metrics for each flow
    for flow in flows:
        if flow['is_webrtc']:
            # Calculate estimated video quality based on loss rate and jitter
            # Assuming a simple model where loss impacts quality more severely
            if flow['rx_packets'] > 0:
                # MOS (Mean Opinion Score) estimate based on network conditions
                # Formula derived from WebRTC performance studies
                # 1-5 scale where 5 is excellent
                loss_factor = max(0, 1 - (flow['loss'] / 20))  # Loss above 20% makes video unusable
                delay_factor = max(0, 1 - (flow['delay'] / 1))  # Delay above 1s makes video unusable
                jitter_factor = max(0, 1 - (flow['jitter'] * 20))  # Jitter above 50ms makes video unusable

                flow['estimated_mos'] = 1 + 4 * loss_factor * delay_factor * jitter_factor

                # Calculate frame delivery ratio (assuming average frame size from simulation params)
                frame_size_bytes = int(params.get("packetSize", 1000)) * 2.5  # Average key frame size
                total_possible_frames = flow['tx_bytes'] / frame_size_bytes
                actual_frames = flow['rx_bytes'] / frame_size_bytes

                if total_possible_frames > 0:
                    flow['frame_delivery_ratio'] = min(1.0, actual_frames / total_possible_frames)
                else:
                    flow['frame_delivery_ratio'] = 0
            else:
                flow['estimated_mos'] = 1.0  # Worst quality
                flow['frame_delivery_ratio'] = 0.0

    return {'flows': flows, 'overall': overall_stats, 'params': params}

def analyze_results(all_results):
    """Analyze and organize the results into a DataFrame"""
    data = []

    for result in all_results:
        # Extract parameters
        params = result['params']

        # Extract overall statistics
        overall = result['overall']

        # Process each flow
        for flow in result['flows']:
            flow_type = "WebRTC" if flow['is_webrtc'] else "Competing UDP"

            # Create a record for this flow
            record = {
                'bottleneck_bw': params.get('bottleneckBw', 'N/A'),
                'delay_ms': params.get('delayMs', 'N/A'),
                'queue_size': params.get('queueSize', 'N/A'),
                'queue_disc': params.get('queueDisc', 'N/A'),
                'key_frame_interval': params.get('keyFrameInterval', 'N/A'),
                'competing_sources': params.get('numCompetingSources', 'N/A'),
                'flow_id': flow['id'],
                'flow_type': flow_type,
                'tx_packets': flow['tx_packets'],
                'rx_packets': flow['rx_packets'],
                'tx_bytes': flow['tx_bytes'],
                'rx_bytes': flow['rx_bytes'],
                'throughput_mbps': flow['throughput'],
                'delay_seconds': flow['delay'],
                'jitter_seconds': flow['jitter'],
                'loss_percent': flow['loss'],
                'network_efficiency': overall['network_efficiency'],
                'overall_packet_loss': overall['overall_packet_loss']
            }

            # Add WebRTC-specific metrics if available
            if flow['is_webrtc']:
                record['estimated_mos'] = flow.get('estimated_mos', 0)
                record['frame_delivery_ratio'] = flow.get('frame_delivery_ratio', 0)

            data.append(record)

    # Convert to DataFrame
    df = pd.DataFrame(data)

    # Add calculated columns
    df['bottleneck_numeric'] = df['bottleneck_bw'].apply(lambda x: float(x.split('M')[0]) if isinstance(x, str) else 0)

    # Save the raw data
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_file = f"{OUTPUT_DIR}/webrtc_nada_results_{timestamp}.csv"
    df.to_csv(csv_file, index=False)
    print(f"Raw data saved to {csv_file}")

    return df

def generate_visualizations(df):
    """Generate visualizations for WebRTC NADA performance analysis"""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Set the style for plots
    sns.set(style="whitegrid")
    plt.rcParams.update({'font.size': 11})

    # Filter WebRTC and competing flows
    webrtc_flows = df[df['flow_type'] == 'WebRTC']
    competing_flows = df[df['flow_type'] == 'Competing UDP']

    # 1. WebRTC Performance by Bottleneck Bandwidth
    plt.figure(figsize=(12, 8))

    plt.subplot(2, 2, 1)
    sns.lineplot(data=webrtc_flows, x='bottleneck_numeric', y='throughput_mbps')
    plt.title('WebRTC Throughput vs. Bottleneck Bandwidth')
    plt.xlabel('Bottleneck Bandwidth (Mbps)')
    plt.ylabel('Throughput (Mbps)')

    plt.subplot(2, 2, 2)
    sns.lineplot(data=webrtc_flows, x='bottleneck_numeric', y='loss_percent')
    plt.title('WebRTC Packet Loss vs. Bottleneck Bandwidth')
    plt.xlabel('Bottleneck Bandwidth (Mbps)')
    plt.ylabel('Packet Loss (%)')

    plt.subplot(2, 2, 3)
    sns.lineplot(data=webrtc_flows, x='bottleneck_numeric', y='delay_seconds')
    plt.title('WebRTC Delay vs. Bottleneck Bandwidth')
    plt.xlabel('Bottleneck Bandwidth (Mbps)')
    plt.ylabel('Delay (seconds)')

    plt.subplot(2, 2, 4)
    # Plot WebRTC estimated MOS if available
    if 'estimated_mos' in webrtc_flows.columns:
        sns.lineplot(data=webrtc_flows, x='bottleneck_numeric', y='estimated_mos')
        plt.title('Estimated Video Quality vs. Bottleneck Bandwidth')
        plt.xlabel('Bottleneck Bandwidth (Mbps)')
        plt.ylabel('Estimated MOS (1-5)')
        plt.ylim(1, 5)

    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/webrtc_performance_by_bandwidth_{timestamp}.png")

    # 2. WebRTC vs. Competing Traffic - Fairness Analysis
    if not competing_flows.empty:
        plt.figure(figsize=(12, 8))

        # Calculate average throughput by bottleneck and number of competing sources
        pivot_data = df.pivot_table(
            index=['bottleneck_bw', 'competing_sources'],
            columns='flow_type',
            values='throughput_mbps',
            aggfunc='mean'
        ).reset_index()

        # If there are both WebRTC and Competing UDP columns, calculate the ratio
        if 'WebRTC' in pivot_data.columns and 'Competing UDP' in pivot_data.columns:
            pivot_data['throughput_ratio'] = pivot_data['WebRTC'] / pivot_data['Competing UDP']

            plt.subplot(2, 1, 1)
            for sources in pivot_data['competing_sources'].unique():
                if sources > 0:  # Only show when there are competing sources
                    source_data = pivot_data[pivot_data['competing_sources'] == sources]
                    source_data = source_data.sort_values('bottleneck_bw')

                    # Extract numeric bandwidth for sorting
                    bandwidths = [float(bw.split('M')[0]) for bw in source_data['bottleneck_bw']]

                    plt.plot(bandwidths, source_data['throughput_ratio'],
                            marker='o', label=f"{sources} Competing")

            plt.axhline(y=1.0, color='r', linestyle='--', label='Equal Share')
            plt.title('Throughput Ratio (WebRTC / Competing UDP) by Bandwidth')
            plt.xlabel('Bottleneck Bandwidth (Mbps)')
            plt.ylabel('Throughput Ratio')
            plt.legend()

            plt.subplot(2, 1, 2)
            # Calculate Jain's fairness index for each scenario
            fairness_data = []

            for (bw, sources), group in df.groupby(['bottleneck_bw', 'competing_sources']):
                if sources > 0 and len(group) > 1:  # Skip if no competing sources
                    throughputs = group['throughput_mbps'].values
                    # Jain's fairness index
                    fairness = np.sum(throughputs)**2 / (len(throughputs) * np.sum(throughputs**2))
                    bw_numeric = float(bw.split('M')[0]) if isinstance(bw, str) else 0
                    fairness_data.append({
                        'bottleneck_bw': bw_numeric,
                        'competing_sources': sources,
                        'fairness_index': fairness
                    })

            if fairness_data:
                fairness_df = pd.DataFrame(fairness_data)
                for sources in fairness_df['competing_sources'].unique():
                    source_data = fairness_df[fairness_df['competing_sources'] == sources]
                    source_data = source_data.sort_values('bottleneck_bw')
                    plt.plot(source_data['bottleneck_bw'], source_data['fairness_index'],
                            marker='o', label=f"{sources} Competing")

                plt.title('Flow Fairness (Jain\'s Index) by Bandwidth')
                plt.xlabel('Bottleneck Bandwidth (Mbps)')
                plt.ylabel('Fairness Index (0-1)')
                plt.ylim(0, 1)
                plt.legend()

        plt.tight_layout()
        plt.savefig(f"{OUTPUT_DIR}/webrtc_fairness_analysis_{timestamp}.png")

    # 3. WebRTC Performance by Queue Discipline
    if 'queue_disc' in df.columns and df['queue_disc'].nunique() > 1:
        plt.figure(figsize=(14, 10))

        plt.subplot(2, 2, 1)
        sns.boxplot(data=webrtc_flows, x='queue_disc', y='throughput_mbps')
        plt.title('Throughput by Queue Discipline')
        plt.xlabel('Queue Discipline')
        plt.ylabel('Throughput (Mbps)')

        plt.subplot(2, 2, 2)
        sns.boxplot(data=webrtc_flows, x='queue_disc', y='loss_percent')
        plt.title('Packet Loss by Queue Discipline')
        plt.xlabel('Queue Discipline')
        plt.ylabel('Packet Loss (%)')

        plt.subplot(2, 2, 3)
        sns.boxplot(data=webrtc_flows, x='queue_disc', y='delay_seconds')
        plt.title('Delay by Queue Discipline')
        plt.xlabel('Queue Discipline')
        plt.ylabel('Delay (seconds)')

        plt.subplot(2, 2, 4)
        # Plot WebRTC frame delivery ratio if available
        if 'frame_delivery_ratio' in webrtc_flows.columns:
            sns.boxplot(data=webrtc_flows, x='queue_disc', y='frame_delivery_ratio')
            plt.title('Frame Delivery Ratio by Queue Discipline')
            plt.xlabel('Queue Discipline')
            plt.ylabel('Frame Delivery Ratio (0-1)')
            plt.ylim(0, 1)
        else:
            sns.boxplot(data=webrtc_flows, x='queue_disc', y='jitter_seconds')
            plt.title('Jitter by Queue Discipline')
            plt.xlabel('Queue Discipline')
            plt.ylabel('Jitter (seconds)')

        plt.tight_layout()
        plt.savefig(f"{OUTPUT_DIR}/webrtc_queue_discipline_performance_{timestamp}.png")

    # 4. WebRTC Performance Metrics Heatmap
    plt.figure(figsize=(14, 10))

    # Create a heatmap of key metrics vs. bottleneck bandwidth and delay
    if not webrtc_flows.empty and 'delay_ms' in webrtc_flows.columns:
        # Filter numeric columns and ensure we have enough data points
        if webrtc_flows['bottleneck_numeric'].nunique() > 1 and webrtc_flows['delay_ms'].nunique() > 1:
            metrics = ['throughput_mbps', 'loss_percent', 'delay_seconds', 'jitter_seconds']
            fig, axes = plt.subplots(2, 2, figsize=(14, 10))
            axes = axes.flatten()

            for i, metric in enumerate(metrics):
                pivot = webrtc_flows.pivot_table(
                    index='bottleneck_numeric',
                    columns='delay_ms',
                    values=metric,
                    aggfunc='mean'
                )

                if not pivot.empty:
                    sns.heatmap(pivot, annot=True, cmap='viridis', ax=axes[i])
                    axes[i].set_title(f'{metric} by Bandwidth and Delay')
                    axes[i].set_xlabel('Delay (ms)')
                    axes[i].set_ylabel('Bottleneck Bandwidth (Mbps)')

            plt.tight_layout()
            plt.savefig(f"{OUTPUT_DIR}/webrtc_performance_heatmap_{timestamp}.png")

    print(f"Visualizations saved to {OUTPUT_DIR}/")

def main():
    print("Starting WebRTC NADA Performance Analysis")

    # Generate a subset of parameter combinations to limit the number of simulations
    all_params = []

    # Default/base parameters
    base_params = {
        "packetSize": 1000,
        "dataRate": "100Mbps",  # Access link capacity
        "bottleneckBw": "10Mbps",
        "delayMs": 50,
        "simulationTime": 60,
        "numCompetingSources": 2,
        "keyFrameInterval": 100,
        "frameRate": 15,
        "logNada": "true",
        "queueSize": 100,
        "queueDisc": "CoDel",
    }

    # Add base case
    all_params.append(base_params.copy())

    # Vary each parameter individually
    for param, values in PARAM_VARIATIONS.items():
        for value in values:
            if str(value) != str(base_params.get(param, "")):  # Skip the base value
                params = base_params.copy()
                params[param] = value
                all_params.append(params)

    # Add some specific combined parameter variations for deeper analysis
    # E.g., Vary both bottleneck and delay
    for bw in ["5Mbps", "15Mbps"]:
        for delay in [25, 100]:
            if bw != base_params["bottleneckBw"] and delay != base_params["delayMs"]:
                params = base_params.copy()
                params["bottleneckBw"] = bw
                params["delayMs"] = delay
                all_params.append(params)

    # Vary queue disciplines
    for disc in ["PIE", "FqCoDel"]:
        if disc != base_params["queueDisc"]:
            params = base_params.copy()
            params["queueDisc"] = disc
            all_params.append(params)

    # Run simulations and collect results
    all_results = []

    for idx, params in enumerate(all_params):
        print(f"\nSimulation {idx+1}/{len(all_params)}")
        print(f"Parameters: {params}")

        output = run_simulation(params)

        if output:
            result = parse_output(output, params)
            if result:
                all_results.append(result)
                print(f"Simulation completed with {len(result['flows'])} flows")
            else:
                print("Failed to parse simulation output")
        else:
            print("Simulation failed or timed out")

    # Analyze results and generate visualizations
    if all_results:
        print("\nAnalyzing results...")
        df = analyze_results(all_results)

        print("\nGenerating visualizations...")
        generate_visualizations(df)

        print("\nWebRTC NADA performance analysis completed successfully")
    else:
        print("\nNo results to analyze")

if __name__ == "__main__":
    main()
