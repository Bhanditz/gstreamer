/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_RTP_L16_PARSE_H__
#define __GST_RTP_L16_PARSE_H__

#include <gst/gst.h>
#include "rtp-packet.h"
#include "gstrtp-common.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

/* Definition of structure storing data for this element. */
typedef struct _GstRtpL16Parse GstRtpL16Parse;
struct _GstRtpL16Parse
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  guint frequency;
  guint channels;

  rtp_payload_t payload_type;
};

/* Standard definition defining a class for this element. */
typedef struct _GstRtpL16ParseClass GstRtpL16ParseClass;
struct _GstRtpL16ParseClass
{
  GstElementClass parent_class;
};

/* Standard macros for defining types for this element.  */
#define GST_TYPE_RTP_L16_PARSE \
  (gst_rtpL16parse_get_type())
#define GST_RTP_L16_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_L16_PARSE,GstRtpL16Parse))
#define GST_RTP_L16_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_L16_PARSE,GstRtpL16Parse))
#define GST_IS_RTP_L16_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_L16_PARSE))
#define GST_IS_RTP_L16_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_L16_PARSE))

gboolean gst_rtpL16parse_plugin_init (GModule * module, GstPlugin * plugin);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_RTP_L16_PARSE_H__ */
