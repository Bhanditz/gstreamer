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


#ifndef __GST_IDENTITY_H__
#define __GST_IDENTITY_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_identity_details;


#define GST_TYPE_IDENTITY \
  (gst_identity_get_type())
#define GST_IDENTITY(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_IDENTITY,GstIdentity))
#define GST_IDENTITY_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_IDENTITY,GstIdentityClass))
#define GST_IS_IDENTITY(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_IDENTITY))
#define GST_IS_IDENTITY_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_IDENTITY))

typedef struct _GstIdentity GstIdentity;
typedef struct _GstIdentityClass GstIdentityClass;

struct _GstIdentity {
  GstFilter filter;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean loop_based;
};

struct _GstIdentityClass {
  GstFilterClass parent_class;
};

GtkType gst_identity_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_IDENTITY_H__ */
