/**
 * Simple example demonstrating programmatic usage of the rerunsink plugin
 * 
 * Build with:
 * g++ -o simple_pipeline simple_pipeline.cpp `pkg-config --cflags --libs gstreamer-1.0`
 * 
 * Run with:
 * GST_PLUGIN_PATH=../build ./simple_pipeline [mode]
 * 
 * Modes:
 *   spawn  - Spawn local viewer (default)
 *   disk   - Save to disk
 *   grpc   - Connect to remote viewer
 */

#include <gst/gst.h>
#include <iostream>
#include <string>
#include <cstring>

// Error checking macro
#define CHECK_ERROR(error) \
    if (error) { \
        g_printerr("Error: %s\n", error->message); \
        g_error_free(error); \
        return -1; \
    }

// Bus message handler
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            g_printerr("Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(message->src)) {
                GstState old_state, new_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
                g_print("Pipeline state changed from %s to %s\n",
                        gst_element_state_get_name(old_state),
                        gst_element_state_get_name(new_state));
            }
            break;
        }
        default:
            break;
    }

    return TRUE;
}

void print_usage(const char* program_name) {
    g_print("Usage: %s [mode]\n", program_name);
    g_print("\nModes:\n");
    g_print("  spawn  - Spawn local Rerun viewer (default)\n");
    g_print("  disk   - Save recording to disk (example.rrd)\n");
    g_print("  grpc   - Connect to remote viewer at custom address\n");
    g_print("\nExamples:\n");
    g_print("  %s             # Default: spawn viewer\n", program_name);
    g_print("  %s spawn       # Explicitly spawn viewer\n", program_name);
    g_print("  %s disk        # Save to example.rrd\n", program_name);
    g_print("  %s grpc        # Connect to remote viewer\n", program_name);
    g_print("\nFor gRPC mode, start viewer with: rerun --serve --port 9090\n");
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source, *filter, *sink;
    GstBus *bus;
    GMainLoop *loop;
    std::string mode = "spawn";  // Default mode

    // Parse command line arguments
    if (argc > 1) {
        mode = argv[1];
        if (mode == "-h" || mode == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (mode != "spawn" && mode != "disk" && mode != "grpc") {
            g_printerr("Invalid mode: %s\n", mode.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // Create main loop
    loop = g_main_loop_new(NULL, FALSE);

    // Create pipeline elements
    pipeline = gst_pipeline_new("test-pipeline");
    source = gst_element_factory_make("videotestsrc", "source");
    filter = gst_element_factory_make("capsfilter", "filter");
    sink = gst_element_factory_make("rerunsink", "sink");

    if (!pipeline || !source || !filter || !sink) {
        g_printerr("Failed to create elements. Make sure the rerunsink plugin is available.\n");
        g_printerr("Try: GST_PLUGIN_PATH=/path/to/build gst-inspect-1.0 rerunsink\n");
        return -1;
    }

    // Configure videotestsrc
    g_object_set(G_OBJECT(source),
                 "pattern", 0,  // SMPTE test pattern
                 "num-buffers", 300,  // Send 300 frames (10 seconds at 30fps)
                 NULL);

    // Configure caps filter for specific format
    GstCaps *caps = gst_caps_from_string("video/x-raw,format=RGB,width=640,height=480,framerate=30/1");
    g_object_set(G_OBJECT(filter), "caps", caps, NULL);
    gst_caps_unref(caps);

    // Configure rerunsink based on mode
    g_print("Mode: %s\n", mode.c_str());
    
    if (mode == "spawn") {
        // Default behavior - spawn viewer
        // Only need to set required properties, viewer will spawn by default
        g_object_set(G_OBJECT(sink),
                     "recording-id", "example-pipeline-spawn",
                     "image-path", "camera/test_pattern",
                     NULL);
        g_print("Will spawn local Rerun viewer\n");
        
    } else if (mode == "disk") {
        // Save to disk - just set output-file
        g_object_set(G_OBJECT(sink),
                     "recording-id", "example-pipeline-disk",
                     "image-path", "camera/test_pattern",
                     "output-file", "example.rrd",
                     NULL);
        g_print("Will save recording to: example.rrd\n");
        
    } else if (mode == "grpc") {
        // Connect to remote viewer - just set custom grpc-address
        g_object_set(G_OBJECT(sink),
                     "recording-id", "example-pipeline-grpc",
                     "image-path", "camera/test_pattern",
                     "grpc-address", "127.0.0.1:9090",  // Custom port
                     NULL);
        g_print("Will connect to gRPC viewer at: 127.0.0.1:9090\n");
        g_print("Make sure to start viewer with: rerun --serve --port 9090\n");
    }

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), source, filter, sink, NULL);

    // Link elements
    if (!gst_element_link_many(source, filter, sink, NULL)) {
        g_printerr("Failed to link elements\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // Set up bus watch
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_callback, loop);
    gst_object_unref(bus);

    // Start playing
    g_print("\nStarting pipeline...\n");
    
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to start pipeline\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // Print information based on mode
    g_print("\nPipeline is running. Streaming test pattern...\n");
    if (mode == "spawn") {
        g_print("Check the spawned Rerun viewer window.\n");
    } else if (mode == "disk") {
        g_print("Recording to file... When done, view with: rerun example.rrd\n");
    } else if (mode == "grpc") {
        g_print("Streaming to remote viewer...\n");
    }
    g_print("\nPress Ctrl+C to stop.\n");

    // Run the main loop
    g_main_loop_run(loop);

    // Clean up
    g_print("Stopping pipeline...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);

    if (mode == "disk") {
        g_print("\nRecording saved to: example.rrd\n");
        g_print("View it with: rerun example.rrd\n");
    }

    return 0;
} 