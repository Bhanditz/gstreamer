/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstautoplug.h: Header for autoplugging functionality
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


#ifndef __GST_STATIC_AUTOPLUG_RENDER_H__
#define __GST_STATIC_AUTOPLUG_RENDER_H__

#include <gst/gstautoplug.h>

G_BEGIN_DECLS

#define GST_TYPE_STATIC_AUTOPLUG_RENDER \
  (gst_static_autoplug_render_get_type())
#define GST_STATIC_AUTOPLUG_RENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STATIC_AUTOPLUG_RENDER,GstStaticAutoplugRender))
#define GST_STATIC_AUTOPLUG_RENDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STATIC_AUTOPLUG_RENDER,GstStaticAutoplugRenderClass))
#define GST_IS_STATIC_AUTOPLUG_RENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STATIC_AUTOPLUG_RENDER))
#define GST_IS_STATIC_AUTOPLUG_RENDER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STATIC_AUTOPLUG_RENDER))

typedef struct _GstStaticAutoplugRender GstStaticAutoplugRender;
typedef struct _GstStaticAutoplugRenderClass GstStaticAutoplugRenderClass;

struct _GstStaticAutoplugRender {
  GstAutoplug object;
};

struct _GstStaticAutoplugRenderClass {
  GstAutoplugClass parent_class;
};


GType			gst_static_autoplug_render_get_type		(void);

G_END_DECLS

#endif /* __GST_STATIC_AUTOPLUG_H__ */

