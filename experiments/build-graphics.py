import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
from datetime import datetime
import os

def create_enhanced_metric_chart(df, metric, output_dir, timestamp):
    """Create an enhanced chart for a specific metric"""

    # Filter data for the specific metric
    metric_data = df[df['Metric'] == metric].copy()

    if metric_data.empty:
        print(f"❌ No data found for metric: {metric}")
        return None

    # Sort scenarios by Multipath-NADA value for better visualization
    metric_data = metric_data.sort_values('Multipath-NADA', ascending=True)

    # Create the plot with enhanced styling
    fig, ax = plt.subplots(figsize=(32, 14))  # Increased width to accommodate larger spacing

    scenarios = metric_data['Scenario']
    x = np.arange(len(scenarios))
    width = 40

    x_scaled = x * 200

    bar_spacing = 50.0

    # Choose colors based on metric type
    if 'Buffer' in metric or 'Loss' in metric or 'Delay' in metric or 'Jitter' in metric:
        # For metrics where lower is better
        colors = ['#2E86AB', '#E63946']  # Blue for multipath, red for aggregated
    else:
        # For metrics where higher is better
        colors = ['#2E86AB', '#F77F00']  # Blue for multipath, orange for aggregated

    # Create bars with enhanced styling
    bars1 = ax.bar(x_scaled - bar_spacing/2, metric_data['Multipath-NADA'], width,
                  label='Multipath-NADA', color=colors[0], alpha=0.8,
                  edgecolor='black', linewidth=1)
    bars2 = ax.bar(x_scaled + bar_spacing/2, metric_data['Aggregated-NADA'], width,
                  label='Aggregated-NADA', color=colors[1], alpha=0.8,
                  edgecolor='black', linewidth=1)

    # Enhanced styling
    ax.set_xlabel('Network Scenarios', fontsize=14, fontweight='bold')
    ax.set_ylabel(metric, fontsize=18, fontweight='bold')

    # Create a more descriptive title
    improvement_avg = metric_data['Improvement (%)'].mean()
    if not np.isnan(improvement_avg):
        title = f'{metric} Comparison Across Network Scenarios\n(Average Improvement: {improvement_avg:.1f}%)'
    else:
        title = f'{metric} Comparison Across Network Scenarios'

    ax.set_title(title, fontsize=22, fontweight='bold', pad=35)

    ax.set_xticks(x_scaled)
    ax.set_xticklabels(scenarios, rotation=45, ha='right', fontsize=13, fontweight='bold')
    ax.legend(fontsize=16, loc='upper right', framealpha=0.9)

    # Enhanced grid
    ax.grid(axis='y', alpha=0.3, linestyle='--', linewidth=0.8)
    ax.set_axisbelow(True)

    # Add value labels on bars with better formatting
    def add_enhanced_value_labels(bars, values, format_str):
        for bar, val in zip(bars, values):
            if not np.isnan(val):
                height = bar.get_height()
                # Position labels slightly above bars
                label_y = height + (ax.get_ylim()[1] - ax.get_ylim()[0]) * 0.01

                ax.annotate(format_str.format(val),
                           xy=(bar.get_x() + bar.get_width() / 2, label_y),
                           xytext=(0, 5),  # Increased offset
                           textcoords="offset points",
                           ha='center', va='bottom',
                           fontsize=9, fontweight='bold',  # Slightly larger font
                           bbox=dict(boxstyle='round,pad=0.1',  # Increased padding
                                   facecolor='white',
                                   edgecolor='gray',
                                   alpha=0.9))

    # Format labels based on metric type
    if 'MOS' in metric:
        format_str = '{:.2f}'
    elif '%' in metric:
        format_str = '{:.1f}%'
    elif 'seconds' in metric:
        if metric_data['Multipath-NADA'].max() < 1:
            format_str = '{:.4f}s'
        else:
            format_str = '{:.2f}s'
    elif 'ms' in metric:
        format_str = '{:.1f}ms'
    elif 'Mbps' in metric:
        format_str = '{:.1f}'
    else:
        format_str = '{:.2f}'

    add_enhanced_value_labels(bars1, metric_data['Multipath-NADA'], format_str)
    add_enhanced_value_labels(bars2, metric_data['Aggregated-NADA'], format_str)

    # Add improvement indicators with better positioning
    for i, (x_pos, improvement) in enumerate(zip(x_scaled, metric_data['Improvement (%)'])):
        if not np.isnan(improvement):
            # Position improvement text between the bars
            y_max = max(metric_data['Multipath-NADA'].iloc[i], metric_data['Aggregated-NADA'].iloc[i])
            y_pos = y_max + (ax.get_ylim()[1] - ax.get_ylim()[0]) * 0.08  # Increased offset

            color = 'green' if improvement > 0 else 'red'
            symbol = '↑' if improvement > 0 else '↓'

            ax.text(x_pos, y_pos, f'{symbol} {improvement:.1f}%',
                   ha='center', va='bottom', fontsize=8, fontweight='bold',
                   color=color,
                   bbox=dict(boxstyle='round,pad=0.2', facecolor=color, alpha=0.2))

    # Adjust y-axis to accommodate labels
    y_min, y_max = ax.get_ylim()
    y_range = y_max - y_min
    ax.set_ylim(y_min, y_max + 0.20 * y_range)  # Increased from 0.15 to 0.20

    # Enhanced layout with more padding
    plt.tight_layout(pad=3.0)

    # Save with high quality
    safe_metric_name = metric.replace(' ', '_').replace('(', '').replace(')', '').replace('%', 'pct').replace('/', '_')
    save_path = f"{output_dir}/enhanced_{safe_metric_name}_{timestamp}.png"
    plt.savefig(save_path, dpi=300, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"✅ Generated enhanced chart for {metric}: {save_path}")
    return save_path

def create_metric_summary_dashboard(df, output_dir, timestamp):
    """Create a comprehensive dashboard showing all metrics"""

    # Get all unique metrics
    metrics = df['Metric'].unique()

    # Create a large dashboard figure
    fig = plt.figure(figsize=(50, 28))  # Increased size for better spacing

    # Calculate grid layout
    n_metrics = len(metrics)
    cols = 4
    rows = (n_metrics + cols - 1) // cols

    fig.suptitle('Comprehensive Network Performance Metrics Dashboard\nMultipath-NADA vs Aggregated-NADA Comparison',
                fontsize=26, fontweight='bold', y=0.98)

    for idx, metric in enumerate(metrics):
        ax = plt.subplot(rows, cols, idx + 1)

        # Get data for this metric
        metric_data = df[df['Metric'] == metric]

        if not metric_data.empty:
            # Calculate average values
            mp_avg = metric_data['Multipath-NADA'].mean()
            agg_avg = metric_data['Aggregated-NADA'].mean()

            # Create simple bar chart for dashboard with more spacing
            x_pos = [0, 2]  # Increased spacing between bars
            bars = ax.bar(x_pos, [mp_avg, agg_avg],
                         width=1.5,  # Slightly wider bars
                         color=['#2E86AB', '#F77F00'], alpha=0.8)

            ax.set_title(metric, fontsize=10, fontweight='bold', pad=15)
            ax.grid(axis='y', alpha=0.3)
            ax.set_xticks(x_pos)
            ax.set_xticklabels(['Multipath-NADA', 'Aggregated-NADA'], fontsize=11)

            # Add value labels
            for bar in bars:
                height = bar.get_height()
                if not np.isnan(height):
                    if 'seconds' in metric and height < 1:
                        label = f'{height:.4f}'
                    elif '%' in metric:
                        label = f'{height:.1f}%'
                    else:
                        label = f'{height:.2f}'

                    ax.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                           label, ha='center', va='bottom', fontsize=10, fontweight='bold')

            # Calculate and show improvement
            improvement = ((mp_avg - agg_avg) / agg_avg * 100) if agg_avg != 0 else 0
            if not np.isnan(improvement):
                color = 'green' if improvement > 0 else 'red'
                ax.text(0.5, 0.95, f'Δ {improvement:.1f}%', transform=ax.transAxes,
                       ha='center', va='top', fontsize=10, fontweight='bold', color=color)

        ax.tick_params(axis='x', labelsize=10)
        ax.tick_params(axis='y', labelsize=10)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95], pad=2.0)  # Increased padding

    # Save dashboard
    dashboard_path = f"{output_dir}/metrics_dashboard_{timestamp}.png"
    plt.savefig(dashboard_path, dpi=300, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"✅ Generated metrics dashboard: {dashboard_path}")
    return dashboard_path

def main():
    """Generate enhanced charts for all specified metrics"""

    # Load the data
    csv_path = "/Users/dani/Desktop/Coding/uni/ns-allinone-3.44/ns-3.44/results/comparison/all_scenarios_summary_20250611_155827.csv"

    try:
        df = pd.read_csv(csv_path)
        print(f"✅ Successfully loaded CSV with {len(df)} rows")
    except Exception as e:
        print(f"❌ Error loading CSV: {e}")
        return

    # Create output directory
    output_dir = "/Users/dani/Desktop/Coding/uni/ns-allinone-3.44/ns-3.44/results/enhanced_metrics_charts"
    os.makedirs(output_dir, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Specified metrics to generate charts for
    target_metrics = [
        "Buffer Length (ms)",
        "Buffer Underruns",
        "Delay (seconds)",
        "Delivery Efficiency (%)",
        "Estimated MOS (1-5)",
        "Jitter (seconds)",
        "Loss (%)",
        "Path Utilization Ratio (%)",
        "Throughput (Mbps)",
        "Throughput Stability (stddev)"
    ]

    print(f"\n🎨 Generating enhanced charts for {len(target_metrics)} metrics...")
    print(f"📁 Output directory: {output_dir}")

    generated_charts = []

    # Generate enhanced charts for each metric
    for i, metric in enumerate(target_metrics, 1):
        print(f"\n[{i}/{len(target_metrics)}] Processing: {metric}")

        if metric in df['Metric'].values:
            chart_path = create_enhanced_metric_chart(df, metric, output_dir, timestamp)
            if chart_path:
                generated_charts.append(chart_path)
        else:
            print(f"⚠️  Metric '{metric}' not found in data")
            print(f"Available metrics: {sorted(df['Metric'].unique())}")

    # Generate comprehensive dashboard
    print(f"\n📊 Creating comprehensive metrics dashboard...")
    dashboard_path = create_metric_summary_dashboard(df, output_dir, timestamp)

    # Generate summary report
    print(f"\n📋 Generating summary report...")

    summary_stats = {}
    for metric in target_metrics:
        if metric in df['Metric'].values:
            metric_data = df[df['Metric'] == metric]
            avg_improvement = metric_data['Improvement (%)'].mean()
            max_improvement = metric_data['Improvement (%)'].max()
            min_improvement = metric_data['Improvement (%)'].min()

            summary_stats[metric] = {
                'avg_improvement': avg_improvement,
                'max_improvement': max_improvement,
                'min_improvement': min_improvement,
                'scenarios_count': len(metric_data)
            }

    # Save summary report
    report_path = f"{output_dir}/metrics_summary_report_{timestamp}.txt"
    with open(report_path, 'w') as f:
        f.write("ENHANCED METRICS ANALYSIS REPORT\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"Generated on: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Total charts generated: {len(generated_charts)}\n")
        f.write(f"Dashboard generated: {dashboard_path}\n\n")

        f.write("METRIC IMPROVEMENT SUMMARY:\n")
        f.write("-" * 30 + "\n")

        for metric, stats in summary_stats.items():
            f.write(f"\n{metric}:\n")
            f.write(f"  Average Improvement: {stats['avg_improvement']:.2f}%\n")
            f.write(f"  Best Case: {stats['max_improvement']:.2f}%\n")
            f.write(f"  Worst Case: {stats['min_improvement']:.2f}%\n")
            f.write(f"  Scenarios Tested: {stats['scenarios_count']}\n")

        f.write(f"\nGENERATED CHARTS:\n")
        f.write("-" * 15 + "\n")
        for chart in generated_charts:
            f.write(f"  {os.path.basename(chart)}\n")

    print(f"\n✅ COMPLETE! Generated {len(generated_charts)} enhanced charts")
    print(f"All files saved to: {os.path.abspath(output_dir)}")
    print(f"Summary report: {report_path}")
    print(f"Dashboard: {dashboard_path}")

    # Print quick summary
    print(f"\nQUICK SUMMARY:")
    print(f"Top performing metrics (by average improvement):")

    top_metrics = sorted(summary_stats.items(),
                        key=lambda x: x[1]['avg_improvement'],
                        reverse=True)[:3]

    for i, (metric, stats) in enumerate(top_metrics, 1):
        print(f"  {i}. {metric}: {stats['avg_improvement']:.1f}% improvement")

if __name__ == "__main__":
    main()
