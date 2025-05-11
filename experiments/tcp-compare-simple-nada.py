import os
import subprocess
import pandas as pd
import matplotlib.pyplot as plt
from datetime import datetime
import re
import numpy as np
import seaborn as sns

# Configuration
OUTPUT_DIR = "results/tcp-simple-nadas"
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Define different scenarios to compare
SIMULATION_SCENARIOS = [
    {
        "name": "Standard",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "delayMs": 50,
            "frameRate": 30
        }
    },
    {
        "name": "High Framerate",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "delayMs": 50,
            "frameRate": 60
        }
    },
    {
        "name": "Low Bandwidth",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "5Mbps",
            "delayMs": 50,
            "frameRate": 30
        }
    },
    {
        "name": "High Bandwidth",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "20Mbps",
            "delayMs": 50,
            "frameRate": 30
        }
    },
    {
        "name": "High Latency",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "delayMs": 150,
            "frameRate": 30
        }
    },
    {
        "name": "HD Video Stream",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "15Mbps",
            "delayMs": 50,
            "frameRate": 30,
            "packetSize": 1500  # Larger packet size for HD content
        }
    },
    {
        "name": "Congested Network",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "delayMs": 100,
            "frameRate": 30,
            "numCompetingSources": 5
        }
    },
    # Modern network scenarios (2025)
    {
        "name": "Budget Home Internet",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "50Mbps",
            "delayMs": 30,
            "frameRate": 30
        }
    },
    {
        "name": "Average Home Internet",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "200Mbps",
            "delayMs": 20,
            "frameRate": 30
        }
    },
    {
        "name": "High-End Fiber",
        "params": {
            "dataRate": "2048Mbps",
            "bottleneckBw": "1000Mbps",
            "delayMs": 10,
            "frameRate": 60
        }
    },
    {
        "name": "Mobile 5G",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "150Mbps",
            "delayMs": 35,
            "frameRate": 30,
            "queueDisc": "CoDel"  # Mobile networks often have AQM
        }
    },
    {
        "name": "Mobile 4G",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "20Mbps",
            "delayMs": 60,
            "frameRate": 30,
            "queueDisc": "CoDel"
        }
    },
    {
        "name": "Rural Connection",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "10Mbps",
            "delayMs": 80,
            "frameRate": 30
        }
    },
    {
        "name": "Corporate Network",
        "params": {
            "dataRate": "10000Mbps",
            "bottleneckBw": "500Mbps",
            "delayMs": 15,
            "frameRate": 60
        }
    },
    {
        "name": "VPN Connection",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "100Mbps",
            "delayMs": 45,
            "frameRate": 30
        }
    },
    {
        "name": "Video Streaming (4K)",
        "params": {
            "dataRate": "1024Mbps",
            "bottleneckBw": "35Mbps",
            "delayMs": 25,
            "frameRate": 60,
            "packetSize": 1400
        }
    }
]

# Function to create a scenario folder
def get_scenario_folder(scenario_name):
    """Create and return path to scenario-specific folder"""
    folder_name = scenario_name.lower().replace(' ', '_')
    folder_path = os.path.join(OUTPUT_DIR, folder_name)
    os.makedirs(folder_path, exist_ok=True)
    return folder_path

# Function to run a simulation and collect results
def run_simulation(script_name, params=None):
    """Run the specified simulation script with parameters and return the output."""
    cmd = ["./ns3", "run", script_name]

    # Add parameters if provided
    if params:
        cmd.append("--")
        for key, value in params.items():
            cmd.append(f"--{key}={value}")

    print(f"Running simulation: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)  # 10 minute timeout

    if result.returncode != 0:
        print(f"Simulation failed with code {result.returncode}")
        print(f"Error: {result.stderr}")
        return None

    return result.stdout

def parse_output(output):
    """Parse the simulation output and extract relevant statistics."""
    if not output:
        return None

    # Regular expressions for flow statistics
    throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
    delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
    loss_re = re.compile(r"Packet loss: ([0-9.]+)%")
    jitter_re = re.compile(r"Mean jitter: ([0-9.e-]+) seconds")

    # Regular expressions for NADA/WebRTC specific statistics
    nada_tx_packets_re = re.compile(r"(NADA|WebRTC) Tx Packets: ([0-9]+)")
    nada_rx_packets_re = re.compile(r"(NADA|WebRTC) Rx Packets: ([0-9]+)")
    nada_tx_bytes_re = re.compile(r"(NADA|WebRTC) Tx Bytes: ([0-9]+)")
    nada_rx_bytes_re = re.compile(r"(NADA|WebRTC) Rx Bytes: ([0-9]+)")
    nada_loss_re = re.compile(r"(NADA|WebRTC) packet loss: ([0-9.]+)%")
    nada_efficiency_re = re.compile(r"(NADA|WebRTC) efficiency: ([0-9.]+)%")


    # Regular expressions for TCP specific statistics
    tcp_tx_packets_re = re.compile(r"TCP Tx Packets: ([0-9]+)")
    tcp_rx_packets_re = re.compile(r"TCP Rx Packets: ([0-9]+)")
    tcp_tx_bytes_re = re.compile(r"TCP Tx Bytes: ([0-9]+)")
    tcp_rx_bytes_re = re.compile(r"TCP Rx Bytes: ([0-9]+)")
    tcp_loss_re = re.compile(r"TCP packet loss: ([0-9.]+)%")
    tcp_efficiency_re = re.compile(r"TCP efficiency: ([0-9.]+)%")

    lines = output.split('\n')

    # Initialize stats dictionary with additional fields for NADA and TCP
    stats = {
        'throughput': [],
        'delay': [],
        'loss': [],
        'jitter': [],

        # NADA specific metrics
        'nada_tx_packets': None,
        'nada_rx_packets': None,
        'nada_tx_bytes': None,
        'nada_rx_bytes': None,
        'nada_loss': None,
        'nada_efficiency': None,
        'nada_throughput': None,

        # TCP specific metrics
        'tcp_tx_packets': None,
        'tcp_rx_packets': None,
        'tcp_tx_bytes': None,
        'tcp_rx_bytes': None,
        'tcp_loss': None,
        'tcp_efficiency': None,
        'tcp_throughput': None
    }

    # Parse individual flow statistics first
    for line in lines:
        # Extract throughput
        throughput_match = throughput_re.search(line)
        if throughput_match:
            stats['throughput'].append(float(throughput_match.group(1)))
            continue

        # Extract delay
        delay_match = delay_re.search(line)
        if delay_match:
            stats['delay'].append(float(delay_match.group(1)))
            continue

        # Extract loss percentage
        loss_match = loss_re.search(line)
        if loss_match and not line.startswith("NADA packet loss") and not line.startswith("TCP packet loss"):
            stats['loss'].append(float(loss_match.group(1)))
            continue

        # Extract jitter
        jitter_match = jitter_re.search(line)
        if jitter_match:
            stats['jitter'].append(float(jitter_match.group(1)))
            continue

    # Parse NADA-specific metrics
    for line in lines:
        # NADA statistics
        nada_tx_packets_match = nada_tx_packets_re.search(line)
        if nada_tx_packets_match:
            stats['nada_tx_packets'] = int(nada_tx_packets_match.group(2))
            continue

        nada_rx_packets_match = nada_rx_packets_re.search(line)
        if nada_rx_packets_match:
            stats['nada_rx_packets'] = int(nada_rx_packets_match.group(2))
            continue

        nada_tx_bytes_match = nada_tx_bytes_re.search(line)
        if nada_tx_bytes_match:
            stats['nada_tx_bytes'] = int(nada_tx_bytes_match.group(2))
            continue

        nada_rx_bytes_match = nada_rx_bytes_re.search(line)
        if nada_rx_bytes_match:
            stats['nada_rx_bytes'] = int(nada_rx_bytes_match.group(2))
            continue

        nada_loss_match = nada_loss_re.search(line)
        if nada_loss_match:
            stats['nada_loss'] = float(nada_loss_match.group(2))
            continue

        nada_efficiency_match = nada_efficiency_re.search(line)
        if nada_efficiency_match:
            stats['nada_efficiency'] = float(nada_efficiency_match.group(2))
            continue

        # TCP statistics
        tcp_tx_packets_match = tcp_tx_packets_re.search(line)
        if tcp_tx_packets_match:
            stats['tcp_tx_packets'] = int(tcp_tx_packets_match.group(1))
            continue

        tcp_rx_packets_match = tcp_rx_packets_re.search(line)
        if tcp_rx_packets_match:
            stats['tcp_rx_packets'] = int(tcp_rx_packets_match.group(1))
            continue

        tcp_tx_bytes_match = tcp_tx_bytes_re.search(line)
        if tcp_tx_bytes_match:
            stats['tcp_tx_bytes'] = int(tcp_tx_bytes_match.group(1))
            continue

        tcp_rx_bytes_match = tcp_rx_bytes_re.search(line)
        if tcp_rx_bytes_match:
            stats['tcp_rx_bytes'] = int(tcp_rx_bytes_match.group(1))
            continue

        tcp_loss_match = tcp_loss_re.search(line)
        if tcp_loss_match:
            stats['tcp_loss'] = float(tcp_loss_match.group(1))
            continue

        tcp_efficiency_match = tcp_efficiency_re.search(line)
        if tcp_efficiency_match:
            stats['tcp_efficiency'] = float(tcp_efficiency_match.group(1))
            continue

    # Calculate derived metrics if we have the raw data
    simulation_time_re = re.compile(r"Simulation time: ([0-9]+) seconds")
    simulation_time = 60  # Default value
    for line in lines:
        sim_time_match = simulation_time_re.search(line)
        if sim_time_match:
            simulation_time = int(sim_time_match.group(1))
            break

    # Calculate NADA throughput in Mbps from rx_bytes if available
    if stats['nada_rx_bytes'] is not None:
        # Convert bytes to bits and divide by simulation time to get bits per second, then to Mbps
        stats['nada_throughput'] = (stats['nada_rx_bytes'] * 8) / (simulation_time * 1000000)

    # Calculate TCP throughput in Mbps from rx_bytes if available
    if stats['tcp_rx_bytes'] is not None:
        # Convert bytes to bits and divide by simulation time to get bits per second, then to Mbps
        stats['tcp_throughput'] = (stats['tcp_rx_bytes'] * 8) / (simulation_time * 1000000)

    # Check if we parsed any data
    if (not stats['throughput'] and not stats['delay'] and not stats['loss'] and
        stats['nada_rx_bytes'] is None and stats['tcp_rx_bytes'] is None):
        print("Warning: No metrics were parsed from the output. Check the simulation output format.")
        print("First few lines of output:", "\n".join(lines[:10]))
    else:
        print(f"Parsed {len(stats['throughput'])} throughput values, "
              f"{len(stats['delay'])} delay values, "
              f"{len(stats['loss'])} loss values, "
              f"{len(stats['jitter'])} jitter values")

        # Print NADA and TCP specific stats if available
        if stats['nada_rx_bytes'] is not None:
            print(f"Parsed NADA stats: {stats['nada_rx_packets']} rx packets, "
                  f"{stats['nada_rx_bytes']} rx bytes, "
                  f"{stats['nada_loss'] if stats['nada_loss'] is not None else 'N/A'}% loss")

        if stats['tcp_rx_bytes'] is not None:
            print(f"Parsed TCP stats: {stats['tcp_rx_packets']} rx packets, "
                  f"{stats['tcp_rx_bytes']} rx bytes, "
                  f"{stats['tcp_loss'] if stats['tcp_loss'] is not None else 'N/A'}% loss")

    return stats

# Update the analyze_results function to use the new parsed metrics
def analyze_results(nada_stats, basic_stats):
    """Analyze and compare the results from NADA and basic simulations."""
    # Ensure we have data to analyze
    if not nada_stats or not basic_stats:
        print("Error: Missing data for analysis")
        return pd.DataFrame()

    # Calculate averages safely (avoid division by zero)
    # Use NADA specific metrics if available, otherwise fall back to general metrics
    if nada_stats['nada_throughput'] is not None:
        nada_throughput = nada_stats['nada_throughput']
    else:
        nada_throughput = sum(nada_stats['throughput']) / len(nada_stats['throughput']) if nada_stats['throughput'] else 0

    # For TCP metrics in NADA simulation
    if nada_stats['tcp_throughput'] is not None:
        nada_tcp_throughput = nada_stats['tcp_throughput']
    else:
        nada_tcp_throughput = 0

    # Use NADA specific loss if available
    if nada_stats['nada_loss'] is not None:
        nada_loss = nada_stats['nada_loss']
    else:
        nada_loss = sum(nada_stats['loss']) / len(nada_stats['loss']) if nada_stats['loss'] else 0

    # Use TCP specific loss if available
    if nada_stats['tcp_loss'] is not None:
        nada_tcp_loss = nada_stats['tcp_loss']
    else:
        nada_tcp_loss = 0

    # For basic stats, use general metrics as before
    basic_throughput = sum(basic_stats['throughput']) / len(basic_stats['throughput']) if basic_stats['throughput'] else 0

    # Get delay and jitter from general metrics
    nada_delay = sum(nada_stats['delay']) / len(nada_stats['delay']) if nada_stats['delay'] else 0
    nada_jitter = sum(nada_stats['jitter']) / len(nada_stats['jitter']) if nada_stats.get('jitter', []) else 0

    basic_delay = sum(basic_stats['delay']) / len(basic_stats['delay']) if basic_stats['delay'] else 0
    basic_loss = sum(basic_stats['loss']) / len(basic_stats['loss']) if basic_stats['loss'] else 0
    basic_jitter = sum(basic_stats['jitter']) / len(basic_stats['jitter']) if basic_stats.get('jitter', []) else 0

    # Calculate MOS (Mean Opinion Score) estimate based on network conditions
    nada_loss_factor = max(0, 1 - (nada_loss / 20))  # Loss above 20% makes video unusable
    nada_delay_factor = max(0, 1 - (nada_delay / 1))  # Delay above 1s makes video unusable
    nada_jitter_factor = max(0, 1 - (nada_jitter * 20))  # Jitter above 50ms makes video unusable
    nada_mos = 1 + 4 * nada_loss_factor * nada_delay_factor * nada_jitter_factor

    basic_loss_factor = max(0, 1 - (basic_loss / 20))
    basic_delay_factor = max(0, 1 - (basic_delay / 1))
    basic_jitter_factor = max(0, 1 - (basic_jitter * 20))
    basic_mos = 1 + 4 * basic_loss_factor * basic_delay_factor * basic_jitter_factor

    # Include TCP metrics in the comparison DataFrame
    comparison_df = pd.DataFrame({
        'Metric': [
            'Throughput (Mbps)',
            'TCP Throughput (Mbps)',
            'Delay (seconds)',
            'Loss (%)',
            'TCP Loss (%)',
            'Jitter (seconds)',
            'Estimated MOS (1-5)'
        ],
        'NADA': [
            nada_throughput,
            nada_tcp_throughput,
            nada_delay,
            nada_loss,
            nada_tcp_loss,
            nada_jitter,
            nada_mos
        ],
        'Basic': [
            basic_throughput,
            0,  # No separate TCP metric for basic
            basic_delay,
            basic_loss,
            0,  # No separate TCP metric for basic
            basic_jitter,
            basic_mos
        ]
    })

    # Add improvement percentage column
    comparison_df['Improvement (%)'] = [
        ((nada_throughput - basic_throughput) / basic_throughput * 100) if basic_throughput else float('nan'),
        float('nan'),  # No comparison for TCP throughput
        ((basic_delay - nada_delay) / basic_delay * 100) if basic_delay else float('nan'),  # Lower delay is better
        ((basic_loss - nada_loss) / basic_loss * 100) if basic_loss else float('nan'),  # Lower loss is better
        float('nan'),  # No comparison for TCP loss
        ((basic_jitter - nada_jitter) / basic_jitter * 100) if basic_jitter else float('nan'),  # Lower jitter is better
        ((nada_mos - basic_mos) / basic_mos * 100) if basic_mos else float('nan')  # Higher MOS is better
    ]

    print("Comparison Results:")
    print(comparison_df)
    return comparison_df

def generate_visualizations(comparison_df, scenario_name):
    """Generate visualizations from the comparison DataFrame."""
    if comparison_df.empty:
        print("No data to visualize")
        return

    # Create scenario folder
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Bar chart for comparison
    plt.figure(figsize=(12, 8))

    # First subplot for raw values (except MOS which has its own scale)
    plt.subplot(2, 1, 1)
    metrics_to_plot = comparison_df[comparison_df['Metric'] != 'Estimated MOS (1-5)']

    # Use the actual column names from the DataFrame ('NADA' and 'Basic') instead of 'TCP-NADA' and 'TCP-Basic'
    ax = metrics_to_plot.set_index('Metric')[['NADA', 'Basic']].plot(kind='bar', ax=plt.gca())
    plt.title(f'Comparison of TCP with NADA vs TCP with Basic WebRTC - {scenario_name} Scenario')
    plt.ylabel('Value')
    plt.xticks(rotation=30)
    plt.grid(axis='y')

    # Add value labels on top of bars
    for container in ax.containers:
        ax.bar_label(container, fmt='%.4f')

    # Second subplot just for MOS
    plt.subplot(2, 1, 2)
    mos_data = comparison_df[comparison_df['Metric'] == 'Estimated MOS (1-5)']
    # Use the actual column names from the DataFrame
    ax2 = mos_data.set_index('Metric')[['NADA', 'Basic']].plot(kind='bar', ax=plt.gca(), color=['#1f77b4', '#ff7f0e'])
    plt.title(f'Video Quality Estimation (MOS) - {scenario_name} Scenario')
    plt.ylabel('Estimated MOS (1-5)')
    plt.ylim(1, 5)  # MOS is on a 1-5 scale
    plt.grid(axis='y')

    # Add value labels on top of bars
    for container in ax2.containers:
        ax2.bar_label(container, fmt='%.2f')

    plt.tight_layout()
    plt.savefig(f"{scenario_folder}/comparison_{timestamp}.png")

    # Create a second plot for improvement percentage
    plt.figure(figsize=(10, 6))
    # Remove NaN values
    improvement_data = comparison_df.dropna(subset=['Improvement (%)'])

    ax3 = improvement_data.set_index('Metric')['Improvement (%)'].plot(kind='bar', color='green')
    plt.title(f'Performance Improvement of TCP-NADA over TCP-Basic WebRTC (%) - {scenario_name} Scenario')
    plt.ylabel('Improvement (%)')
    plt.xticks(rotation=30)
    plt.grid(axis='y')
    plt.axhline(y=0, color='r', linestyle='-')  # Add a line at 0% for reference

    # Add value labels on top of bars
    ax3.bar_label(ax3.containers[0], fmt='%.1f%%')

    plt.tight_layout()
    plt.savefig(f"{scenario_folder}/improvement_{timestamp}.png")

    plt.close('all')  # Close all figures to free memory

# Function to generate summary visualizations across all scenarios
def generate_summary_visualizations(all_results):
    """Generate summary visualizations comparing all scenarios."""
    # Create summary folder
    summary_folder = os.path.join(OUTPUT_DIR, "summary")
    os.makedirs(summary_folder, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Create a dataframe with all scenarios
    summary_data = []
    for scenario_name, comparison_df in all_results.items():
        for _, row in comparison_df.iterrows():
            metric = row['Metric']
            nada_value = row['NADA']  # Use 'NADA' instead of 'TCP-NADA'
            basic_value = row['Basic']  # Use 'Basic' instead of 'TCP-Basic'
            improvement = row['Improvement (%)']

            summary_data.append({
                'Scenario': scenario_name,
                'Metric': metric,
                'TCP-NADA': nada_value,  # Keep output column names as desired
                'TCP-Basic': basic_value,
                'Improvement (%)': improvement
            })

    summary_df = pd.DataFrame(summary_data)

    # Save summary data
    summary_df.to_csv(f"{summary_folder}/all_metrics_summary_{timestamp}.csv", index=False)

    # Create a heatmap of improvements across all scenarios and metrics
    plt.figure(figsize=(14, 10))
    pivot_df = summary_df.pivot(index='Metric', columns='Scenario', values='Improvement (%)')

    # Use a diverging colormap centered at 0
    cmap = sns.diverging_palette(240, 10, as_cmap=True)

    # Set a mask for NaN values
    mask = np.isnan(pivot_df.values)

    # Create the heatmap
    sns.heatmap(pivot_df, annot=True, fmt=".1f", cmap=cmap, center=0,
                linewidths=.5, cbar_kws={"label": "Improvement %"}, mask=mask)

    plt.title('TCP-NADA Improvement (%) Over TCP-Basic WebRTC Across Different Scenarios')
    plt.tight_layout()
    plt.savefig(f"{summary_folder}/improvement_heatmap_{timestamp}.png")
    plt.close()

    # Create a grouped bar chart comparing NADA vs Basic across scenarios for each key metric
    for metric in ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Estimated MOS (1-5)']:
        metric_data = summary_df[summary_df['Metric'] == metric]

        fig, ax = plt.subplots(figsize=(14, 8))

        # Calculate the positions of the bars
        scenarios = metric_data['Scenario'].unique()
        x = np.arange(len(scenarios))
        width = 0.35

        # Create the grouped bars
        rects1 = ax.bar(x - width/2, metric_data['TCP-NADA'].values, width, label='TCP-NADA')
        rects2 = ax.bar(x + width/2, metric_data['TCP-Basic'].values, width, label='TCP-Basic WebRTC')

        # Add labels and title
        ax.set_ylabel(metric)
        ax.set_title(f'Comparison of {metric} Across Different Scenarios')
        ax.set_xticks(x)
        ax.set_xticklabels(scenarios, rotation=45, ha='right')
        ax.legend()

        # Add value labels on each bar
        def autolabel(rects):
            for rect in rects:
                height = rect.get_height()
                ax.annotate(f'{height:.2f}',
                            xy=(rect.get_x() + rect.get_width() / 2, height),
                            xytext=(0, 3),  # 3 points vertical offset
                            textcoords="offset points",
                            ha='center', va='bottom')

        autolabel(rects1)
        autolabel(rects2)

        # Adjust layout and save
        fig.tight_layout()
        metric_name = metric.split('(')[0].strip().lower().replace(' ', '_')
        plt.savefig(f"{summary_folder}/{metric_name}_comparison_{timestamp}.png")
        plt.close(fig)

    # Create additional summary visualizations
    # Average improvement across all metrics by scenario
    plt.figure(figsize=(12, 6))
    avg_improvement = summary_df.groupby('Scenario')['Improvement (%)'].mean().sort_values()

    bars = avg_improvement.plot(kind='barh', color='green')
    plt.axvline(x=0, color='r', linestyle='-')
    plt.title('Average Improvement (%) by Scenario')
    plt.xlabel('Average Improvement (%)')
    plt.tight_layout()

    # Add value labels
    for i, v in enumerate(avg_improvement):
        if not np.isnan(v):
            plt.text(v + (1 if v >= 0 else -1), i, f"{v:.1f}%", va='center')

    plt.savefig(f"{summary_folder}/average_improvement_by_scenario_{timestamp}.png")
    plt.close()

# Function to save raw simulation data
def save_raw_data(scenario_name, tcp_nada_output, tcp_basic_output):
    """Save raw simulation output for reference"""
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if tcp_nada_output:
        with open(f"{scenario_folder}/tcp_nada_raw_output_{timestamp}.txt", 'w') as f:
            f.write(tcp_nada_output)

    if tcp_basic_output:
        with open(f"{scenario_folder}/tcp_basic_raw_output_{timestamp}.txt", 'w') as f:
            f.write(tcp_basic_output)


def main():
    print("Starting comparison of TCP-NADA and TCP-Basic WebRTC simulations across multiple scenarios")

    # Create main output directory
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    all_results = {}

    for scenario in SIMULATION_SCENARIOS:
        scenario_name = scenario["name"]
        params = scenario["params"]

        print(f"\n========== Running {scenario_name} Scenario ==========")
        print(f"Parameters: {params}")

        # Create scenario-specific folder
        scenario_folder = get_scenario_folder(scenario_name)

        # Run TCP-NADA simulation
        print("\nRunning TCP-NADA simulation...")
        tcp_nada_output = run_simulation("scratch/tcp-simple-nada-webrtc", params)

        # Run TCP-Basic WebRTC simulation
        print("\nRunning TCP-Basic WebRTC simulation...")
        tcp_basic_output = run_simulation("scratch/tcp-simple-webrtc", params)

        # Save raw output
        save_raw_data(scenario_name, tcp_nada_output, tcp_basic_output)

        # Parse outputs
        tcp_nada_stats = parse_output(tcp_nada_output)
        tcp_basic_stats = parse_output(tcp_basic_output)

        # Analyze results
        comparison_df = analyze_results(tcp_nada_stats, tcp_basic_stats)

        if not comparison_df.empty:
            # Store results for this scenario
            all_results[scenario_name] = comparison_df

            # Save comparison results to CSV
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            comparison_csv = f"{scenario_folder}/comparison_results_{timestamp}.csv"
            comparison_df.to_csv(comparison_csv, index=False)
            print(f"Comparison results saved to {comparison_csv}")

            # Generate visualizations for this scenario
            generate_visualizations(comparison_df, scenario_name)
        else:
            print(f"Analysis failed for {scenario_name} scenario - no comparison data generated")

    # Generate summary visualizations across all scenarios
    if all_results:
        print("\nGenerating summary visualizations across all scenarios...")
        generate_summary_visualizations(all_results)

        # Save all results to a single CSV file in the summary folder
        summary_folder = os.path.join(OUTPUT_DIR, "summary")
        os.makedirs(summary_folder, exist_ok=True)

        all_scenarios_df = pd.concat([df.assign(Scenario=name) for name, df in all_results.items()])
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        all_scenarios_csv = f"{summary_folder}/all_scenarios_comparison_{timestamp}.csv"
        all_scenarios_df.to_csv(all_scenarios_csv, index=False)
        print(f"All scenarios comparison saved to {all_scenarios_csv}")

    print("\nComparison analysis completed - Results organized by scenario in subfolders")
    print(f"Summary visualizations in: {OUTPUT_DIR}/summary/")

if __name__ == "__main__":
    main()
