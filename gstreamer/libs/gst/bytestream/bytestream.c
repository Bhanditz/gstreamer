/* GStreamer
 * Copyright (C) 2001 Erik Walthinsen <omega@temple-baptist.com>
 *
 * gstbytestream.c: adds a convenient bytestream based API to a pad.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gst/gstinfo.h>
#include "bytestream.h"

/* #define BS_DEBUG */

#ifdef G_HAVE_ISO_VARARGS

#ifdef BS_DEBUG
# define bs_print(...)	GST_DEBUG (GST_CAT_BUFFER, __VA_ARGS__)
# define bs_status(bs)			gst_bytestream_print_status(bs)
#else
# define bs_print(...)
# define bs_status(bs)
#endif

#elif defined(G_HAVE_GNUC_VARARGS)

#ifdef BS_DEBUG
# define bs_print(format,args...)	GST_DEBUG (GST_CAT_BUFFER,  format, ## args)
# define bs_status(bs)			gst_bytestream_print_status(bs)
#else
# define bs_print(format,args...)
# define bs_status(bs)
#endif

#endif

static guint8 *gst_bytestream_assemble (GstByteStream * bs, guint32 len);

/**
 * gst_bytestream_new:
 * @pad: the pad to attach the bytestream to
 *
 * creates a bytestream from the given pad
 *
 * Returns: a new #GstByteStream object
 */
GstByteStream *
gst_bytestream_new (GstPad * pad)
{
  GstByteStream *bs = g_new (GstByteStream, 1);

  bs->pad = pad;
  bs->event = NULL;
  bs->buflist = NULL;
  bs->headbufavail = 0;
  bs->listavail = 0;
  bs->assembled = NULL;
  bs->offset = 0LL;
  bs->in_seek = FALSE;

  return bs;
}

void
gst_bytestream_destroy (GstByteStream * bs)
{
  GSList *walk;

  if (bs->event)
    gst_event_unref (bs->event);

  walk = bs->buflist;
  while (walk) {
    gst_buffer_unref (GST_BUFFER (walk->data));
    walk = g_slist_next (walk);
  }
  g_slist_free (bs->buflist);
  if (bs->assembled)
    g_free (bs->assembled);
  g_free (bs);
}

/* HOW THIS WORKS:
 *
 * The fundamental structure is a singly-linked list of buffers.  The
 * buffer on the front is the oldest, and thus the first to read data
 * from.  The number of bytes left to be read in this buffer is stored
 * in bs->headbufavail.  The number of bytes available in the entire
 * list (including the head buffer) is in bs->listavail.
 *
 * When a request is made for data (peek), _fill_bytes is called with
 * the number of bytes needed, but only if the listavail indicates
 * that there aren't already enough.  This calls _get_next_buf until
 * the listavail is sufficient to satisfy the demand.
 *
 * _get_next_buf pulls a buffer from the pad the bytestream is attached
 * to, and shoves it in the list.  There are actually two things it can
 * do.  If there's already a buffer in the list, and the _is_span_fast()
 * test returns true, it will merge it with that last buffer.  Otherwise
 * it will simply tack it onto the end of the list.
 *
 * The _peek itself first checks the simple case of the request fitting
 * within the head buffer, and if so creates a subbuffer and returns.
 * Otherwise, it creates a new buffer and allocates space for the request
 * and calls _assemble to fill it.  We know we have to copy because this
 * case only happens when the _merge wasn't feasible during _get_next_buf.
 *
 * The _flush method repeatedly inspects the head buffer and flushes as
 * much data from it as it needs to, up to the size of the buffer.  If
 * the flush decimates the buffer, it's stripped, unref'd, and removed.
 */


/* get the next buffer
 * if the buffer can be merged with the head buffer, do so
 * else add it onto the head of the 
 */
static gboolean
gst_bytestream_get_next_buf (GstByteStream *bs)
{
  GstBuffer *nextbuf, *lastbuf, *headbuf;
  GSList *end;
  GstClockTime ts;

  /* if there is an event pending, return FALSE */
  if (bs->event)
    return FALSE;

  bs_print ("get_next_buf: pulling buffer");
  nextbuf = gst_pad_pull (bs->pad);

  if (!nextbuf)
    return FALSE;

  if (GST_IS_EVENT (nextbuf)) {
    bs->event = GST_EVENT (nextbuf);
    return FALSE;
  }

  ts = GST_BUFFER_TIMESTAMP (nextbuf);
  if (ts != GST_CLOCK_TIME_NONE)
    bs->last_ts = ts;

  bs_print ("get_next_buf: got buffer of %d bytes", GST_BUFFER_SIZE (nextbuf));

  /* first see if there are any buffers in the list at all */
  if (bs->buflist) {
    bs_print ("gst_next_buf: there is at least one buffer in the list");
    /* now find the end of the list */
    end = g_slist_last (bs->buflist);
    /* get the buffer that's there */
    lastbuf = GST_BUFFER (end->data);

    /* see if we can marge cheaply */
    if (gst_buffer_is_span_fast (lastbuf, nextbuf)) {
      bs_print ("get_next_buf: merging new buffer with last buf on list");
      /* it is, let's merge them (this is really an append, but...) */
      end->data = gst_buffer_merge (lastbuf, nextbuf);
      /* add to the length of the list */
      bs->listavail += GST_BUFFER_SIZE (nextbuf);

      /* have to check to see if we merged with the head buffer */
      if (end == bs->buflist) {
	bs->headbufavail += GST_BUFFER_SIZE (nextbuf);
      }

      gst_buffer_unref (lastbuf);
      gst_buffer_unref (nextbuf);

      /* if we can't, we just append this buffer */
    }
    else {
      bs_print ("get_next_buf: adding new buffer to the end of the list");
      end = g_slist_append (end, nextbuf);
      /* also need to increment length of list and buffer count */
      bs->listavail += GST_BUFFER_SIZE (nextbuf);
    }

    /* if there are no buffers in the list */
  }
  else {
    bs_print ("get_next_buf: buflist is empty, adding new buffer to list");
    /* put this on the end of the list */
    bs->buflist = g_slist_append (bs->buflist, nextbuf);
    /* and increment the number of bytes in the list */
    bs->listavail = GST_BUFFER_SIZE (nextbuf);
    /* set the head buffer avail to the size */
    bs->headbufavail = GST_BUFFER_SIZE (nextbuf);
  }

  /* a zero offset is a indication that we might need to set the timestamp */ 
  if (bs->offset == 0LL){
    headbuf = GST_BUFFER (bs->buflist->data);
    bs->offset = GST_BUFFER_OFFSET(headbuf);
  }
  
  return TRUE;
}

static gboolean
gst_bytestream_fill_bytes (GstByteStream *bs, guint32 len)
{
  /* as long as we don't have enough, we get more buffers */
  while (bs->listavail < len) {
    bs_print ("fill_bytes: there are %d bytes in the list, we need %d", bs->listavail, len);
    if (!gst_bytestream_get_next_buf (bs))
      return FALSE;
  }

  return TRUE;
}

guint32
gst_bytestream_peek (GstByteStream *bs, GstBuffer **buf, guint32 len)
{
  GstBuffer *headbuf, *retbuf = NULL;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (len > 0, 0);

  bs_print ("peek: asking for %d bytes", len);

  /* make sure we have enough */
  bs_print ("peek: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len)) {
      /* we must have an event coming up */
      if (bs->listavail > 0) {
        /* we have some data left, len will be shrunk to the amount of data available */
        len = bs->listavail;
      }
      else {
        /* there is no data */
        *buf = retbuf;
        return 0;
      }
    }
    bs_print ("peek: there are now %d bytes in the list", bs->listavail);
  }
  bs_status (bs);

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  /* if the requested bytes are in the current buffer */
  bs_print ("peek: headbufavail is %d", bs->headbufavail);
  if (len <= bs->headbufavail) {
    bs_print ("peek: there are enough bytes in headbuf (need %d, have %d)", len, bs->headbufavail);
    /* create a sub-buffer of the headbuf */
    retbuf = gst_buffer_create_sub (headbuf, GST_BUFFER_SIZE (headbuf) - bs->headbufavail, len);

    /* otherwise we need to figure out how to assemble one */
  }
  else {
    bs_print ("peek: current buffer is not big enough for len %d", len);

    retbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (retbuf) = len;
    GST_BUFFER_DATA (retbuf) = gst_bytestream_assemble (bs, len);
    GST_BUFFER_TIMESTAMP (retbuf) = bs->last_ts;
    if (GST_BUFFER_OFFSET (headbuf) != -1)
      GST_BUFFER_OFFSET (retbuf) = GST_BUFFER_OFFSET (headbuf) + (GST_BUFFER_SIZE (headbuf) - bs->headbufavail);
  }

  *buf = retbuf;
  return len;
}

guint32
gst_bytestream_peek_bytes (GstByteStream *bs, guint8** data, guint32 len)
{
  GstBuffer *headbuf;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (len > 0, 0);

  bs_print ("peek_bytes: asking for %d bytes", len);
  if (bs->assembled) {
    g_free (bs->assembled);
    bs->assembled = NULL;
  }

  /* make sure we have enough */
  bs_print ("peek_bytes: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len)){
      /* we must have an event coming up */
      if (bs->listavail > 0){
        /* we have some data left, len will be shrunk to the amount of data available */
        len = bs->listavail;
      }
      else {
        /* there is no data */
        *data = NULL;
        return 0;
      }
    }
    bs_print ("peek_bytes: there are now %d bytes in the list", bs->listavail);
  }
  bs_status (bs);

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  /* if the requested bytes are in the current buffer */
  bs_print ("peek_bytes: headbufavail is %d", bs->headbufavail);
  if (len <= bs->headbufavail) {
    bs_print ("peek_bytes: there are enough bytes in headbuf (need %d, have %d)", len, bs->headbufavail);
    /* create a sub-buffer of the headbuf */
    *data = GST_BUFFER_DATA (headbuf) + (GST_BUFFER_SIZE (headbuf) - bs->headbufavail);

    /* otherwise we need to figure out how to assemble one */
  }
  else {
    bs_print ("peek_bytes: current buffer is not big enough for len %d", len);

    *data = gst_bytestream_assemble (bs, len);
    bs->assembled = *data;
    bs->assembled_len = len;
  }

  return len;
}

static guint8*
gst_bytestream_assemble (GstByteStream *bs, guint32 len)
{
  guint8 *data = g_malloc (len);
  GSList *walk;
  guint32 copied = 0;
  GstBuffer *buf;

  /* copy the data from the curbuf */
  buf = GST_BUFFER (bs->buflist->data);
  bs_print ("assemble: copying %d bytes from curbuf at %d to *data", bs->headbufavail,
	    GST_BUFFER_SIZE (buf) - bs->headbufavail);
  memcpy (data, GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf) - bs->headbufavail, bs->headbufavail);
  copied += bs->headbufavail;

  /* asumption is made that the buffers all exist in the list */
  walk = g_slist_next (bs->buflist);
  while (copied < len) {
    buf = GST_BUFFER (walk->data);
    if (GST_BUFFER_SIZE (buf) < (len - copied)) {
      bs_print ("assemble: copying %d bytes from buf to output offset %d", GST_BUFFER_SIZE (buf), copied);
      memcpy (data + copied, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      copied += GST_BUFFER_SIZE (buf);
    }
    else {
      bs_print ("assemble: copying %d bytes from buf to output offset %d", len - copied, copied);
      memcpy (data + copied, GST_BUFFER_DATA (buf), len - copied);
      copied = len;
    }
    walk = g_slist_next (walk);
  }

  return data;
}

gboolean
gst_bytestream_flush (GstByteStream *bs, guint32 len)
{
  bs_print ("flush: flushing %d bytes", len);

  if (len == 0)
    return TRUE;

  /* make sure we have enough */
  bs_print ("flush: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len))
      return FALSE;
    bs_print ("flush: there are now %d bytes in the list", bs->listavail);
  }

  gst_bytestream_flush_fast (bs, len);

  return TRUE;
}

void
gst_bytestream_flush_fast (GstByteStream *bs, guint32 len)
{
  GstBuffer *headbuf;

  if (len == 0)
    return;
		  
  g_assert (len <= bs->listavail);

  if (bs->assembled) {
    g_free (bs->assembled);
    bs->assembled = NULL;
  }

  /* update the byte offset */
  bs->offset += len;

  /* repeat until we've flushed enough data */
  while (len > 0) {
    headbuf = GST_BUFFER (bs->buflist->data);

    bs_print ("flush: analyzing buffer that's %d bytes long, offset %llu", GST_BUFFER_SIZE (headbuf),
	      GST_BUFFER_OFFSET (headbuf));

    /* if there's enough to complete the flush */
    if (bs->headbufavail > len) {
      /* just trim it off */
      bs_print ("flush: trimming %d bytes off end of headbuf", len);
      bs->headbufavail -= len;
      bs->listavail -= len;
      len = 0;

      /* otherwise we have to trim the whole buffer */
    }
    else {
      bs_print ("flush: removing head buffer completely");
      /* remove it from the list */
      bs->buflist = g_slist_delete_link (bs->buflist, bs->buflist);
      /* trim it from the avail size */
      bs->listavail -= bs->headbufavail;
      /* record that we've trimmed this many bytes */
      len -= bs->headbufavail;
      /* unref it */
      gst_buffer_unref (headbuf);

      /* record the new headbufavail */
      if (bs->buflist) {
	bs->headbufavail = GST_BUFFER_SIZE (GST_BUFFER (bs->buflist->data));
	bs_print ("flush: next headbuf is %d bytes", bs->headbufavail);
      }
      else {
	bs_print ("flush: no more bytes at all");
      }
    }

    bs_print ("flush: bottom of while(), len is now %d", len);
  }
}

gboolean
gst_bytestream_seek (GstByteStream *bs, gint64 offset, GstSeekType method)
{
  GstRealPad *peer;
  
  g_return_val_if_fail (bs != NULL, FALSE);
  
  peer = GST_RPAD_PEER (bs->pad);

  bs_print ("bs: send event\n");
  if (gst_pad_send_event (GST_PAD (peer), gst_event_new_seek (
			  GST_FORMAT_BYTES | 
			  (method & GST_SEEK_METHOD_MASK) | 
			  GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 
			  offset))) 
  {
    gst_bytestream_flush_fast (bs, bs->listavail);

    /* we set the seek flag here. We cannot pull the pad here
     * bacause a seek might occur outisde of the pads cothread context */
    bs->in_seek = TRUE;
    
    return TRUE;
  }
  bs_print ("bs: send event failed\n");
  return FALSE;
}

guint64
gst_bytestream_tell (GstByteStream *bs)
{
  GstFormat format;
  gint64 value;
  
  g_return_val_if_fail (bs != NULL, -1);

  format = GST_FORMAT_BYTES;

  if (gst_pad_query (GST_PAD_PEER (bs->pad), GST_QUERY_POSITION, &format, &value)) {
    return value - bs->listavail;
  }
  
  return -1;
}

guint64
gst_bytestream_length (GstByteStream *bs)
{
  GstFormat format;
  gint64 value;
  
  g_return_val_if_fail (bs != NULL, -1);

  format = GST_FORMAT_BYTES;

  if (gst_pad_query (GST_PAD_PEER (bs->pad), GST_QUERY_TOTAL, &format, &value)) 
    return value;
  
  return -1;
}

guint32
gst_bytestream_read (GstByteStream *bs, GstBuffer** buf, guint32 len)
{
  guint32 len_peeked;

  g_return_val_if_fail (bs != NULL, -1);
  
  len_peeked = gst_bytestream_peek (bs, buf, len);
  if (len_peeked == 0)
    return 0;

  gst_bytestream_flush_fast (bs, len_peeked);

  return len_peeked;
}

/**
 * gst_bytestream_size_hint
 * @bs: a bytestream
 * @size: the size to hint
 *
 * Give a hint that we are going to read chunks of the given size
 *
 * Returns: TRUE if the hint was accepted
 */
gboolean
gst_bytestream_size_hint (GstByteStream *bs, guint32 size)
{
  GstEvent *event;

  g_return_val_if_fail (bs != NULL, FALSE);

  event = gst_event_new_size (GST_FORMAT_BYTES, size);

  return gst_pad_send_event (GST_PAD_PEER (bs->pad), event);
}

/**
 * gst_bytestream_get_status
 * @bs: a bytestream
 * @avail_out: total number of bytes buffered
 * @event_out: an event
 *
 * When an event occurs, the bytestream will return NULL.  You must
 * retrieve the event using this API before reading more bytes from
 * the stream.
 *
 * It is possible for the bytestream to return NULL due to running
 * out of buffers, however, this indicates a bug because an EOS
 * event should have been sent.
 */
void
gst_bytestream_get_status (GstByteStream *bs,
			   guint32 	 *avail_out,
			   GstEvent 	**event_out)
{
  if (avail_out)
    *avail_out = bs->listavail;

  if (event_out) {
    *event_out = bs->event;
    bs->event = NULL;
  }
}

/**
 * gst_bytestream_get_timestamp
 * @bs: a bytestream
 *
 * Get the timestamp of the first data in the bytestream.  If no data
 * exists 1 byte is read to load a new buffer.
 *
 * This function will not check input buffer boundries.  It is  possible
 * the next read could span two or more input buffers with different
 * timestamps.
 */
guint64
gst_bytestream_get_timestamp (GstByteStream *bs)
{
  GstBuffer *headbuf;

  g_return_val_if_fail (bs != NULL, 0);

  bs_print ("get_timestamp: getting timestamp");

  /* make sure we have a buffer */
  if (bs->listavail == 0) {
    bs_print ("gst_timestamp: fetching a buffer");
    if (!gst_bytestream_fill_bytes (bs, 1))
      return 0;
  }

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  return GST_BUFFER_TIMESTAMP (headbuf);
}

void
gst_bytestream_print_status (GstByteStream * bs)
{
  GSList *walk;
  GstBuffer *buf;

  bs_print ("STATUS: head buffer has %d bytes available", bs->headbufavail);
  bs_print ("STATUS: list has %d bytes available", bs->listavail);
  walk = bs->buflist;
  while (walk) {
    buf = GST_BUFFER (walk->data);
    walk = g_slist_next (walk);

    bs_print ("STATUS: buffer starts at %llu and is %d bytes long", GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf));
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gst_plugin_set_longname (plugin, "GstByteStream: a byte-oriented layer on top of buffer-passing");
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstbytestream",
  plugin_init
};
