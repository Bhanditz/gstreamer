/*
 * GStreamer
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixerctrl.h"
#include "gstsunaudiomixertrack.h"

#define SCALE_FACTOR 2.55       /* 255/100 */

static gboolean
gst_sunaudiomixer_ctrl_open (GstSunAudioMixerCtrl * mixer)
{
  int fd;

  /* First try to open non-blocking */
  fd = open (mixer->device, O_RDWR | O_NONBLOCK);

  if (fd >= 0) {
    close (fd);
    fd = open (mixer->device, O_WRONLY);
  }

  if (fd == -1) {
    GST_DEBUG_OBJECT (mixer,
        "Failed to open mixer device %s, mixing disabled: %s", mixer->device,
        strerror (errno));

    return FALSE;
  }

  mixer->mixer_fd = fd;
  return TRUE;
}

void
gst_sunaudiomixer_ctrl_build_list (GstSunAudioMixerCtrl * mixer)
{
  GstMixerTrack *track;

  g_return_if_fail (mixer->mixer_fd != -1);

  /*
   * Do not continue appending the same 3 static tracks onto the list
   */
  if (mixer->tracklist == NULL) {
    track = gst_sunaudiomixer_track_new (0, 1, GST_MIXER_TRACK_OUTPUT);
    mixer->tracklist = g_list_append (mixer->tracklist, track);
    track = gst_sunaudiomixer_track_new (1, 1, GST_MIXER_TRACK_INPUT);
    mixer->tracklist = g_list_append (mixer->tracklist, track);
    track = gst_sunaudiomixer_track_new (2, 1, GST_MIXER_TRACK_OUTPUT);
    mixer->tracklist = g_list_append (mixer->tracklist, track);
  }
}

GstSunAudioMixerCtrl *
gst_sunaudiomixer_ctrl_new (const char *device)
{
  GstSunAudioMixerCtrl *ret = NULL;

  g_return_val_if_fail (device != NULL, NULL);

  ret = g_new0 (GstSunAudioMixerCtrl, 1);

  ret->device = g_strdup (device);
  ret->mixer_fd = -1;
  ret->tracklist = NULL;

  if (!gst_sunaudiomixer_ctrl_open (ret))
    goto error;

  return ret;

error:
  if (ret)
    gst_sunaudiomixer_ctrl_free (ret);

  return NULL;
}

void
gst_sunaudiomixer_ctrl_free (GstSunAudioMixerCtrl * mixer)
{
  g_return_if_fail (mixer != NULL);

  if (mixer->device) {
    g_free (mixer->device);
    mixer->device = NULL;
  }

  if (mixer->tracklist) {
    g_list_foreach (mixer->tracklist, (GFunc) g_object_unref, NULL);
    g_list_free (mixer->tracklist);
    mixer->tracklist = NULL;
  }

  if (mixer->mixer_fd != -1) {
    close (mixer->mixer_fd);
    mixer->mixer_fd = -1;
  }

  g_free (mixer);
}

const GList *
gst_sunaudiomixer_ctrl_list_tracks (GstSunAudioMixerCtrl * mixer)
{
  gst_sunaudiomixer_ctrl_build_list (mixer);
  return (const GList *) mixer->tracklist;
}

void
gst_sunaudiomixer_ctrl_get_volume (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes)
{
  struct audio_info audioinfo;
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device volume");
    return;
  }

  switch (sunaudiotrack->track_num) {
    case 0:
      sunaudiotrack->vol = volumes[0] =
          (audioinfo.play.gain / SCALE_FACTOR) + 0.5;
      break;
    case 1:
      sunaudiotrack->vol = volumes[0] =
          (audioinfo.record.gain / SCALE_FACTOR) + 0.5;
      break;
    case 2:
      sunaudiotrack->vol = volumes[0] =
          (audioinfo.monitor_gain / SCALE_FACTOR) + 0.5;
      break;
  }
}

void
gst_sunaudiomixer_ctrl_set_volume (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  struct audio_info audioinfo;
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);

  if (volume < 0)
    volume = 0;

  volume = volumes[0] * SCALE_FACTOR;

  /* Set the volume */
  AUDIO_INITINFO (&audioinfo);

  switch (sunaudiotrack->track_num) {
    case 0:
      audioinfo.play.gain = volume;
      break;
    case 1:
      audioinfo.record.gain = volume;
      break;
    case 2:
      audioinfo.monitor_gain = volume;
      break;
  }

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio device volume");
    return;
  }

  sunaudiotrack->vol = volume;
}

void
gst_sunaudiomixer_ctrl_set_mute (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gboolean mute)
{
  struct audio_info audioinfo;
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);
  gint volume;

  g_return_if_fail (mixer->mixer_fd != -1);

  AUDIO_INITINFO (&audioinfo);

  if (mute) {
    volume = 0;
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    volume = sunaudiotrack->vol;
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }

  switch (sunaudiotrack->track_num) {
    case 0:

      if (mute)
        audioinfo.output_muted = 1;
      else
        audioinfo.output_muted = 0;

      audioinfo.play.gain = volume;
      break;
    case 1:
      audioinfo.record.gain = volume;
      break;
    case 2:
      audioinfo.monitor_gain = volume;
      break;
  }

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio device volume");
    return;
  }
}

void
gst_sunaudiomixer_ctrl_set_record (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gboolean record)
{
  /* Implementation Pending */
}
