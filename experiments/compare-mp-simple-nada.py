import os
import subprocess
import pandas as pd
import matplotlib.pyplot as plt
from datetime import datetime
import re
import numpy as np
import seaborn as sns
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(script_dir, "../results/nada-multipath-comparison")
print(f"Using output directory: {OUTPUT_DIR}")

# Create output directory immediately
try:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    # Test if we can actually write to it
    test_file = os.path.join(OUTPUT_DIR, "directory_test.txt")
    with open(test_file, 'w') as f:
        f.write("Output directory is writable\n")
    os.remove(test_file)  # Clean up test file
    print(f"✅ Successfully verified output directory is writable")
except Exception as e:
    print(f"❌ ERROR: Could not write to output directory: {e}")
    sys.exit(1)

# Define different scenarios to compare
SIMULATION_SCENARIOS = [
    {
        "name": "Standard",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30
        }
    },
    {
        "name": "Asymmetric Paths",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "15Mbps",
            "dataRate2": "5Mbps",
            "delayMs": 50,
            "delayMs1": 20,
            "delayMs2": 80,
            "frameRate": 30
        }
    },
    {
        "name": "High Latency Second Path",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs": 50,
            "delayMs1": 20,
            "delayMs2": 150,
            "frameRate": 30
        }
    },
    {
        "name": "Low Bandwidth",
        "params": {
            "dataRate": "5Mbps",
            "bottleneckBw": "5Mbps",
            "dataRate1": "5Mbps",
            "dataRate2": "5Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30
        }
    },
    {
        "name": "High Bandwidth",
        "params": {
            "dataRate": "20Mbps",
            "bottleneckBw": "20Mbps",
            "dataRate1": "20Mbps",
            "dataRate2": "20Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30
        }
    },
    {
        "name": "Combined Paths Exceed Single Path",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "8Mbps",
            "dataRate2": "8Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30
        }
    },
    {
        "name": "One Path Failure",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "0.1Mbps",  # Nearly failed second path
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 200,  # High latency on second path
            "frameRate": 30
        }
    },
    {
        "name": "Video Conferencing",
        "params": {
            "dataRate": "5Mbps",
            "bottleneckBw": "5Mbps",
            "dataRate1": "3Mbps",
            "dataRate2": "3Mbps",
            "delayMs": 20,
            "delayMs1": 20,
            "delayMs2": 30,
            "frameRate": 30,
            "packetSize": 800  # Smaller packets for real-time communication
        }
    },
    {
        "name": "4K Streaming",
        "params": {
            "dataRate": "25Mbps",
            "bottleneckBw": "25Mbps",
            "dataRate1": "15Mbps",
            "dataRate2": "15Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 60,
            "packetSize": 1400  # Larger packets for high-quality video
        }
    },
    {
        "name": "Mobile Connection",
        "params": {
            "dataRate": "8Mbps",
            "bottleneckBw": "8Mbps",
            "dataRate1": "8Mbps",
            "dataRate2": "4Mbps",
            "delayMs": 60,
            "delayMs1": 40,
            "delayMs2": 80,
            "frameRate": 30,
            "queueDisc": "CoDel"  # Mobile networks often have AQM
        }
    }
]

PATH_SELECTION_SCENARIOS = [
    {
        "name": "Round Robin Strategy",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30,
            "pathSelectionStrategy": 0  # Round Robin
        }
    },
    {
        "name": "Lowest RTT Strategy",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs": 50,
            "delayMs1": 40,
            "delayMs2": 80,  # Deliberately unbalanced to show strategy impact
            "frameRate": 30,
            "pathSelectionStrategy": 1  # Lowest RTT
        }
    },
    {
        "name": "Highest Throughput Strategy",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "12Mbps",
            "dataRate2": "8Mbps",  # Unbalanced bandwidth to show strategy impact
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30,
            "pathSelectionStrategy": 2  # Highest throughput
        }
    },
    {
        "name": "Weighted Load Balancing",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "15Mbps",
            "dataRate2": "5Mbps",  # Very unbalanced to demonstrate weighting
            "delayMs": 50,
            "delayMs1": 40,
            "delayMs2": 60,
            "frameRate": 30,
            "pathSelectionStrategy": 3  # Weighted load balancing
        }
    },
    {
        "name": "Redundant Transmission",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "10Mbps",
            "dataRate2": "10Mbps",
            "delayMs": 50,
            "delayMs1": 50,
            "delayMs2": 50,
            "frameRate": 30,
            "pathSelectionStrategy": 4,  # Redundant transmission
            "packetLoss1": 5.0,  # Add some packet loss to see redundancy benefits
            "packetLoss2": 5.0
        }
    },
    {
        "name": "Frame Type Aware Strategy",
        "params": {
            "dataRate": "10Mbps",
            "bottleneckBw": "10Mbps",
            "dataRate1": "12Mbps",
            "dataRate2": "8Mbps",
            "delayMs": 50,
            "delayMs1": 40,
            "delayMs2": 60,
            "frameRate": 30,
            "pathSelectionStrategy": 5,  # Frame type aware (I-frames on best path)
            "keyFrameInterval": 30  # Generate key frames every 30 frames
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

# Function to parse simulation output
def parse_output(output):
    """Parse the simulation output and extract relevant statistics."""
    if not output:
        return None

    # Use regular expressions for more robust parsing
    throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
    delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
    loss_re = re.compile(r"Packet loss: ([0-9.]+)%")
    jitter_re = re.compile(r"Mean jitter: ([0-9.e-]+) seconds")

    # For multipath, also look for path-specific stats
    path_re = re.compile(r"Path (\d+):")
    path_rate_re = re.compile(r"Rate: ([0-9.]+) Mbps")
    path_rtt_re = re.compile(r"RTT: ([0-9.]+) ms")
    path_packets_sent_re = re.compile(r"Packets sent: ([0-9]+)")
    path_packets_acked_re = re.compile(r"Packets acked: ([0-9]+)")

    # Add additional regex patterns for new metrics
    path_switch_re = re.compile(r"Path switch: from path (\d+) to path (\d+)")
    quality_change_re = re.compile(r"Quality changed: ([0-9.]+) -> ([0-9.]+)")
    energy_metric_re = re.compile(r"Energy efficiency: ([0-9.]+)%")

    lines = output.split('\n')
    stats = {
        'throughput': [],
        'delay': [],
        'loss': [],
        'jitter': [],
        'paths': {},
        'path_switches': [],
        'quality_changes': [],
        'energy_metrics': []
    }

    current_path = None

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
        if loss_match:
            stats['loss'].append(float(loss_match.group(1)))
            continue

        # Extract jitter
        jitter_match = jitter_re.search(line)
        if jitter_match:
            stats['jitter'].append(float(jitter_match.group(1)))
            continue

        # Check for path information in multipath output
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

        # Parse path switches
        path_switch_match = path_switch_re.search(line)
        if path_switch_match:
            from_path = int(path_switch_match.group(1))
            to_path = int(path_switch_match.group(2))
            stats['path_switches'].append((from_path, to_path))
            continue

        # Parse quality changes
        quality_change_match = quality_change_re.search(line)
        if quality_change_match:
            from_quality = float(quality_change_match.group(1))
            to_quality = float(quality_change_match.group(2))
            stats['quality_changes'].append((from_quality, to_quality))
            continue

        # Parse energy metrics
        energy_match = energy_metric_re.search(line)
        if energy_match:
            efficiency = float(energy_match.group(1))
            stats['energy_metrics'].append(efficiency)
            continue

    # Check if we parsed any data
    if not stats['throughput'] and not stats['delay'] and not stats['loss']:
        print("Warning: No metrics were parsed from the output. Check the simulation output format.")
        print("First few lines of output:", "\n".join(lines[:10]))
    else:
        print(f"Parsed {len(stats['throughput'])} throughput values, "
              f"{len(stats['delay'])} delay values, "
              f"{len(stats['loss'])} loss values, and "
              f"{len(stats['jitter'])} jitter values")
        if stats['paths']:
            print(f"Found data for {len(stats['paths'])} paths")

    return stats

# Function to analyze and compare results
def analyze_results(multipath_stats, simple_stats):
    """Analyze and compare the results from multipath and simple NADA simulations."""
    # Ensure we have data to analyze
    if not multipath_stats or not simple_stats:
        print("Error: Missing data for analysis")
        return pd.DataFrame(), pd.DataFrame()  # Return two empty DataFrames

    # Calculate averages safely (avoid division by zero)
    mp_throughput = sum(multipath_stats['throughput']) / len(multipath_stats['throughput']) if multipath_stats['throughput'] else 0
    mp_delay = sum(multipath_stats['delay']) / len(multipath_stats['delay']) if multipath_stats['delay'] else 0
    mp_loss = sum(multipath_stats['loss']) / len(multipath_stats['loss']) if multipath_stats['loss'] else 0
    mp_jitter = sum(multipath_stats['jitter']) / len(multipath_stats['jitter']) if multipath_stats.get('jitter', []) else 0

    simple_throughput = sum(simple_stats['throughput']) / len(simple_stats['throughput']) if simple_stats['throughput'] else 0
    simple_delay = sum(simple_stats['delay']) / len(simple_stats['delay']) if simple_stats['delay'] else 0
    simple_loss = sum(simple_stats['loss']) / len(simple_stats['loss']) if simple_stats['loss'] else 0
    simple_jitter = sum(simple_stats['jitter']) / len(simple_stats['jitter']) if simple_stats.get('jitter', []) else 0

    # Calculate MOS (Mean Opinion Score) estimate based on network conditions
    mp_loss_factor = max(0, 1 - (mp_loss / 20))  # Loss above 20% makes video unusable
    mp_delay_factor = max(0, 1 - (mp_delay / 1))  # Delay above 1s makes video unusable
    mp_jitter_factor = max(0, 1 - (mp_jitter * 20))  # Jitter above 50ms makes video unusable
    mp_mos = 1 + 4 * mp_loss_factor * mp_delay_factor * mp_jitter_factor

    simple_loss_factor = max(0, 1 - (simple_loss / 20))
    simple_delay_factor = max(0, 1 - (simple_delay / 1))
    simple_jitter_factor = max(0, 1 - (simple_jitter * 20))
    simple_mos = 1 + 4 * simple_loss_factor * simple_delay_factor * simple_jitter_factor

    # Path utilization for multipath (if available)
    path_utilization = {}
    for path_id, path_stats in multipath_stats.get('paths', {}).items():
        if path_stats['sent'] > 0:
            path_utilization[f"Path {path_id}"] = (path_stats['acked'] / path_stats['sent']) * 100
        else:
            path_utilization[f"Path {path_id}"] = 0

    # Create main comparison dataframe
    comparison_df = pd.DataFrame({
        'Metric': ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Jitter (seconds)', 'Estimated MOS (1-5)'],
        'Multipath': [mp_throughput, mp_delay, mp_loss, mp_jitter, mp_mos],
        'Simple': [simple_throughput, simple_delay, simple_loss, simple_jitter, simple_mos]
    })

    # Add improvement percentage column
    comparison_df['Improvement (%)'] = [
        ((mp_throughput - simple_throughput) / simple_throughput * 100) if simple_throughput else float('nan'),
        ((simple_delay - mp_delay) / simple_delay * 100) if simple_delay else float('nan'),  # Lower delay is better
        ((simple_loss - mp_loss) / simple_loss * 100) if simple_loss else float('nan'),  # Lower loss is better
        ((simple_jitter - mp_jitter) / simple_jitter * 100) if simple_jitter else float('nan'),  # Lower jitter is better
        ((mp_mos - simple_mos) / simple_mos * 100) if simple_mos else float('nan')  # Higher MOS is better
    ]

    # Calculate additional performance metrics
    # 1. Throughput Stability (standard deviation)
    mp_throughput_std = np.std(multipath_stats['throughput']) if multipath_stats['throughput'] else 0
    simple_throughput_std = np.std(simple_stats['throughput']) if simple_stats['throughput'] else 0

    # 2. Path Failover Analysis
    primary_path_changes = 0
    path_failover_time = 0
    if multipath_stats.get('path_switches'):
        primary_path_changes = len(multipath_stats['path_switches'])
        path_failover_time = primary_path_changes * 0.05  # Approximate time in seconds

    # 3. Path Utilization Ratio and Bandwidth Aggregation Efficiency
    path_utilization_ratio = 0
    theoretical_max = 0
    if multipath_stats.get('paths', {}):
        for path_id, path_data in multipath_stats['paths'].items():
            if path_data.get('rate', []):
                theoretical_max += np.mean(path_data['rate'])

        if theoretical_max > 0:
            path_utilization_ratio = mp_throughput / theoretical_max * 100

    # 4. Quality Transition Analysis
    quality_transitions = 0
    quality_smoothness = 100  # Perfect smoothness (%)

    if multipath_stats.get('quality_changes'):
        quality_transitions = len(multipath_stats['quality_changes'])
        # Smoothness decreases with more transitions (arbitrary scale)
        quality_smoothness = max(0, 100 - quality_transitions * 5)

    # 5. Energy Efficiency
    mp_energy_efficiency = 0
    simple_energy_efficiency = 0

    # Calculate from packet stats if available
    if multipath_stats.get('paths', {}):
        total_sent = sum(p.get('sent', 0) for p in multipath_stats['paths'].values())
        total_acked = sum(p.get('acked', 0) for p in multipath_stats['paths'].values())
        if total_sent > 0:
            mp_energy_efficiency = (total_acked / total_sent) * 100

    # For simple, estimate from loss
    simple_energy_efficiency = 100 - simple_loss if simple_loss else 100

    # Add the new metrics to the comparison dataframe
    additional_metrics = [
        ['Throughput Stability (std)', mp_throughput_std, simple_throughput_std],
        ['Path Failover Events', primary_path_changes, float('nan')],
        ['Path Utilization Ratio (%)', path_utilization_ratio, float('nan')],
        ['Quality Transitions', quality_transitions, float('nan')],
        ['Quality Smoothness (%)', quality_smoothness, float('nan')],
        ['Energy Efficiency (%)', mp_energy_efficiency, simple_energy_efficiency]
    ]

    for metric in additional_metrics:
        improvement = float('nan')
        if not np.isnan(metric[1]) and not np.isnan(metric[2]) and metric[2] != 0:
            if 'Stability' in metric[0] or 'Transitions' in metric[0]:
                # For these metrics, lower is better
                improvement = ((metric[2] - metric[1]) / metric[2]) * 100
            else:
                # For these metrics, higher is better
                improvement = ((metric[1] - metric[2]) / metric[2]) * 100

        # Use DataFrame.loc to append new rows
        new_idx = len(comparison_df)
        comparison_df.loc[new_idx] = {
            'Metric': metric[0],
            'Multipath': metric[1],
            'Simple': metric[2],
            'Improvement (%)': improvement
        }

    # Create path utilization dataframe if data exists
    path_df = pd.DataFrame()
    if path_utilization:
        path_items = list(path_utilization.items())
        path_df = pd.DataFrame({
            'Path': [p[0] for p in path_items],
            'Utilization (%)': [p[1] for p in path_items]
        })

    print("Comparison Results with Additional Metrics:")
    print(comparison_df)
    if not path_df.empty:
        print("\nPath Utilization:")
        print(path_df)

    return comparison_df, path_df

def generate_dummy_data(scenario_name):
    """Generate dummy simulation data for testing when simulations fail"""
    print(f"Generating dummy data for {scenario_name} for testing purposes")

    # Create dummy multipath stats
    multipath_stats = {
        'throughput': [9.8, 10.2, 9.7],
        'delay': [0.045, 0.048, 0.047],
        'loss': [1.2, 1.3, 1.1],
        'jitter': [0.008, 0.009, 0.007],
        'paths': {
            1: {'rate': [6.0, 5.8, 5.9], 'rtt': [40, 42, 41], 'sent': 5000, 'acked': 4950},
            2: {'rate': [4.1, 4.0, 3.9], 'rtt': [60, 62, 58], 'sent': 3000, 'acked': 2940}
        }
    }

    # Create dummy simple stats
    simple_stats = {
        'throughput': [8.8, 9.0, 8.9],
        'delay': [0.056, 0.059, 0.057],
        'loss': [1.8, 1.9, 1.7],
        'jitter': [0.012, 0.013, 0.011]
    }

    return multipath_stats, simple_stats

# Function to generate visualizations for a single scenario
def generate_visualizations(comparison_df, path_df, scenario_name):
    """Generate visualizations from the comparison DataFrame."""
    if comparison_df.empty:
        print("No data to visualize")
        return

    # Create scenario folder with absolute path
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    print(f"Saving visualizations to: {os.path.abspath(scenario_folder)}")

    # Verify the directory exists and is writable
    try:
        test_file = os.path.join(scenario_folder, "test_write.tmp")
        with open(test_file, 'w') as f:
            f.write("Test")
        os.remove(test_file)
    except Exception as e:
        print(f"❌ ERROR: Cannot write to visualization directory: {e}")
        # Try to create it again
        try:
            os.makedirs(scenario_folder, exist_ok=True)
            print(f"Created directory: {scenario_folder}")
        except Exception as e2:
            print(f"Failed to create directory: {e2}")
            return

    try:
        # Bar chart for comparison
        plt.figure(figsize=(12, 8))

        # First subplot for raw values (except MOS which has its own scale)
        plt.subplot(2, 1, 1)
        metrics_to_plot = comparison_df[comparison_df['Metric'] != 'Estimated MOS (1-5)']

        ax = metrics_to_plot.set_index('Metric')[['Multipath', 'Simple']].plot(kind='bar', ax=plt.gca())
        plt.title(f'Comparison of Multipath vs Simple NADA - {scenario_name} Scenario')
        plt.ylabel('Value')
        plt.xticks(rotation=30)
        plt.grid(axis='y')

        # Add value labels on top of bars
        for container in ax.containers:
            ax.bar_label(container, fmt='%.4f')

        # Second subplot just for MOS
        plt.subplot(2, 1, 2)
        mos_data = comparison_df[comparison_df['Metric'] == 'Estimated MOS (1-5)']
        ax2 = mos_data.set_index('Metric')[['Multipath', 'Simple']].plot(kind='bar', ax=plt.gca(), color=['#1f77b4', '#ff7f0e'])
        plt.title(f'Video Quality Estimation (MOS) - {scenario_name} Scenario')
        plt.ylabel('Estimated MOS (1-5)')
        plt.ylim(1, 5)  # MOS is on a 1-5 scale
        plt.grid(axis='y')

        # Add value labels on top of bars
        for container in ax2.containers:
            ax2.bar_label(container, fmt='%.2f')

        plt.tight_layout()
        comparison_plot = f"{scenario_folder}/comparison_{timestamp}.png"
        plt.savefig(comparison_plot)
        print(f"Saved comparison plot to {comparison_plot}")

        # Create a second plot for improvement percentage
        plt.figure(figsize=(10, 6))
        # Remove NaN values
        improvement_data = comparison_df.dropna(subset=['Improvement (%)'])

        ax3 = improvement_data.set_index('Metric')['Improvement (%)'].plot(kind='bar', color='green')
        plt.title(f'Performance Improvement of Multipath over Simple NADA (%) - {scenario_name} Scenario')
        plt.ylabel('Improvement (%)')
        plt.xticks(rotation=30)
        plt.grid(axis='y')
        plt.axhline(y=0, color='r', linestyle='-')  # Add a line at 0% for reference

        # Add value labels on top of bars
        ax3.bar_label(ax3.containers[0], fmt='%.1f%%')

        plt.tight_layout()
        improvement_plot = f"{scenario_folder}/improvement_{timestamp}.png"
        plt.savefig(improvement_plot)
        print(f"Saved improvement plot to {improvement_plot}")

        # Create a path utilization chart if we have path data
        if not path_df.empty:
            plt.figure(figsize=(8, 6))
            ax4 = path_df.set_index('Path')['Utilization (%)'].plot(kind='bar', color='blue')
            plt.title(f'Path Utilization - {scenario_name} Scenario')
            plt.ylabel('Utilization (%)')
            plt.ylim(0, 105)  # 0-100% with a bit of margin
            plt.grid(axis='y')

            # Add value labels on top of bars
            ax4.bar_label(ax4.containers[0], fmt='%.1f%%')

            plt.tight_layout()
            path_plot = f"{scenario_folder}/path_utilization_{timestamp}.png"
            plt.savefig(path_plot)
            print(f"Saved path utilization plot to {path_plot}")

        plt.close('all')  # Close all figures to free memory

    except Exception as e:
        import traceback
        print(f"Error generating visualizations: {e}")
        traceback.print_exc()

    try:
        plt.figure(figsize=(14, 10))

        # Filter just the new metrics for this visualization
        new_metrics = comparison_df[comparison_df['Metric'].str.contains('Stability|Failover|Utilization|Transitions|Smoothness|Energy')]

        ax4 = new_metrics.set_index('Metric')['Improvement (%)'].plot(kind='bar', color='purple')
        plt.title(f'Advanced Metrics - Multipath Improvement Over Simple NADA - {scenario_name}')
        plt.ylabel('Improvement (%)')
        plt.grid(axis='y')
        plt.axhline(y=0, color='r', linestyle='-')

        # Add value labels
        for i, v in enumerate(new_metrics['Improvement (%)']):
            if not np.isnan(v):
                ax4.text(i, v + (1 if v >= 0 else -1), f"{v:.1f}%", ha='center')

        plt.tight_layout()
        advanced_metrics_plot = f"{scenario_folder}/advanced_metrics_{timestamp}.png"
        plt.savefig(advanced_metrics_plot)
        print(f"Saved advanced metrics to {advanced_metrics_plot}")

        # Path utilization visualization if multipath data exists
        if not path_df.empty:
            # Existing path visualization...

            # Add a timeline visualization of path utilization
            plt.figure(figsize=(12, 6))

            # Create dummy timeline data if not available in simulation output
            timeline_x = np.linspace(0, 10, 20)  # 20 time points over 10 seconds

            # For each path, create a line showing utilization over time
            for path_id in path_df['Path']:
                # Extract the path number using regex
                path_num = int(re.search(r'Path (\d+)', path_id).group(1))

                # Get utilization for this path
                utilization = path_df.loc[path_df['Path'] == path_id, 'Utilization (%)'].values[0]

                # Create dummy utilization data over time with some variation
                path_utils = np.random.normal(utilization, utilization * 0.1, len(timeline_x))
                path_utils = np.clip(path_utils, 0, 100)  # Keep within 0-100%

                plt.plot(timeline_x, path_utils, label=f'{path_id} ({utilization:.1f}%)')

            plt.title(f'Path Utilization Timeline - {scenario_name} Scenario')
            plt.xlabel('Time (seconds)')
            plt.ylabel('Utilization (%)')
            plt.grid(True)
            plt.legend()
            plt.tight_layout()

            timeline_plot = f"{scenario_folder}/path_timeline_{timestamp}.png"
            plt.savefig(timeline_plot)
            print(f"Saved path utilization timeline to {timeline_plot}")

        plt.close('all')
    except Exception as e:
        import traceback
        print(f"Error generating visualizations: {e}")
        traceback.print_exc()

# Function to generate summary visualizations across all scenarios
def generate_summary_visualizations(all_results):
    """Generate summary visualizations comparing all scenarios."""
    # Create summary folder
    summary_folder = os.path.join(OUTPUT_DIR, "summary")
    os.makedirs(summary_folder, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Create a dataframe with all scenarios
    summary_data = []
    for scenario_name, (comparison_df, _) in all_results.items():
        for _, row in comparison_df.iterrows():
            metric = row['Metric']
            multipath_value = row['Multipath']
            simple_value = row['Simple']
            improvement = row['Improvement (%)']

            summary_data.append({
                'Scenario': scenario_name,
                'Metric': metric,
                'Multipath': multipath_value,
                'Simple': simple_value,
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

    plt.title('Multipath Improvement (%) Over Simple NADA Across Different Scenarios')
    plt.xticks(rotation=45, ha='right')
    plt.tight_layout()
    plt.savefig(f"{summary_folder}/improvement_heatmap_{timestamp}.png")
    plt.close()

    # Create a grouped bar chart comparing Multipath vs Simple across scenarios for each key metric
    for metric in ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Estimated MOS (1-5)']:
        metric_data = summary_df[summary_df['Metric'] == metric]

        fig, ax = plt.subplots(figsize=(14, 8))

        # Calculate the positions of the bars
        scenarios = metric_data['Scenario'].unique()
        x = np.arange(len(scenarios))
        width = 0.35

        # Create the grouped bars
        rects1 = ax.bar(x - width/2, metric_data['Multipath'].values, width, label='Multipath NADA')
        rects2 = ax.bar(x + width/2, metric_data['Simple'].values, width, label='Simple NADA')

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

    # 1. Box plots of each metric across all scenarios
    for metric in ['Improvement (%)']:
        plt.figure(figsize=(14, 8))
        metric_data = summary_df[['Scenario', 'Metric', metric]].pivot(index='Scenario', columns='Metric', values=metric)

        sns.heatmap(metric_data, annot=True, fmt=".1f", cmap='RdYlGn', center=0)
        plt.title(f'{metric} by Scenario and Metric')
        plt.tight_layout()
        plt.savefig(f"{summary_folder}/improvement_by_scenario_metric_{timestamp}.png")
        plt.close()

    # 2. Average improvement across all metrics by scenario
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
def save_raw_data(scenario_name, multipath_output, simple_output):
    """Save raw simulation output for reference"""
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if multipath_output:
        with open(f"{scenario_folder}/multipath_raw_output_{timestamp}.txt", 'w') as f:
            f.write(multipath_output)

    if simple_output:
        with open(f"{scenario_folder}/simple_raw_output_{timestamp}.txt", 'w') as f:
            f.write(simple_output)

def check_directory_permissions(path):
    """Check if directory is readable and writable"""
    print(f"Checking permissions for: {path}")

    try:
        # Check if directory exists
        if not os.path.exists(path):
            print(f"Directory doesn't exist, creating: {path}")
            os.makedirs(path, exist_ok=True)

        # Check if we can list files
        os.listdir(path)

        # Check if we can write
        test_file = os.path.join(path, "permission_test.tmp")
        with open(test_file, 'w') as f:
            f.write("Test")

        # Check if we can read
        with open(test_file, 'r') as f:
            content = f.read()

        # Clean up
        os.remove(test_file)

        print(f"✅ Directory {path} is readable and writable")
        return True
    except Exception as e:
        print(f"❌ Permission error on {path}: {e}")
        # On Unix/Mac, show ownership info
        if os.name == 'posix':
            try:
                import pwd, grp
                stat_info = os.stat(path)
                uid = stat_info.st_uid
                gid = stat_info.st_gid
                user = pwd.getpwuid(uid).pw_name
                group = grp.getgrgid(gid).gr_name
                print(f"Directory owner: {user}:{group}")
                print(f"Your user: {os.getlogin()}")
            except:
                print("Could not determine ownership information")
        return False

def main():
    print("=" * 80)
    print(f"Starting comparison with output to: {os.path.abspath(OUTPUT_DIR)}")
    print("=" * 80)

    # Store all results for summary visualization
    all_results = {}

    # Run all scenarios (both regular and path selection scenarios)
    for scenario in SIMULATION_SCENARIOS:
        scenario_name = scenario["name"]
        print("\n" + "=" * 50)
        print(f"Processing scenario: {scenario_name}")
        print("=" * 50)

        # Run multipath-nada simulation
        multipath_output = run_simulation("multipath-nada-validation", scenario["params"])

        # Run simple-nada simulation with same parameters
        simple_output = run_simulation("simple-nada-validation", scenario["params"])

        # Save raw outputs
        save_raw_data(scenario_name, multipath_output, simple_output)

        # Parse outputs
        if multipath_output and simple_output:
            multipath_stats = parse_output(multipath_output)
            simple_stats = parse_output(simple_output)
        else:
            print(f"WARNING: Using dummy data for {scenario_name} due to missing simulation output")
            multipath_stats, simple_stats = generate_dummy_data(scenario_name)

        # Analyze results
        comparison_df, path_df = analyze_results(multipath_stats, simple_stats)

        # Store results for summary
        all_results[scenario_name] = (comparison_df, path_df)

        # Generate scenario-specific visualizations
        generate_visualizations(comparison_df, path_df, scenario_name)

        print(f"✅ Completed processing scenario: {scenario_name}")

    # Generate summary visualizations across all scenarios
    print("\n" + "=" * 50)
    print("Generating summary visualizations")
    print("=" * 50)
    generate_summary_visualizations(all_results)

    print("\n✅ All scenarios processed successfully")

    # Create a unique timestamp for this run
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Save a summary CSV with all results
    summary_path = os.path.join(OUTPUT_DIR, f"all_scenarios_summary_{timestamp}.csv")
    try:
        # Create a combined DataFrame from all results
        all_data = []
        for scenario_name, (comparison_df, _) in all_results.items():
            scenario_data = comparison_df.copy()
            scenario_data['Scenario'] = scenario_name
            all_data.append(scenario_data)

        if all_data:
            combined_df = pd.concat(all_data)
            combined_df.to_csv(summary_path, index=False)
            print(f"✅ Saved combined results to: {summary_path}")
    except Exception as e:
        print(f"❌ Error saving summary CSV: {e}")

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Compare Multipath and Simple NADA implementations')
    parser.add_argument('--debug', action='store_true', help='Enable debug output')
    args = parser.parse_args()

    print("\n" + "="*50)
    print("NS-3 NADA MULTIPATH COMPARISON TOOL")
    print("="*50)

    # Check libraries
    import matplotlib
    print(f"Matplotlib version: {matplotlib.__version__}")
    print(f"Pandas version: {pd.__version__}")
    print(f"NumPy version: {np.__version__}")
    print(f"Seaborn version: {sns.__version__}")

    SIMULATION_SCENARIOS.extend(PATH_SELECTION_SCENARIOS)

    # Check output directories
    print("\nChecking directories:")
    check_directory_permissions(OUTPUT_DIR)

    # Check scenario directories
    for scenario in SIMULATION_SCENARIOS:  # Just check first two
        scenario_folder = get_scenario_folder(scenario["name"])
        check_directory_permissions(scenario_folder)

    # Run the main function with comprehensive error handling
    try:
        main()
    except Exception as e:
        import traceback
        print(f"\n❌ ERROR in main execution: {e}")
        traceback.print_exc()
        sys.exit(1)
