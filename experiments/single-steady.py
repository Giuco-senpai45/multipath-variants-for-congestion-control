import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import subprocess
import re
import pandas as pd
from datetime import datetime

# Configuration
OUTPUT_DIR = "results/steady_nada"
SIM_EXEC = "./ns3 run 'scratch/steady'"
PARAM_VARIATIONS = {
    "packetSize": [500, 1000, 1500],
    "bottleneckBw": ["3Mbps", "5Mbps", "8Mbps"],
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
    cmd = ["./ns3", "run", "scratch/steady", "--"]

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
            # Identify if this is the NADA flow or a competing UDP flow
            is_nada = flow['src'].startswith('10.1.1')
            flow_type = "NADA" if is_nada else "Competing UDP"

            data.append({
                'packet_size': result['params']['packetSize'],
                'bottleneck_bw': result['params']['bottleneckBw'],
                'delay_ms': result['params']['delayMs'],
                'competing_sources': result['params']['numCompetingSources'],
                'flow_id': flow['id'],
                'flow_type': flow_type,
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
    csv_file = f"{OUTPUT_DIR}/steady_nada_results_{timestamp}.csv"
    df.to_csv(csv_file, index=False)
    print(f"Raw data saved to {csv_file}")

    return df

# Function to generate visualizations
def generate_visualizations(df):
    """Generate visualizations from the analyzed results"""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Separate NADA and competing flows for comparison
    nada_flows = df[df['flow_type'] == "NADA"]
    competing_flows = df[df['flow_type'] == "Competing UDP"]

    # 1. NADA vs Competing - Throughput Comparison
    plt.figure(figsize=(12, 6))

    # Group by competing sources and calculate mean throughput for each flow type
    nada_throughput = nada_flows.groupby('competing_sources')['throughput'].mean()
    competing_throughput = competing_flows.groupby('competing_sources')['throughput'].mean()

    plt.plot(nada_throughput.index, nada_throughput.values, marker='o', linewidth=2, label='NADA Flow')
    plt.plot(competing_throughput.index, competing_throughput.values, marker='s', linewidth=2, label='Competing UDP Flow')

    plt.xlabel('Number of Competing Sources')
    plt.ylabel('Average Throughput (Mbps)')
    plt.title('NADA vs Competing UDP Throughput')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/nada_vs_competing_throughput_{timestamp}.png")

    # 2. Bottleneck Bandwidth Impact on Throughput Ratio
    plt.figure(figsize=(12, 6))

    # Calculate throughput ratio (NADA / Competing) for different bottleneck bandwidths
    throughput_ratios = []

    for bw in sorted(df['bottleneck_bw'].unique()):
        bw_subset = df[df['bottleneck_bw'] == bw]
        nada_subset = bw_subset[bw_subset['flow_type'] == "NADA"]
        competing_subset = bw_subset[bw_subset['flow_type'] == "Competing UDP"]

        if not nada_subset.empty and not competing_subset.empty:
            nada_mean = nada_subset['throughput'].mean()
            competing_mean = competing_subset['throughput'].mean()

            if competing_mean > 0:  # Avoid division by zero
                ratio = nada_mean / competing_mean
                bw_num = int(bw.split('M')[0])  # Extract numeric value from "XMbps"
                throughput_ratios.append((bw_num, ratio))

    if throughput_ratios:
        bw_values, ratio_values = zip(*sorted(throughput_ratios))
        plt.plot(bw_values, ratio_values, marker='o', linewidth=2)
        plt.axhline(y=1.0, color='r', linestyle='--', alpha=0.5, label='Equal Share (Ratio=1)')
        plt.xlabel('Bottleneck Bandwidth (Mbps)')
        plt.ylabel('Throughput Ratio (NADA / Competing)')
        plt.title('NADA Fairness: Throughput Ratio vs. Bottleneck Bandwidth')
        plt.grid(True, linestyle='--', alpha=0.7)
        plt.legend()
        plt.tight_layout()
        plt.savefig(f"{OUTPUT_DIR}/throughput_ratio_vs_bottleneck_{timestamp}.png")

    # 3. Packet Loss Comparison
    plt.figure(figsize=(12, 6))

    # Group by bottleneck bandwidth and calculate mean loss for each flow type
    nada_loss = nada_flows.groupby('bottleneck_bw')['loss'].mean()
    competing_loss = competing_flows.groupby('bottleneck_bw')['loss'].mean()

    # Convert bottleneck_bw to numeric for sorting
    bw_values = [int(bw.split('M')[0]) for bw in nada_loss.index]
    sort_idx = np.argsort(bw_values)

    plt.plot(np.array(bw_values)[sort_idx], nada_loss.values[sort_idx], marker='o', linewidth=2, label='NADA Flow')
    plt.plot(np.array(bw_values)[sort_idx], competing_loss.values[sort_idx], marker='s', linewidth=2, label='Competing UDP Flow')

    plt.xlabel('Bottleneck Bandwidth (Mbps)')
    plt.ylabel('Average Packet Loss (%)')
    plt.title('NADA vs Competing UDP: Packet Loss')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/nada_vs_competing_loss_{timestamp}.png")

    # 4. Delay Comparison for Different Packet Sizes
    plt.figure(figsize=(12, 6))

    # Group by packet size and calculate mean delay for each flow type
    nada_delay = nada_flows.groupby('packet_size')['delay'].mean() * 1000  # Convert to ms
    competing_delay = competing_flows.groupby('packet_size')['delay'].mean() * 1000  # Convert to ms

    plt.plot(nada_delay.index, nada_delay.values, marker='o', linewidth=2, label='NADA Flow')
    plt.plot(competing_delay.index, competing_delay.values, marker='s', linewidth=2, label='Competing UDP Flow')

    plt.xlabel('Packet Size (bytes)')
    plt.ylabel('Average Delay (ms)')
    plt.title('NADA vs Competing UDP: Delay by Packet Size')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{OUTPUT_DIR}/delay_by_packet_size_{timestamp}.png")

    # 5. Jain's Fairness Index for Different Scenarios
    plt.figure(figsize=(12, 6))
    fairness_data = []

    # Calculate fairness for different bottleneck bandwidths and number of sources
    for (bw, sources), group in df.groupby(['bottleneck_bw', 'competing_sources']):
        if len(group) > 1:  # Need at least 2 flows for fairness
            throughputs = group['throughput'].values
            fairness = np.sum(throughputs)**2 / (len(throughputs) * np.sum(throughputs**2))
            bw_num = int(bw.split('M')[0])
            fairness_data.append({
                'bottleneck_bw': bw_num,
                'competing_sources': sources,
                'fairness': fairness
            })

    if fairness_data:
        fairness_df = pd.DataFrame(fairness_data)
        for bw in sorted(fairness_df['bottleneck_bw'].unique()):
            subset = fairness_df[fairness_df['bottleneck_bw'] == bw]
            if not subset.empty:
                plt.plot(
                    subset['competing_sources'],
                    subset['fairness'],
                    marker='o',
                    label=f"BW={bw}Mbps"
                )

        plt.xlabel('Number of Competing Sources')
        plt.ylabel('Jain\'s Fairness Index')
        plt.title('Flow Fairness with Fixed Sending Rates')
        plt.grid(True, linestyle='--', alpha=0.7)
        plt.legend()
        plt.tight_layout()
        plt.savefig(f"{OUTPUT_DIR}/fairness_index_{timestamp}.png")

    print(f"Visualizations saved to {OUTPUT_DIR}/")

# Main function
def main():
    print("Starting Steady NADA Statistics Gathering")

    # Generate all parameter combinations (limited)
    all_params = []

    # Only vary one parameter at a time to limit the number of simulations
    base_params = {
        "packetSize": 1000,
        "bottleneckBw": "5Mbps",
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
