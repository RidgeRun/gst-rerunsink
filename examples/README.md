# GStreamer Rerun Sink Examples

This directory contains example programs demonstrating how to use the rerunsink plugin.

## Examples

### simple_pipeline.cpp
A comprehensive example showing how to:
- Create a GStreamer pipeline programmatically
- Configure the rerunsink element with different output modes
- Handle pipeline messages
- Stream test pattern video to Rerun

The example supports three output modes:
1. **spawn** - Spawns a local Rerun viewer (default)
2. **disk** - Saves recording to disk as `.rrd` file
3. **grpc** - Connects to a remote Rerun viewer

## Building

Make sure you've built the main plugin first:
```bash
cd ..
./build.sh
```

Then build the examples:
```bash
make
```

## Running

### Simple Pipeline

#### Default Mode (Spawn Viewer)
```bash
# Using make - spawns local viewer
make run

# Or manually
GST_PLUGIN_PATH=../build ./simple_pipeline
GST_PLUGIN_PATH=../build ./simple_pipeline spawn
```

#### Save to Disk Mode
```bash
# Save recording to example.rrd
GST_PLUGIN_PATH=../build ./simple_pipeline disk

# View the saved recording
rerun example.rrd
```

#### Remote Viewer Mode (gRPC)
```bash
# First, start a Rerun viewer in server mode
rerun --serve --port 9876

# In another terminal, run the pipeline
GST_PLUGIN_PATH=../build ./simple_pipeline grpc
```

### Command Line Help
```bash
./simple_pipeline --help
```

## Output Files

When using disk mode, the example creates:
- `example.rrd` - Rerun recording file containing the video frames

## Common Use Cases

### Long Recording to Disk
```c++
// Modify num-buffers for longer recording
g_object_set(G_OBJECT(source),
             "pattern", 0,
             "num-buffers", 3000,  // 100 seconds at 30fps
             NULL);
```

### Custom gRPC Address
```c++
// Connect to remote viewer on different host/port
g_object_set(G_OBJECT(sink),
             "grpc-address", "192.168.1.100:9876",
             NULL);
```

### Different Video Formats
```c++
// Change caps for different formats
GstCaps *caps = gst_caps_from_string(
    "video/x-raw,format=NV12,width=1920,height=1080,framerate=60/1"
);
```

## Adding New Examples

To add a new example:
1. Create your `.cpp` file in this directory
2. Add a build target in the Makefile
3. Update this README with a description

## Common Issues

**Plugin not found:**
Make sure `GST_PLUGIN_PATH` points to the directory containing `libgstrerunsink.so`

**Rerun not installed:**
Install Rerun: `pip install rerun-sdk`

**gRPC connection failed:**
- Ensure the viewer is started with `--serve` flag
- Check the port is not blocked by firewall
- Verify the address is correct

**Disk space:**
When using disk mode, ensure you have enough space for the recording. A rough estimate:
- 640x480 RGB @ 30fps ≈ 25 MB/second
- 1920x1080 RGB @ 30fps ≈ 180 MB/second 