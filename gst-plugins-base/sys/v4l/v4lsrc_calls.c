/* G-Streamer BT8x8/V4L frame grabber plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4lsrc_calls.h"
#include <sys/time.h>

/* number of buffers to be queued *at least* before syncing */
#define MIN_BUFFERS_QUEUED 2

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

#define DEBUG(format, args...) \
	GST_DEBUG_ELEMENT(GST_CAT_PLUGIN_INFO, \
		GST_ELEMENT(v4lsrc), \
		"V4LSRC: " format, ##args)

/* palette names */
char *palette_name[] = {
  "",                          /* 0 */
  "grayscale",                 /* VIDEO_PALETTE_GREY */
  "Hi-420",                    /* VIDEO_PALETTE_HI420 */
  "16-bit RGB (RGB-565)",      /* VIDEO_PALETTE_RB565 */
  "24-bit RGB",                /* VIDEO_PALETTE_RGB24 */
  "32-bit RGB",                /* VIDEO_PALETTE_RGB32 */
  "15-bit RGB (RGB-555)",      /* VIDEO_PALETTE_RGB555 */
  "YUV-4:2:2 (packed)",        /* VIDEO_PALETTE_YUV422 */
  "YUYV",                      /* VIDEO_PALETTE_YUYV */
  "UYVY",                      /* VIDEO_PALETTE_UYVY */
  "YUV-4:2:0 (packed)",        /* VIDEO_PALETTE_YUV420 */
  "YUV-4:1:1 (packed)",        /* VIDEO_PALETTE_YUV411 */
  "Raw",                       /* VIDEO_PALETTE_RAW */
  "YUV-4:2:2 (planar)",        /* VIDEO_PALETTE_YUV422P */
  "YUV-4:1:1 (planar)",        /* VIDEO_PALETTE_YUV411P */
  "YUV-4:2:0 (planar)",        /* VIDEO_PALETTE_YUV420P */
  "YUV-4:1:0 (planar)"         /* VIDEO_PALETTE_YUV410P */
};


/******************************************************
 * gst_v4lsrc_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lsrc_queue_frame (GstV4lSrc *v4lsrc,
                        gint      num)
{
  DEBUG("queueing frame %d", num);

  v4lsrc->mmap.frame = num;

  if (v4lsrc->frame_queued[num] < 0)
  {
    v4lsrc->frame_queued[num] = 0;
    return TRUE;
  }

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &(v4lsrc->mmap)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error queueing a buffer (%d): %s",
      num, g_strerror(errno));
    return FALSE;
  }

  v4lsrc->frame_queued[num] = 1;

  g_mutex_lock(v4lsrc->mutex_queued_frames);
  v4lsrc->num_queued_frames++;
  g_cond_broadcast(v4lsrc->cond_queued_frames);
  g_mutex_unlock(v4lsrc->mutex_queued_frames);

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_soft_sync_thread()
 *   syncs on frames and signals the main thread
 * purpose: actually get the correct frame timestamps
 ******************************************************/

static void *
gst_v4lsrc_soft_sync_thread (void *arg)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC(arg);
  gint frame = 0;

  DEBUG("starting software sync thread");

#if 0
  /* Allow easy shutting down by other processes... */
  pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL );
  pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );
#endif

  while (1)
  {
    /* are there queued frames left? */
    g_mutex_lock(v4lsrc->mutex_queued_frames);
    if (v4lsrc->num_queued_frames < MIN_BUFFERS_QUEUED)
    {
      if (v4lsrc->frame_queued[frame] < 0)
        break;

      DEBUG("Waiting for new frames to be queued (%d < %d)",
        v4lsrc->num_queued_frames, MIN_BUFFERS_QUEUED);

      g_cond_wait(v4lsrc->cond_queued_frames, v4lsrc->mutex_queued_frames);
    }
    g_mutex_unlock(v4lsrc->mutex_queued_frames);

    if (!v4lsrc->num_queued_frames)
    {
      DEBUG("Got signal to exit...");
      goto end;
    }

    /* sync on the frame */
    DEBUG("Sync\'ing on frame %d", frame);
retry:
    if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, &frame) < 0)
    {
      /* if the sync() got interrupted, we can retry */
      if (errno == EINTR)
        goto retry;
      gst_element_error(GST_ELEMENT(v4lsrc),
        "Error syncing on a buffer (%d): %s",
        frame, g_strerror(errno));
      g_mutex_lock(v4lsrc->mutex_soft_sync);
      v4lsrc->isready_soft_sync[frame] = -1;
      g_cond_broadcast(v4lsrc->cond_soft_sync[frame]);
      g_mutex_unlock(v4lsrc->mutex_soft_sync);
      goto end;
    }
    else
    {
      g_mutex_lock(v4lsrc->mutex_soft_sync);
      gettimeofday(&(v4lsrc->timestamp_soft_sync[frame]), NULL);
      v4lsrc->isready_soft_sync[frame] = 1;
      g_cond_broadcast(v4lsrc->cond_soft_sync[frame]);
      g_mutex_unlock(v4lsrc->mutex_soft_sync);
    }

    g_mutex_lock(v4lsrc->mutex_queued_frames);
    v4lsrc->num_queued_frames--;
    v4lsrc->frame_queued[frame] = 0;
    g_mutex_unlock(v4lsrc->mutex_queued_frames);

    frame = (frame+1)%v4lsrc->mbuf.frames;
  }

end:
  DEBUG("Software sync thread got signalled to exit");
  g_thread_exit(NULL);
  return NULL;
}


/******************************************************
 * gst_v4lsrc_sync_frame():
 *   sync on a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lsrc_sync_next_frame (GstV4lSrc *v4lsrc,
                            gint      *num)
{
  *num = v4lsrc->sync_frame = (v4lsrc->sync_frame + 1)%v4lsrc->mbuf.frames;

  DEBUG("syncing on next frame (%d)", *num);

  /* "software sync()" on the frame */
  g_mutex_lock(v4lsrc->mutex_soft_sync);
  if (v4lsrc->isready_soft_sync[*num] == 0)
  {
    DEBUG("Waiting for frame %d to be synced on", *num);
    g_cond_wait(v4lsrc->cond_soft_sync[*num], v4lsrc->mutex_soft_sync);
  }

  if (v4lsrc->isready_soft_sync[*num] < 0)
    return FALSE;
  v4lsrc->isready_soft_sync[*num] = 0;
  g_mutex_unlock(v4lsrc->mutex_soft_sync);

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_set_capture():
 *   set capture parameters, palette = VIDEO_PALETTE_*
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_set_capture (GstV4lSrc *v4lsrc,
                        gint      width,
                        gint      height,
                        gint      palette)
{
  DEBUG("capture properties set to width = %d, height = %d, palette = %d",
    width, height, palette);

  /*GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));*/
  /*GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));*/

  v4lsrc->mmap.width = width;
  v4lsrc->mmap.height = height;
  v4lsrc->mmap.format = palette;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_init (GstV4lSrc *v4lsrc)
{
  int n;

  DEBUG("initting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* request buffer info */
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCGMBUF, &(v4lsrc->mbuf)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error getting buffer information: %s",
      g_strerror(errno));
    return FALSE;
  }

  if (v4lsrc->mbuf.frames < MIN_BUFFERS_QUEUED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Too little buffers. We got %d, we want at least %d",
      v4lsrc->mbuf.frames, MIN_BUFFERS_QUEUED);
    return FALSE;
  }

  gst_info("Got %d buffers (\'%s\') of size %d KB\n",
    v4lsrc->mbuf.frames, palette_name[v4lsrc->mmap.format],
    v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));

  /* keep trakc of queued buffers */
  v4lsrc->frame_queued = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->frame_queued)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating buffer tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->frame_queued[n] = 0;

  /* init the GThread stuff */
  v4lsrc->mutex_soft_sync = g_mutex_new();
  v4lsrc->isready_soft_sync = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->isready_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync buffer tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->isready_soft_sync[n] = 0;
  v4lsrc->timestamp_soft_sync = (struct timeval *)
    malloc(sizeof(struct timeval) * v4lsrc->mbuf.frames);
  if (!v4lsrc->timestamp_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync timestamp tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  v4lsrc->cond_soft_sync = (GCond **) malloc( sizeof(GCond *) * v4lsrc->mbuf.frames);
  if (!v4lsrc->cond_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync condition tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->cond_soft_sync[n] = g_cond_new();

  v4lsrc->mutex_queued_frames = g_mutex_new();
  v4lsrc->cond_queued_frames = g_cond_new();

  /* Map the buffers */
  GST_V4LELEMENT(v4lsrc)->buffer = mmap(0, v4lsrc->mbuf.size, 
    PROT_READ|PROT_WRITE, MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
  if (GST_V4LELEMENT(v4lsrc)->buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error mapping video buffers: %s",
      g_strerror(errno));
    GST_V4LELEMENT(v4lsrc)->buffer = NULL;
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_start (GstV4lSrc *v4lsrc)
{
  GError *error = NULL;
  int n;

  DEBUG("starting capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  v4lsrc->num_queued_frames = 0;

  /* queue all buffers, this starts streaming capture */
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (!gst_v4lsrc_queue_frame(v4lsrc, n))
      return FALSE;

  v4lsrc->sync_frame = -1;

  /* start the sync() thread (correct timestamps) */
  v4lsrc->thread_soft_sync = g_thread_create(gst_v4lsrc_soft_sync_thread,
    (void *) v4lsrc, TRUE, &error);
  if (!v4lsrc->thread_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Failed to create software sync thread: %s",error->message);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_grab_frame():
 *   capture one frame during streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_grab_frame (GstV4lSrc *v4lsrc, gint *num)
{
  DEBUG("grabbing frame");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* syncing on the buffer grabs it */
  if (!gst_v4lsrc_sync_next_frame(v4lsrc, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_get_buffer():
 *   get the address of the just-capture buffer
 * return value: the buffer's address or NULL
 ******************************************************/

guint8 *
gst_v4lsrc_get_buffer (GstV4lSrc *v4lsrc, gint  num)
{
  DEBUG("gst_v4lsrc_get_buffer(), num = %d", num);

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc)) ||
      !GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lsrc)))
    return NULL;

  if (num < 0 || num >= v4lsrc->mbuf.frames)
    return NULL;

  return GST_V4LELEMENT(v4lsrc)->buffer+v4lsrc->mbuf.offsets[num];
}


/******************************************************
 * gst_v4lsrc_requeue_frame():
 *   re-queue a frame after we're done with the buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_requeue_frame (GstV4lSrc *v4lsrc, gint  num)
{
  DEBUG("requeueing frame %d", num);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* and let's queue the buffer */
  if (!gst_v4lsrc_queue_frame(v4lsrc, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_stop (GstV4lSrc *v4lsrc)
{
  int n;

  DEBUG("stopping capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* we actually need to sync on all queued buffers but not on the non-queued ones */
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->frame_queued[n] = -1;

  g_thread_join(v4lsrc->thread_soft_sync);

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_deinit (GstV4lSrc *v4lsrc)
{
  int n;

  DEBUG("quitting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* free buffer tracker */
  g_mutex_free(v4lsrc->mutex_queued_frames);
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    g_cond_free(v4lsrc->cond_soft_sync[n]);
  free(v4lsrc->frame_queued);
  free(v4lsrc->cond_soft_sync);
  free(v4lsrc->isready_soft_sync);
  free(v4lsrc->timestamp_soft_sync);

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lsrc)->buffer, v4lsrc->mbuf.size);
  GST_V4LELEMENT(v4lsrc)->buffer = NULL;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_try_palette():
 *   try out a palette on the device
 *   This has to be done before initializing the
 *   actual capture system, to make sure we don't
 *   mess up anything. So we need to mini-mmap()
 *   a buffer here, queue and sync on one buffer,
 *   and unmap it.
 *   This is ugly, yes, I know - but it's a major
 *   design flaw of v4l1 that you don't know in
 *   advance which formats will be supported...
 *   This is better than "just assuming that it'll
 *   work"...
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_try_palette (GstV4lSrc *v4lsrc,
                        gint       palette)
{
  /* so, we need a buffer and some more stuff */
  int frame = 0;
  guint8 *buffer;
  struct video_mbuf vmbuf;
  struct video_mmap vmmap;

  DEBUG("gonna try out palette format %d (%s)",
    palette, palette_name[palette]);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* let's start by requesting a buffer and mmap()'ing it */
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCGMBUF, &vmbuf) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error getting buffer information: %s",
      g_strerror(errno));
    return FALSE;
  }
  /* Map the buffers */
  buffer = mmap(0, vmbuf.size, PROT_READ|PROT_WRITE,
                MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
  if (buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error mapping our try-out buffer: %s",
      g_strerror(errno));
    return FALSE;
  }

  /* now that we have a buffer, let's try out our format */
  vmmap.width = GST_V4LELEMENT(v4lsrc)->vcap.minwidth;
  vmmap.height = GST_V4LELEMENT(v4lsrc)->vcap.minheight;
  vmmap.format = palette;
  vmmap.frame = frame;
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &vmmap) < 0)
  {
    if (errno != EINVAL) /* our format failed! */
      gst_element_error(GST_ELEMENT(v4lsrc),
        "Error queueing our try-out buffer: %s",
        g_strerror(errno));
    munmap(buffer, vmbuf.size);
    return FALSE;
  }

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, &frame) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error syncing on a buffer (%d): %s",
      frame, g_strerror(errno));
    munmap(buffer, vmbuf.size);
    return FALSE;
  }

  munmap(buffer, vmbuf.size);

  /* if we got here, it worked! woohoo, the format is supported! */
  return TRUE;
}

