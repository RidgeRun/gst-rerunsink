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

#ifndef __GST_RERUN_SINK_H__
#define __GST_RERUN_SINK_H__
   
#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <rerun.hpp>

G_BEGIN_DECLS

#define GST_TYPE_RERUN_SINK (gst_rerun_sink_get_type())
G_DECLARE_FINAL_TYPE(GstRerunSink, gst_rerun_sink, GST, RERUN_SINK, GstVideoSink)

G_END_DECLS

#endif // __GST_RERUN_SINK_H__

