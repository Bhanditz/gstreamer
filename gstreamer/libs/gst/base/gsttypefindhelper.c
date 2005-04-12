/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gsttypefindhelper.c: 
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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttypefindhelper.h"

/**
 * typefind code here
 */
typedef struct
{
  GstPad *src;
  guint best_probability;
  GstCaps *caps;
  guint64 size;
  GstBuffer *buffer;
}
GstTypeFindHelper;

static guint8 *
helper_find_peek (gpointer data, gint64 offset, guint size)
{
  GstTypeFindHelper *find;
  GstBuffer *buffer;
  GstPad *src;
  GstFlowReturn ret;

  if (size == 0)
    return NULL;

  find = (GstTypeFindHelper *) data;
  src = find->src;

  if (offset < 0) {
    if (find->size == -1)
      return NULL;

    offset += find->size;
  }

  buffer = NULL;
  ret = GST_RPAD_GETRANGEFUNC (src) (src, offset, size, &buffer);

  if (find->buffer) {
    gst_buffer_unref (find->buffer);
    find->buffer = NULL;
  }

  if (ret != GST_FLOW_OK)
    goto error;

  find->buffer = buffer;

  return GST_BUFFER_DATA (buffer);

error:
  {
    return NULL;
  }
}

static void
helper_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  GstTypeFindHelper *find = (GstTypeFindHelper *) data;

  if (probability > find->best_probability) {
    gst_caps_replace (&find->caps, gst_caps_copy (caps));
    find->best_probability = probability;
  }
}

GstCaps *
gst_type_find_helper (GstPad * src, guint64 size)
{
  GstTypeFind gst_find;
  GstTypeFindHelper find;
  GList *walk, *type_list = NULL;
  GstCaps *result = NULL;

  walk = type_list = gst_type_find_factory_get_list ();

  find.src = src;
  find.best_probability = 0;
  find.caps = NULL;
  find.size = size;
  find.buffer = NULL;
  gst_find.data = &find;
  gst_find.peek = helper_find_peek;
  gst_find.suggest = helper_find_suggest;
  gst_find.get_length = NULL;

  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }

  if (find.best_probability > 0)
    result = find.caps;

  return result;
}
