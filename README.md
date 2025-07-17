# GStreamer Rerun Sink

A high-performance GStreamer video sink element that logs frames into [Rerun](https://rerun.io) for real-time visualization and analysis.

## Features

- **Multiple Format Support**: 
  - Raw formats: NV12, I420, RGB, GRAY8, RGBA
  - Encoded formats: H.264, H.265 (structure in place, logging coming soon)
- **NVIDIA NVMM Support** (optional): Zero-copy processing for GPU memory buffers
- **Efficient Processing**: Optimized buffer handling for both CPU and GPU memory
- **Flexible Output Options**:
  - Spawn local Rerun viewer (default)
  - Save recordings to disk (.rrd files)
  - Connect to remote viewers via gRPC
- **Smart Mode Selection**: Output mode automatically determined by properties
- **Clean Architecture**: Modular code structure with separate handlers for different memory types

## Documentation

The official user documentation is held at [RidgeRun's DevelopersWiki](https://developer.ridgerun.com/wiki/index.php/GstRerunSink)

## Prerequisites

### Required
- CMake 3.16 or higher
- GStreamer 1.0 development libraries
- C++14 compatible compiler
- Rerun SDK (automatically downloaded during build)

### Optional (for NVMM support)
- CUDA Toolkit (installed at `/usr/local/cuda`)
- NVIDIA DeepStream SDK 6.3 (installed at `/opt/nvidia/deepstream/deepstream-6.3`)

## Building

### Quick Start

Use the provided build script for easy compilation:

```bash
# Basic build (CPU support only)
./build.sh

# Build with NVMM support
./build.sh --nvmm

# Clean build with installation
./build.sh --clean --install

# Debug build with verbose output
./build.sh --debug --verbose
```

### Manual Build

#### Without NVMM Support
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

#### With NVMM Support
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DWITH_NVMM_SUPPORT=ON ..
make -j$(nproc)
```

### Build Options

| Option | Description | Default |
|--------|-------------|---------|
| `WITH_NVMM_SUPPORT` | Enable NVIDIA GPU memory support | OFF |
| `CMAKE_BUILD_TYPE` | Build configuration (Debug/Release) | Release |

## Installation

```bash
# Install to system GStreamer plugin directory
cd build
sudo make install

# Verify installation
gst-inspect-1.0 rerunsink
```

## Usage

The sink automatically selects the output mode based on which properties are set:
- If `output-file` is set → Save to disk mode
- If `grpc-address` is set to non-default value → gRPC connection mode
- Otherwise → Spawn local viewer (controlled by `spawn-viewer` property)

**Note**: You cannot use multiple output methods simultaneously. Setting both `output-file` and a custom `grpc-address` will result in an error.

### Basic Examples

#### Default: Spawn Local Viewer
```bash
# Test pattern visualization (spawns viewer by default)
gst-launch-1.0 videotestsrc ! \
    video/x-raw,format=RGB,width=640,height=480 ! \
    rerunsink recording-id="test" image-path="camera/image"

# Disable viewer spawning
gst-launch-1.0 videotestsrc ! \
    video/x-raw,format=RGB ! \
    rerunsink recording-id="test" image-path="camera/image" spawn-viewer=false
```

#### Save to Disk
```bash
# Setting output-file automatically enables disk saving mode
gst-launch-1.0 videotestsrc num-buffers=300 ! \
    video/x-raw,format=RGB,width=640,height=480 ! \
    rerunsink recording-id="test" image-path="camera/image" \
    output-file="recording.rrd"

# Record webcam to file with timestamp
gst-launch-1.0 v4l2src num-buffers=600 ! \
    video/x-raw,format=YUY2 ! videoconvert ! \
    video/x-raw,format=RGB ! \
    rerunsink recording-id="webcam-recording" image-path="camera/front" \
    output-file="/tmp/webcam_$(date +%Y%m%d_%H%M%S).rrd"
```

#### Connect to Remote Viewer
```bash
# Setting a custom grpc-address automatically enables gRPC mode
gst-launch-1.0 videotestsrc ! \
    video/x-raw,format=RGB ! \
    rerunsink recording-id="remote-test" image-path="camera/image" \
    grpc-address="grpc://192.168.1.100:9876"

# Connect to local viewer on custom port
gst-launch-1.0 videotestsrc ! \
    video/x-raw,format=RGB ! \
    rerunsink recording-id="test" image-path="camera/image" \
    grpc-address="grpc://127.0.0.1:9090"
```

### NVMM Examples (NVIDIA Jetson/GPU)

```bash
# NVIDIA camera source with hardware acceleration, save to disk
gst-launch-1.0 nvv4l2camerasrc num-buffers=1800 ! \
    'video/x-raw(memory:NVMM),format=NV12,width=1920,height=1080' ! \
    rerunsink recording-id="jetson-camera" image-path="camera/main" \
    output-file="jetson_capture.rrd"

# Hardware-accelerated decode with remote viewer
gst-launch-1.0 filesrc location=video.mp4 ! \
    qtdemux ! h264parse ! nvv4l2decoder ! \
    'video/x-raw(memory:NVMM),format=NV12' ! \
    rerunsink recording-id="hw-decode" image-path="video/decoded" \
    grpc-address="grpc://0.0.0.5:9876"
```

### Encoded Video Examples (Future Support)

These pipelines demonstrate H.264/H.265 support (currently no-op, logging coming soon):

```bash
# H.264 encoded stream (structure ready, logging coming soon)
gst-launch-1.0 videotestsrc ! x264enc ! h264parse ! \
    rerunsink recording-id="h264-test" image-path="video/encoded"

# H.265 encoded stream from file
gst-launch-1.0 filesrc location=video.mp4 ! \
    qtdemux ! h265parse ! \
    rerunsink recording-id="h265-test" image-path="video/encoded"

# Camera to H.264 with tee for preview and recording
gst-launch-1.0 v4l2src ! videoconvert ! \
    x264enc ! h264parse ! tee name=t \
    t. ! queue ! rerunsink recording-id="camera-h264" image-path="camera/encoded" \
    t. ! queue ! mp4mux ! filesink location=output.mp4
```

**Note**: H.264/H.265 logging is not yet implemented in Rerun. The sink accepts these formats and processes them as a no-op to maintain pipeline compatibility. Full support will be enabled when Rerun adds encoded video capabilities.

### Properties

| Property | Type | Description | Default |
|----------|------|-------------|---------|
| `recording-id` | string | Rerun recording/session identifier | "my_gst_element" |
| `image-path` | string | Entity path for logging images | NULL (required) |
| `spawn-viewer` | boolean | Spawn a local Rerun viewer (only if no output-file and default grpc-address) | true |
| `output-file` | string | Path to output .rrd file (if set, saves to disk) | NULL |
| `grpc-address` | string | gRPC server address (if non-default, connects via gRPC) | "127.0.0.1:9876" |

## Output Mode Selection Logic

The sink automatically determines the output mode:

```
if output-file is set:
    → Save to disk mode
elif grpc-address != "127.0.0.1:9876":
    → Connect to gRPC mode  
elif spawn-viewer == true:
    → Spawn local viewer
else:
    → No output (valid for custom implementations)
```

## Architecture

The plugin follows a clean, modular architecture:

```
┌─────────────────────┐
│  GstRerunSink       │
├─────────────────────┤
│ Output Mode Logic:  │
│                     │
│ output-file? ──────►│ Save to Disk
│                     │
│ custom grpc? ──────►│ Connect gRPC
│                     │
│ spawn-viewer? ─────►│ Spawn Viewer
├─────────────────────┤
│ render()            │──┬──> Regular Memory Handler
│                     │  │      └─> RGB/RGBA/GRAY8/NV12/I420
│                     │  │
│                     │  └──> NVMM Memory Handler (if enabled)
│                     │         └─> NV12 (GPU memory)
├─────────────────────┤
│ Properties          │
│ - recording-id      │
│ - image-path        │
│ - spawn-viewer      │
│ - output-file       │
│ - grpc-address      │
└─────────────────────┘
```

## Output Methods

### 1. Spawn Viewer (Default)
- Automatically spawns a local Rerun viewer
- Best for interactive development and debugging
- Active when no output-file is set and grpc-address is default

### 2. Save to Disk
- Records directly to `.rrd` files
- Ideal for headless systems or batch processing
- Automatically active when output-file is set
- View recordings later: `rerun recording.rrd`

### 3. Connect gRPC
- Streams to a remote Rerun viewer
- Perfect for distributed systems or remote monitoring
- Automatically active when grpc-address is changed from default
- Start remote viewer with: `rerun --serve`

## Supported Formats

### Raw Video Formats
- **RGB**: 24-bit RGB
- **RGBA**: 32-bit RGBA with alpha
- **GRAY8**: 8-bit grayscale
- **NV12**: YUV 4:2:0 semi-planar
- **I420**: YUV 4:2:0 planar

### Encoded Video Formats (Structure Ready)
- **H.264**: AVC/byte-stream format (logging coming soon)
- **H.265**: HEVC/byte-stream format (logging coming soon)

### GPU Memory Formats (NVMM)
- **NV12**: Hardware-accelerated YUV 4:2:0

## Performance Tips

1. **Use NVMM when available**: For NVIDIA hardware, building with NVMM support enables zero-copy processing
2. **Match formats**: Avoid unnecessary conversions by matching your pipeline format to supported formats
3. **Hardware decoding**: Use NVIDIA hardware decoders (nvv4l2decoder) with NVMM for best performance
4. **Batch recording**: Use disk saving for long recordings to avoid network overhead
5. **Remote monitoring**: Use gRPC connection for live monitoring of headless systems

## Common Use Cases

### Continuous Recording System
```bash
# Record security camera feed to hourly files
DATE=$(date +%Y%m%d_%H0000)
gst-launch-1.0 v4l2src ! \
    video/x-raw ! videoconvert ! \
    rerunsink recording-id="security-cam" image-path="camera/entrance" \
    output-file="/recordings/cam1_${DATE}.rrd"
```

### Remote Debugging
```bash
# On remote system (e.g., robot, drone)
gst-launch-1.0 v4l2src ! \
    video/x-raw ! \
    rerunsink recording-id="robot-vision" image-path="sensors/camera" \
    grpc-address="control-station.local:9876"

# On control station
rerun --serve --port 9876
```

### Development Pipeline
```bash
# Interactive development with local viewer (default behavior)
gst-launch-1.0 filesrc location=test-video.mp4 ! \
    decodebin ! videoconvert ! \
    rerunsink recording-id="dev-test" image-path="video/processed"
```

## Troubleshooting

### Build Issues

**CUDA not found:**
```bash
# Set CUDA path explicitly
export CUDA_HOME=/usr/local/cuda
export PATH=$CUDA_HOME/bin:$PATH
```

**DeepStream not found:**
- Ensure DeepStream SDK is installed at the expected path
- Or build without NVMM support: `./build.sh` (without --nvmm)

### Runtime Issues

**Plugin not found:**
```bash
# Use GST_PLUGIN_PATH for local testing
export GST_PLUGIN_PATH=/path/to/build:$GST_PLUGIN_PATH
gst-inspect-1.0 rerunsink
```

**NVMM format not accepted:**
- Ensure the plugin was built with NVMM support
- Check that you're running on NVIDIA hardware with proper drivers

**gRPC connection failed:**
- Verify the remote viewer is running: `rerun --serve --port 9876`
- Check firewall settings
- Ensure the address format is correct: `host:port`

**Output file not created:**
- Check write permissions for the output directory
- Verify `output-file` path is set correctly

## Development

### Code Structure
```
src/
├── gstrerunsink.cpp    # Main implementation
├── gstrerunsink.hpp    # Public header
├── gstrerunsink.h      # C API header
└── gstrerunsink.c      # C wrapper (if needed)
```

### Adding New Formats

To add support for a new format:

1. Update the caps string in `gst_rerun_sink_class_init()`
2. Add the format case in `create_image_from_format()`
3. Test with appropriate pipeline

## License

This project is licensed under the LGPL - see the plugin definition for details.


## Contact

General Contact: RidgeRun Support <support@ridgerun.com>
Maintainer: Frander Diaz <frander.diaz@ridgerun.com> 
