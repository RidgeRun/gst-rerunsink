/*
 * This file is part of GstRerunSink
 * Copyright 2025 Ridgerun, LLC (http://www.ridgerun.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstrerunsink.hpp"

#include "gst/gstbuffer.h"
#include "gst/gstminiobject.h"
#include "gst/gstutils.h"
#include "gst/gstversion.h"
#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/gstinfo.h>
#include <gst/gstmemory.h>
#include <gst/video/colorbalance.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/navigation.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include <rerun.hpp>
#include <rerun/archetypes/video_stream.hpp>
#include <rerun/components/image_format.hpp>

#include <vector> 

#ifdef HAVE_NVMM_SUPPORT
#include <cuda_runtime.h>
#include <cuda.h>
#include <nvbufsurface.h>
#endif

using namespace std::chrono;

GST_DEBUG_CATEGORY_STATIC(gst_rerun_sink_debug);

#define DEFAULT_GRPC_ADDRESS "127.0.0.1:9876"
#define DEFAULT_RECORDING_ID NULL
#define DEFAULT_IMAGE_PATH NULL
#define DEFAULT_SPAWN_VIEWER TRUE
#define DEFAULT_OUTPUT_FILE NULL
#define DEFAULT_VIDEO_PATH NULL

#define FORMAT_CAPS GST_VIDEO_CAPS_MAKE("{ NV12, I420, RGB, GRAY8, RGBA }")
#define FORMAT_NVMM_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:NVMM", "{NV12}")
#define ENCODED_CAPS "video/x-h264, stream-format=(string)byte-stream; video/x-h265, stream-format=(string){ hvc1, hev1, byte-stream }"

#ifdef HAVE_NVMM_SUPPORT
#define RERUN_SINK_CAPS FORMAT_CAPS ";" FORMAT_NVMM_CAPS ";" ENCODED_CAPS
#else
#define RERUN_SINK_CAPS FORMAT_CAPS ";" ENCODED_CAPS
#endif

enum {
  PROP_0,
  PROP_RECORDING_ID,
  PROP_IMAGE_PATH,
  PROP_SPAWN_VIEWER,
  PROP_OUTPUT_FILE,
  PROP_GRPC_ADDRESS,
  PROP_VIDEO_PATH,
};

#define GST_CAT_DEFAULT gst_rerun_sink_debug

typedef struct _GstRerunSinkPrivate {
  rerun::RecordingStream* rec_stream;
  gboolean rerun_initialized;

  gchar *recording_id;
  gchar *image_path;
  gchar *video_path;

  gboolean spawn_viewer;      // Whether to spawn a Rerun viewer (only if output_file and grpc_address are not set)
  gchar *output_file;         // Path to output .rrd file (if set, saves to disk)
  gchar *grpc_address;        // gRPC connection string (if set to non-default, connects via gRPC)

  gboolean codec_sent;

} GstRerunSinkPrivate;

typedef struct _GstRerunSink {
  GstVideoSink      parent_instance;
  GstRerunSinkPrivate *priv;
} GstRerunSink;

struct _GstRerunSinkClass {
  GstVideoSinkClass parent_class;
};

G_DEFINE_TYPE_WITH_PRIVATE(GstRerunSink, gst_rerun_sink, GST_TYPE_VIDEO_SINK)

static rerun::archetypes::Image create_image_from_format(
    const std::vector<std::uint8_t>& raw_data,
    GstVideoFormat format,
    gint width,
    gint height);

static GstFlowReturn process_encoded_video(
    GstRerunSink* self,
    GstBuffer* buffer,
    GstCaps* caps);

static gboolean is_encoded_format(GstCaps* caps);

#ifdef HAVE_NVMM_SUPPORT
static gboolean is_nvmm_memory(GstBuffer* buffer);
static GstFlowReturn process_nvmm_buffer(
    GstRerunSink* self,
    GstBuffer* buffer,
    const GstVideoInfo* info,
    rerun::archetypes::Image& image);
#endif

static GstFlowReturn process_regular_buffer(
    GstRerunSink* self,
    GstBuffer* buffer,
    const GstVideoInfo* info,
    rerun::archetypes::Image& image);

static GstFlowReturn gst_rerun_sink_render(GstBaseSink *sink, GstBuffer *buffer) {
    GstRerunSink* self = GST_RERUN_SINK(sink);
    GstRerunSinkPrivate* priv = (GstRerunSinkPrivate*)gst_rerun_sink_get_instance_private(self);

    GstCaps *caps = gst_pad_get_current_caps(GST_VIDEO_SINK_PAD(sink));
    if (!caps) {
        GST_ERROR_OBJECT(self, "Failed to get caps");
        return GST_FLOW_ERROR;
    }

    if (is_encoded_format(caps)) {
        GstFlowReturn ret = process_encoded_video(self, buffer, caps);
        gst_caps_unref(caps);
        return ret;
    }

    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        GST_ERROR_OBJECT(self, "Failed to get video info from caps");
        gst_caps_unref(caps);
        return GST_FLOW_ERROR;
    }
    gst_caps_unref(caps);

    // Process the buffer based on memory type
    rerun::archetypes::Image image;
    GstFlowReturn ret;

#ifdef HAVE_NVMM_SUPPORT
    if (is_nvmm_memory(buffer)) {
        ret = process_nvmm_buffer(self, buffer, &info, image);
    } else 
#endif
    {
        ret = process_regular_buffer(self, buffer, &info, image);
    }

    if (ret != GST_FLOW_OK) {
        return ret;
    }

    if (priv->rerun_initialized && priv->rec_stream && priv->image_path) {
        priv->rec_stream->log(priv->image_path, image);
    } else if (!priv->image_path) {
        GST_WARNING_OBJECT(self, "image-path property not set, skipping frame logging");
    }

    return GST_FLOW_OK;
}

static gboolean is_encoded_format(GstCaps* caps) {
    if (!caps) return FALSE;
    
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    if (!structure) return FALSE;
    
    const gchar* name = gst_structure_get_name(structure);
    return (g_strcmp0(name, "video/x-h264") == 0 || 
            g_strcmp0(name, "video/x-h265") == 0);
}

static GstFlowReturn process_encoded_video(
    GstRerunSink* self,
    GstBuffer* buffer,
    GstCaps* caps) {
    
    GstRerunSinkPrivate* priv = (GstRerunSinkPrivate*)gst_rerun_sink_get_instance_private(self);
    
    if (!priv->rerun_initialized || !priv->rec_stream || !priv->video_path) {
        GST_WARNING_OBJECT(self, "video-path property not set, skipping frame logging");
        return GST_FLOW_OK;
    }
    
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* format_name = gst_structure_get_name(structure);
    
    gint width = 0, height = 0;
    gst_structure_get_int(structure, "width", &width);

    if (!width) {
        GST_ERROR_OBJECT(self, "Failed to get width of encoded frame");
        return GST_FLOW_ERROR;
    }

    gst_structure_get_int(structure, "height", &height);

    if (!height) {
        GST_ERROR_OBJECT(self, "Failed to get width of encoded frame");
        return GST_FLOW_ERROR;
    }
    
    const gchar* codec_data_str = NULL;
    rerun::components::VideoCodec codec;
    if (g_strcmp0(format_name, "video/x-h264") == 0) {
        codec_data_str = gst_structure_get_string(structure, "stream-format");
        GST_INFO_OBJECT(self, "H.264 stream detected: %dx%d, stream-format: %s", 
                        width, height, codec_data_str ? codec_data_str : "unknown");
        codec = rerun::components::VideoCodec::H264;
    } else if (g_strcmp0(format_name, "video/x-h265") == 0) {
        codec_data_str = gst_structure_get_string(structure, "stream-format");
        GST_INFO_OBJECT(self, "H.265 stream detected: %dx%d, stream-format: %s", 
                        width, height, codec_data_str ? codec_data_str : "unknown");
        codec = rerun::components::VideoCodec::H265;
    }


    if (!priv->codec_sent) {
        auto video_stream = rerun::archetypes::VideoStream().with_codec(codec);
        priv->rec_stream->log_static(priv->video_path, video_stream);
        priv->codec_sent = true;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map buffer for reading");
        return GST_FLOW_ERROR;
    }

    auto timestamp = time_point<steady_clock, nanoseconds>(nanoseconds(GST_BUFFER_DTS(buffer)));
    priv->rec_stream->set_time_timestamp("time", timestamp);


    auto byte_collection = rerun::Collection<uint8_t>::borrow(map.data, map.size);
    auto sample = rerun::components::VideoSample(std::move(byte_collection));
    auto video_stream = rerun::archetypes::VideoStream().with_sample(sample);

    priv->rec_stream->log(priv->video_path, video_stream);
    
    gst_buffer_unmap(buffer, &map);

    return GST_FLOW_OK;
}

#ifdef HAVE_NVMM_SUPPORT
static gboolean is_nvmm_memory(GstBuffer* buffer) {
    GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
    if (!memory) {
        return FALSE;
    }

    GstAllocator *allocator = memory->allocator;
    if (!allocator) {
        return FALSE;
    }

    const gchar *allocator_name = gst_object_get_name(GST_OBJECT(allocator));

    gboolean is_nvmm = (
        g_strcmp0(allocator_name, "nvfiltermemoryallocator0") == 0 ||
        g_strcmp0(allocator_name, "nvdsmemoryallocator0") == 0
    );
    
    
    GST_DEBUG("Allocator: %s, is NVMM: %s", allocator_name, is_nvmm ? "yes" : "no");
    
    return is_nvmm;
}

static GstFlowReturn process_nvmm_buffer(
    GstRerunSink* self,
    GstBuffer* buffer,
    const GstVideoInfo* info,
    rerun::archetypes::Image& image) {
    
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map NVMM buffer");
        return GST_FLOW_ERROR;
    }

    NvBufSurface *surface = (NvBufSurface *)map.data;
    if (!surface) {
        GST_ERROR_OBJECT(self, "Failed to cast map.data to NvBufSurface");
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    if (NvBufSurfaceMap(surface, -1, -1, NVBUF_MAP_READ) != 0) {
        GST_ERROR_OBJECT(self, "Failed to map NvBufSurface for CPU access");
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    if (NvBufSurfaceSyncForCpu(surface, -1, -1) != 0) {
        GST_ERROR_OBJECT(self, "Failed to sync NvBufSurface for CPU");
        NvBufSurfaceUnMap(surface, -1, -1);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    // Get CPU Luma pointer
    uint8_t* cpu_y_ptr = reinterpret_cast<uint8_t*>(surface->surfaceList[0].mappedAddr.addr[0]);
    if (!cpu_y_ptr) {
        GST_ERROR_OBJECT(self, "Mapped CPU pointer is null");
        NvBufSurfaceUnMap(surface, -1, -1);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    // Get CPU Chroma pointer
    uint8_t* cpu_uv_ptr = reinterpret_cast<uint8_t*>(surface->surfaceList[0].mappedAddr.addr[1]);
    if (!cpu_uv_ptr) {
        GST_ERROR_OBJECT(self, "Mapped CPU pointer is null");
        NvBufSurfaceUnMap(surface, -1, -1);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    gint width = surface->surfaceList[0].width;
    gint height = surface->surfaceList[0].height;
    gint y_pitch = surface->surfaceList[0].planeParams.pitch[0];
    gint uv_pitch = surface->surfaceList[0].planeParams.pitch[1];
    gint y_plane_size  = surface->surfaceList[0].planeParams.psize[0];
    gint uv_plane_size = surface->surfaceList[0].planeParams.psize[1];
    size_t buffer_size = y_plane_size + uv_plane_size;

    // Create image from NVMM data
    std::vector<std::uint8_t> raw_data;

    // Copy Y plane
    for (int i = 0; i < height; ++i) {
        raw_data.insert(
            raw_data.end(),
            cpu_y_ptr + i * y_pitch,
            cpu_y_ptr + i * y_pitch + width
        );
    }

    // Copy UV plane
    for (int i = 0; i < height / 2; ++i) {
        raw_data.insert(
            raw_data.end(),
            cpu_uv_ptr + i * uv_pitch,
            cpu_uv_ptr + i * uv_pitch + width
        );
    }
    
    // Create Rerun's image with raw_data
    if (info->finfo->format == GST_VIDEO_FORMAT_NV12) {
        image = rerun::archetypes::Image(
            raw_data,
            rerun::WidthHeight(width, height),
            rerun::datatypes::PixelFormat::NV12
        );
    } else {
        GST_WARNING_OBJECT(self, "Unsupported NVMM format: %s",
                          gst_video_format_to_string(info->finfo->format));
        NvBufSurfaceUnMap(surface, -1, -1);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_NOT_NEGOTIATED;
    }

    // Clean up
    NvBufSurfaceUnMap(surface, -1, -1);
    gst_buffer_unmap(buffer, &map);

    return GST_FLOW_OK;
}
#endif

// Process regular CPU buffer
static GstFlowReturn process_regular_buffer(
    GstRerunSink* self,
    GstBuffer* buffer,
    const GstVideoInfo* info,
    rerun::archetypes::Image& image) {
    
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map buffer for reading");
        return GST_FLOW_ERROR;
    }

    // Create a copy of the data
    std::vector<std::uint8_t> raw_data(map.data, map.data + map.size);
    
    gint width = GST_VIDEO_INFO_WIDTH(info);
    gint height = GST_VIDEO_INFO_HEIGHT(info);
    
    GST_DEBUG_OBJECT(self, "Regular buffer: %dx%d, format: %s", 
                     width, height, gst_video_format_to_string(info->finfo->format));

    image = create_image_from_format(raw_data, info->finfo->format, width, height);

    gst_buffer_unmap(buffer, &map);

    if (info->finfo->format != GST_VIDEO_FORMAT_RGB &&
        info->finfo->format != GST_VIDEO_FORMAT_RGBA &&
        info->finfo->format != GST_VIDEO_FORMAT_GRAY8 &&
        info->finfo->format != GST_VIDEO_FORMAT_NV12 &&
        info->finfo->format != GST_VIDEO_FORMAT_I420) {
        GST_WARNING_OBJECT(self, "Unsupported format: %s",
                          gst_video_format_to_string(info->finfo->format));
        return GST_FLOW_NOT_NEGOTIATED;
    }

    return GST_FLOW_OK;
}

static rerun::archetypes::Image create_image_from_format(
    const std::vector<std::uint8_t>& raw_data,
    GstVideoFormat format,
    gint width,
    gint height) {
    
    switch (format) {
        case GST_VIDEO_FORMAT_RGB:
            return rerun::archetypes::Image::from_rgb24(
                raw_data,
                rerun::WidthHeight(width, height)
            );

        case GST_VIDEO_FORMAT_RGBA:
            return rerun::archetypes::Image::from_rgba32(
                raw_data,
                rerun::WidthHeight(width, height)
            );

        case GST_VIDEO_FORMAT_GRAY8:
            return rerun::archetypes::Image::from_grayscale8(
                raw_data,
                rerun::WidthHeight(width, height)
            );

        case GST_VIDEO_FORMAT_NV12:
            return rerun::archetypes::Image(
                raw_data,
                rerun::WidthHeight(width, height),
                rerun::datatypes::PixelFormat::NV12
            );

        case GST_VIDEO_FORMAT_I420:
            return rerun::archetypes::Image(
                raw_data,
                rerun::WidthHeight(width, height),
                rerun::datatypes::PixelFormat::Y_U_V12_LimitedRange
            );

        default:
            // Return empty image for unsupported formats
            return rerun::archetypes::Image();
    }
}

static gboolean gst_rerun_sink_set_caps(GstBaseSink *sink, GstCaps *caps) {
    GstRerunSink *self = GST_RERUN_SINK(sink);
    
    gchar *caps_str = gst_caps_to_string(caps);
    GST_INFO_OBJECT(self, "Caps negotiated: %s", caps_str);
    g_free(caps_str);
    
    return GST_BASE_SINK_CLASS(gst_rerun_sink_parent_class)->set_caps(sink, caps);
}

static void gst_rerun_sink_set_property(GObject *object, guint prop_id,
                                        const GValue *value, GParamSpec *pspec) {
    GstRerunSink *self = GST_RERUN_SINK(object);
    GstRerunSinkPrivate *priv = (GstRerunSinkPrivate *)gst_rerun_sink_get_instance_private(self);

    switch (prop_id) {
        case PROP_RECORDING_ID:
            g_free(priv->recording_id);
            priv->recording_id = g_value_dup_string(value);
            GST_INFO_OBJECT(self, "Set recording-id: %s", priv->recording_id);
            break;
            
        case PROP_IMAGE_PATH:
            g_free(priv->image_path);
            priv->image_path = g_value_dup_string(value);
            GST_INFO_OBJECT(self, "Set image-path: %s", priv->image_path);
            break;
            
        case PROP_VIDEO_PATH:
            g_free(priv->video_path);
            priv->video_path = g_value_dup_string(value);
            GST_INFO_OBJECT(self, "Set video-path: %s", priv->video_path);
            break;

        case PROP_SPAWN_VIEWER:
            priv->spawn_viewer = g_value_get_boolean(value);
            GST_INFO_OBJECT(self, "Set spawn-viewer: %s", priv->spawn_viewer ? "true" : "false");
            break;
            
        case PROP_OUTPUT_FILE:
            g_free(priv->output_file);
            priv->output_file = g_value_dup_string(value);
            GST_INFO_OBJECT(self, "Set output-file: %s", priv->output_file);
            break;
            
        case PROP_GRPC_ADDRESS:
            g_free(priv->grpc_address);
            priv->grpc_address = g_value_dup_string(value);
            GST_INFO_OBJECT(self, "Set grpc-address: %s", priv->grpc_address);
            break;
            
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_rerun_sink_get_property(GObject *object, guint prop_id,
                                        GValue *value, GParamSpec *pspec) {
    GstRerunSink *self = GST_RERUN_SINK(object);
    GstRerunSinkPrivate *priv = (GstRerunSinkPrivate *)gst_rerun_sink_get_instance_private(self);

    switch (prop_id) {
        case PROP_RECORDING_ID:
            g_value_set_string(value, priv->recording_id);
            break;
            
        case PROP_IMAGE_PATH:
            g_value_set_string(value, priv->image_path);
            break;
            
        case PROP_VIDEO_PATH:
            g_value_set_string(value, priv->video_path);
            break;

        case PROP_SPAWN_VIEWER:
            g_value_set_boolean(value, priv->spawn_viewer);
            break;
            
        case PROP_OUTPUT_FILE:
            g_value_set_string(value, priv->output_file);
            break;
            
        case PROP_GRPC_ADDRESS:
            g_value_set_string(value, priv->grpc_address);
            break;
            
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_rerun_sink_init(GstRerunSink *self) {
    GstRerunSinkPrivate* priv = (GstRerunSinkPrivate*)gst_rerun_sink_get_instance_private(self);

    priv->rec_stream = nullptr;
    priv->rerun_initialized = FALSE;
    priv->recording_id = DEFAULT_RECORDING_ID;
    priv->image_path = DEFAULT_IMAGE_PATH;
    priv->video_path = DEFAULT_VIDEO_PATH;

    priv->spawn_viewer = DEFAULT_SPAWN_VIEWER;
    priv->output_file = DEFAULT_OUTPUT_FILE;
    priv->grpc_address = g_strdup(DEFAULT_GRPC_ADDRESS);

    priv->codec_sent = FALSE;
}

static gboolean gst_rerun_sink_start(GstBaseSink *sink) {
    GstRerunSink *self = GST_RERUN_SINK(sink);
    GstRerunSinkPrivate *priv = (GstRerunSinkPrivate *)gst_rerun_sink_get_instance_private(self);

    if (!priv->rerun_initialized) {
        const char* rec_id = priv->recording_id ? priv->recording_id : "gst-rerun";
        priv->rec_stream = new rerun::RecordingStream(rec_id);

        // Check for conflicting options
        gboolean has_output_file = (priv->output_file != NULL);
        gboolean has_custom_grpc = (priv->grpc_address && 
                                   g_strcmp0(priv->grpc_address, DEFAULT_GRPC_ADDRESS) != 0);
        
        gint options_count = (has_output_file ? 1 : 0) + (has_custom_grpc ? 1 : 0);
        
        if (options_count > 1) {
            GST_ERROR_OBJECT(self, "Conflicting output options: both output-file and custom grpc-address are set. "
                           "Please use only one output method at a time.");
            delete priv->rec_stream;
            priv->rec_stream = nullptr;
            return FALSE;
        }
        
        if (has_output_file) {
            GST_INFO_OBJECT(self, "Saving to disk: %s", priv->output_file);
            auto result = priv->rec_stream->save(priv->output_file);
            if (result.is_err()) {
                GST_ERROR_OBJECT(self, "Failed to save to disk: %s", priv->output_file);
                delete priv->rec_stream;
                priv->rec_stream = nullptr;
                return FALSE;
            }
        } else if (has_custom_grpc) {
            GST_INFO_OBJECT(self, "Connecting to gRPC at: %s", priv->grpc_address);
            auto result = priv->rec_stream->connect_grpc(priv->grpc_address);
            if (result.is_err()) {
                GST_ERROR_OBJECT(self, "Failed to connect to gRPC: %s", priv->grpc_address);
                delete priv->rec_stream;
                priv->rec_stream = nullptr;
                return FALSE;
            }
        } else if (priv->spawn_viewer) {
            GST_INFO_OBJECT(self, "Spawning Rerun viewer");
            rerun::Error err = priv->rec_stream->spawn();
            if (err.is_err()) {
                GST_ERROR_OBJECT(self, "Error spawning Rerun viewer");
                return FALSE;
            }
        } else {
            GST_WARNING_OBJECT(self, "No output method enabled: spawn-viewer is false and no output-file or custom grpc-address specified");
            // This is valid - user might just want to create recording without output
        }
        #ifdef HAVE_NVMM_SUPPORT
            // Initialize CUDA context (required for NVMM handling)
            cudaFree(0);
        #endif

        priv->codec_sent = FALSE;
        priv->rerun_initialized = TRUE;
        GST_INFO_OBJECT(self, "Initialized Rerun with recording ID: %s", rec_id);
    }

    return TRUE;
}

static gboolean gst_rerun_sink_stop(GstBaseSink *sink) {
    GstRerunSink *self = GST_RERUN_SINK(sink);
    GstRerunSinkPrivate *priv = (GstRerunSinkPrivate *)gst_rerun_sink_get_instance_private(self);

    if (priv->rec_stream) {
        delete priv->rec_stream;
        priv->rec_stream = nullptr;
        priv->rerun_initialized = FALSE;
        priv->codec_sent = FALSE;
        GST_INFO_OBJECT(self, "Stopped Rerun recording");
    }

    return TRUE;
}

static void gst_rerun_sink_dispose(GObject *object) {
    GstRerunSink *self = GST_RERUN_SINK(object);
    GstRerunSinkPrivate *priv = (GstRerunSinkPrivate *)gst_rerun_sink_get_instance_private(self);

    g_clear_pointer(&priv->recording_id, g_free);
    g_clear_pointer(&priv->image_path, g_free);
    g_clear_pointer(&priv->output_file, g_free);
    g_clear_pointer(&priv->grpc_address, g_free);

    G_OBJECT_CLASS(gst_rerun_sink_parent_class)->dispose(object);
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_rerun_sink_debug, "rerunsink", 0, "Rerun sink");
    
    return gst_element_register(plugin, "rerunsink", GST_RANK_NONE, GST_TYPE_RERUN_SINK);
}

static void gst_rerun_sink_class_init(GstRerunSinkClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS(klass);

    gst_element_class_set_static_metadata(element_class,
        "RerunSink", 
        "Sink/Video", 
        "Video sink that logs frames to Rerun for visualization",
        "Frander Diaz <support@ridgerun.com>");

    gobject_class->set_property = gst_rerun_sink_set_property;
    gobject_class->get_property = gst_rerun_sink_get_property;
    gobject_class->dispose = gst_rerun_sink_dispose;

    g_object_class_install_property(gobject_class, PROP_RECORDING_ID,
        g_param_spec_string("recording-id", "Recording ID",
                            "Rerun recording/session identifier",
                            DEFAULT_RECORDING_ID,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
                        
    g_object_class_install_property(gobject_class, PROP_IMAGE_PATH,
        g_param_spec_string("image-path", "Image Path",
                            "Entity path for logging images (e.g. 'camera/front/frame')",
                            DEFAULT_IMAGE_PATH,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_VIDEO_PATH,
        g_param_spec_string("video-path", "Video Path",
                            "Entity path for logging video (e.g. 'camera/front/frame')",
                            DEFAULT_VIDEO_PATH,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SPAWN_VIEWER,
        g_param_spec_boolean("spawn-viewer", "Spawn Viewer",
                             "Spawn a Rerun viewer instance (ignored if output-file is set or grpc-address is non-default)",
                             DEFAULT_SPAWN_VIEWER,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_OUTPUT_FILE,
        g_param_spec_string("output-file", "Output File",
                            "Path to output .rrd file (if set, saves to disk instead of spawning viewer)",
                            DEFAULT_OUTPUT_FILE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(gobject_class, PROP_GRPC_ADDRESS,
        g_param_spec_string("grpc-address", "gRPC Address",
                            "gRPC server address (if non-default, connects via gRPC instead of spawning viewer)",
                            DEFAULT_GRPC_ADDRESS,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    basesink_class->start = GST_DEBUG_FUNCPTR(gst_rerun_sink_start);
    basesink_class->stop = GST_DEBUG_FUNCPTR(gst_rerun_sink_stop);
    basesink_class->render = GST_DEBUG_FUNCPTR(gst_rerun_sink_render);
    basesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_rerun_sink_set_caps);

    gst_element_class_add_pad_template(element_class,
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(RERUN_SINK_CAPS)));
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rerunsink,
    "GStreamer sink plugin for Rerun visualization",
    plugin_init,
    "1.0",
    "LGPL",
    "GStreamer Rerun Sink",
    "https://github.com/rerun-io/rerun"
)

