#!/bin/bash
set -e
exec python3 gb28181_device_simulator.py \
  --platform-host "${PLATFORM_HOST:-media}" \
  --platform-port "${PLATFORM_PORT:-5060}" \
  --platform-id "${PLATFORM_ID:-34020000002000000001}" \
  --realm "${REALM:-3402000000}" \
  --device-id "${DEVICE_ID:-34020000001320000001}" \
  --channel-id "${CHANNEL_ID:-34020000001320000002}" \
  --channel-name "${CHANNEL_NAME:-模拟摄像头-睡岗}" \
  --password "${DEVICE_PASSWORD:-12345678}" \
  --device-port "${DEVICE_PORT:-15060}" \
  --heartbeat-interval "${HEARTBEAT_INTERVAL:-15}" \
  --input "${INPUT_VIDEO:-/app/sleep_source.mp4}"
