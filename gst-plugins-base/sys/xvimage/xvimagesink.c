/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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

/* Our interfaces */
#include <gst-libs/gst/navigation/navigation.h>
#include <gst-libs/gst/xoverlay/xoverlay.h>

/* Object header */
#include "xvimagesink.h"

/* ElementFactory information */
static GstElementDetails gst_xvimagesink_details = GST_ELEMENT_DETAILS (
  "Video sink",
  "Sink/Video",
  "A Xv based videosink",
  "Julien Moutte <julien@moutte.net>"
);

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
GST_PAD_TEMPLATE_FACTORY (gst_xvimagesink_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW ("xvimagesink_rgbsink", "video/x-raw-rgb",
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
                "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
                "height", GST_PROPS_INT_RANGE (0, G_MAXINT)),
  GST_CAPS_NEW ("xvimagesink_yuvsink", "video/x-raw-yuv",
                "framerate", GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT),
                "width", GST_PROPS_INT_RANGE(0, G_MAXINT),
                "height", GST_PROPS_INT_RANGE(0, G_MAXINT))
)

static GstElementClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* Interfaces stuff */

static gboolean
gst_xvimagesink_interface_supported (GstInterface *iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION ||
	    type == GST_TYPE_X_OVERLAY);

  return (GST_STATE (iface) != GST_STATE_NULL);
}

static void
gst_xvimagesink_interface_init (GstInterfaceClass *klass)
{
  klass->supported = gst_xvimagesink_interface_supported;
}

static void
gst_xvimagesink_navigation_send_event (GstNavigation *navigation, GstStructure *structure)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (navigation);
  XWindowAttributes attr; 
  GstEvent *event;
  double x,y;

  g_mutex_lock (xvimagesink->x_lock);
  XGetWindowAttributes (xvimagesink->xcontext->disp,
                        xvimagesink->xwindow->win, &attr); 
  g_mutex_unlock (xvimagesink->x_lock);

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;
  
  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x *= xvimagesink->width;
    x /= attr.width;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y *= xvimagesink->height;
    y /= attr.height;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  gst_pad_send_event (gst_pad_get_peer (xvimagesink->sinkpad), event);
}

static void
gst_xvimagesink_navigation_init (GstNavigationInterface *iface)
{
  iface->send_event = gst_xvimagesink_navigation_send_event;
}

/* X11 stuff */

/* This function handles GstXvImage creation depending on XShm availability */
static GstXvImage *
gst_xvimagesink_xvimage_new (GstXvImageSink *xvimagesink,
                             gint width, gint height)
{
  GstXvImage *xvimage = NULL;
  
  g_return_val_if_fail (xvimagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);
  
  xvimage = g_new0 (GstXvImage, 1);
  
  xvimage->width = width;
  xvimage->height = height;
  xvimage->data = NULL;
  
  g_mutex_lock (xvimagesink->x_lock);

  xvimage->size =  (xvimagesink->xcontext->bpp / 8) * xvimage->width * xvimage->height;
  
#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm)
    {
      xvimage->xvimage = XvShmCreateImage (xvimagesink->xcontext->disp,
                                           xvimagesink->xcontext->xv_port_id,
                                           xvimagesink->xcontext->im_format,
                                           NULL, xvimage->width,
                                           xvimage->height, &xvimage->SHMInfo);
      
      xvimage->SHMInfo.shmid = shmget (IPC_PRIVATE, xvimage->size,
                                       IPC_CREAT | 0777);
  
      xvimage->SHMInfo.shmaddr = shmat (xvimage->SHMInfo.shmid, 0, 0);
      xvimage->xvimage->data = xvimage->SHMInfo.shmaddr;
  
      xvimage->SHMInfo.readOnly = FALSE;
  
      XShmAttach (xvimagesink->xcontext->disp, &xvimage->SHMInfo);
    }
  else
    {
      xvimage->xvimage = XvCreateImage (xvimagesink->xcontext->disp,
                                        xvimagesink->xcontext->xv_port_id,
                                        xvimagesink->xcontext->im_format,
                                        xvimage->data,
                                        xvimage->width, xvimage->height);
      
      xvimage->data = g_malloc (xvimage->xvimage->data_size);
    }
#else
  xvimage->xvimage = XvCreateImage (xvimagesink->xcontext->disp,
                                    xvimagesink->xcontext->xv_port_id,
                                    xvimagesink->xcontext->im_format,
                                    xvimage->data,
                                    xvimage->width, xvimage->height);
  
  xvimage->data = g_malloc (xvimage->xvimage->data_size);
#endif /* HAVE_XSHM */
  
  if (xvimage->xvimage)
    {
      XSync(xvimagesink->xcontext->disp, 0);
    }
  else
    {
      if (xvimage->data)
        g_free (xvimage->data);
      
      g_free (xvimage);
      
      xvimage = NULL;
    }
    
  g_mutex_unlock (xvimagesink->x_lock);
  
  return xvimage;
}

/* This function destroys a GstXvImage handling XShm availability */ 
static void
gst_xvimagesink_xvimage_destroy (GstXvImageSink *xvimagesink,
                                 GstXvImage *xvimage)
{
  g_return_if_fail (xvimage != NULL);
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  g_mutex_lock (xvimagesink->x_lock);
  
#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm)
    {
      if (xvimage->SHMInfo.shmaddr)
        XShmDetach (xvimagesink->xcontext->disp, &xvimage->SHMInfo);
  
      if (xvimage->xvimage)
        XFree (xvimage->xvimage);
  
      if (xvimage->SHMInfo.shmaddr)
        shmdt (xvimage->SHMInfo.shmaddr);
  
      if (xvimage->SHMInfo.shmid > 0)
        shmctl (xvimage->SHMInfo.shmid, IPC_RMID, 0);
    }
  else
    {
      if (xvimage->xvimage)
        XFree (xvimage->xvimage);
    }
#else
  if (xvimage->xvimage)
    XFree (xvimage->xvimage);
#endif /* HAVE_XSHM */ 
  
  g_mutex_unlock (xvimagesink->x_lock);
  
  g_free (xvimage);
}

/* This function puts a GstXvImage on a GstXvImageSink's window */
static void
gst_xvimagesink_xvimage_put (GstXvImageSink *xvimagesink, GstXvImage *xvimage)
{
  XWindowAttributes attr; 
  
  g_return_if_fail (xvimage != NULL);
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  g_mutex_lock (xvimagesink->x_lock);
  
  /* We scale to the window's geometry */
  XGetWindowAttributes (xvimagesink->xcontext->disp,
                        xvimagesink->xwindow->win, &attr); 

#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm)
    {  
      XvShmPutImage (xvimagesink->xcontext->disp,
                     xvimagesink->xcontext->xv_port_id,
                     xvimagesink->xwindow->win, 
                     xvimagesink->xwindow->gc, xvimage->xvimage,
                     0, 0, xvimage->width, xvimage->height,
                     0, 0, attr.width, attr.height, FALSE);
    }
  else
    {
      XvPutImage (xvimagesink->xcontext->disp,
                  xvimagesink->xcontext->xv_port_id,
                  xvimagesink->xwindow->win, 
                  xvimagesink->xwindow->gc, xvimage->xvimage,
                  0, 0, xvimage->width, xvimage->height,
                  0, 0, attr.width, attr.height);
    }
#else
  XvPutImage (xvimagesink->xcontext->disp,
              xvimagesink->xcontext->xv_port_id,
              xvimagesink->xwindow->win, 
              xvimagesink->xwindow->gc, xvimage->xvimage,
              0, 0, xvimage->width, xvimage->height,
              0, 0, attr.width, attr.height);
#endif /* HAVE_XSHM */
  
  XSync(xvimagesink->xcontext->disp, FALSE);
  
  g_mutex_unlock (xvimagesink->x_lock);
}

/* This function handles a GstXWindow creation */
static GstXWindow *
gst_xvimagesink_xwindow_new (GstXvImageSink *xvimagesink,
                             gint width, gint height)
{
  GstXWindow *xwindow = NULL;
  XGCValues values;
  
  g_return_val_if_fail (xvimagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);
  
  xwindow = g_new0 (GstXWindow, 1);
  
  xwindow->width = width;
  xwindow->height = height;
  
  g_mutex_lock (xvimagesink->x_lock);
  
  xwindow->win = XCreateSimpleWindow (xvimagesink->xcontext->disp,
                                      xvimagesink->xcontext->root, 
                                      0, 0, xwindow->width, xwindow->height, 
                                      0, 0, xvimagesink->xcontext->black);
  
  XSelectInput (xvimagesink->xcontext->disp, xwindow->win, PointerMotionMask |
                KeyPressMask | KeyReleaseMask | ButtonPressMask |
                ButtonReleaseMask);
  
  xwindow->gc = XCreateGC (xvimagesink->xcontext->disp,
                           xwindow->win, 0, &values);
  
  XMapRaised (xvimagesink->xcontext->disp, xwindow->win);
  
  g_mutex_unlock (xvimagesink->x_lock);
  
  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_xvimagesink_xwindow_destroy (GstXvImageSink *xvimagesink, GstXWindow *xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  g_mutex_lock (xvimagesink->x_lock);
  
  XDestroyWindow (xvimagesink->xcontext->disp, xwindow->win);
  
  XFreeGC (xvimagesink->xcontext->disp, xwindow->gc);
  
  g_mutex_unlock (xvimagesink->x_lock);
  
  g_free (xwindow);
}

/* This function resizes a GstXWindow */
/*static void
gst_xvimagesink_xwindow_resize (GstXvImageSink *xvimagesink,
                                GstXWindow  *xwindow,
                                gint width, gint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  g_mutex_lock (xvimagesink->x_lock);
  
  XResizeWindow (xvimagesink->xcontext->disp, xwindow->win, width, height);
  
  g_mutex_unlock (xvimagesink->x_lock);
} */

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_xvimagesink_handle_xevents (GstXvImageSink *xvimagesink, GstPad *pad)
{
  XEvent e;
  
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  /* We get all events on our window to throw them upstream */
  g_mutex_lock (xvimagesink->x_lock);
  while (XCheckWindowEvent (xvimagesink->xcontext->disp,
                            xvimagesink->xwindow->win,
                            PointerMotionMask | KeyPressMask |
                            KeyReleaseMask | ButtonPressMask |
                            ButtonReleaseMask, &e))
    {
      GstEvent *event = NULL;
      KeySym keysym;
      
      /* We lock only for the X function call */
      g_mutex_unlock (xvimagesink->x_lock);
      
      switch (e.type)
        {
          case MotionNotify:
            /* Mouse pointer moved over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("xvimagesink pointer moved over window at %d,%d",
                       e.xmotion.x, e.xmotion.y);
            gst_navigation_send_mouse_event (GST_NAVIGATION (xvimagesink),
                                             e.xmotion.x, e.xmotion.y);
            break;
          case ButtonPress:
          case ButtonRelease:
            /* Mouse button pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("xvimagesink button %d pressed over window at %d,%d",
                       e.xbutton.button, e.xbutton.x, e.xbutton.y);
            gst_navigation_send_mouse_event (GST_NAVIGATION (xvimagesink),
                                             e.xbutton.x, e.xbutton.y);
            break;
          case KeyPress:
          case KeyRelease:
            /* Key pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("xvimagesink key %d pressed over window at %d,%d",
                       e.xkey.keycode, e.xkey.x, e.xkey.y);
            keysym = XKeycodeToKeysym (xvimagesink->xcontext->disp,
                                       e.xkey.keycode, 0);
            gst_navigation_send_key_event (GST_NAVIGATION (xvimagesink),
                                           XKeysymToString (keysym));
            break;
          default:
            GST_DEBUG ("xvimagesink unhandled X event (%d)", e.type);
        }
        
      if (event)
        gst_pad_send_event (gst_pad_get_peer (pad), event);
      
      g_mutex_lock (xvimagesink->x_lock);
    }
  g_mutex_unlock (xvimagesink->x_lock);
}

static GstCaps *
gst_xvimagesink_get_xv_support (GstXContext *xcontext)
{
  gint i, nb_adaptors;
  XvAdaptorInfo *adaptors;
  
  g_return_val_if_fail (xcontext != NULL, NULL);
  
  /* First let's check that XVideo extension is available */
  if (!XQueryExtension (xcontext->disp, "XVideo", &i, &i, &i))
    return NULL;
  
  /* Then we get adaptors list */
  if (Success != XvQueryAdaptors (xcontext->disp, xcontext->root,
                                  &nb_adaptors, &adaptors))
    return NULL;
  
  xcontext->xv_port_id = 0;
  
  /* Now search for an adaptor that supports XvImageMask */
  for (i = 0; i < nb_adaptors && !xcontext->xv_port_id; i++)
    {
      if (adaptors[i].type & XvImageMask)
        {
          gint j;
          
          /* We found such an adaptor, looking for an available port */
          for (j = 0; j < adaptors[i].num_ports && !xcontext->xv_port_id; j++)
            {
              /* We try to grab the port */
              if (Success == XvGrabPort (xcontext->disp,
                                         adaptors[i].base_id + j, 0))
                {
                  xcontext->xv_port_id = adaptors[i].base_id + j;
                }
            }
        }
        
      GST_DEBUG ("XV Adaptor %s with %ld ports", adaptors[i].name,
                 adaptors[i].num_ports);
        
      XvFreeAdaptorInfo (&(adaptors[i]));
    }
    
  if (xcontext->xv_port_id)
    {
      gint nb_formats;
      XvImageFormatValues *formats = NULL;
      GstCaps *caps = NULL;
      
      /* We get all image formats supported by our port */
      formats = XvListImageFormats (xcontext->disp,
                                    xcontext->xv_port_id, &nb_formats);
      for (i = 0; i < nb_formats; i++)
        {
          GstCaps *format_caps = NULL;
          
          switch (formats[i].type)
            {
              case XvRGB:
                format_caps = GST_CAPS_NEW ("xvimagesink_caps",
                                            "video/x-raw-rgb", 
                                            "endianness", GST_PROPS_INT (xcontext->endianness),
                                            "depth", GST_PROPS_INT (xcontext->depth),
                                            "bpp", GST_PROPS_INT (xcontext->bpp),
                                            "blue_mask", GST_PROPS_INT (formats[i].red_mask),
                                            "green_mask", GST_PROPS_INT (formats[i].green_mask),
                                            "red_mask", GST_PROPS_INT (formats[i].blue_mask),
                                            "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                            "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));
                break;
              case XvYUV:
                format_caps = GST_CAPS_NEW ("xvimagesink_caps",
                                            "video/x-raw-yuv", 
                                            "format", GST_PROPS_FOURCC (formats[i].id),
                                            "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                            "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));
                break;
            }
          
          if (format_caps)
            caps = gst_caps_append (caps, format_caps);
        }
        
      if (formats)
        XFree (formats);
      
      GST_DEBUG ("Generated the following caps %s", gst_caps_to_string (caps));
      return caps;
    }
    
  return NULL;
}

/* This function get the X Display and global infos about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or 
   image creation */
static GstXContext *
gst_xvimagesink_xcontext_get (GstXvImageSink *xvimagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;
  
  g_return_val_if_fail (xvimagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);
  
  xcontext = g_new0 (GstXContext, 1);
  
  g_mutex_lock (xvimagesink->x_lock);
  
  xcontext->disp = XOpenDisplay (NULL);
  
  if (!xcontext->disp)
    {
      g_mutex_unlock (xvimagesink->x_lock);
      g_free (xcontext);
      return NULL;
    }
  
  xcontext->screen = DefaultScreenOfDisplay (xcontext->disp);
  xcontext->screen_num = DefaultScreen (xcontext->disp);
  xcontext->visual = DefaultVisual(xcontext->disp, xcontext->screen_num);
  xcontext->root = DefaultRootWindow (xcontext->disp);
  xcontext->white = XWhitePixel (xcontext->disp, xcontext->screen_num);
  xcontext->black = XBlackPixel (xcontext->disp, xcontext->screen_num);
  xcontext->depth = DefaultDepthOfScreen (xcontext->screen);
  
  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);
  
  if (!px_formats)
    {
      XCloseDisplay (xcontext->disp);
      g_mutex_unlock (xvimagesink->x_lock);
      g_free (xcontext);
      return NULL;
    }
  
  /* We get bpp value corresponding to our running depth */
  for (i=0; i<nb_formats; i++)
    {
      if (px_formats[i].depth == xcontext->depth)
        xcontext->bpp = px_formats[i].bits_per_pixel;
    }
    
  XFree (px_formats);
    
  xcontext->endianness = (ImageByteOrder (xcontext->disp) == LSBFirst) ? G_LITTLE_ENDIAN:G_BIG_ENDIAN;
  
#ifdef HAVE_XSHM
  /* Search for XShm extension support */
  if (XQueryExtension (xcontext->disp, "MIT-SHM", &i, &i, &i))
    {
      xcontext->use_xshm = TRUE;
      GST_DEBUG ("xvimagesink is using XShm extension");
    }
  else
    {
      xcontext->use_xshm = FALSE;
      GST_DEBUG ("xvimagesink is not using XShm extension");
    }
#endif /* HAVE_XSHM */
  
  if (xcontext->endianness == G_LITTLE_ENDIAN && xcontext->depth == 24)
    {
      xcontext->endianness = G_BIG_ENDIAN;
      xcontext->visual->red_mask = GUINT32_SWAP_LE_BE (xcontext->visual->red_mask);
      xcontext->visual->green_mask = GUINT32_SWAP_LE_BE (xcontext->visual->green_mask);
      xcontext->visual->blue_mask = GUINT32_SWAP_LE_BE (xcontext->visual->blue_mask);
    }
  
  xcontext->caps = gst_xvimagesink_get_xv_support (xcontext);
  
  if (!xcontext->caps)
    {
      XCloseDisplay (xcontext->disp);
      g_mutex_unlock (xvimagesink->x_lock);
      g_free (xcontext);
      return NULL;
    }
    
  g_mutex_unlock (xvimagesink->x_lock);
  
  /* We make this caps non floating. This way we keep it during our whole life */
  gst_caps_ref (xcontext->caps);
  gst_caps_sink (xcontext->caps);
  
  return xcontext;
}

/* This function cleans the X context. Closing the Display, releasing the XV
   port and unrefing the caps for supported formats. */
static void
gst_xvimagesink_xcontext_clear (GstXvImageSink *xvimagesink)
{
  g_return_if_fail (xvimagesink != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  
  gst_caps_unref (xvimagesink->xcontext->caps);
  
  g_mutex_lock (xvimagesink->x_lock);
  
  XvUngrabPort (xvimagesink->xcontext->disp,
                xvimagesink->xcontext->xv_port_id, 0);
  
  XCloseDisplay (xvimagesink->xcontext->disp);
  
  g_mutex_unlock (xvimagesink->x_lock);
  
  xvimagesink->xcontext = NULL;
}

/* Element stuff */

static GstCaps *
gst_xvimagesink_getcaps (GstPad *pad, GstCaps *caps)
{
  GstXvImageSink *xvimagesink;
  
  xvimagesink = GST_XVIMAGESINK (gst_pad_get_parent (pad));
  
  if (xvimagesink->xcontext)
    return gst_caps_copy (xvimagesink->xcontext->caps);

  return gst_caps_append (GST_CAPS_NEW ("xvimagesink_rgbsink",
                                        "video/x-raw-rgb",
                                        "framerate", GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT),
                                        "width", GST_PROPS_INT_RANGE(0, G_MAXINT),
                                        "height", GST_PROPS_INT_RANGE(0, G_MAXINT)),
                          GST_CAPS_NEW ("xvimagesink_yuvsink",
                                        "video/x-raw-yuv",
                                        "framerate", GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT),
                                        "width", GST_PROPS_INT_RANGE(0, G_MAXINT),
                                        "height", GST_PROPS_INT_RANGE(0, G_MAXINT)));
}

static GstPadLinkReturn
gst_xvimagesink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  if (GST_CAPS_IS_CHAINED (caps))
    return GST_PAD_LINK_DELAYED;
  
  GST_DEBUG ("sinkconnect %s with %s", gst_caps_to_string(caps),
             gst_caps_to_string(xvimagesink->xcontext->caps));
  
  if (!gst_caps_get_int (caps, "width", &xvimagesink->width))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_int (caps, "height", &xvimagesink->height))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_float (caps, "framerate", &xvimagesink->framerate))
    return GST_PAD_LINK_REFUSED;
  
  if (gst_caps_has_fixed_property (caps, "format"))
    gst_caps_get_fourcc_int (caps, "format", &xvimagesink->xcontext->im_format);
  
  if (gst_caps_has_fixed_property (caps, "pixel_width"))
    gst_caps_get_int (caps, "pixel_width", &xvimagesink->pixel_width);
  else
    xvimagesink->pixel_width = 1;

  if (gst_caps_has_fixed_property (caps, "pixel_height"))
    gst_caps_get_int (caps, "pixel_height", &xvimagesink->pixel_height);
  else
    xvimagesink->pixel_height = 1;
  
  /* Creating our window and our image */
  if (!xvimagesink->xwindow)
    xvimagesink->xwindow = gst_xvimagesink_xwindow_new (xvimagesink,
                                                      xvimagesink->width,
                                                      xvimagesink->height);
  else
    { /* We resize our window only if size has changed, preventing us from
         infinite loops with XConfigure events.
      if ( (xvimagesink->width != xvimagesink->xwindow->width) ||
           (xvimagesink->height != xvimagesink->xwindow->height) )
        gst_xvimagesink_xwindow_resize (xvimagesink, xvimagesink->xwindow,
                                       xvimagesink->width, xvimagesink->height);*/
    }
  
  if ( (xvimagesink->xvimage) &&
       ( (xvimagesink->width != xvimagesink->xvimage->width) ||
         (xvimagesink->height != xvimagesink->xvimage->height) ) )
    { /* We renew our xvimage only if size changed */
      gst_xvimagesink_xvimage_destroy (xvimagesink, xvimagesink->xvimage);
  
      xvimagesink->xvimage = gst_xvimagesink_xvimage_new (xvimagesink,
                                                      xvimagesink->width,
                                                      xvimagesink->height);
    }
  else if (!xvimagesink->xvimage) /* If no xvimage, creating one */
    xvimagesink->xvimage = gst_xvimagesink_xvimage_new (xvimagesink,
                                                    xvimagesink->width,
                                                    xvimagesink->height);
  
  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_xvimagesink_change_state (GstElement *element)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      xvimagesink->xcontext = gst_xvimagesink_xcontext_get (xvimagesink);
      if (!xvimagesink->xcontext)
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static void
gst_xvimagesink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstClockTime time = GST_BUFFER_TIMESTAMP (buf);
  GstXvImageSink *xvimagesink;
  
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  xvimagesink = GST_XVIMAGESINK (gst_pad_get_parent (pad));
    
  if (GST_IS_EVENT (buf))
    {
      GstEvent *event = GST_EVENT (buf);
      gint64 offset;

      switch (GST_EVENT_TYPE (event))
        {
          case GST_EVENT_DISCONTINUOUS:
	    offset = GST_EVENT_DISCONT_OFFSET (event, 0).value;
	    GST_DEBUG ("xvimage discont %" G_GINT64_FORMAT "\n", offset);
	    break;
          default:
	    gst_pad_event_default (pad, event);
	    return;
        }
      gst_event_unref (event);
      return;
    }
  
  GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT, time);
  
  if (xvimagesink->clock) {
    GstClockID id = gst_clock_new_single_shot_id (xvimagesink->clock, time);
    gst_element_clock_wait (GST_ELEMENT (xvimagesink), id, NULL);
    gst_clock_id_free (id);
  }
  
  /* If we have a pool and the image is from this pool, simply put it. */
  if ( (xvimagesink->bufferpool) &&
       (GST_BUFFER_BUFFERPOOL (buf) == xvimagesink->bufferpool) )
    gst_xvimagesink_xvimage_put (xvimagesink, GST_BUFFER_POOL_PRIVATE (buf));
  else /* Else we have to copy the data into our private image, */
    {  /* if we have one... */
      if (xvimagesink->xvimage)
        {
          memcpy (xvimagesink->xvimage->xvimage->data, 
                  GST_BUFFER_DATA (buf), 
                  MIN (GST_BUFFER_SIZE (buf), xvimagesink->xvimage->size));
          gst_xvimagesink_xvimage_put (xvimagesink, xvimagesink->xvimage);
        }
      else /* No image available. Something went wrong during capsnego ! */
        {
          gst_buffer_unref (buf);
          gst_element_error (GST_ELEMENT (xvimagesink), "no image to draw");
          return;
        }
    }
  
  gst_buffer_unref (buf);
    
  gst_xvimagesink_handle_xevents (xvimagesink, pad);
}

static void
gst_xvimagesink_set_clock (GstElement *element, GstClock *clock)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (element);
  
  xvimagesink->clock = clock;
}

static GstBuffer*
gst_xvimagesink_buffer_new (GstBufferPool *pool,  
		           gint64 location, guint size, gpointer user_data)
{
  GstXvImageSink *xvimagesink;
  GstBuffer *buffer;
  GstXvImage *xvimage = NULL;
  gboolean not_found = TRUE;
  
  xvimagesink = GST_XVIMAGESINK (user_data);
  
  g_mutex_lock (xvimagesink->pool_lock);
  
  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && xvimagesink->image_pool)
    {
      xvimage = xvimagesink->image_pool->data;
      
      if (xvimage)
        {
          /* Removing from the pool */
          xvimagesink->image_pool = g_slist_delete_link (xvimagesink->image_pool,
                                                        xvimagesink->image_pool);
          
          if ( (xvimage->width != xvimagesink->width) ||
               (xvimage->height != xvimagesink->height) )
            { /* This image is unusable. Destroying... */
              gst_xvimagesink_xvimage_destroy (xvimagesink, xvimage);
              xvimage = NULL;
            }
          else /* We found a suitable image */
            break;
        }
    }
   
  g_mutex_unlock (xvimagesink->pool_lock);
  
  if (!xvimage) /* We found no suitable image in the pool. Creating... */
    xvimage = gst_xvimagesink_xvimage_new (xvimagesink,
                                        xvimagesink->width,
                                        xvimagesink->height);
  
  if (xvimage)
    {
      buffer = gst_buffer_new ();
      GST_BUFFER_POOL_PRIVATE (buffer) = xvimage;
      GST_BUFFER_DATA (buffer) = xvimage->xvimage->data;
      GST_BUFFER_SIZE (buffer) = xvimage->size;
      return buffer;
    }
  else
    return NULL;
}

static void
gst_xvimagesink_buffer_free (GstBufferPool *pool,
                            GstBuffer *buffer, gpointer user_data)
{
  GstXvImageSink *xvimagesink;
  GstXvImage *xvimage;
  
  xvimagesink = GST_XVIMAGESINK (user_data);
  
  xvimage = GST_BUFFER_POOL_PRIVATE (buffer);
  
  /* If our geometry changed we can't reuse that image. */
  if ( (xvimage->width != xvimagesink->width) ||
       (xvimage->height != xvimagesink->height) )
    gst_xvimagesink_xvimage_destroy (xvimagesink, xvimage);
  else /* In that case we can reuse the image and add it to our image pool. */
    {
      g_mutex_lock (xvimagesink->pool_lock);
      xvimagesink->image_pool = g_slist_prepend (xvimagesink->image_pool, xvimage);
      g_mutex_unlock (xvimagesink->pool_lock);
    }
    
  GST_BUFFER_DATA (buffer) = NULL;

  gst_buffer_default_free (buffer);
}

static void
gst_xvimagesink_imagepool_clear (GstXvImageSink *xvimagesink)
{
  g_mutex_lock(xvimagesink->pool_lock);
  
  while (xvimagesink->image_pool)
    {
      GstXvImage *xvimage = xvimagesink->image_pool->data;
      xvimagesink->image_pool = g_slist_delete_link (xvimagesink->image_pool,
                                                    xvimagesink->image_pool);
      gst_xvimagesink_xvimage_destroy (xvimagesink, xvimage);
    }
  
  g_mutex_unlock(xvimagesink->pool_lock);
}

static GstBufferPool*
gst_xvimagesink_get_bufferpool (GstPad *pad)
{
  GstXvImageSink *xvimagesink;
  
  xvimagesink = GST_XVIMAGESINK (gst_pad_get_parent (pad));

  if (!xvimagesink->bufferpool) {
    xvimagesink->bufferpool = gst_buffer_pool_new (
                NULL,		/* free */
                NULL,		/* copy */
                (GstBufferPoolBufferNewFunction) gst_xvimagesink_buffer_new,
                NULL,		/* buffer copy, the default is fine */
                (GstBufferPoolBufferFreeFunction) gst_xvimagesink_buffer_free,
                xvimagesink);

    xvimagesink->image_pool = NULL;
  }

  gst_buffer_pool_ref (xvimagesink->bufferpool);

  return xvimagesink->bufferpool;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_xvimagesink_dispose (GObject *object)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (object);

  if (xvimagesink->xvimage)
    {
      gst_xvimagesink_xvimage_destroy (xvimagesink, xvimagesink->xvimage);
      xvimagesink->xvimage = NULL;
    }
    
  if (xvimagesink->image_pool)
    gst_xvimagesink_imagepool_clear (xvimagesink);
  
  if (xvimagesink->xwindow)
    {
      gst_xvimagesink_xwindow_destroy (xvimagesink, xvimagesink->xwindow);
      xvimagesink->xwindow = NULL;
    }
  
  if (xvimagesink->xcontext)
    gst_xvimagesink_xcontext_clear (xvimagesink);
    
  g_mutex_free (xvimagesink->x_lock);
  g_mutex_free (xvimagesink->pool_lock);

  if (xvimagesink->bufferpool) 
    gst_buffer_pool_free (xvimagesink->bufferpool);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_xvimagesink_init (GstXvImageSink *xvimagesink)
{
  xvimagesink->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (
                                         gst_xvimagesink_sink_template_factory),
                                         "sink");
  gst_element_add_pad (GST_ELEMENT (xvimagesink), xvimagesink->sinkpad);

  gst_pad_set_chain_function (xvimagesink->sinkpad, gst_xvimagesink_chain);
  gst_pad_set_link_function (xvimagesink->sinkpad, gst_xvimagesink_sinkconnect);
  gst_pad_set_getcaps_function (xvimagesink->sinkpad, gst_xvimagesink_getcaps);
  gst_pad_set_bufferpool_function (xvimagesink->sinkpad,
                                   gst_xvimagesink_get_bufferpool);

  xvimagesink->xcontext = NULL;
  xvimagesink->xwindow = NULL;
  xvimagesink->xvimage = NULL;
  xvimagesink->clock = NULL;
  
  xvimagesink->width = xvimagesink->height = 0;
  
  xvimagesink->framerate = 0;
  
  xvimagesink->x_lock = g_mutex_new ();
  
  xvimagesink->pixel_width = xvimagesink->pixel_height = 1;
  
  xvimagesink->image_pool = NULL;
  xvimagesink->pool_lock = g_mutex_new ();

  GST_FLAG_SET(xvimagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET(xvimagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_xvimagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_xvimagesink_details);

  gst_element_class_add_pad_template (element_class, 
    GST_PAD_TEMPLATE_GET (gst_xvimagesink_sink_template_factory));
}

static void
gst_xvimagesink_class_init (GstXvImageSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_xvimagesink_dispose;
  
  gstelement_class->change_state = gst_xvimagesink_change_state;
  gstelement_class->set_clock    = gst_xvimagesink_set_clock;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_xvimagesink_get_type (void)
{
  static GType xvimagesink_type = 0;

  if (!xvimagesink_type)
    {
      static const GTypeInfo xvimagesink_info = {
        sizeof(GstXvImageSinkClass),
        gst_xvimagesink_base_init,
        NULL,
        (GClassInitFunc) gst_xvimagesink_class_init,
        NULL,
        NULL,
        sizeof(GstXvImageSink),
        0,
        (GInstanceInitFunc) gst_xvimagesink_init,
      };
      static const GInterfaceInfo iface_info = {
        (GInterfaceInitFunc) gst_xvimagesink_interface_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo navigation_info = {
        (GInterfaceInitFunc) gst_xvimagesink_navigation_init,
        NULL,
        NULL,
      };
      /*static const GInterfaceInfo xoverlay_info = {
        (GInterfaceInitFunc) gst_xvimagesink_xoverlay_init,
        NULL,
        NULL,
      };*/
      
      xvimagesink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                                 "GstXvImageSink",
                                                 &xvimagesink_info, 0);
      
      g_type_add_interface_static (xvimagesink_type, GST_TYPE_INTERFACE,
        &iface_info);
      g_type_add_interface_static (xvimagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
      /*g_type_add_interface_static (xvimagesink_type, GST_TYPE_X_OVERLAY,
        &xoverlay_info);*/
    }
    
  return xvimagesink_type;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "xvimagesink",
                             GST_RANK_PRIMARY, GST_TYPE_XVIMAGESINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "xvimagesink",
  "XFree86 video output plugin using Xv extension",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_COPYRIGHT,
  GST_PACKAGE,
  GST_ORIGIN)
