import os
import subprocess
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
from datetime import datetime
import re
import sys

# Create output directory
script_dir = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(script_dir, "../results/competing-comparison")
print(f"Using output directory: {OUTPUT_DIR}")

# Create output directory immediately
try:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    test_file = os.path.join(OUTPUT_DIR, "directory_test.txt")
    with open(test_file, 'w') as f:
        f.write("Output directory is writable\n")
    os.remove(test_file)  # Clean up test file
    print(f"✅ Successfully verified output directory is writable")
except Exception as e:
    print(f"❌ ERROR: Could not write to output directory: {e}")
    sys.exit(1)

# Define scenarios to simulate competing sources
COMPETING_SCENARIOS = [
    {
        "name": "No Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 0,
            "competingSourcesB": 0,
            "competingIntensityA": 0.0,
            "competingIntensityB": 0.0,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Light Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 1,
            "competingSourcesB": 1,
            "competingIntensityA": 0.2,
            "competingIntensityB": 0.2,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Balanced Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Heavy Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 4,
            "competingSourcesB": 4,
            "competingIntensityA": 0.8,
            "competingIntensityB": 0.8,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Asymmetric Competition - Path A Heavy",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 3,
            "competingSourcesB": 1,
            "competingIntensityA": 0.7,
            "competingIntensityB": 0.2,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Asymmetric Competition - Path B Heavy",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 1,
            "competingSourcesB": 3,
            "competingIntensityA": 0.2,
            "competingIntensityB": 0.7,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Different Bandwidth Paths with Competition",
        "params": {
            "dataRate1": "15Mbps",
            "dataRate2": "5Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Different Latency Paths with Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 10,
            "delayMs2": 100,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30
        }
    },
    {
        "name": "Video Conferencing with Competition",
        "params": {
            "dataRate1": "5Mbps",
            "dataRate2": "5Mbps",
            "delayMs1": 20,
            "delayMs2": 30,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.6,
            "competingIntensityB": 0.6,
            "simulationTime": 60,
            "frameRate": 30,
            "packetSize": 800
        }
    },
    {
        "name": "Bursty Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 3,
            "competingSourcesB": 3,
            "competingIntensityA": 0.9,
            "competingIntensityB": 0.9,
            "simulationTime": 60,
            "frameRate": 30,
            "burstMode": 1  # Enable bursty traffic (will need to add this feature)
        }
    }
]

# Define scenarios to compare path selection strategies under competing traffic
PATH_SELECTION_SCENARIOS = [
    {
        "name": "Weighted Strategy Under Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30,
            "pathSelection": 0  # Weighted strategy
        }
    },
    {
        "name": "Best Path Strategy Under Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30,
            "pathSelection": 1  # Best path strategy
        }
    },
    {
        "name": "Equal Strategy Under Competition",
        "params": {
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs1": 20,
            "delayMs2": 40,
            "competingSourcesA": 2,
            "competingSourcesB": 2,
            "competingIntensityA": 0.5,
            "competingIntensityB": 0.5,
            "simulationTime": 60,
            "frameRate": 30,
            "pathSelection": 2  # Equal strategy
        }
    }
]

def get_scenario_folder(scenario_name):
    """Create and return path to scenario-specific folder"""
    folder_name = scenario_name.lower().replace(' ', '_')
    folder_path = os.path.join(OUTPUT_DIR, folder_name)
    os.makedirs(folder_path, exist_ok=True)
    return folder_path

def run_simulation(script_name, params=None):
    """Run the specified simulation script with parameters and return the output."""
    cmd = ["./ns3", "run", script_name]
    if params:
        cmd.append("--")
        for key, value in params.items():
            cmd.append(f"--{key}={value}")

    print(f"Running simulation: {' '.join(cmd)}")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)

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

def parse_output(output):
    """Parse the simulation output and extract relevant statistics."""
    if not output:
        return None

    # Use regular expressions for parsing
    throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
    delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
    loss_re = re.compile(r"Packet loss: ([0-9.]+)%")
    jitter_re = re.compile(r"Mean jitter: ([0-9.e-]+) seconds")

    # Path-specific stats
    path_re = re.compile(r"Path (\d+):")
    path_rate_re = re.compile(r"Rate: ([0-9.]+) Mbps")
    path_rtt_re = re.compile(r"RTT: ([0-9.]+) ms")
    path_packets_sent_re = re.compile(r"Packets sent: ([0-9]+)")
    path_packets_acked_re = re.compile(r"Packets acked: ([0-9]+)")

    # WebRTC frame stats
    frame_stats_re = re.compile(r"WebRTC Frame Statistics:")
    key_frames_sent_re = re.compile(r"Key frames sent: ([0-9]+)")
    key_frames_acked_re = re.compile(r"Key frames acked: ([0-9]+)")
    key_frame_loss_re = re.compile(r"Key frame loss: ([0-9.]+)%")
    delta_frames_sent_re = re.compile(r"Delta frames sent: ([0-9]+)")
    delta_frames_acked_re = re.compile(r"Delta frames acked: ([0-9]+)")
    delta_frame_loss_re = re.compile(r"Delta frame loss: ([0-9.]+)%")

    lines = output.split('\n')
    stats = {
        'throughput': [],
        'delay': [],
        'loss': [],
        'jitter': [],
        'paths': {},
        'webrtc': {
            'key_frames_sent': 0,
            'key_frames_acked': 0,
            'key_frame_loss': 0,
            'delta_frames_sent': 0,
            'delta_frames_acked': 0,
            'delta_frame_loss': 0
        }
    }

    current_path = None
    in_frame_stats = False

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

        # Extract loss
        loss_match = loss_re.search(line)
        if loss_match:
            stats['loss'].append(float(loss_match.group(1)))
            continue

        # Extract jitter
        jitter_match = jitter_re.search(line)
        if jitter_match:
            stats['jitter'].append(float(jitter_match.group(1)))
            continue

        # Check for frame stats section
        if frame_stats_re.search(line):
            in_frame_stats = True
            continue

        if in_frame_stats:
            # Key frames sent
            key_sent_match = key_frames_sent_re.search(line)
            if key_sent_match:
                stats['webrtc']['key_frames_sent'] = int(key_sent_match.group(1))
                continue

            # Key frames acked
            key_acked_match = key_frames_acked_re.search(line)
            if key_acked_match:
                stats['webrtc']['key_frames_acked'] = int(key_acked_match.group(1))
                continue

            # Key frame loss
            key_loss_match = key_frame_loss_re.search(line)
            if key_loss_match:
                stats['webrtc']['key_frame_loss'] = float(key_loss_match.group(1))
                continue

            # Delta frames sent
            delta_sent_match = delta_frames_sent_re.search(line)
            if delta_sent_match:
                stats['webrtc']['delta_frames_sent'] = int(delta_sent_match.group(1))
                continue

            # Delta frames acked
            delta_acked_match = delta_frames_acked_re.search(line)
            if delta_acked_match:
                stats['webrtc']['delta_frames_acked'] = int(delta_acked_match.group(1))
                continue

            # Delta frame loss
            delta_loss_match = delta_frame_loss_re.search(line)
            if delta_loss_match:
                stats['webrtc']['delta_frame_loss'] = float(delta_loss_match.group(1))
                continue

        # Check for path information
        path_match = path_re.search(line)
        if path_match:
            current_path = int(path_match.group(1))
            if current_path not in stats['paths']:
                stats['paths'][current_path] = {'rate': [], 'rtt': [], 'sent': 0, 'acked': 0}
            continue

        if current_path is not None:
            # Extract path rate
            path_rate_match = path_rate_re.search(line)
            if path_rate_match:
                stats['paths'][current_path]['rate'].append(float(path_rate_match.group(1)))
                continue

            # Extract path RTT
            path_rtt_match = path_rtt_re.search(line)
            if path_rtt_match:
                stats['paths'][current_path]['rtt'].append(float(path_rtt_match.group(1)))
                continue

            # Extract packets sent
            packets_sent_match = path_packets_sent_re.search(line)
            if packets_sent_match:
                stats['paths'][current_path]['sent'] = int(packets_sent_match.group(1))
                continue

            # Extract packets acked
            packets_acked_match = path_packets_acked_re.search(line)
            if packets_acked_match:
                stats['paths'][current_path]['acked'] = int(packets_acked_match.group(1))
                continue

    # Print parsing summary
    print(f"Parsed {len(stats['throughput'])} throughput values, "
          f"{len(stats['delay'])} delay values, "
          f"{len(stats['loss'])} loss values, and "
          f"{len(stats['jitter'])} jitter values")
    if stats['paths']:
        print(f"Found data for {len(stats['paths'])} paths")
    if stats['webrtc']['key_frames_sent'] > 0:
        print("Found WebRTC frame statistics")

    return stats

def analyze_results(multipath_stats):
    """Analyze the results from the simulation."""
    if not multipath_stats:
        print("Error: Missing data for analysis")
        return pd.DataFrame(), pd.DataFrame()

    # Calculate averages safely
    mp_throughput = np.mean(multipath_stats['throughput']) if multipath_stats['throughput'] else 0
    mp_delay = np.mean(multipath_stats['delay']) if multipath_stats['delay'] else 0
    mp_loss = np.mean(multipath_stats['loss']) if multipath_stats['loss'] else 0
    mp_jitter = np.mean(multipath_stats['jitter']) if multipath_stats.get('jitter', []) else 0

    # Calculate estimated video quality score (MOS)
    mp_loss_factor = max(0, 1 - (mp_loss / 20))
    mp_delay_factor = max(0, 1 - (mp_delay / 1))
    mp_jitter_factor = max(0, 1 - (mp_jitter * 20))
    mp_mos = 1 + 4 * mp_loss_factor * mp_delay_factor * mp_jitter_factor

    # Calculate path utilization
    path_utilization = {}
    total_packets_sent = 0
    total_packets_acked = 0

    for path_id, path_stats in multipath_stats.get('paths', {}).items():
        total_packets_sent += path_stats['sent']
        total_packets_acked += path_stats['acked']

        if path_stats['sent'] > 0:
            path_utilization[f"Path {path_id}"] = (path_stats['acked'] / path_stats['sent']) * 100
        else:
            path_utilization[f"Path {path_id}"] = 0

    # Calculate overall packet delivery ratio
    overall_pdr = (total_packets_acked / total_packets_sent) * 100 if total_packets_sent > 0 else 0

    # Create results dataframe
    results_df = pd.DataFrame({
        'Metric': [
            'Throughput (Mbps)',
            'Delay (seconds)',
            'Loss (%)',
            'Jitter (seconds)',
            'Estimated MOS (1-5)',
            'Overall Packet Delivery Ratio (%)'
        ],
        'Value': [
            mp_throughput,
            mp_delay,
            mp_loss,
            mp_jitter,
            mp_mos,
            overall_pdr
        ]
    })

    # Calculate path-specific metrics
    path_data = []
    for path_id, path_stats in multipath_stats.get('paths', {}).items():
        avg_rate = np.mean(path_stats['rate']) if path_stats['rate'] else 0
        avg_rtt = np.mean(path_stats['rtt']) if path_stats['rtt'] else 0
        pdr = (path_stats['acked'] / path_stats['sent']) * 100 if path_stats['sent'] > 0 else 0
        utilization = path_stats['sent'] / total_packets_sent * 100 if total_packets_sent > 0 else 0

        path_data.append({
            'Path': f"Path {path_id}",
            'Rate (Mbps)': avg_rate,
            'RTT (ms)': avg_rtt,
            'Packet Delivery Ratio (%)': pdr,
            'Path Utilization (%)': utilization,
            'Packets Sent': path_stats['sent'],
            'Packets Acked': path_stats['acked']
        })

    path_df = pd.DataFrame(path_data) if path_data else pd.DataFrame()

    # Print results
    print("\nAnalysis Results:")
    print(results_df)
    if not path_df.empty:
        print("\nPath Statistics:")
        print(path_df)

    return results_df, path_df

def generate_visualizations(results_df, path_df, scenario_name):
    """Generate visualizations for simulation results."""
    if results_df.empty:
        print("No data to visualize")
        return

    # Create scenario folder
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Create a bar chart for main metrics
    plt.figure(figsize=(10, 6))
    metrics_to_plot = results_df[results_df['Metric'].isin(['Throughput (Mbps)', 'Loss (%)', 'Estimated MOS (1-5)', 'Overall Packet Delivery Ratio (%)'])]

    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
    bars = plt.bar(metrics_to_plot['Metric'], metrics_to_plot['Value'], color=colors)

    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                f'{height:.2f}', ha='center', va='bottom')

    plt.title(f'Performance Metrics - {scenario_name}')
    plt.ylabel('Value')
    plt.xticks(rotation=15)
    plt.tight_layout()

    metrics_plot = f"{scenario_folder}/metrics_{timestamp}.png"
    plt.savefig(metrics_plot)
    plt.close()
    print(f"Saved metrics plot to {metrics_plot}")

    # Create a bar chart for path utilization if data exists
    if not path_df.empty:
        plt.figure(figsize=(10, 6))

        # Plot path utilization
        plt.subplot(2, 1, 1)
        sns.barplot(x='Path', y='Path Utilization (%)', data=path_df, palette='viridis')
        plt.title(f'Path Utilization - {scenario_name}')
        plt.ylim(0, 100)

        # Plot packet delivery ratio
        plt.subplot(2, 1, 2)
        sns.barplot(x='Path', y='Packet Delivery Ratio (%)', data=path_df, palette='viridis')
        plt.title('Path Packet Delivery Ratio')
        plt.ylim(0, 100)

        plt.tight_layout()
        path_plot = f"{scenario_folder}/path_stats_{timestamp}.png"
        plt.savefig(path_plot)
        plt.close()
        print(f"Saved path statistics plot to {path_plot}")

        # Create a bubble chart showing relationship between RTT, Rate, and Utilization
        plt.figure(figsize=(10, 8))

        # Size bubbles by number of packets sent
        sizes = path_df['Packets Sent'] / path_df['Packets Sent'].max() * 500

        scatter = plt.scatter(
            path_df['RTT (ms)'],
            path_df['Rate (Mbps)'],
            s=sizes,
            c=path_df['Path Utilization (%)'],
            cmap='viridis',
            alpha=0.7
        )

        # Add path labels
        for i, row in path_df.iterrows():
            plt.annotate(
                row['Path'],
                (row['RTT (ms)'], row['Rate (Mbps)']),
                xytext=(5, 5),
                textcoords='offset points'
            )

        plt.colorbar(scatter, label='Path Utilization (%)')
        plt.title(f'Path Performance Comparison - {scenario_name}')
        plt.xlabel('RTT (ms)')
        plt.ylabel('Rate (Mbps)')
        plt.tight_layout()

        bubble_plot = f"{scenario_folder}/path_bubble_{timestamp}.png"
        plt.savefig(bubble_plot)
        plt.close()
        print(f"Saved path bubble chart to {bubble_plot}")

def save_raw_data(scenario_name, simulation_output):
    """Save raw simulation output for reference."""
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if simulation_output:
        with open(f"{scenario_folder}/simulation_raw_output_{timestamp}.txt", 'w') as f:
            f.write(simulation_output)

def generate_summary_visualizations(all_results):
    """Generate summary visualizations comparing all scenarios."""
    # Create summary folder
    summary_folder = os.path.join(OUTPUT_DIR, "summary")
    os.makedirs(summary_folder, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Extract data for comparison
    scenarios = []
    throughputs = []
    delays = []
    losses = []
    mosses = []
    pdrs = []

    for scenario_name, (results_df, _) in all_results.items():
        if not results_df.empty:
            scenarios.append(scenario_name)

            # Extract metrics
            throughput = results_df.loc[results_df['Metric'] == 'Throughput (Mbps)', 'Value'].values[0]
            delay = results_df.loc[results_df['Metric'] == 'Delay (seconds)', 'Value'].values[0]
            loss = results_df.loc[results_df['Metric'] == 'Loss (%)', 'Value'].values[0]
            mos = results_df.loc[results_df['Metric'] == 'Estimated MOS (1-5)', 'Value'].values[0]

            try:
                pdr = results_df.loc[results_df['Metric'] == 'Overall Packet Delivery Ratio (%)', 'Value'].values[0]
            except:
                pdr = 100 - loss  # Fallback calculation

            throughputs.append(throughput)
            delays.append(delay)
            losses.append(loss)
            mosses.append(mos)
            pdrs.append(pdr)

    # Create comparison DataFrame
    comparison_df = pd.DataFrame({
        'Scenario': scenarios,
        'Throughput (Mbps)': throughputs,
        'Delay (seconds)': delays,
        'Loss (%)': losses,
        'Estimated MOS (1-5)': mosses,
        'Packet Delivery Ratio (%)': pdrs
    })

    # Save comparison data
    comparison_df.to_csv(f"{summary_folder}/scenarios_comparison_{timestamp}.csv", index=False)

    # Create comparison visualizations
    if len(scenarios) > 1:  # Only create comparisons if we have multiple scenarios
        # Throughput comparison
        plt.figure(figsize=(12, 6))
        bars = plt.bar(scenarios, throughputs, color='skyblue')
        plt.title('Throughput Comparison Across Scenarios')
        plt.ylabel('Throughput (Mbps)')
        plt.xticks(rotation=45, ha='right')

        # Add value labels
        for bar in bars:
            height = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                    f'{height:.2f}', ha='center', va='bottom')

        plt.tight_layout()
        plt.savefig(f"{summary_folder}/throughput_comparison_{timestamp}.png")
        plt.close()

        # Loss comparison
        plt.figure(figsize=(12, 6))
        bars = plt.bar(scenarios, losses, color='salmon')
        plt.title('Packet Loss Comparison Across Scenarios')
        plt.ylabel('Loss (%)')
        plt.xticks(rotation=45, ha='right')

        # Add value labels
        for bar in bars:
            height = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                    f'{height:.2f}', ha='center', va='bottom')

        plt.tight_layout()
        plt.savefig(f"{summary_folder}/loss_comparison_{timestamp}.png")
        plt.close()

        # MOS comparison
        plt.figure(figsize=(12, 6))
        bars = plt.bar(scenarios, mosses, color='lightgreen')
        plt.title('Estimated Video Quality (MOS) Comparison Across Scenarios')
        plt.ylabel('Estimated MOS (1-5)')
        plt.xticks(rotation=45, ha='right')
        plt.ylim(1, 5)  # MOS is on a 1-5 scale

        # Add value labels
        for bar in bars:
            height = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                    f'{height:.2f}', ha='center', va='bottom')

        plt.tight_layout()
        plt.savefig(f"{summary_folder}/mos_comparison_{timestamp}.png")
        plt.close()

        # Create a heatmap comparing all metrics across all scenarios
        plt.figure(figsize=(14, 10))

        # Prepare data for heatmap
        heatmap_data = comparison_df.set_index('Scenario')

        # Normalize values for better visualization
        normalized_data = pd.DataFrame(index=heatmap_data.index)

        for column in heatmap_data.columns:
            if column in ['Throughput (Mbps)', 'Estimated MOS (1-5)', 'Packet Delivery Ratio (%)']:
                # For these metrics, higher is better
                normalized_data[column] = (heatmap_data[column] - heatmap_data[column].min()) / \
                                          (heatmap_data[column].max() - heatmap_data[column].min())
            else:
                # For delay and loss, lower is better, so invert the normalization
                normalized_data[column] = 1 - (heatmap_data[column] - heatmap_data[column].min()) / \
                                             (heatmap_data[column].max() - heatmap_data[column].min())

        # Plot heatmap
        sns.heatmap(normalized_data, annot=heatmap_data, fmt='.2f', cmap='RdYlGn',
                    linewidths=.5, cbar_kws={'label': 'Normalized Score (Higher is Better)'})

        plt.title('Performance Metrics Across Scenarios')
        plt.tight_layout()
        plt.savefig(f"{summary_folder}/metrics_heatmap_{timestamp}.png")
        plt.close()

def main():
    print("=" * 80)
    print(f"Starting MP-NADA-Competing simulation scenarios")
    print("=" * 80)

    # Combine all scenarios
    all_scenarios = COMPETING_SCENARIOS + PATH_SELECTION_SCENARIOS

    # Store all results for summary visualization
    all_results = {}

    # Run all scenarios
    for scenario in all_scenarios:
        scenario_name = scenario["name"]
        print("\n" + "=" * 50)
        print(f"Processing scenario: {scenario_name}")
        print("=" * 50)

        # Run mp-nada-competing simulation
        simulation_output = run_simulation("mp-nada-competing", scenario["params"])

        # Save raw output
        save_raw_data(scenario_name, simulation_output)

        # Parse output
        if simulation_output:
            stats = parse_output(simulation_output)

            # Analyze results
            results_df, path_df = analyze_results(stats)

            # Store results for summary
            all_results[scenario_name] = (results_df, path_df)

            # Generate scenario-specific visualizations
            generate_visualizations(results_df, path_df, scenario_name)

            print(f"✅ Completed processing scenario: {scenario_name}")
        else:
            print(f"❌ Failed to run simulation for scenario: {scenario_name}")

    # Generate summary visualizations across all scenarios
    if all_results:
        print("\n" + "=" * 50)
        print("Generating summary visualizations")
        print("=" * 50)
        generate_summary_visualizations(all_results)

        print("\n✅ All scenarios processed successfully")
    else:
        print("\n❌ No successful simulations to summarize")

    # Create a unique timestamp for this run
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Save a summary CSV with all results
    summary_path = os.path.join(OUTPUT_DIR, f"all_scenarios_summary_{timestamp}.csv")
    try:
        # Create a combined DataFrame from all results
        all_data = []
        for scenario_name, (results_df, _) in all_results.items():
            if not results_df.empty:
                for _, row in results_df.iterrows():
                    all_data.append({
                        'Scenario': scenario_name,
                        'Metric': row['Metric'],
                        'Value': row['Value']
                    })

        if all_data:
            combined_df = pd.DataFrame(all_data)
            combined_df.to_csv(summary_path, index=False)
            print(f"✅ Saved combined results to: {summary_path}")
    except Exception as e:
        print(f"❌ Error saving summary CSV: {e}")

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Run and analyze MP-NADA-Competing simulations')
    parser.add_argument('--debug', action='store_true', help='Enable debug output')
    args = parser.parse_args()

    print("\n" + "="*50)
    print("NS-3 MP-NADA-COMPETING SIMULATION TOOL")
    print("="*50)

    # Run main with error handling
    try:
        main()
    except KeyboardInterrupt:
        print("\n Simulation interrupted by user")
    except Exception as e:
        import traceback
        print(f"\n❌ ERROR in execution: {e}")
        traceback.print_exc()
        sys.exit(1)
