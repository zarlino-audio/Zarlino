#!/bin/bash
set -e

PLUGIN_PATH="$1"
if [ -z "$PLUGIN_PATH" ]; then
  echo "Usage: $0 /path/to/YourPlugin.vst3"
  exit 1
fi

if [ ! -d "$PLUGIN_PATH" ] && [ ! -f "$PLUGIN_PATH" ]; then
  echo "Plugin path does not exist: $PLUGIN_PATH"
  exit 1
fi

INSTALL_DIR="/c/Program Files/Common Files/VST3"
if [ ! -d "$INSTALL_DIR" ]; then
  INSTALL_DIR="/mnt/c/Program Files/Common Files/VST3"
fi

if [ ! -d "$INSTALL_DIR" ]; then
  echo "Could not find the system VST3 folder. Install manually to C:\\Program Files\\Common Files\\VST3"
  exit 1
fi

cp -r "$PLUGIN_PATH" "$INSTALL_DIR/"

echo "Installed $PLUGIN_PATH to $INSTALL_DIR"
