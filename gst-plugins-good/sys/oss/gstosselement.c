/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "gstosselement.h"
#include "gstossmixer.h"

enum {
  ARG_0,
  ARG_DEVICE,
  ARG_MIXERDEV,
};

/* elementfactory information */
static GstElementDetails gst_osselement_details = {
  "Audio Element (OSS)",
  "Generic/Audio",
  "LGPL",
  "Generic OSS element",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

static void 			gst_osselement_class_init	(GstOssElementClass *klass);
static void 			gst_osselement_init		(GstOssElement *oss);
static void 			gst_osselement_dispose		(GObject *object);

static void 			gst_osselement_set_property	(GObject *object,
								 guint prop_id, 
								 const GValue *value,
								 GParamSpec *pspec);
static void 			gst_osselement_get_property	(GObject *object,
								 guint prop_id,
								 GValue *value,
								 GParamSpec *pspec);
static GstElementStateReturn 	gst_osselement_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_osssrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_osselement_get_type (void) 
{
  static GType osselement_type = 0;

  if (!osselement_type) {
    static const GTypeInfo osselement_info = {
      sizeof(GstOssElementClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_osselement_class_init,
      NULL,
      NULL,
      sizeof(GstOssElement),
      0,
      (GInstanceInitFunc)gst_osselement_init,
    };
    static const GInterfaceInfo ossiface_info = {
      (GInterfaceInitFunc) gst_oss_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo ossmixer_info = {
      (GInterfaceInitFunc) gst_ossmixer_interface_init,
      NULL,
      NULL,
    };

    osselement_type = g_type_register_static (GST_TYPE_ELEMENT,
					      "GstOssElement",
					      &osselement_info, 0);
    g_type_add_interface_static (osselement_type,
				 GST_TYPE_INTERFACE,
				 &ossiface_info);
    g_type_add_interface_static (osselement_type,
				 GST_TYPE_MIXER,
				 &ossmixer_info);
  }

  return osselement_type;
}

static void
gst_osselement_class_init (GstOssElementClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string ("device", "device", "oss device (/dev/dspN usually)",
                         "default", G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MIXERDEV,
    g_param_spec_string ("mixerdev", "mixer device",
			 "oss mixer device (/dev/mixerN usually)",
                         "default", G_PARAM_READWRITE));
  
  gobject_class->set_property = gst_osselement_set_property;
  gobject_class->get_property = gst_osselement_get_property;
  gobject_class->dispose      = gst_osselement_dispose;

  gstelement_class->change_state = gst_osselement_change_state;
}

static void 
gst_osselement_init (GstOssElement *oss) 
{
  oss->device = g_strdup ("/dev/dsp");
  oss->mixer_dev = g_strdup ("/dev/mixer");
  oss->fd = -1;
  oss->mixer_fd = -1;
  oss->channellist = NULL;

  gst_osselement_reset (oss);
}

static void
gst_osselement_dispose (GObject *object)
{
  GstOssElement *oss = (GstOssElement *) object;

  g_free (oss->device);
  g_free (oss->mixer_dev);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void 
gst_osselement_reset (GstOssElement *oss) 
{
  oss->law = 0;
  oss->endianness = G_BYTE_ORDER;
  oss->sign = TRUE;
  oss->width = 16;
  oss->depth = 16;
  oss->channels = 2;
  oss->rate = 44100;
  oss->fragment = 6;
  oss->bps = 0;

/* AFMT_*_BE not available on all OSS includes (e.g. FBSD) */
#ifdef WORDS_BIGENDIAN
  oss->format = AFMT_S16_BE;
#else
  oss->format = AFMT_S16_LE;
#endif /* WORDS_BIGENDIAN */  
}

static gboolean 
gst_ossformat_get (gint law, gint endianness, gboolean sign, gint width, gint depth,
		   gint *format, gint *bps) 
{
  if (width != depth) 
    return FALSE;

  *bps = 1;

  if (law == 0) {
    if (width == 16) {
      if (sign == TRUE) {
        if (endianness == G_LITTLE_ENDIAN) {
	  *format = AFMT_S16_LE;
	  GST_DEBUG (
	             "16 bit signed LE, no law (%d)", *format);
	}
        else if (endianness == G_BIG_ENDIAN) {
	  *format = AFMT_S16_BE;
	  GST_DEBUG (
	             "16 bit signed BE, no law (%d)", *format);
	}
      }
      else {
        if (endianness == G_LITTLE_ENDIAN) {
	  *format = AFMT_U16_LE;
	  GST_DEBUG (
	             "16 bit unsigned LE, no law (%d)", *format);
	}
        else if (endianness == G_BIG_ENDIAN) {
	  *format = AFMT_U16_BE;
	  GST_DEBUG (
	             "16 bit unsigned BE, no law (%d)", *format);
	}
      }
      *bps = 2;
    }
    else if (width == 8) {
      if (sign == TRUE) {
	*format = AFMT_S8;
	GST_DEBUG (
	           "8 bit signed, no law (%d)", *format);
      }
      else {
        *format = AFMT_U8;
	GST_DEBUG (
	           "8 bit unsigned, no law (%d)", *format);
      }
      *bps = 1;
    }
  } else if (law == 1) {
    *format = AFMT_MU_LAW;
    GST_DEBUG (
	       "mu law (%d)", *format);
  } else if (law == 2) {
    *format = AFMT_A_LAW;
    GST_DEBUG (
	       "a law (%d)", *format);
  } else {
    g_critical ("unknown law");
    return FALSE;
  }

  return TRUE;
}

gboolean 
gst_osselement_parse_caps (GstOssElement *oss, GstCaps *caps) 
{
  gint bps, format;

  gst_caps_get_int (caps, "width", &oss->width);
  gst_caps_get_int (caps, "depth", &oss->depth);
	        
  if (oss->width != oss->depth) 
    return FALSE;
		  
  gst_caps_get_int (caps, "law", &oss->law); 
  gst_caps_get_int (caps, "endianness", &oss->endianness);
  gst_caps_get_boolean (caps, "signed", &oss->sign);
			    
  if (!gst_ossformat_get (oss->law, oss->endianness, oss->sign, 
                          oss->width, oss->depth, &format, &bps))
  { 
     GST_DEBUG ("could not get format");
     return FALSE;
  }

  gst_caps_get_int (caps, "channels", &oss->channels);
  gst_caps_get_int (caps, "rate", &oss->rate);
			      
  oss->bps = bps * oss->channels * oss->rate;
  oss->format = format;

  return TRUE;
}

#define GET_FIXED_INT(caps, name, dest)         \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_int (caps, name, dest);        \
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest)     \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_boolean (caps, name, dest);    \
} G_STMT_END

gboolean 
gst_osselement_merge_fixed_caps (GstOssElement *oss, GstCaps *caps) 
{
  gint bps, format;

  /* peel off fixed stuff from the caps */
  GET_FIXED_INT         (caps, "law",        &oss->law);
  GET_FIXED_INT         (caps, "endianness", &oss->endianness);
  GET_FIXED_BOOLEAN     (caps, "signed",     &oss->sign);
  GET_FIXED_INT         (caps, "width",      &oss->width);
  GET_FIXED_INT         (caps, "depth",      &oss->depth);

  if (!gst_ossformat_get (oss->law, oss->endianness, oss->sign, 
                          oss->width, oss->depth, &format, &bps))
  { 
     return FALSE;
  }

  GET_FIXED_INT         (caps, "rate",       &oss->rate);
  GET_FIXED_INT         (caps, "channels",   &oss->channels);
			      
  oss->bps = bps * oss->channels * oss->rate;
  oss->format = format;
					  
  return TRUE;
}

gboolean 
gst_osselement_sync_parms (GstOssElement *oss) 
{
  audio_buf_info space;
  int frag;
  gint target_format;
  gint target_channels;
  gint target_rate;
  gint fragscale, frag_ln;

  if (oss->fd == -1)
    return FALSE;
  
  if (oss->fragment >> 16)
    frag = oss->fragment;
  else
    frag = 0x7FFF0000 | oss->fragment;
  
  GST_INFO ("osselement: setting sound card to %dHz %d format %s (%08x fragment)",
            oss->rate, oss->format,
            (oss->channels == 2) ? "stereo" : "mono", frag);

  ioctl (oss->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl (oss->fd, SNDCTL_DSP_RESET, 0);

  target_format   = oss->format;
  target_channels = oss->channels;
  target_rate     = oss->rate;

  ioctl (oss->fd, SNDCTL_DSP_SETFMT,   &oss->format);
  ioctl (oss->fd, SNDCTL_DSP_CHANNELS, &oss->channels);
  ioctl (oss->fd, SNDCTL_DSP_SPEED,    &oss->rate);

  ioctl (oss->fd, SNDCTL_DSP_GETBLKSIZE, &oss->fragment_size);

  if (oss->mode == GST_OSSELEMENT_WRITE) {
    ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &space);
  }
  else {
    ioctl (oss->fd, SNDCTL_DSP_GETISPACE, &space);
  }

  /* calculate new fragment using a poor man's logarithm function */
  fragscale = 1;
  frag_ln = 0;
  while (fragscale < space.fragsize) {
    fragscale <<= 1;
    frag_ln++;
  }
  oss->fragment = space.fragstotal << 16 | frag_ln;
	  
  GST_INFO ("osselement: set sound card to %dHz, %d format, %s "
	    "(%d bytes buffer, %08x fragment)",
            oss->rate, oss->format,
            (oss->channels == 2) ? "stereo" : "mono", 
	    space.bytes, oss->fragment);

  oss->fragment_time = (GST_SECOND * oss->fragment_size) / oss->bps;
  GST_INFO ("fragment time %u %" G_GUINT64_FORMAT "\n", 
            oss->bps, oss->fragment_time);

  if (target_format   != oss->format   ||
      target_channels != oss->channels ||
      target_rate     != oss->rate) 
  {
    g_warning ("couldn't set requested OSS parameters, enjoy the noise :)");
    /* we could eventually return FALSE here, or just do some additional tests
     * to see that the frequencies don't differ too much etc.. */
  }
  return TRUE;
}

static gboolean
gst_osselement_open_audio (GstOssElement *oss)
{
  gint caps;
  GstOssOpenMode mode = GST_OSSELEMENT_READ;
  const GList *padlist;

  g_return_val_if_fail (oss->fd == -1, FALSE);
  GST_INFO ("osselement: attempting to open sound device");

  /* Ok, so how do we open the device? We assume that we have (max.) one
   * pad, and if this is a sinkpad, we're osssink (w). else, we're osssrc (r) */
  padlist = gst_element_get_pad_list (GST_ELEMENT (oss));
  if (padlist != NULL) {
    GstPad *firstpad = padlist->data;
    if (GST_PAD_IS_SINK (firstpad)) {
      mode = GST_OSSELEMENT_WRITE;
    }
  }

  /* first try to open the sound card */
  if (mode == GST_OSSELEMENT_WRITE) {
    /* open non blocking first so that it returns immediatly with an error
     * when we cannot get to the device */
    oss->fd = open (oss->device, O_WRONLY | O_NONBLOCK);

    if (oss->fd >= 0) {
      close (oss->fd);
			  
      /* re-open the sound device in blocking mode */
      oss->fd = open (oss->device, O_WRONLY);
    }
  }
  else {
    oss->fd = open (oss->device, O_RDONLY);
  }

  if (oss->fd < 0) {
    switch (errno) {
      case EBUSY:
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Unable to open %s (in use ?)",
			   oss->device));
	break;
      case EISDIR:
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Device %s is a directory",
			   oss->device));
	break;
      case EACCES:
      case ETXTBSY:
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Cannot access %s, check permissions",
			   oss->device));
	break;
      case ENXIO:
      case ENODEV:
      case ENOENT:
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Cannot access %s, does it exist ?",
			   oss->device));
	break;
      case EROFS:
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Cannot access %s, read-only filesystem ?",
			   oss->device));
      default:
	/* FIXME: strerror is not threadsafe */
	gst_element_gerror(GST_ELEMENT (oss), GST_ERROR_UNKNOWN,
	  g_strdup ("unconverted error, file a bug"),
	  g_strdup_printf("osselement: Cannot open %s, generic error: %s",
			   oss->device, strerror (errno)));
	break;
    }
    return FALSE;
  }

  oss->mode = mode;

  /* we have it, set the default parameters and go have fun */
  /* set card state */
  ioctl (oss->fd, SNDCTL_DSP_GETCAPS, &caps);

  GST_INFO ("osselement: Capabilities %08x", caps);

  if (caps & DSP_CAP_DUPLEX)	GST_INFO ( "osselement:   Full duplex");
  if (caps & DSP_CAP_REALTIME) 	GST_INFO ( "osselement:   Realtime");
  if (caps & DSP_CAP_BATCH)    	GST_INFO ( "osselement:   Batch");
  if (caps & DSP_CAP_COPROC)   	GST_INFO ( "osselement:   Has coprocessor");
  if (caps & DSP_CAP_TRIGGER)  	GST_INFO ( "osselement:   Trigger");
  if (caps & DSP_CAP_MMAP)     	GST_INFO ( "osselement:   Direct access");

#ifdef DSP_CAP_MULTI
  if (caps & DSP_CAP_MULTI)    	GST_INFO ( "osselement:   Multiple open");
#endif /* DSP_CAP_MULTI */

#ifdef DSP_CAP_BIND
  if (caps & DSP_CAP_BIND)     	GST_INFO ( "osselement:   Channel binding");
#endif /* DSP_CAP_BIND */

  ioctl(oss->fd, SNDCTL_DSP_GETFMTS, &caps);

  GST_INFO ( "osselement: Formats %08x", caps);
  if (caps & AFMT_MU_LAW)  	GST_INFO ( "osselement:   MU_LAW");
  if (caps & AFMT_A_LAW)    	GST_INFO ( "osselement:   A_LAW");
  if (caps & AFMT_IMA_ADPCM)   	GST_INFO ( "osselement:   IMA_ADPCM");
  if (caps & AFMT_U8)    	GST_INFO ( "osselement:   U8");
  if (caps & AFMT_S16_LE)    	GST_INFO ( "osselement:   S16_LE");
  if (caps & AFMT_S16_BE)    	GST_INFO ( "osselement:   S16_BE");
  if (caps & AFMT_S8)    	GST_INFO ( "osselement:   S8");
  if (caps & AFMT_U16_LE)       GST_INFO ( "osselement:   U16_LE");
  if (caps & AFMT_U16_BE)    	GST_INFO ( "osselement:   U16_BE");
  if (caps & AFMT_MPEG)    	GST_INFO ( "osselement:   MPEG");
#ifdef AFMT_AC3
  if (caps & AFMT_AC3)    	GST_INFO ( "osselement:   AC3");
#endif

  GST_INFO ("osselement: opened audio (%s) with fd=%d",
	    oss->device, oss->fd);

  oss->caps = caps;

  gst_ossmixer_build_list (oss);

  return TRUE;
}

static void
gst_osselement_close_audio (GstOssElement *oss)
{
  if (oss->fd < 0) 
    return;

  gst_ossmixer_free_list (oss);
  close(oss->fd);
  oss->fd = -1;
}

gboolean
gst_osselement_convert (GstOssElement *oss,
			GstFormat      src_format,
			gint64         src_value,
	        	GstFormat     *dest_format,
			gint64        *dest_value)
{
  gboolean res = TRUE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (oss->bps == 0 || oss->channels == 0 || oss->width == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
	  *dest_value = src_value * GST_SECOND / oss->bps;
          break;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value / (oss->channels * oss->width);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * oss->bps / GST_SECOND;
          break;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * oss->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
	  *dest_value = src_value * GST_SECOND / oss->rate;
          break;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * oss->channels * oss->width;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static void 
gst_osselement_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec) 
{
  GstOssElement *oss = GST_OSSELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
      /* disallow changing the device while it is opened
         get_property("device") should return the right one */
      if (gst_element_get_state (GST_ELEMENT (oss)) != GST_STATE_NULL) {
        g_free (oss->device);
        oss->device = g_strdup (g_value_get_string (value));
      }
      break;
    case ARG_MIXERDEV:
      /* disallow changing the device while it is opened
         get_property("mixerdev") should return the right one */
      if (gst_element_get_state (GST_ELEMENT (oss)) != GST_STATE_NULL) {
        g_free (oss->mixer_dev);
        oss->mixer_dev = g_strdup (g_value_get_string (value));
      }
      break;
    default:
      break;
  }
}

static void 
gst_osselement_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec) 
{
  GstOssElement *oss = GST_OSSELEMENT (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, oss->device);
      break;
    case ARG_MIXERDEV:
      g_value_set_string (value, oss->mixer_dev);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn 
gst_osselement_change_state (GstElement *element) 
{
  GstOssElement *oss = GST_OSSELEMENT (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_osselement_open_audio (oss)) {
        return GST_STATE_FAILURE;
      }
      GST_INFO ("osselement: opened sound device");
      break;
    case GST_STATE_READY_TO_NULL:
      gst_osselement_close_audio (oss);
      gst_osselement_reset (oss);
      GST_INFO ("osselement: closed sound device");
      break;
    default:
      break;
  }
      
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean 
gst_osselement_factory_init (GstPlugin *plugin) 
{ 
  GstElementFactory *factory;

  factory = gst_element_factory_new ("osselement",
				     GST_TYPE_OSSELEMENT,
				     &gst_osselement_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
