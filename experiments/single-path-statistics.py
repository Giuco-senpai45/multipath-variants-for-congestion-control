import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import re
import pandas as pd
from datetime import datetime

# Configuration
OUTPUT_DIR = "results/nada"
SIM_EXEC = "./ns3 run 'scratch/basic'∏"
PARAM_VARIATIONS = {
    "packetSize": [500, 1000, 1500],
    "dataRate": ["3Mbps", "5Mbps", "8Mbps"],
    "bottleneckBw": ["6Mbps", "10Mbps", "15Mbps"],
    "delayMs": [20, 50, 100],
    "numCompetingSources": [1, 3, 5]
}

# Create output directory if it doesn't exist
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Regular expressions to parse simulation output
flow_re = re.compile(r"Flow (\d+) \(([0-9.]+) -> ([0-9.]+)\)")
tx_re = re.compile(r"Tx Packets: (\d+)")
rx_re = re.compile(r"Rx Packets: (\d+)")
throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
loss_re = re.compile(r"Packet loss: ([0-9.]+)%")

# Function to run a simulation with specific parameters
def run_simulation(params):
    """Run the simulation with the given parameters and return the results"""
    # Create a list for the command and its arguments
    cmd = ["./ns3", "run", "scratch/basic", "--"]

    # Add parameters as arguments
    for key, value in params.items():
        cmd.append(f"--{key}={value}")

    # Convert the command list to a string for display
    cmd_str = " ".join(cmd)
    print(f"Running: {cmd_str}")

    # Run the command
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Simulation failed with code {result.returncode}")
        print(f"Error: {result.stderr}")
        return None

    return result.stdout

# Function to parse simulation output
def parse_output(output):
    """Parse the simulation output and extract statistics"""
    if not output:
        return None

    lines = output.split('\n')
    flows = []
    current_flow = None

    for line in lines:
        # Check for flow header
        flow_match = flow_re.search(line)
        if flow_match:
            if current_flow:
                flows.append(current_flow)

            current_flow = {
                'id': int(flow_match.group(1)),
                'src': flow_match.group(2),
                'dst': flow_match.group(3),
                'tx_packets': 0,
                'rx_packets': 0,
                'throughput': 0,
                'delay': 0,
                'loss': 0
            }
            continue

        # Check for tx packets
        tx_match = tx_re.search(line)
        if tx_match and current_flow:
            current_flow['tx_packets'] = int(tx_match.group(1))
            continue

        # Check for rx packets
        rx_match = rx_re.search(line)
        if rx_match and current_flow:
            current_flow['rx_packets'] = int(rx_match.group(1))
            continue

        # Check for throughput
        throughput_match = throughput_re.search(line)
        if throughput_match and current_flow:
            current_flow['throughput'] = float(throughput_match.group(1))
            continue

        # Check for delay
        delay_match = delay_re.search(line)
        if delay_match and current_flow:
            current_flow['delay'] = float(delay_match.group(1))
            continue

        # Check for loss
        loss_match = loss_re.search(line)
        if loss_match and current_flow:
            current_flow['loss'] = float(loss_match.group(1))
            continue

    # Add the last flow if it exists
    if current_flow:
        flows.append(current_flow)

    return flows

# Function to analyze results
def analyze_results(all_results):
    """Analyze the results and generate insights"""
    # Convert results to a DataFrame for easier analysis
    data = []

    for result in all_results:
        for flow in result['flows']:
            data.append({
                'packet_size': result['params']['packetSize'],
                'data_rate': result['params']['dataRate'],
                'bottleneck_bw': result['params']['bottleneckBw'],
                'delay_ms': result['params']['delayMs'],
                'competing_sources': result['params']['numCompetingSources'],
                'flow_id': flow['id'],
                'src': flow['src'],
                'dst': flow['dst'],
                'tx_packets': flow['tx_packets'],
                'rx_packets': flow['rx_packets'],
                'throughput': flow['throughput'],
                'delay': flow['delay'],
                'loss': flow['loss']
            })

    df = pd.DataFrame(data)

    # Save raw data
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_file = f"{OUTPUT_DIR}/nada_results_{timestamp}.csv"
    df.to_csv(csv_file, index=False)
    print(f"Raw data saved to {csv_file}")

    return df

# Function to generate visualizations
def generate_visualizations(df):
    """Generate visualizations from the analyzed results"""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # 1. Effect of competing sources on throughput
    plt.figure(figsize=(12, 6))
    df_grouped = df.groupby('competing_sources')['throughput'].mean().reset_index()
    plt.plot(df_grouped['competing_sources'], df_grouped['throughput'], marker='o', linewidth=2)
    plt.xlabel('Number of Competing Sources')
    plt.ylabel('Average Throughput (Mbps)')
    plt.title('NADA Throughput vs. Number of Competing Sources')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/throughput_vs_competing_sources_{timestamp}.png")

    # 2. Effect of packet size on delay
    plt.figure(figsize=(12, 6))
    df_grouped = df.groupby('packet_size')['delay'].mean().reset_index()
    plt.plot(df_grouped['packet_size'], df_grouped['delay'] * 1000, marker='o', linewidth=2)  # Convert to ms
    plt.xlabel('Packet Size (bytes)')
    plt.ylabel('Average Delay (ms)')
    plt.title('NADA Delay vs. Packet Size')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/delay_vs_packet_size_{timestamp}.png")

    # 3. Effect of bottleneck bandwidth on packet loss
    plt.figure(figsize=(12, 6))
    # Convert string bottleneck_bw to numeric for sorting
    df['bottleneck_bw_num'] = df['bottleneck_bw'].apply(lambda x: int(x.split('M')[0]))
    df_grouped = df.groupby('bottleneck_bw_num')['loss'].mean().reset_index()
    plt.plot(df_grouped['bottleneck_bw_num'], df_grouped['loss'], marker='o', linewidth=2)
    plt.xlabel('Bottleneck Bandwidth (Mbps)')
    plt.ylabel('Average Packet Loss (%)')
    plt.title('NADA Packet Loss vs. Bottleneck Bandwidth')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/loss_vs_bottleneck_bw_{timestamp}.png")

    # 4. Delay-throughput trade-off for different link delays
    plt.figure(figsize=(12, 6))
    df_grouped = df.groupby('delay_ms')[['throughput', 'delay']].mean().reset_index()
    plt.scatter(df_grouped['delay'] * 1000, df_grouped['throughput'], s=100)  # Convert to ms
    for i, row in df_grouped.iterrows():
        plt.annotate(f"{row['delay_ms']}ms",
                    (row['delay'] * 1000, row['throughput']),
                    textcoords="offset points",
                    xytext=(0, 10),
                    ha='center')
    plt.xlabel('Average Delay (ms)')
    plt.ylabel('Average Throughput (Mbps)')
    plt.title('NADA Delay-Throughput Trade-off')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/delay_throughput_tradeoff_{timestamp}.png")

    # 5. Fairness analysis
    plt.figure(figsize=(12, 6))
    fairness_data = []

    for competing_sources in sorted(df['competing_sources'].unique()):
        subset = df[df['competing_sources'] == competing_sources]
        # Group by experiment (using all the parameters to identify unique runs)
        for (packet_size, data_rate, bottleneck_bw, delay_ms), group in subset.groupby(
            ['packet_size', 'data_rate', 'bottleneck_bw', 'delay_ms']):

            if len(group) > 1:  # Need at least 2 flows for fairness calculation
                throughputs = group['throughput'].values
                # Calculate Jain's fairness index
                fairness = np.sum(throughputs)**2 / (len(throughputs) * np.sum(throughputs**2))
                fairness_data.append({
                    'competing_sources': competing_sources,
                    'bottleneck_bw': bottleneck_bw,
                    'fairness': fairness
                })

    if fairness_data:
        fairness_df = pd.DataFrame(fairness_data)
        for bottleneck_bw in sorted(fairness_df['bottleneck_bw'].unique()):
            subset = fairness_df[fairness_df['bottleneck_bw'] == bottleneck_bw]
            if not subset.empty:
                plt.plot(
                    subset['competing_sources'],
                    subset['fairness'],
                    marker='o',
                    label=f"BW={bottleneck_bw}"
                )

        plt.xlabel('Number of Competing Sources')
        plt.ylabel('Jain\'s Fairness Index')
        plt.title('NADA Flow Fairness by Number of Competing Sources')
        plt.grid(True, linestyle='--', alpha=0.7)
        plt.legend()
        plt.tight_layout()
        plt.savefig(f"{OUTPUT_DIR}/fairness_by_competing_sources_{timestamp}.png")

    print(f"Visualizations saved to {OUTPUT_DIR}/")

# Main function
def main():
    print("Starting NADA statistics gathering")

    # Generate all parameter combinations (limited)
    all_params = []

    # Only vary one parameter at a time to limit the number of simulations
    base_params = {
        "packetSize": 1000,
        "dataRate": "5Mbps",
        "bottleneckBw": "10Mbps",
        "delayMs": 50,
        "simulationTime": 60,
        "numCompetingSources": 3
    }

    # Add base case
    all_params.append(base_params.copy())

    # Vary each parameter individually
    for param, values in PARAM_VARIATIONS.items():
        for value in values:
            if str(value) != str(base_params[param]):  # Skip the base case we already added
                params = base_params.copy()
                params[param] = value
                all_params.append(params)

    # Run simulations and collect results
    all_results = []

    for params in all_params:
        print(f"\nRunning simulation with parameters: {params}")
        output = run_simulation(params)

        if output:
            flows = parse_output(output)
            if flows:
                all_results.append({
                    'params': params,
                    'flows': flows
                })
                print(f"Simulation completed with {len(flows)} flows")
            else:
                print("No flow data found in simulation output")
        else:
            print("Simulation failed, skipping")

    # Analyze results
    if all_results:
        print("\nAnalyzing results...")
        df = analyze_results(all_results)

        # Generate visualizations
        print("\nGenerating visualizations...")
        generate_visualizations(df)
        print("\nStatistics gathering completed successfully")
    else:
        print("\nNo results to analyze")

if __name__ == "__main__":
    main()
