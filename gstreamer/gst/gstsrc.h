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


#ifndef __GST_SRC_H__
#define __GST_SRC_H__


#include <gst/gstelement.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_SRC \
  (gst_src_get_type())
#define GST_SRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_SRC,GstSrc))
#define GST_SRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_SRC,GstSrcClass))
#define GST_IS_SRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_SRC))
#define GST_IS_SRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_SRC))

typedef enum {
  GST_SRC_ASYNC		= 1 << 0,
} GstSrcFlags;

#define GST_SRC_FLAGS(obj) \
	(GST_SRC(obj)->flags)
#define GST_SRC_ASYNC(obj) \
  ((GST_SRC_FLAGS(obj) & GST_SRC_ASYNC))

typedef struct _GstSrc GstSrc;
typedef struct _GstSrcClass GstSrcClass;

struct _GstSrc {
  GstElement element;
  gint32 flags;
};

struct _GstSrcClass {
  GstElementClass parent_class;

  /* subclass functions */
  void (*push) (GstSrc *src);
  void (*push_region) (GstSrc *src,gulong offset,gulong size);

  /* signals */
  void (*eos) (GstSrc *src);
};

#define GST_SRC_SET_FLAGS(src,flag) \
  G_STMT_START{ (GST_SRC_FLAGS (src) |= (flag)); }G_STMT_END
#define GST_SRC_UNSET_FLAGS(src,flag) \
	G_STMT_START{ (GST_SRC_FLAGS (src) &= ~(flag)); }G_STMT_END


GtkType gst_src_get_type(void);

void gst_src_signal_eos(GstSrc *src);

void gst_src_push(GstSrc *src);
void gst_src_push_region(GstSrc *src,gulong offset,gulong size);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SRC_H__ */
