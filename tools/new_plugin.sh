#!/bin/bash
set -e

NAME="$1"
if [ -z "$NAME" ]; then
  echo "Usage: $0 PluginName"
  exit 1
fi

TARGET="plugins/$NAME"
if [ -d "$TARGET" ]; then
  echo "Plugin '$NAME' already exists."
  exit 1
fi

cp -r plugins/_template "$TARGET"

find "$TARGET" -type f | while read -r file; do
  sed -i "s/TemplatePlugin/$NAME/g" "$file"
done

echo "Created $TARGET"
echo "Add it to the root CMakeLists.txt: add_subdirectory(plugins/$NAME)"
