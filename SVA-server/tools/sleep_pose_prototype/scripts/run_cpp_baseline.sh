#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "Usage: $0 <PoseSmokeTest> <pose.onnx> <input-dir> <output-dir>" >&2
  exit 2
fi

smoke_test=$1
model_path=$2
input_dir=$3
output_dir=$4

if [[ ! -x "$smoke_test" ]]; then
  echo "PoseSmokeTest is not executable: $smoke_test" >&2
  exit 3
fi
if [[ ! -f "$model_path" ]]; then
  echo "Pose model does not exist: $model_path" >&2
  exit 4
fi
if [[ ! -d "$input_dir" ]]; then
  echo "Input directory does not exist: $input_dir" >&2
  exit 5
fi

mkdir -p "$output_dir"
summary_path="$output_dir/cpp_baseline_summary.txt"
: > "$summary_path"

shopt -s nullglob
inputs=("$input_dir"/*.mp4)
if [[ ${#inputs[@]} -eq 0 ]]; then
  echo "No MP4 files found in: $input_dir" >&2
  exit 6
fi

for input_path in "${inputs[@]}"; do
  filename=$(basename "$input_path")
  sample_name=${filename%.*}
  output_path="$output_dir/${sample_name}_cpp.mp4"
  features_path="$output_dir/${sample_name}_cpp_features.csv"
  echo "sample=$filename" | tee -a "$summary_path"
  "$smoke_test" "$model_path" "$input_path" "$output_path" "$features_path" | tee -a "$summary_path"
done

echo "summary=$summary_path"
