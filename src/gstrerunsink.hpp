#ifndef __GST_RERUN_SINK_H__
#define __GST_RERUN_SINK_H__
   
#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <rerun.hpp>

G_BEGIN_DECLS

#define GST_TYPE_RERUN_SINK (gst_rerun_sink_get_type())
G_DECLARE_DERIVABLE_TYPE(GstRerunSink, gst_rerun_sink, GST, RERUN_SINK, GstVideoSink)

/* Class structure */
struct _GstRerunSinkClass {
  GstVideoSinkClass parent_class;
};

typedef struct _GstRerunSinkPrivate {
  rerun::RecordingStream* rec_stream;  // Pointer to RecordingStream
  gboolean rerun_initialized;

  // Properties
  gchar *recording_id;
  gchar *image_path;
  
  // Output control properties
  gboolean spawn_viewer;      // Whether to spawn a Rerun viewer (only if output_file and grpc_address are not set)
  gchar *output_file;         // Path to output .rrd file (if set, saves to disk)
  gchar *grpc_address;        // gRPC connection string (if set to non-default, connects via gRPC)

} GstRerunSinkPrivate;

G_END_DECLS

#endif // __GST_MY_SINK_H__

