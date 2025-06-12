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
OUTPUT_DIR = os.path.join(script_dir, "../results/comparison")
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

PATH_SELECTION_STRATEGIES = [
    {"name": "Dynamic RTT Weights", "strategy": 0},
    {"name": "Weighted Best Path", "strategy": 1},
    {"name": "Round Robin", "strategy": 2},
    # {"name": "Redundant Transmission", "strategy": 3},
    {"name": "Frame Aware", "strategy": 4},
    {"name": "Buffer_Aware", "strategy": 5}
]


BASE_SIMULATION_SCENARIOS = [
    # 1. Legacy Broadband - Moderate congestion
    {
        "name": "Legacy Broadband",
        "params": {
            "bottleneckBw": "100Mbps",
            "dataRate1": "60Mbps",
            "dataRate2": "50Mbps",
            "delayMs1": 35,
            "delayMs2": 50,
            "frameRate": 30,
            "maxPackets": 3000,
            "simulationTime": 60,
            "competingSourcesA": 8,      # Creates ~32 Mbps competing traffic
            "competingSourcesB": 6,      # Creates ~24 Mbps competing traffic
            "competingIntensityA": 0.8,
            "competingIntensityB": 0.85
        }
    },

    # 2. Unbalanced Dual-WAN - Heavy congestion on backup path
    {
        "name": "Unbalanced Dual-WAN",
        "params": {
            "bottleneckBw": "200Mbps",
            "dataRate1": "200Mbps",      # Primary fast link
            "dataRate2": "50Mbps",       # Secondary slow link
            "delayMs1": 20,
            "delayMs2": 80,
            "frameRate": 60,
            "maxPackets": 6000,
            "simulationTime": 60,
            "competingSourcesA": 10,     # Creates ~40 Mbps competing (20% of 200Mbps)
            "competingSourcesB": 8,      # Creates ~32 Mbps competing (64% of 50Mbps - heavy!)
            "competingIntensityA": 0.75,
            "competingIntensityB": 0.9   # Heavy congestion on backup path
        }
    },

    # 3. Congested Metro Network - Balanced heavy congestion
    {
        "name": "Congested Metro Network",
        "params": {
            "bottleneckBw": "300Mbps",
            "dataRate1": "500Mbps",
            "dataRate2": "500Mbps",
            "delayMs1": 40,
            "delayMs2": 50,
            "frameRate": 60,
            "maxPackets": 4500,
            "simulationTime": 60,
            "competingSourcesA": 12,     # Creates ~48 Mbps competing traffic
            "competingSourcesB": 12,     # Creates ~48 Mbps competing traffic
            "competingIntensityA": 0.85,
            "competingIntensityB": 0.85
        }
    },

    # 4. Balanced Gigabit - Controlled congestion
    {
        "name": "Balanced Gigabit",
        "params": {
            "bottleneckBw": "1Gbps",
            "dataRate1": "600Mbps",
            "dataRate2": "600Mbps",
            "delayMs1": 18,
            "delayMs2": 22,
            "frameRate": 60,
            "maxPackets": 10000,
            "simulationTime": 60,
            "competingSourcesA": 10,     # Creates ~40 Mbps competing (6.7% of 600Mbps)
            "competingSourcesB": 10,     # Creates ~40 Mbps competing (6.7% of 600Mbps)
            "competingIntensityA": 0.8,
            "competingIntensityB": 0.8
        }
    },

    # 5. Hybrid Fiber-5G - Asymmetric realistic congestion
    {
        "name": "Hybrid Fiber-5G",
        "params": {
            "bottleneckBw": "1.2Gbps",
            "dataRate1": "1Gbps",        # Fiber - stable, light congestion
            "dataRate2": "800Mbps",      # 5G - variable, moderate congestion
            "delayMs1": 10,
            "delayMs2": 45,
            "frameRate": 60,
            "maxPackets": 12000,
            "simulationTime": 60,
            "competingSourcesA": 8,      # Creates ~32 Mbps competing (3.2% of 1Gbps)
            "competingSourcesB": 12,     # Creates ~48 Mbps competing (6% of 800Mbps)
            "competingIntensityA": 0.7,
            "competingIntensityB": 0.85
        }
    },

    # 6. High-Speed Asymmetric - Proportional congestion
    {
        "name": "High-Speed Asymmetric",
        "params": {
            "bottleneckBw": "2.5Gbps",
            "dataRate1": "2Gbps",
            "dataRate2": "1.2Gbps",
            "delayMs1": 10,
            "delayMs2": 25,
            "frameRate": 90,
            "maxPackets": 20000,
            "simulationTime": 50,        # 50 seconds allows 10 sources max
            "competingSourcesA": 10,     # Creates ~40 Mbps competing (2% of 2Gbps)
            "competingSourcesB": 8,      # Creates ~32 Mbps competing (2.7% of 1.2Gbps)
            "competingIntensityA": 0.8,
            "competingIntensityB": 0.8
        }
    },

    # 7. Enterprise Multi-Gigabit - Controlled high-speed congestion
    {
        "name": "Enterprise Multi-Gigabit",
        "params": {
            "bottleneckBw": "5Gbps",
            "dataRate1": "3Gbps",
            "dataRate2": "3Gbps",
            "delayMs1": 6,
            "delayMs2": 10,
            "frameRate": 120,
            "maxPackets": 40000,
            "simulationTime": 45,        # 45 seconds allows 9 sources max
            "competingSourcesA": 9,      # Creates ~36 Mbps competing (1.2% of 3Gbps)
            "competingSourcesB": 9,      # Creates ~36 Mbps competing (1.2% of 3Gbps)
            "competingIntensityA": 0.75,
            "competingIntensityB": 0.75
        }
    },

    # 8. Data Center Network
    {
        "name": "Data Center Network",
        "params": {
            "bottleneckBw": "10Gbps",
            "dataRate1": "6Gbps",
            "dataRate2": "6Gbps",
            "delayMs1": 1,
            "delayMs2": 3,
            "frameRate": 120,
            "maxPackets": 60000,
            "simulationTime": 40,        # 40 seconds allows 8 sources max
            "competingSourcesA": 8,      # Creates ~32 Mbps competing (0.53% of 6Gbps)
            "competingSourcesB": 8,      # Creates ~32 Mbps competing (0.53% of 6Gbps)
            "competingIntensityA": 0.8,
            "competingIntensityB": 0.8,
            "packetSize": 1500
        }
    },

    # 9. Satellite + Terrestrial - Realistic satellite congestion
    {
        "name": "Satellite + Terrestrial Hybrid",
        "params": {
            "bottleneckBw": "1Gbps",
            "dataRate1": "800Mbps",      # Terrestrial
            "dataRate2": "600Mbps",      # Satellite
            "delayMs1": 20,
            "delayMs2": 650,             # High latency satellite
            "frameRate": 60,
            "maxPackets": 8000,
            "simulationTime": 60,
            "competingSourcesA": 8,      # Creates ~32 Mbps competing (4% of 800Mbps)
            "competingSourcesB": 12,     # Creates ~48 Mbps competing (8% of 600Mbps)
            "competingIntensityA": 0.7,
            "competingIntensityB": 0.9   # Heavy congestion typical of shared satellite
        }
    },

    # 10. Ultra-High Speed Research Network - Conservative high-speed
    {
        "name": "Ultra-High Speed Research Network",
        "params": {
            "bottleneckBw": "20Gbps",
            "dataRate1": "15Gbps",
            "dataRate2": "12Gbps",
            "delayMs1": 2,
            "delayMs2": 4,
            "frameRate": 120,
            "maxPackets": 150000,
            "simulationTime": 35,        # 35 seconds allows 7 sources max
            "competingSourcesA": 7,      # Creates ~28 Mbps competing (0.19% of 15Gbps)
            "competingSourcesB": 7,      # Creates ~28 Mbps competing (0.23% of 12Gbps)
            "competingIntensityA": 0.7,
            "competingIntensityB": 0.7,
            "packetSize": 9000
        }
    }
]


def calculate_adaptive_params(data_rate_str, simulation_time=60):
    """Calculate adaptive parameters based on data rate for realistic testing"""
    # Convert data rate to bps
    multipliers = {'Mbps': 1e6, 'Gbps': 1e9, 'kbps': 1e3}

    rate_bps = 0
    for unit, mult in multipliers.items():
        if unit in data_rate_str:
            rate_bps = float(data_rate_str.replace(unit, '')) * mult
            break

    # Adaptive parameters based on link speed
    if rate_bps >= 10e9:  # 10Gbps+
        target_utilization = 0.15  # 15% for high-speed links
        competing_sources = min(10, max(6, int(rate_bps / 5e9)))  # Scale with speed
        simulation_time = min(simulation_time, 40)  # Shorter for very high speeds
    elif rate_bps >= 5e9:  # 5Gbps+
        target_utilization = 0.12
        competing_sources = 6
        simulation_time = min(simulation_time, 50)
    elif rate_bps >= 1e9:  # 1Gbps+
        target_utilization = 0.10
        competing_sources = 4
    else:  # < 1Gbps
        target_utilization = 0.08
        competing_sources = 2

    # Calculate packet count
    target_bps = rate_bps * target_utilization
    bytes_per_second = target_bps / 8
    total_bytes = bytes_per_second * simulation_time
    packets_needed = int(total_bytes / 1000)  # 1000 byte packets

    # Cap maximum packets to prevent extremely long simulations
    max_packets = min(packets_needed, 1000000)  # Cap at 1M packets
    max_packets = max(max_packets, 1000)  # Minimum 1000 packets

    return {
        'maxPackets': max_packets,
        'simulationTime': simulation_time,
        'targetUtilization': target_utilization * 100
    }

def apply_fast_mode_optimizations():
    """Apply optimizations for faster comparative testing"""
    for scenario in BASE_SIMULATION_SCENARIOS:
        params = scenario["params"]

        # More aggressive reductions for high-speed scenarios
        data_rate1 = params.get('dataRate1', params.get('dataRate', '1Mbps'))

        # Reduce packet counts based on data rate
        if "maxPackets" in params:
            reduction_factor = 0.2  # Default 20%

            # More aggressive reduction for high-speed links
            if 'Gbps' in data_rate1:
                rate_val = float(data_rate1.replace('Gbps', ''))
                if rate_val >= 25:      # 25Gbps+
                    reduction_factor = 0.1   # 10%
                elif rate_val >= 10:    # 10Gbps+
                    reduction_factor = 0.15  # 15%
                elif rate_val >= 5:     # 5Gbps+
                    reduction_factor = 0.2   # 20%

            params["maxPackets"] = max(1000, int(params["maxPackets"] * reduction_factor))

        # Reduce simulation time more aggressively
        if "simulationTime" in params:
            if 'Gbps' in data_rate1 and float(data_rate1.replace('Gbps', '')) >= 10:
                params["simulationTime"] = min(20, params["simulationTime"])  # 20s max for 10Gbps+
            else:
                params["simulationTime"] = min(30, params["simulationTime"])

        # Reduce competing sources significantly

        # Reduce frame rate for high-speed scenarios
        if "frameRate" in params and 'Gbps' in data_rate1:
            params["frameRate"] = min(30, params["frameRate"])

# Function to create a scenario folder
def get_scenario_folder(scenario_name):
    """Create and return path to scenario-specific folder"""
    folder_name = scenario_name.lower().replace(' ', '_')
    folder_path = os.path.join(OUTPUT_DIR, folder_name)
    os.makedirs(folder_path, exist_ok=True)
    return folder_path

def run_simulation(script_name, params=None):
    """Run the specified simulation script with parameters and return the output."""
    if params:
        # Auto-calculate adaptive parameters if not already set
        data_rate1 = params.get('dataRate1', params.get('dataRate', '1Mbps'))

        if 'maxPackets' not in params or 'simulationTime' not in params:
            adaptive_params = calculate_adaptive_params(
                data_rate1,
                params.get('simulationTime', 60)
            )

            # Apply adaptive parameters if not explicitly set
            for key, value in adaptive_params.items():
                if key not in params:
                    params[key] = value
                    print(f"Auto-set {key}={value} for {data_rate1}")

    cmd_string = script_name
    if params:
        for key, value in params.items():
            cmd_string += f" --{key}={value}"

    # Create the command list with quotes around the full command string
    cmd = ["./ns3", "run", f'"{cmd_string}"']

    print(f"Running simulation: {' '.join(cmd)}")
    try:
        timeout = 1800 # 30 mins

        if params:
            strategy = params.get('pathSelectionStrategy', 0)
            data_rate1 = params.get('dataRate1', '1Mbps')
            max_packets = params.get('maxPackets', 1000)


        if 'Gbps' in data_rate1:
            rate_val = float(data_rate1.replace('Gbps', ''))
            if rate_val >= 10:      # 10Gbps+
                timeout = 3600      # 60 minutes
            elif rate_val >= 5:     # 5Gbps+
                timeout = 2700      # 45 minutes
            elif rate_val >= 1:     # 1Gbps+
                timeout = 2400      # 40 minutes

        if max_packets > 100000:
            timeout = max(timeout, 4800)  # 80 minutes for very large packet counts
        elif max_packets > 50000:
            timeout = max(timeout, 3600)  # 60 minutes for large packet counts

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

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

    # Use regular expressions for more robust parsing
    throughput_re = re.compile(r"Throughput: ([0-9.]+) Mbps")
    delay_re = re.compile(r"Mean delay: ([0-9.e-]+) seconds")
    loss_re = re.compile(r"Packet loss: ([0-9.]+)%")
    jitter_re = re.compile(r"Mean jitter: ([0-9.e-]+) seconds")

    buffer_length_re = re.compile(r"Average buffer length: ([0-9.]+) ms")
    buffer_underruns_re = re.compile(r"Buffer underruns: ([0-9]+)")

    # For multipath, also look for path-specific stats
    path_re = re.compile(r"Path (\d+):")
    path_rate_re = re.compile(r"Rate: ([0-9.]+) Mbps")
    path_rtt_re = re.compile(r"RTT: ([0-9.]+) ms")
    path_packets_sent_re = re.compile(r"Packets sent: ([0-9]+)")
    path_packets_acked_re = re.compile(r"Packets acked: ([0-9]+)")
    path_weight_re = re.compile(r"Weight: ([0-9.]+)")

    packets_sent_re = re.compile(r"Total packets sent: ([0-9]+)")
    packets_delivered_re = re.compile(r"Total packets delivered: ([0-9]+)")
    bytes_sent_re = re.compile(r"Total bytes sent: ([0-9]+)")
    bytes_delivered_re = re.compile(r"Total bytes delivered: ([0-9]+)")

    path_switch_re = re.compile(r"Path switch: from path (\d+) to path (\d+)")
    quality_change_re = re.compile(r"Quality changed: ([0-9.]+) -> ([0-9.]+)")

    lines = output.split('\n')
    stats = {
        'throughput': [],
        'delay': [],
        'loss': [],
        'jitter': [],
        'paths': {},
        'delivery_stats': {
            'packets_sent': 0,
            'packets_delivered': 0,
            'bytes_sent': 0,
            'bytes_delivered': 0
        },
        'path_switches': [],
        'quality_changes': []
    }

    stats['buffer_stats'] = {
        'length': [],
        'underruns': 0,
        'average_ms': 0
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
                stats['paths'][current_path] = {
                    'rate': [], 'rtt': [], 'sent': 0, 'acked': 0, 'weight': 0
                }
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

            # Extract path weight
            weight_match = path_weight_re.search(line)
            if weight_match:
                stats['paths'][current_path]['weight'] = float(weight_match.group(1))
                continue

        packets_sent_match = packets_sent_re.search(line)
        if packets_sent_match:
            stats['delivery_stats']['packets_sent'] = int(packets_sent_match.group(1))
            continue

        packets_delivered_match = packets_delivered_re.search(line)
        if packets_delivered_match:
            stats['delivery_stats']['packets_delivered'] = int(packets_delivered_match.group(1))
            continue

        bytes_sent_match = bytes_sent_re.search(line)
        if bytes_sent_match:
            stats['delivery_stats']['bytes_sent'] = int(bytes_sent_match.group(1))
            continue

        bytes_delivered_match = bytes_delivered_re.search(line)
        if bytes_delivered_match:
            stats['delivery_stats']['bytes_delivered'] = int(bytes_delivered_match.group(1))
            continue

        path_switch_match = path_switch_re.search(line)
        if path_switch_match:
            from_path = int(path_switch_match.group(1))
            to_path = int(path_switch_match.group(2))
            stats['path_switches'].append((from_path, to_path))
            continue

        quality_change_match = quality_change_re.search(line)
        if quality_change_match:
            from_quality = float(quality_change_match.group(1))
            to_quality = float(quality_change_match.group(2))
            stats['quality_changes'].append((from_quality, to_quality))
            continue

        buffer_length_match = buffer_length_re.search(line)
        if buffer_length_match:
            stats['buffer_stats']['average_ms'] = float(buffer_length_match.group(1))
            stats['buffer_stats']['length'].append(float(buffer_length_match.group(1)))
            continue

        # Extract buffer underruns
        buffer_underruns_match = buffer_underruns_re.search(line)
        if buffer_underruns_match:
            stats['buffer_stats']['underruns'] = int(buffer_underruns_match.group(1))
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

def analyze_results(multipath_stats, simple_stats):
    """Analyze and compare the results from TCP multipath and TCP simple NADA simulations."""
    # Ensure we have data to analyze
    if not multipath_stats or not simple_stats:
        print("Error: Missing data for analysis")
        # Return empty DataFrames with the expected structure
        empty_df = pd.DataFrame(columns=['Metric', 'Multipath-NADA', 'Aggregated-NADA', 'Improvement (%)'])
        path_df = pd.DataFrame(columns=['Path', 'Utilization (%)', 'Weight'])
        return empty_df, path_df

    # Check if the stats dictionaries have the expected structure
    if not isinstance(multipath_stats, dict) or not isinstance(simple_stats, dict):
        print("Error: Invalid stats format")
        empty_df = pd.DataFrame(columns=['Metric', 'Multipath-NADA', 'Aggregated-NADA', 'Improvement (%)'])
        path_df = pd.DataFrame(columns=['Path', 'Utilization (%)', 'Weight'])
        return empty_df, path_df

    # Calculate averages safely (avoid division by zero)
    mp_throughput = sum(multipath_stats['throughput']) / len(multipath_stats['throughput']) if multipath_stats.get('throughput') else 0
    mp_delay = sum(multipath_stats['delay']) / len(multipath_stats['delay']) if multipath_stats.get('delay') else 0
    mp_loss = sum(multipath_stats['loss']) / len(multipath_stats['loss']) if multipath_stats.get('loss') else 0
    mp_jitter = sum(multipath_stats['jitter']) / len(multipath_stats['jitter']) if multipath_stats.get('jitter') else 0

    simple_throughput = sum(simple_stats['throughput']) / len(simple_stats['throughput']) if simple_stats.get('throughput') else 0
    simple_delay = sum(simple_stats['delay']) / len(simple_stats['delay']) if simple_stats.get('delay') else 0
    simple_loss = sum(simple_stats['loss']) / len(simple_stats['loss']) if simple_stats.get('loss') else 0
    simple_jitter = sum(simple_stats['jitter']) / len(simple_stats['jitter']) if simple_stats.get('jitter') else 0

    # Calculate MOS (Mean Opinion Score) estimate based on network conditions
    mp_loss_factor = max(0, 1 - (mp_loss / 20))  # Loss above 20% makes video unusable
    mp_delay_factor = max(0, 1 - (mp_delay / 1))  # Delay above 1s makes video unusable
    mp_jitter_factor = max(0, 1 - (mp_jitter * 20))  # Jitter above 50ms makes video unusable
    mp_mos = 1 + 4 * mp_loss_factor * mp_delay_factor * mp_jitter_factor

    simple_loss_factor = max(0, 1 - (simple_loss / 20))
    simple_delay_factor = max(0, 1 - (simple_delay / 1))
    simple_jitter_factor = max(0, 1 - (simple_jitter * 20))
    simple_mos = 1 + 4 * simple_loss_factor * simple_delay_factor * simple_jitter_factor


    mp_buffer_avg = multipath_stats.get('buffer_stats', {}).get('average_ms', 0)
    mp_buffer_underruns = multipath_stats.get('buffer_stats', {}).get('underruns', 0)

    simple_buffer_avg = simple_stats.get('buffer_stats', {}).get('average_ms', 0)
    simple_buffer_underruns = simple_stats.get('buffer_stats', {}).get('underruns', 0)

    buffer_metrics = [
        ['Buffer Length (ms)', mp_buffer_avg, simple_buffer_avg],
        ['Buffer Underruns', mp_buffer_underruns, simple_buffer_underruns]
    ]

    # Path utilization for multipath (if available)
    path_utilization = {}
    total_weight = 0
    for path_id, path_stats in multipath_stats.get('paths', {}).items():
        if path_stats.get('sent', 0) > 0:
            path_utilization[f"Path {path_id}"] = (path_stats.get('acked', 0) / path_stats['sent']) * 100
        else:
            path_utilization[f"Path {path_id}"] = 0
        total_weight += path_stats.get('weight', 0)

    # Normalize weights if we have them
    for path_id, path_stats in multipath_stats.get('paths', {}).items():
        if total_weight > 0:
            path_stats['norm_weight'] = path_stats.get('weight', 0) / total_weight
        else:
            path_stats['norm_weight'] = 0

    # Create main comparison dataframe
    comparison_df = pd.DataFrame({
        'Metric': ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Jitter (seconds)', 'Estimated MOS (1-5)'],
        'Multipath-NADA': [mp_throughput, mp_delay, mp_loss, mp_jitter, mp_mos],
        'Aggregated-NADA': [simple_throughput, simple_delay, simple_loss, simple_jitter, simple_mos]
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
    mp_throughput_std = np.std(multipath_stats.get('throughput', [])) if multipath_stats.get('throughput') else 0
    simple_throughput_std = np.std(simple_stats.get('throughput', [])) if simple_stats.get('throughput') else 0

    # 2. Path Utilization Ratio and Bandwidth Aggregation Efficiency
    path_utilization_ratio = 0
    theoretical_max = 0
    if multipath_stats.get('paths', {}):
        for path_id, path_data in multipath_stats['paths'].items():
            if path_data.get('rate', []):
                theoretical_max += np.mean(path_data['rate'])

        if theoretical_max > 0:
            path_utilization_ratio = mp_throughput / theoretical_max * 100

    mp_delivery_stats = multipath_stats.get('delivery_stats', {})
    simple_delivery_stats = simple_stats.get('delivery_stats', {})

    # Calculate energy efficiency as successful delivery ratio (bytes delivered / bytes sent)
    mp_energy_efficiency = 0
    if mp_delivery_stats.get('bytes_sent', 0) > 0:
        mp_energy_efficiency = (mp_delivery_stats.get('bytes_delivered', 0) / mp_delivery_stats['bytes_sent']) * 100

    simple_energy_efficiency = 0
    if simple_delivery_stats.get('bytes_sent', 0) > 0:
        simple_energy_efficiency = (simple_delivery_stats.get('bytes_delivered', 0) / simple_delivery_stats['bytes_sent']) * 100

    # If bytes data is not available, fall back to packet-based calculation
    if mp_energy_efficiency == 0 and mp_delivery_stats.get('packets_sent', 0) > 0:
        mp_energy_efficiency = (mp_delivery_stats.get('packets_delivered', 0) / mp_delivery_stats['packets_sent']) * 100

    if simple_energy_efficiency == 0 and simple_delivery_stats.get('packets_sent', 0) > 0:
        simple_energy_efficiency = (simple_delivery_stats.get('packets_delivered', 0) / simple_delivery_stats['packets_sent']) * 100

    # Add the new metrics to the comparison dataframe
    additional_metrics = [
        ['Throughput Stability (stddev)', mp_throughput_std, simple_throughput_std],
        ['Path Utilization Ratio (%)', path_utilization_ratio, float('nan')],
        ['Delivery Efficiency (%)', mp_energy_efficiency, simple_energy_efficiency]  # Renamed from Energy Efficiency
    ]

    for metric in additional_metrics:
        improvement = float('nan')
        if not np.isnan(metric[1]) and not np.isnan(metric[2]) and metric[2] != 0:
            if 'Stability' in metric[0]:
                # For stability, lower stddev is better
                improvement = ((simple_throughput_std - mp_throughput_std) / simple_throughput_std) * 100 if simple_throughput_std > 0 else float('nan')
            else:
                # For these metrics, higher is better
                improvement = ((metric[1] - metric[2]) / metric[2]) * 100

        # Use DataFrame.loc to append new rows
        new_idx = len(comparison_df)
        comparison_df.loc[new_idx] = {
            'Metric': metric[0],
            'Multipath-NADA': metric[1],
            'Aggregated-NADA': metric[2],
            'Improvement (%)': improvement
        }

    for metric in buffer_metrics:
        improvement = float('nan')
        if not np.isnan(metric[1]) and not np.isnan(metric[2]) and metric[2] != 0:
            if 'Underruns' in metric[0]:
                # For underruns, lower is better (calculate percentage reduction)
                improvement = ((simple_buffer_underruns - mp_buffer_underruns) / max(1, simple_buffer_underruns)) * 100
            else:
                # For buffer length, higher is better
                improvement = ((mp_buffer_avg - simple_buffer_avg) / simple_buffer_avg) * 100 if simple_buffer_avg > 0 else float('nan')

        new_idx = len(comparison_df)
        comparison_df.loc[new_idx] = {
            'Metric': metric[0],
            'Multipath-NADA': metric[1],
            'Aggregated-NADA': metric[2],
            'Improvement (%)': improvement
        }

    # Create path utilization dataframe if data exists
    path_df = pd.DataFrame(columns=['Path', 'Utilization (%)', 'Weight'])
    if path_utilization:
        path_items = list(path_utilization.items())
        path_df = pd.DataFrame({
            'Path': [p[0] for p in path_items],
            'Utilization (%)': [p[1] for p in path_items],
            'Weight': [multipath_stats['paths'][int(p[0].split()[1])].get('norm_weight', 0) * 100 for p in path_items]
        })

    print("Comparison Results with Additional Metrics:")
    print(comparison_df)
    if not path_df.empty:
        print("\nPath Utilization:")
        print(path_df)

    return comparison_df, path_df


def generate_buffer_visualizations(comparison_df, scenario_name, scenario_folder, timestamp):
    """Generate visualizations specifically for buffer metrics."""
    buffer_data = comparison_df[comparison_df['Metric'].isin(['Buffer Length (ms)', 'Buffer Underruns'])]

    if buffer_data.empty:
        print("No buffer data to visualize")
        return

    plt.figure(figsize=(12, 6))

    # Create subplots for different buffer metrics
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 6))

    # Plot buffer length
    buffer_length_data = buffer_data[buffer_data['Metric'] == 'Buffer Length (ms)']
    if not buffer_length_data.empty:
        length_bars = ax1.bar(['Multipath-NADA', 'Aggregated-NADA'],
                            [buffer_length_data['Multipath-NADA'].values[0],
                             buffer_length_data['Aggregated-NADA'].values[0]],
                            color=['#1f77b4', '#ff7f0e'])
        ax1.set_title('Buffer Length Comparison')
        ax1.set_ylabel('Average Buffer Length (ms)')
        ax1.grid(axis='y')
        # Add value labels
        for bar in length_bars:
            height = bar.get_height()
            ax1.text(bar.get_x() + bar.get_width()/2., height + 1,
                    f'{height:.1f}',
                    ha='center', va='bottom')

    # Plot buffer underruns
    buffer_underruns_data = buffer_data[buffer_data['Metric'] == 'Buffer Underruns']
    if not buffer_underruns_data.empty:
        underrun_bars = ax2.bar(['Multipath-NADA', 'Aggregated-NADA'],
                              [buffer_underruns_data['Multipath-NADA'].values[0],
                               buffer_underruns_data['Aggregated-NADA'].values[0]],
                              color=['#1f77b4', '#ff7f0e'])
        ax2.set_title('Buffer Underruns Comparison')
        ax2.set_ylabel('Number of Buffer Underruns')
        ax2.grid(axis='y')
        # Add value labels
        for bar in underrun_bars:
            height = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                    f'{int(height)}',
                    ha='center', va='bottom')

    plt.suptitle(f'Buffer Performance Metrics - {scenario_name}')
    plt.tight_layout()

    buffer_plot = f"{scenario_folder}/buffer_metrics_{timestamp}.png"
    plt.savefig(buffer_plot)
    print(f"Saved buffer metrics plot to {buffer_plot}")

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
        # Extract multipath-specific metrics for a separate chart
        multipath_only_metrics = ['Path Utilization Ratio (%)', 'Delivery Efficiency (%)']  # Updated name
        multipath_data = comparison_df[comparison_df['Metric'].isin(multipath_only_metrics)]

        # Remove these metrics from the main comparison dataframe for plotting
        common_metrics = comparison_df[~comparison_df['Metric'].isin(multipath_only_metrics)]

        # Generate dedicated buffer visualizations
        generate_buffer_visualizations(comparison_df, scenario_name, scenario_folder, timestamp)

        # Bar chart for comparison of common metrics (excluding buffer metrics and TCP metrics)
        plt.figure(figsize=(12, 8))

        # First subplot for raw values (except MOS, buffer metrics, and TCP metrics)
        plt.subplot(2, 1, 1)
        metrics_to_plot = common_metrics[~common_metrics['Metric'].isin(['Estimated MOS (1-5)', 'Buffer Length (ms)', 'Buffer Underruns'])]

        ax = metrics_to_plot.set_index('Metric')[['Multipath-NADA', 'Aggregated-NADA']].plot(kind='bar', ax=plt.gca())
        plt.title(f'Comparison of Multipath-NADA vs Aggregated-NADA NADA - {scenario_name}')
        plt.ylabel('Value')
        plt.xticks(rotation=30)
        plt.grid(axis='y')

        # Add value labels on top of bars
        for container in ax.containers:
            ax.bar_label(container, fmt='%.4f')

        # Second subplot for MOS
        plt.subplot(2, 1, 2)
        mos_data = common_metrics[common_metrics['Metric'] == 'Estimated MOS (1-5)']
        ax2 = mos_data.set_index('Metric')[['Multipath-NADA', 'Aggregated-NADA']].plot(kind='bar', ax=plt.gca(), color=['#1f77b4', '#ff7f0e'])
        plt.title(f'Video Quality Estimation (MOS) - {scenario_name}')
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

        # Create a separate visualization for multipath-specific metrics
        if not multipath_data.empty:
            plt.figure(figsize=(10, 6))
            ax_mp = multipath_data.set_index('Metric')['Multipath-NADA'].plot(kind='bar', color='green')
            plt.title(f'Multipath-NADA Specific Metrics - {scenario_name}')
            plt.ylabel('Value (%)')
            plt.grid(axis='y')
            plt.ylim(0, 105)  # Assuming percentages 0-100 with a bit of margin

            # Add value labels on top of bars
            for container in ax_mp.containers:
                ax_mp.bar_label(container, fmt='%.1f%%')

            plt.tight_layout()
            multipath_plot = f"{scenario_folder}/multipath_metrics_{timestamp}.png"
            plt.savefig(multipath_plot)
            print(f"Saved multipath-specific metrics plot to {multipath_plot}")

        # Create a plot for improvement percentage (only for common metrics)
        plt.figure(figsize=(10, 6))
        # Remove NaN values and multipath-only metrics
        improvement_data = common_metrics.dropna(subset=['Improvement (%)'])

        ax4 = improvement_data.set_index('Metric')['Improvement (%)'].plot(kind='bar', color='green')
        plt.title(f'Performance Improvement of Multipath-NADA over Aggregated-NADA NADA (%) - {scenario_name}')
        plt.ylabel('Improvement (%)')
        plt.xticks(rotation=30)
        plt.grid(axis='y')
        plt.axhline(y=0, color='r', linestyle='-')  # Add a line at 0% for reference

        # Add value labels on top of bars
        ax4.bar_label(ax4.containers[0], fmt='%.1f%%')

        plt.tight_layout()
        improvement_plot = f"{scenario_folder}/improvement_{timestamp}.png"
        plt.savefig(improvement_plot)
        print(f"Saved improvement plot to {improvement_plot}")

        # Create a path utilization chart if we have path data
        if not path_df.empty:
            plt.figure(figsize=(8, 6))

            # Create a bar chart with two metrics per path
            paths = path_df['Path']
            x = np.arange(len(paths))
            width = 0.35

            fig, ax = plt.subplots(figsize=(8, 6))
            rects1 = ax.bar(x - width/2, path_df['Utilization (%)'], width, label='Utilization (%)')
            rects2 = ax.bar(x + width/2, path_df['Weight'], width, label='Weight (%)')

            # Set labels and title
            ax.set_title(f'Path Utilization vs Weight - {scenario_name}')
            ax.set_ylabel('Percentage (%)')
            ax.set_xticks(x)
            ax.set_xticklabels(paths)
            ax.legend()

            # Add value labels
            ax.bar_label(rects1, fmt='%.1f%%')
            ax.bar_label(rects2, fmt='%.1f%%')

            plt.grid(axis='y')
            plt.ylim(0, 105)  # 0-100% with a bit of margin

            plt.tight_layout()
            path_plot = f"{scenario_folder}/path_utilization_{timestamp}.png"
            plt.savefig(path_plot)
            print(f"Saved path utilization plot to {path_plot}")

        plt.close('all')  # Close all figures to free memory

    except Exception as e:
        import traceback
        print(f"Error generating visualizations: {e}")
        traceback.print_exc()


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
            multipath_value = row['Multipath-NADA']
            simple_value = row['Aggregated-NADA']
            improvement = row['Improvement (%)']

            summary_data.append({
                'Scenario': scenario_name,
                'Metric': metric,
                'Multipath-NADA': multipath_value,
                'Aggregated-NADA': simple_value,
                'Improvement (%)': improvement
            })

    summary_df = pd.DataFrame(summary_data)

    # Save summary data
    summary_df.to_csv(f"{summary_folder}/all_metrics_summary_{timestamp}.csv", index=False)

    # Create a heatmap of improvements across all scenarios and metrics
    plt.figure(figsize=(18, 12))  # Increased figure size
    pivot_df = summary_df.pivot(index='Metric', columns='Scenario', values='Improvement (%)')

    # Use a diverging colormap centered at 0
    cmap = sns.diverging_palette(240, 10, as_cmap=True)

    # Set a mask for NaN values
    mask = np.isnan(pivot_df.values)

    # Create the heatmap with better formatting
    sns.heatmap(pivot_df, annot=True, fmt=".1f", cmap=cmap, center=0,
                linewidths=.5, cbar_kws={"label": "Improvement %"}, mask=mask,
                square=False, annot_kws={'size': 8})

    plt.title('Multipath-NADA Improvement (%) Over Aggregated-NADA Across Different Scenarios',
              fontsize=14, pad=20)
    plt.xticks(rotation=45, ha='right', fontsize=10)
    plt.yticks(fontsize=10)
    plt.tight_layout()
    plt.savefig(f"{summary_folder}/improvement_heatmap_{timestamp}.png", dpi=300, bbox_inches='tight')
    plt.close()

    # Create a grouped bar chart comparing Multipath-NADA vs Aggregated-NADA across scenarios for key metrics
    for metric in ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Estimated MOS (1-5)',
                   'Buffer Length (ms)', 'Buffer Underruns', 'Delivery Efficiency (%)']:
        metric_data = summary_df[summary_df['Metric'] == metric]

        if metric_data.empty:
            print(f"No data for metric: {metric}")
            continue

        # SIGNIFICANTLY increased figure size and spacing for better text label visibility
        fig, ax = plt.subplots(figsize=(28, 12))  # Increased from (20, 10)

        # Calculate the positions of the bars with MUCH MORE spacing
        scenarios = metric_data['Scenario'].unique()
        x = np.arange(len(scenarios))
        width = 0.25  # Further reduced bar width from 0.35

        # SIGNIFICANTLY increase spacing between groups
        x_scaled = x * 2.5  # Increased from 1.5 to 2.5 for much more spacing

        # Create the grouped bars
        rects1 = ax.bar(x_scaled - width/2, metric_data['Multipath-NADA'].values, width,
                       label='Multipath-NADA', alpha=0.8)
        rects2 = ax.bar(x_scaled + width/2, metric_data['Aggregated-NADA'].values, width,
                       label='Aggregated-NADA', alpha=0.8)

        # Add labels and title
        ax.set_ylabel(metric, fontsize=14)  # Increased font size

        if metric in ['Buffer Length (ms)', 'Buffer Underruns']:
            ax.set_title(f'Buffer Performance: {metric} Across Different Scenarios',
                        fontsize=16, pad=25)  # Increased font size and padding
        elif metric == 'Delivery Efficiency (%)':
            ax.set_title(f'Delivery Performance: {metric} Across Different Scenarios',
                        fontsize=16, pad=25)
        else:
            ax.set_title(f'Comparison of {metric} Across Different Scenarios',
                        fontsize=16, pad=25)

        ax.set_xticks(x_scaled)
        ax.set_xticklabels(scenarios, rotation=45, ha='right', fontsize=11)  # Increased font size
        ax.legend(fontsize=12)  # Increased font size
        ax.grid(axis='y', alpha=0.3)

        # Add value labels on each bar with MUCH better positioning
        def autolabel(rects):
            for rect in rects:
                height = rect.get_height()
                if not np.isnan(height):
                    # SIGNIFICANTLY increased vertical offset and improved formatting
                    ax.annotate(f'{height:.2f}',
                                xy=(rect.get_x() + rect.get_width() / 2, height),
                                xytext=(0, 20),  # Increased from 10 to 20 for more space
                                textcoords="offset points",
                                ha='center', va='bottom',
                                fontsize=10,  # Increased font size
                                fontweight='bold',
                                bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8, edgecolor='gray'))

        autolabel(rects1)
        autolabel(rects2)

        # Adjust y-axis limits to accommodate the higher text labels
        y_min, y_max = ax.get_ylim()
        y_range = y_max - y_min
        ax.set_ylim(y_min, y_max + 0.3 * y_range)  # Increased from 0.2 to 0.3 for more space

        # Adjust layout and save with higher DPI
        fig.tight_layout()
        metric_name = metric.split('(')[0].strip().lower().replace(' ', '_')
        plt.savefig(f"{summary_folder}/{metric_name}_comparison_{timestamp}.png",
                   dpi=300, bbox_inches='tight', facecolor='white')
        plt.close(fig)

    # Average improvement across all metrics by scenario - HORIZONTAL BAR CHART with better spacing
    plt.figure(figsize=(16, 14))  # Increased height even more
    avg_improvement = summary_df.groupby('Scenario')['Improvement (%)'].mean().sort_values()

    # Create horizontal bar chart for better label readability with MORE spacing
    bars = avg_improvement.plot(kind='barh', color='green', figsize=(16, 14), alpha=0.7)
    plt.axvline(x=0, color='r', linestyle='-', alpha=0.7)
    plt.title('Average Improvement (%) by Scenario', fontsize=18, pad=25, fontweight='bold')  # Increased sizes
    plt.xlabel('Average Improvement (%)', fontsize=14)
    plt.ylabel('Scenario', fontsize=14)

    # Improve tick labels with larger fonts
    plt.yticks(fontsize=12)
    plt.xticks(fontsize=12)
    plt.grid(axis='x', alpha=0.3)

    # Add value labels with MUCH better positioning for horizontal bars
    for i, v in enumerate(avg_improvement):
        if not np.isnan(v):
            # SIGNIFICANTLY increased spacing and better formatting
            offset = max(abs(v) * 0.08, 3)  # Increased from 0.05 and 2
            text_x = v + offset if v >= 0 else v - offset
            plt.text(text_x, i, f"{v:.1f}%", va='center',
                    ha='left' if v >= 0 else 'right',
                    fontsize=12,  # Increased font size
                    fontweight='bold',
                    bbox=dict(boxstyle='round,pad=0.4', facecolor='white', alpha=0.9, edgecolor='gray'))

    # Adjust x-axis limits to accommodate the text labels with MORE space
    x_min, x_max = plt.xlim()
    x_range = x_max - x_min
    plt.xlim(x_min - 0.4 * x_range, x_max + 0.4 * x_range)  # Increased from 0.25 to 0.4

    plt.tight_layout()
    plt.savefig(f"{summary_folder}/average_improvement_by_scenario_{timestamp}.png",
               dpi=300, bbox_inches='tight', facecolor='white')
    plt.close()


def generate_strategy_comparison_visualizations(strategy_comparison_results):
    """Generate visualizations comparing different strategies for each base scenario."""
    strategy_folder = os.path.join(OUTPUT_DIR, "strategy_comparisons")
    os.makedirs(strategy_folder, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    for base_scenario, strategies in strategy_comparison_results.items():
        print(f"Generating strategy comparison for: {base_scenario}")

        # Create dataframe comparing all strategies for this base scenario
        strategy_data = []
        for strategy_name, (comparison_df, _) in strategies.items():
            for _, row in comparison_df.iterrows():
                strategy_data.append({
                    'Strategy': strategy_name,
                    'Metric': row['Metric'],
                    'Multipath-NADA': row['Multipath-NADA'],
                    'Aggregated-NADA': row['Aggregated-NADA'],
                    'Improvement (%)': row['Improvement (%)']
                })

        if not strategy_data:
            continue

        strategy_df = pd.DataFrame(strategy_data)

        # Save strategy comparison data
        clean_base_name = base_scenario.lower().replace(' ', '_').replace('/', '_').replace('+', 'plus').replace('(', '').replace(')', '')
        scenario_strategy_folder = os.path.join(strategy_folder, clean_base_name)
        os.makedirs(scenario_strategy_folder, exist_ok=True)

        # Save strategy comparison data
        strategy_csv_path = os.path.join(scenario_strategy_folder, f"{clean_base_name}_strategies_{timestamp}.csv")
        strategy_df.to_csv(strategy_csv_path, index=False)

        # Create visualizations for key metrics
        key_metrics = ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Estimated MOS (1-5)']

        for metric in key_metrics:
            metric_data = strategy_df[strategy_df['Metric'] == metric]
            if metric_data.empty:
                continue

            plt.figure(figsize=(12, 8))

            # Create grouped bar chart
            strategies_list = metric_data['Strategy'].unique()
            x = np.arange(len(strategies_list))
            width = 0.35

            multipath_values = metric_data['Multipath-NADA'].values
            simple_values = metric_data['Aggregated-NADA'].values

            fig, ax = plt.subplots(figsize=(12, 8))
            rects1 = ax.bar(x - width/2, multipath_values, width, label='Multipath-NADA')
            rects2 = ax.bar(x + width/2, simple_values, width, label='Aggregated-NADA')

            ax.set_xlabel('Path Selection Strategy')
            ax.set_ylabel(metric)
            ax.set_title(f'{metric} Comparison Across Strategies - {base_scenario}')
            ax.set_xticks(x)
            ax.set_xticklabels(strategies_list, rotation=45, ha='right')
            ax.legend()

            # Add value labels
            for rect in rects1:
                height = rect.get_height()
                ax.annotate(f'{height:.2f}',
                           xy=(rect.get_x() + rect.get_width() / 2, height),
                           xytext=(0, 3),
                           textcoords="offset points",
                           ha='center', va='bottom')

            for rect in rects2:
                height = rect.get_height()
                ax.annotate(f'{height:.2f}',
                           xy=(rect.get_x() + rect.get_width() / 2, height),
                           xytext=(0, 3),
                           textcoords="offset points",
                           ha='center', va='bottom')

            plt.tight_layout()
            metric_clean = metric.split('(')[0].strip().lower().replace(' ', '_')
            plt.savefig(f"{strategy_folder}/{clean_base_name}_{metric_clean}_strategies_{timestamp}.png")
            plt.close()

            plt.close('all')
            matplotlib.rcParams['figure.max_open_warning'] = 50

        # Create improvement comparison chart
        plt.figure(figsize=(12, 8))
        improvement_data = strategy_df.groupby('Strategy')['Improvement (%)'].mean().sort_values()

        bars = improvement_data.plot(kind='bar', color='green')
        plt.axhline(y=0, color='r', linestyle='-')
        plt.title(f'Average Improvement (%) by Strategy - {base_scenario}')
        plt.ylabel('Average Improvement (%)')
        plt.xlabel('Path Selection Strategy')
        plt.xticks(rotation=45, ha='right')
        plt.grid(axis='y')

        # Add value labels
        for i, v in enumerate(improvement_data):
            if not np.isnan(v):
                plt.text(i, v + (1 if v >= 0 else -1), f"{v:.1f}%", ha='center', va='bottom' if v >= 0 else 'top')

        plt.tight_layout()
        plt.savefig(f"{strategy_folder}/{clean_base_name}_strategy_improvements_{timestamp}.png")
        plt.close()

# Function to save raw simulation data
def save_raw_data(scenario_name, multipath_output, simple_output):
    """Save raw simulation output for reference"""
    scenario_folder = get_scenario_folder(scenario_name)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if multipath_output:
        with open(f"{scenario_folder}/tcp_multipath_raw_output_{timestamp}.txt", 'w') as f:
            f.write(multipath_output)

    if simple_output:
        with open(f"{scenario_folder}/tcp_simple_raw_output_{timestamp}.txt", 'w') as f:
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
        return False


def generate_combined_scenarios():
    """Generate all combinations of base scenarios with path selection strategies"""
    combined_scenarios = []

    for base_scenario in BASE_SIMULATION_SCENARIOS:
        for strategy in PATH_SELECTION_STRATEGIES:
            # Create a new scenario combining base scenario with strategy
            combined_name = f"{base_scenario['name']} - {strategy['name']}"
            combined_params = base_scenario['params'].copy()
            combined_params['pathSelectionStrategy'] = strategy['strategy']

            if strategy['name'] == "Frame Type Aware":
                combined_params['keyFrameInterval'] = 30

            combined_scenarios.append({
                "name": combined_name,
                "base_scenario": base_scenario['name'],
                "strategy": strategy['name'],
                "params": combined_params
            })

    return combined_scenarios

def get_scenario_folder(scenario_name):
    """Create and return path to scenario-specific folder"""
    # Replace problematic characters and create a clean folder name
    folder_name = scenario_name.lower().replace(' ', '_').replace('-', '_').replace('(', '').replace(')', '')
    folder_path = os.path.join(OUTPUT_DIR, folder_name)
    os.makedirs(folder_path, exist_ok=True)
    return folder_path

def generate_strategy_focused_summary_visualizations(all_results):
    """Generate summary visualizations with separate subcharts for each strategy."""
    # Create summary folder
    summary_folder = os.path.join(OUTPUT_DIR, "summary")
    os.makedirs(summary_folder, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Set matplotlib to handle memory better
    import matplotlib
    matplotlib.rcParams['figure.max_open_warning'] = 100

    # Organize data by strategy
    strategy_data = {}

    for scenario_name, (comparison_df, _) in all_results.items():
        # Check if comparison_df is empty or doesn't have the required columns
        if comparison_df.empty or 'Metric' not in comparison_df.columns:
            print(f"Warning: Skipping scenario '{scenario_name}' - no valid data or missing columns")
            continue

        # Extract strategy from scenario name (format: "Base Scenario - Strategy")
        if " - " in scenario_name:
            base_scenario, strategy = scenario_name.split(" - ", 1)
        else:
            print(f"Warning: Skipping scenario '{scenario_name}' - format doesn't match expected pattern")
            continue  # Skip if format doesn't match

        if strategy not in strategy_data:
            strategy_data[strategy] = {}

        if base_scenario not in strategy_data[strategy]:
            strategy_data[strategy][base_scenario] = comparison_df

    # Check if we have any valid data
    if not strategy_data:
        print("Warning: No valid strategy data found. Skipping strategy-focused visualizations.")
        return

    # Create visualizations for key metrics with better spacing
    key_metrics = ['Throughput (Mbps)', 'Delay (seconds)', 'Loss (%)', 'Estimated MOS (1-5)']

    for metric in key_metrics:
        print(f"Processing metric: {metric}")

        # Create a figure with subplots for each strategy - increased size
        fig, axes = plt.subplots(2, 3, figsize=(24, 16))  # Increased from (20, 12)
        fig.suptitle(f'{metric} Comparison: Multipath vs Single Path by Strategy',
                    fontsize=18, fontweight='bold', y=0.98)

        # Flatten axes for easier iteration
        axes_flat = axes.flatten()
        strategy_names = list(strategy_data.keys())

        for idx, strategy in enumerate(strategy_names):
            if idx >= len(axes_flat):
                break

            ax = axes_flat[idx]

            # Collect data for this strategy
            base_scenarios = []
            multipath_values = []
            simple_values = []

            for base_scenario, comparison_df in strategy_data[strategy].items():
                # Additional check for DataFrame validity
                if comparison_df.empty or 'Metric' not in comparison_df.columns:
                    continue

                metric_row = comparison_df[comparison_df['Metric'] == metric]
                if not metric_row.empty:
                    base_scenarios.append(base_scenario)
                    multipath_values.append(metric_row['Multipath-NADA'].iloc[0])
                    simple_values.append(metric_row['Aggregated-NADA'].iloc[0])

            if base_scenarios:
                # Increase spacing between bars
                x = np.arange(len(base_scenarios))
                width = 0.3  # Reduced bar width for more spacing
                spacing_factor = 1.2  # Add spacing between groups
                x_scaled = x * spacing_factor

                # Create bars with better spacing
                bars1 = ax.bar(x_scaled - width/2, multipath_values, width,
                              label='Multipath-NADA', color='#1f77b4', alpha=0.8)
                bars2 = ax.bar(x_scaled + width/2, simple_values, width,
                              label='Aggregated-NADA', color='#ff7f0e', alpha=0.8)

                # Customize subplot
                ax.set_title(f'{strategy}', fontweight='bold', pad=15, fontsize=14)
                ax.set_xlabel('Base Scenario', fontsize=11)
                ax.set_ylabel(metric, fontsize=11)
                ax.set_xticks(x_scaled)
                ax.set_xticklabels(base_scenarios, rotation=45, ha='right', fontsize=9)
                ax.legend(fontsize=10, loc='upper right')
                ax.grid(axis='y', alpha=0.3)

                # Add value labels on bars with better positioning
                def add_value_labels(bars):
                    for bar in bars:
                        height = bar.get_height()
                        if not np.isnan(height):
                            ax.annotate(f'{height:.2f}',
                                       xy=(bar.get_x() + bar.get_width() / 2, height),
                                       xytext=(0, 8),  # Increased offset
                                       textcoords="offset points",
                                       ha='center', va='bottom', fontsize=8, fontweight='bold')

                add_value_labels(bars1)
                add_value_labels(bars2)

                # Adjust y-limits for text spacing
                y_min, y_max = ax.get_ylim()
                y_range = y_max - y_min
                ax.set_ylim(y_min, y_max + 0.15 * y_range)

            else:
                ax.set_title(f'{strategy} (No Data)', fontweight='bold', fontsize=14)
                ax.text(0.5, 0.5, 'No data available',
                       ha='center', va='center', transform=ax.transAxes, fontsize=12)

        # Hide unused subplots
        for idx in range(len(strategy_names), len(axes_flat)):
            axes_flat[idx].set_visible(False)

        plt.tight_layout(rect=[0, 0.03, 1, 0.95])  # Adjust for suptitle
        metric_clean = metric.split('(')[0].strip().lower().replace(' ', '_')
        plt.savefig(f"{summary_folder}/strategy_comparison_{metric_clean}_{timestamp}.png",
                   dpi=300, bbox_inches='tight', facecolor='white')
        plt.close(fig)
        print(f"Saved strategy comparison for {metric}")

    # Create improvement percentage comparison by strategy with better spacing
    print("Creating improvement percentage comparison...")
    fig, axes = plt.subplots(2, 3, figsize=(24, 16))  # Increased size
    fig.suptitle('Average Improvement (%) by Strategy Across All Metrics',
                fontsize=18, fontweight='bold', y=0.98)

    axes_flat = axes.flatten()

    for idx, strategy in enumerate(strategy_names):
        if idx >= len(axes_flat):
            break

        ax = axes_flat[idx]

        # Calculate average improvement for each base scenario
        base_scenarios = []
        avg_improvements = []

        for base_scenario, comparison_df in strategy_data[strategy].items():
            # Additional check for DataFrame validity
            if comparison_df.empty or 'Improvement (%)' not in comparison_df.columns:
                continue

            # Calculate average improvement across all metrics for this base scenario
            improvements = comparison_df['Improvement (%)'].dropna()
            if not improvements.empty:
                base_scenarios.append(base_scenario)
                avg_improvements.append(improvements.mean())

        if base_scenarios:
            # Create horizontal bar chart with better spacing
            y_pos = np.arange(len(base_scenarios))
            y_scaled = y_pos * 1.2  # Add spacing between bars

            bars = ax.barh(y_scaled, avg_improvements,
                          height=0.6,  # Reduced bar height for spacing
                          color=['green' if x > 0 else 'red' for x in avg_improvements],
                          alpha=0.7)

            ax.set_title(f'{strategy}', fontweight='bold', pad=15, fontsize=14)
            ax.set_xlabel('Average Improvement (%)', fontsize=11)
            ax.set_yticks(y_scaled)
            ax.set_yticklabels(base_scenarios, fontsize=9)
            ax.axvline(x=0, color='black', linestyle='-', alpha=0.5)
            ax.grid(axis='x', alpha=0.3)

            # Add value labels with better positioning
            for i, (bar, val) in enumerate(zip(bars, avg_improvements)):
                if not np.isnan(val):
                    offset = max(abs(val) * 0.05, 1)  # Dynamic offset
                    text_x = val + offset if val >= 0 else val - offset
                    ax.text(text_x, y_scaled[i], f'{val:.1f}%',
                           va='center', ha='left' if val >= 0 else 'right',
                           fontsize=9, fontweight='bold',
                           bbox=dict(boxstyle='round,pad=0.2', facecolor='white', alpha=0.8))
        else:
            ax.set_title(f'{strategy} (No Data)', fontweight='bold', fontsize=14)
            ax.text(0.5, 0.5, 'No data available',
                   ha='center', va='center', transform=ax.transAxes, fontsize=12)

    # Hide unused subplots
    for idx in range(len(strategy_names), len(axes_flat)):
        axes_flat[idx].set_visible(False)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    plt.savefig(f"{summary_folder}/strategy_improvements_comparison_{timestamp}.png",
               dpi=300, bbox_inches='tight', facecolor='white')
    plt.close(fig)
    print("Saved strategy improvements comparison")

    # Create a comprehensive strategy ranking chart with better spacing
    print("Creating comprehensive strategy ranking...")
    plt.figure(figsize=(16, 10))

    # Calculate overall performance for each strategy
    strategy_performance = {}

    for strategy in strategy_names:
        all_improvements = []
        scenario_count = 0

        for base_scenario, comparison_df in strategy_data[strategy].items():
            # Additional check for DataFrame validity
            if comparison_df.empty or 'Improvement (%)' not in comparison_df.columns:
                continue

            improvements = comparison_df['Improvement (%)'].dropna()
            if not improvements.empty:
                all_improvements.extend(improvements.tolist())
                scenario_count += 1

        if all_improvements:
            strategy_performance[strategy] = {
                'avg_improvement': np.mean(all_improvements),
                'std_improvement': np.std(all_improvements),
                'scenario_count': scenario_count
            }

    # Sort strategies by average improvement
    sorted_strategies = sorted(strategy_performance.items(),
                              key=lambda x: x[1]['avg_improvement'], reverse=True)

    if sorted_strategies:
        strategies = [s[0] for s in sorted_strategies]
        avg_improvements = [s[1]['avg_improvement'] for s in sorted_strategies]
        std_improvements = [s[1]['std_improvement'] for s in sorted_strategies]

        # Create bar chart with error bars and better spacing
        x_pos = np.arange(len(strategies))
        x_scaled = x_pos * 1.3  # Increase spacing between bars

        bars = plt.bar(x_scaled, avg_improvements,
                      width=0.6,  # Reduced bar width
                      yerr=std_improvements, capsize=5,
                      color=['green' if x > 0 else 'red' for x in avg_improvements],
                      alpha=0.7)

        plt.title('Overall Strategy Performance Ranking\n(Average Improvement % with Standard Deviation)',
                 fontweight='bold', fontsize=16, pad=20)
        plt.xlabel('Path Selection Strategy', fontsize=12)
        plt.ylabel('Average Improvement (%)', fontsize=12)
        plt.xticks(x_scaled, strategies, rotation=45, ha='right', fontsize=11)
        plt.axhline(y=0, color='black', linestyle='-', alpha=0.5)
        plt.grid(axis='y', alpha=0.3)

        # Add value labels with better positioning
        for i, (bar, val, std) in enumerate(zip(bars, avg_improvements, std_improvements)):
            offset = std + max(abs(val) * 0.05, 2)
            text_y = val + offset if val >= 0 else val - offset
            plt.text(x_scaled[i], text_y, f'{val:.1f}%',
                    ha='center', va='bottom' if val >= 0 else 'top',
                    fontweight='bold', fontsize=11,
                    bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9))

        # Adjust y-limits
        y_min, y_max = plt.ylim()
        y_range = y_max - y_min
        plt.ylim(y_min - 0.1 * y_range, y_max + 0.15 * y_range)

        plt.tight_layout()
        plt.savefig(f"{summary_folder}/overall_strategy_ranking_{timestamp}.png",
                   dpi=300, bbox_inches='tight', facecolor='white')
        plt.close()
        print("Saved overall strategy ranking")
    else:
        print("Warning: No valid strategy performance data for ranking chart")
        plt.close()


def main():
    print("=" * 80)
    print(f"Starting TCP NADA comparison with output to: {os.path.abspath(OUTPUT_DIR)}")
    print(f"Total scenarios to process: {len(SIMULATION_SCENARIOS)}")
    print("=" * 80)

    # Store all results for summary visualization
    all_results = {}
    strategy_comparison_results = {}

    # Run all scenarios
    for i, scenario in enumerate(SIMULATION_SCENARIOS, 1):
        scenario_name = scenario["name"]
        base_scenario = scenario["base_scenario"]
        strategy = scenario["strategy"]

        print(f"\n[{i}/{len(SIMULATION_SCENARIOS)}] " + "=" * 50)
        print(f"Processing scenario: {scenario_name}")
        print(f"Base scenario: {base_scenario}")
        print(f"Strategy: {strategy}")
        print("=" * 50)

        # Run Multipath-NADA-nada simulation
        multipath_output = run_simulation("scratch/strategy-mp", scenario["params"])

        # Run Aggregated-NADA-nada simulation with same parameters (excluding strategy)
        simple_params = scenario["params"].copy()
        simple_params.pop('pathSelectionStrategy', None)  # Remove strategy for simple TCP
        simple_output = run_simulation("scratch/simple-nada", simple_params)

        # Save raw outputs
        save_raw_data(scenario_name, multipath_output, simple_output)

        # Parse outputs
        multipath_stats, simple_stats = {}, {}
        if multipath_output and simple_output:
            multipath_stats = parse_output(multipath_output)
            simple_stats = parse_output(simple_output)

        # Analyze results
        comparison_df, path_df = analyze_results(multipath_stats, simple_stats)

        # Store results for summary
        all_results[scenario_name] = (comparison_df, path_df)

        # Store results grouped by base scenario for strategy comparison
        if base_scenario not in strategy_comparison_results:
            strategy_comparison_results[base_scenario] = {}
        strategy_comparison_results[base_scenario][strategy] = (comparison_df, path_df)

        # Generate scenario-specific visualizations
        generate_visualizations(comparison_df, path_df, scenario_name)

        print(f"✅ Completed processing scenario: {scenario_name}")

    # Generate summary visualizations across all scenarios
    print("\n" + "=" * 50)
    print("Generating summary visualizations")
    print("=" * 50)
    generate_summary_visualizations(all_results)

    print("\n" + "=" * 50)
    print("Generating strategy-focused summary visualizations")
    print("=" * 50)
    generate_strategy_focused_summary_visualizations(all_results)

    # Generate strategy comparison visualizations
    print("\n" + "=" * 50)
    print("Generating strategy comparison visualizations")
    print("=" * 50)
    generate_strategy_comparison_visualizations(strategy_comparison_results)

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
    print("\n" + "="*50)
    print("NS-3 TCP NADA MULTIPATH COMPARISON TOOL")
    print("="*50)

    import sys
    if len(sys.argv) > 1:
        if sys.argv[1] == "--fast":
            print("🚀 FAST MODE: Applying optimizations for quick comparative testing")
            apply_fast_mode_optimizations()
        elif sys.argv[1] == "--quick":
            print("⚡ QUICK MODE: Using subset of scenarios")
            # Use only first 5 scenarios for quick testing
            BASE_SIMULATION_SCENARIOS = BASE_SIMULATION_SCENARIOS[1:2]
            apply_fast_mode_optimizations()

    SIMULATION_SCENARIOS = generate_combined_scenarios()

    # Check libraries
    import matplotlib
    print(f"Matplotlib version: {matplotlib.__version__}")
    print(f"Pandas version: {pd.__version__}")
    print(f"NumPy version: {np.__version__}")
    print(f"Seaborn version: {sns.__version__}")

    # Check output directories
    print("\nChecking directories:")
    check_directory_permissions(OUTPUT_DIR)

    # Check scenario directories (just check first few)
    for scenario in SIMULATION_SCENARIOS[:2]:
        scenario_folder = get_scenario_folder(scenario["name"])
        check_directory_permissions(scenario_folder)

    print(f"\nTotal scenarios to run: {len(SIMULATION_SCENARIOS)}")
    print(f"Base scenarios: {len(BASE_SIMULATION_SCENARIOS)}")
    print(f"Path selection strategies: {len(PATH_SELECTION_STRATEGIES)}")

    # Run the main function with comprehensive error handling
    try:
        main()
    except Exception as e:
        import traceback
        print(f"\n❌ ERROR in main execution: {e}")
        traceback.print_exc()
        sys.exit(1)
