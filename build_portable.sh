#!/bin/bash
set -e

echo "Starting build process..."
mkdir -p build && cd build

# Build the project
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja

echo "Building portable version..."
cd ..
PORTABLE_DIR="SharpMark_Portable"
mkdir -p "$PORTABLE_DIR"

# 1. Copy the executable
cp build/SharpMark.exe "$PORTABLE_DIR/"

# 2. Prepare directories for GTK3 resources
echo "Copying GTK3 resources..."
MSYS_UCRT_ROOT="/c/msys64/ucrt64"

mkdir -p "$PORTABLE_DIR/share/glib-2.0/schemas"
cp -r "$MSYS_UCRT_ROOT/share/glib-2.0/schemas/"* "$PORTABLE_DIR/share/glib-2.0/schemas/"

mkdir -p "$PORTABLE_DIR/share/icons"
cp -r "$MSYS_UCRT_ROOT/share/icons/Adwaita" "$PORTABLE_DIR/share/icons/"
cp -r "$MSYS_UCRT_ROOT/share/icons/hicolor" "$PORTABLE_DIR/share/icons/"

echo "Copying application icons..."
for size in 512x512 256x256 32x32 16x16; do
    APP_ICON_DIR="$PORTABLE_DIR/share/icons/hicolor/$size/apps"
    mkdir -p "$APP_ICON_DIR"
    if [ -f "assets/icons/$size/icon.png" ]; then
        cp "assets/icons/$size/icon.png" "$APP_ICON_DIR/"
        echo " - Copied $size icon"
    else
        echo " - Warning: assets/icons/$size/icon.png not found, skipping."
    fi
done

mkdir -p "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$MSYS_UCRT_ROOT/lib/gdk-pixbuf-2.0/2.10.0/loaders/"*.dll "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders/"

# 3. Copy DLL dependencies (for both the executable and plugins)
echo "Copying dependencies..."

# Create a temporary file with a list of everything that requires DLLs
TMP_LIST="dll_targets.txt"
echo "$PORTABLE_DIR/SharpMark.exe" > "$TMP_LIST"
find "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders" -name "*.dll" >> "$TMP_LIST"

# Gather all required DLLs (filtering for ucrt64 libraries to avoid copying Windows system DLLs)
cat "$TMP_LIST" | xargs ldd | grep -i "ucrt64" | awk '{print $3}' | sort | uniq | xargs -I '{}' cp '{}' "$PORTABLE_DIR/"
rm "$TMP_LIST"

# 4. Recreate loaders.cache with relative paths
echo "Generating local loaders.cache..."
cd "$PORTABLE_DIR"

# Temporarily copy the cache generator to the portable folder (will be deleted later)
cp "$MSYS_UCRT_ROOT/bin/gdk-pixbuf-query-loaders.exe" ./

# Generate cache. Running it locally forces it to read the local DLLs in the lib/ folder
./gdk-pixbuf-query-loaders.exe lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll > lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

# Remove the generator, it is no longer needed
rm gdk-pixbuf-query-loaders.exe

# Change absolute paths to relative ones in the generated cache (relative to the program root)
sed -i 's|lib/gdk-pixbuf|../lib/gdk-pixbuf|g' lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

echo "Done! Portable app is ready in '$PORTABLE_DIR'"