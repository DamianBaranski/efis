#!/bin/bash
## Export each Inkscape layer of an SVG to its own PNG.
## \param SVG_FILE Source drawing. One argument is required.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <SVG_FILE>"
    exit 1
fi

# The SVG file to be processed (input argument)
SVG_FILE="$1"

# Extract the base filename without the extension
FILENAME=$(basename -- "$SVG_FILE")
OUTPUT_DIR="./${FILENAME%.*}"
mkdir -p ${OUTPUT_DIR}

# Extract layer names using Inkscape and loop through them
inkscape --query-all "$SVG_FILE" | grep layer | cut -d, -f1 | while read -r layer_name; do
    echo "Exporting layer: $layer_name"
    inkscape --export-id="$layer_name" \
             --export-id-only \
             --export-type="png" \
             --export-filename="$OUTPUT_DIR/${layer_name}.png" \
             "$SVG_FILE"
done

echo "All layers have been exported."
