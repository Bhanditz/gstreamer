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
#include "ximagesink.h"

/* ElementFactory information */
static GstElementDetails gst_ximagesink_details = GST_ELEMENT_DETAILS (
  "Video sink",
  "Sink/Video",
  "A standard X based videosink",
  "Julien Moutte <julien@moutte.net>"
);

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
GST_PAD_TEMPLATE_FACTORY (gst_ximagesink_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW ("ximagesink_rgbsink", "video/x-raw-rgb",
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
                "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
                "height", GST_PROPS_INT_RANGE (0, G_MAXINT))
)

static GstElementClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* Interfaces stuff */

static gboolean
gst_ximagesink_interface_supported (GstInterface *iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION ||
	    type == GST_TYPE_X_OVERLAY);

  return (GST_STATE (iface) != GST_STATE_NULL);
}

static void
gst_ximagesink_interface_init (GstInterfaceClass *klass)
{
  klass->supported = gst_ximagesink_interface_supported;
}

static void
gst_ximagesink_navigation_send_event (GstNavigation *navigation, GstStructure *structure)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (navigation);
  GstEvent *event;

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* We are not converting the pointer coordinates as there's no hardware 
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling */
  
  gst_pad_send_event (gst_pad_get_peer (ximagesink->sinkpad), event);
}

static void
gst_ximagesink_navigation_init (GstNavigationInterface *iface)
{
  iface->send_event = gst_ximagesink_navigation_send_event;
}

/* X11 stuff */

/* This function handles GstXImage creation depending on XShm availability */
static GstXImage *
gst_ximagesink_ximage_new (GstXImageSink *ximagesink, gint width, gint height)
{
  GstXImage *ximage = NULL;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  ximage = g_new0 (GstXImage, 1);
  
  ximage->width = width;
  ximage->height = height;
  ximage->data = NULL;
  
  g_mutex_lock (ximagesink->x_lock);

  ximage->size =  (ximagesink->xcontext->bpp / 8) * ximage->width * ximage->height;
  
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {
      ximage->ximage = XShmCreateImage (ximagesink->xcontext->disp,
                                        ximagesink->xcontext->visual,
                                        ximagesink->xcontext->depth,
                                        ZPixmap, NULL, &ximage->SHMInfo,
                                        ximage->width, ximage->height);
      
      ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
                                      IPC_CREAT | 0777);
  
      ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
      ximage->ximage->data = ximage->SHMInfo.shmaddr;
  
      ximage->SHMInfo.readOnly = FALSE;
  
      XShmAttach (ximagesink->xcontext->disp, &ximage->SHMInfo);
    }
  else
    {
      ximage->data = g_malloc (ximage->size);
      
      ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
                                     ximagesink->xcontext->visual, 
                                     ximagesink->xcontext->depth,
                                     ZPixmap, 0, ximage->data, 
                                     ximage->width, ximage->height,
                                     ximagesink->xcontext->bpp,
                                     ximage->width * (ximagesink->xcontext->bpp / 8));
    }
#else
  ximage->data = g_malloc (ximage->size);
    
  ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
                                 ximagesink->xcontext->visual, 
                                 ximagesink->xcontext->depth,
                                 ZPixmap, 0, ximage->data, 
                                 ximage->width, ximage->height,
                                 ximagesink->xcontext->bpp,
                                 ximage->width * (ximagesink->xcontext->bpp / 8));
#endif /* HAVE_XSHM */
  
  if (ximage->ximage)
    {
      XSync(ximagesink->xcontext->disp, 0);
    }
  else
    {
      if (ximage->data)
        g_free (ximage->data);
      
      g_free (ximage);
      
      ximage = NULL;
    }
    
  g_mutex_unlock (ximagesink->x_lock);
  
  return ximage;
}

/* This function destroys a GstXImage handling XShm availability */ 
static void
gst_ximagesink_ximage_destroy (GstXImageSink *ximagesink, GstXImage *ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {
      if (ximage->SHMInfo.shmaddr)
        XShmDetach (ximagesink->xcontext->disp, &ximage->SHMInfo);
  
      if (ximage->ximage)
        XDestroyImage (ximage->ximage);
  
      if (ximage->SHMInfo.shmaddr)
        shmdt (ximage->SHMInfo.shmaddr);
  
      if (ximage->SHMInfo.shmid > 0)
        shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);
    }
  else
    {
      if (ximage->ximage)
        XDestroyImage (ximage->ximage);
    }
#else
  if (ximage->ximage)
    XDestroyImage (ximage->ximage);
#endif /* HAVE_XSHM */ 
  
  g_mutex_unlock (ximagesink->x_lock);
  
  g_free (ximage);
}

/* This function puts a GstXImage on a GstXImageSink's window */
static void
gst_ximagesink_ximage_put (GstXImageSink *ximagesink, GstXImage *ximage)
{
  gint x, y;
  XWindowAttributes attr; 
  
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
  /* We center the image in the window */
  XGetWindowAttributes (ximagesink->xcontext->disp,
                        ximagesink->xwindow->win, &attr); 

  x = MAX (0, (attr.width - ximage->width) / 2);
  y = MAX (0, (attr.height - ximage->height) / 2);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {  
      XShmPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win, 
                    ximagesink->xwindow->gc, ximage->ximage, 
                    0, 0, x, y, ximage->width, ximage->height, 
                    FALSE);
    }
  else
    {
      XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win, 
                 ximagesink->xwindow->gc, ximage->ximage,  
                 0, 0, x, y, ximage->width, ximage->height);
    }
#else
  XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win, 
             ximagesink->xwindow->gc, ximage->ximage,  
             0, 0, x, y, ximage->width, ximage->height);
#endif /* HAVE_XSHM */
  
  XSync(ximagesink->xcontext->disp, FALSE);
  
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function handles a GstXWindow creation */
static GstXWindow *
gst_ximagesink_xwindow_new (GstXImageSink *ximagesink, gint width, gint height)
{
  GstXWindow *xwindow = NULL;
  XGCValues values;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  xwindow = g_new0 (GstXWindow, 1);
  
  xwindow->width = width;
  xwindow->height = height;
  
  g_mutex_lock (ximagesink->x_lock);
  
  xwindow->win = XCreateSimpleWindow (ximagesink->xcontext->disp,
                                      ximagesink->xcontext->root, 
                                      0, 0, xwindow->width, xwindow->height, 
                                      0, 0, ximagesink->xcontext->black);
  
  XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
                StructureNotifyMask | PointerMotionMask | KeyPressMask |
                KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  
  xwindow->gc = XCreateGC (ximagesink->xcontext->disp,
                           xwindow->win, 0, &values);
  
  XMapRaised (ximagesink->xcontext->disp, xwindow->win);
  
  g_mutex_unlock (ximagesink->x_lock);
  
  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_ximagesink_xwindow_destroy (GstXImageSink *ximagesink, GstXWindow *xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
  XDestroyWindow (ximagesink->xcontext->disp, xwindow->win);
  
  XFreeGC (ximagesink->xcontext->disp, xwindow->gc);
  
  g_mutex_unlock (ximagesink->x_lock);
  
  g_free (xwindow);
}

/* This function resizes a GstXWindow */
/*static void
gst_ximagesink_xwindow_resize (GstXImageSink *ximagesink, GstXWindow  *xwindow,
                               gint width, gint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
  XResizeWindow (ximagesink->xcontext->disp, xwindow->win, width, height);
  
  g_mutex_unlock (ximagesink->x_lock);
} */

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_ximagesink_handle_xevents (GstXImageSink *ximagesink, GstPad *pad)
{
  XEvent e;
  
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  /* We get all events on our window to throw them upstream */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
                            ximagesink->xwindow->win,
                            ExposureMask | StructureNotifyMask |
                            PointerMotionMask | KeyPressMask |
                            KeyReleaseMask | ButtonPressMask |
                            ButtonReleaseMask, &e))
    {
      GstEvent *event = NULL;
      KeySym keysym;
      
      /* We lock only for the X function call */
      g_mutex_unlock (ximagesink->x_lock);
      
      switch (e.type)
        {
          case ConfigureNotify:
            /* Window got resized or moved. We do caps negotiation
               again to get video scaler to fit that new size only if size
               of the window changed. */
            GST_DEBUG ("ximagesink window is at %d, %d with geometry : %d,%d",
                       e.xconfigure.x, e.xconfigure.y,
                       e.xconfigure.width, e.xconfigure.height);
            if ( (ximagesink->xwindow->width != e.xconfigure.width) ||
                 (ximagesink->xwindow->height != e.xconfigure.height) )
              {
                gst_pad_try_set_caps (ximagesink->sinkpad,
                                      GST_CAPS_NEW ("ximagesink_ximage_caps", "video/x-raw-rgb",
                                                    "bpp",        GST_PROPS_INT (ximagesink->xcontext->bpp),
                                                    "depth",      GST_PROPS_INT (ximagesink->xcontext->depth),
                                                    "endianness", GST_PROPS_INT (ximagesink->xcontext->endianness),
                                                    "red_mask",   GST_PROPS_INT (ximagesink->xcontext->visual->red_mask),
                                                    "green_mask", GST_PROPS_INT (ximagesink->xcontext->visual->green_mask),
                                                    "blue_mask",  GST_PROPS_INT (ximagesink->xcontext->visual->blue_mask),
                                                    "width",      GST_PROPS_INT (e.xconfigure.width),
                                                    "height",     GST_PROPS_INT (e.xconfigure.height),
                                                    "framerate",  GST_PROPS_FLOAT (ximagesink->framerate)));
                
                /* We should check for _try_set_caps result */
                ximagesink->width = e.xconfigure.width;
                ximagesink->height = e.xconfigure.height;
                
                if ( (ximagesink->ximage) &&
                     ( (ximagesink->width != ximagesink->ximage->width) ||
                       (ximagesink->height != ximagesink->ximage->height) ) )
                  {
                    /* We renew our ximage only if size changed */
                    gst_ximagesink_ximage_destroy (ximagesink,
                                                   ximagesink->ximage);
                
                    ximagesink->ximage = gst_ximagesink_ximage_new (
                                                            ximagesink,
                                                            ximagesink->width,
                                                            ximagesink->height);
                  }
              }
            break;
          case MotionNotify:
            /* Mouse pointer moved over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink pointer moved over window at %d,%d",
                       e.xmotion.x, e.xmotion.y);
            gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
                                             e.xmotion.x, e.xmotion.y);
            break;
          case ButtonPress:
          case ButtonRelease:
            /* Mouse button pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink button %d pressed over window at %d,%d",
                       e.xbutton.button, e.xbutton.x, e.xbutton.x);
            gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
                                             e.xmotion.x, e.xmotion.y);
            break;
          case KeyPress:
          case KeyRelease:
            /* Key pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink key %d pressed over window at %d,%d",
                       e.xkey.keycode, e.xkey.x, e.xkey.x);
            keysym = XKeycodeToKeysym (ximagesink->xcontext->disp,
                                       e.xkey.keycode, 0);
            gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
                                           XKeysymToString (keysym));
            /* What's that ? */
            gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
                                           "unknown");
            break;
          default:
            GST_DEBUG ("ximagesink unhandled X event (%d)", e.type);
        }
        
      if (event)
        gst_pad_send_event (gst_pad_get_peer (pad), event);
      
      g_mutex_lock (ximagesink->x_lock);
    }
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function get the X Display and global infos about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or 
   image creation */
static GstXContext *
gst_ximagesink_xcontext_get (GstXImageSink *ximagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  xcontext = g_new0 (GstXContext, 1);
  
  g_mutex_lock (ximagesink->x_lock);
  
  xcontext->disp = XOpenDisplay (NULL);
  
  if (!xcontext->disp)
    {
      g_mutex_unlock (ximagesink->x_lock);
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
      g_mutex_unlock (ximagesink->x_lock);
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
      GST_DEBUG ("ximagesink is using XShm extension");
    }
  else
    {
      xcontext->use_xshm = FALSE;
      GST_DEBUG ("ximagesink is not using XShm extension");
    }
#endif /* HAVE_XSHM */
  
  if (xcontext->endianness == G_LITTLE_ENDIAN && xcontext->depth == 24)
    {
      xcontext->endianness = G_BIG_ENDIAN;
      xcontext->visual->red_mask = GUINT32_SWAP_LE_BE (xcontext->visual->red_mask);
      xcontext->visual->green_mask = GUINT32_SWAP_LE_BE (xcontext->visual->green_mask);
      xcontext->visual->blue_mask = GUINT32_SWAP_LE_BE (xcontext->visual->blue_mask);
    }
    
  xcontext->caps = GST_CAPS_NEW ("ximagesink_ximage_caps", "video/x-raw-rgb",
      "bpp",        GST_PROPS_INT (xcontext->bpp),
      "depth",      GST_PROPS_INT (xcontext->depth),
      "endianness", GST_PROPS_INT (xcontext->endianness),
      "red_mask",   GST_PROPS_INT (xcontext->visual->red_mask),
      "green_mask", GST_PROPS_INT (xcontext->visual->green_mask),
      "blue_mask",  GST_PROPS_INT (xcontext->visual->blue_mask),
      "width",      GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",     GST_PROPS_INT_RANGE (0, G_MAXINT),
      "framerate",  GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));
 
  g_mutex_unlock (ximagesink->x_lock);
  
  /* We make this caps non floating. This way we keep it during our whole life */
  gst_caps_ref (xcontext->caps);
  gst_caps_sink (xcontext->caps);
  
  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
static void
gst_ximagesink_xcontext_clear (GstXImageSink *ximagesink)
{
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  gst_caps_unref (ximagesink->xcontext->caps);
  
  g_mutex_lock (ximagesink->x_lock);
  
  XCloseDisplay (ximagesink->xcontext->disp);
  
  g_mutex_unlock (ximagesink->x_lock);
  
  ximagesink->xcontext = NULL;
}

/* Element stuff */

static GstCaps *
gst_ximagesink_getcaps (GstPad *pad, GstCaps *caps)
{
  GstXImageSink *ximagesink;
  
  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));
  
  if (ximagesink->xcontext)
    return gst_caps_copy (ximagesink->xcontext->caps);

  return GST_CAPS_NEW ("ximagesink_rgbsink", "video/x-raw-rgb",
                       "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
                       "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
                       "height", GST_PROPS_INT_RANGE (0, G_MAXINT));
}

static GstPadLinkReturn
gst_ximagesink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  if (GST_CAPS_IS_CHAINED (caps))
    return GST_PAD_LINK_DELAYED;
  
  GST_DEBUG ("sinkconnect %s with %s", gst_caps_to_string(caps),
             gst_caps_to_string(ximagesink->xcontext->caps));
  
  if (!gst_caps_get_int (caps, "width", &ximagesink->width))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_int (caps, "height", &ximagesink->height))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_float (caps, "framerate", &ximagesink->framerate))
    return GST_PAD_LINK_REFUSED;
  
  if (gst_caps_has_fixed_property (caps, "pixel_width"))
    gst_caps_get_int (caps, "pixel_width", &ximagesink->pixel_width);
  else
    ximagesink->pixel_width = 1;

  if (gst_caps_has_fixed_property (caps, "pixel_height"))
    gst_caps_get_int (caps, "pixel_height", &ximagesink->pixel_height);
  else
    ximagesink->pixel_height = 1;
  
  /* Creating our window and our image */
  if (!ximagesink->xwindow)
    ximagesink->xwindow = gst_ximagesink_xwindow_new (ximagesink,
                                                      ximagesink->width,
                                                      ximagesink->height);
  else
    { /* We resize our window only if size has changed, preventing us from
         infinite loops with XConfigure events.
      if ( (ximagesink->width != ximagesink->xwindow->width) ||
           (ximagesink->height != ximagesink->xwindow->height) )
        gst_ximagesink_xwindow_resize (ximagesink, ximagesink->xwindow,
                                       ximagesink->width, ximagesink->height);*/
    }
  
  if ( (ximagesink->ximage) &&
       ( (ximagesink->width != ximagesink->ximage->width) ||
         (ximagesink->height != ximagesink->ximage->height) ) )
    { /* We renew our ximage only if size changed */
      gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
  
      ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
                                                      ximagesink->width,
                                                      ximagesink->height);
    }
  else if (!ximagesink->ximage) /* If no ximage, creating one */
    ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
                                                    ximagesink->width,
                                                    ximagesink->height);
  
  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_ximagesink_change_state (GstElement *element)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);
      if (!ximagesink->xcontext)
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
gst_ximagesink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstClockTime time = GST_BUFFER_TIMESTAMP (buf);
  GstXImageSink *ximagesink;
  
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));
    
  if (GST_IS_EVENT (buf))
    {
      GstEvent *event = GST_EVENT (buf);
      gint64 offset;

      switch (GST_EVENT_TYPE (event))
        {
          case GST_EVENT_DISCONTINUOUS:
	    offset = GST_EVENT_DISCONT_OFFSET (event, 0).value;
	    GST_DEBUG ("ximage discont %" G_GINT64_FORMAT "\n", offset);
	    break;
          default:
	    gst_pad_event_default (pad, event);
	    return;
        }
      gst_event_unref (event);
      return;
    }
  
  GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT, time);
  
  if (ximagesink->clock) {
    GstClockID id = gst_clock_new_single_shot_id (ximagesink->clock, time);
    gst_element_clock_wait (GST_ELEMENT (ximagesink), id, NULL);
    gst_clock_id_free (id);
  }
  
  /* If we have a pool and the image is from this pool, simply put it. */
  if ( (ximagesink->bufferpool) &&
       (GST_BUFFER_BUFFERPOOL (buf) == ximagesink->bufferpool) )
    gst_ximagesink_ximage_put (ximagesink, GST_BUFFER_POOL_PRIVATE (buf));
  else /* Else we have to copy the data into our private image, */
    {  /* if we have one... */
      if (ximagesink->ximage)
        {
          memcpy (ximagesink->ximage->ximage->data, 
                  GST_BUFFER_DATA (buf), 
                  MIN (GST_BUFFER_SIZE (buf), ximagesink->ximage->size));
          gst_ximagesink_ximage_put (ximagesink, ximagesink->ximage);
        }
      else /* No image available. Something went wrong during capsnego ! */
        {
          gst_buffer_unref (buf);
          gst_element_error (GST_ELEMENT (ximagesink), "no image to draw");
          return;
        }
    }
  
  gst_buffer_unref (buf);
    
  gst_ximagesink_handle_xevents (ximagesink, pad);
}

static void
gst_ximagesink_set_clock (GstElement *element, GstClock *clock)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (element);
  
  ximagesink->clock = clock;
}

static GstBuffer*
gst_ximagesink_buffer_new (GstBufferPool *pool,  
		           gint64 location, guint size, gpointer user_data)
{
  GstXImageSink *ximagesink;
  GstBuffer *buffer;
  GstXImage *ximage = NULL;
  gboolean not_found = TRUE;
  
  ximagesink = GST_XIMAGESINK (user_data);
  
  g_mutex_lock (ximagesink->pool_lock);
  
  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && ximagesink->image_pool)
    {
      ximage = ximagesink->image_pool->data;
      
      if (ximage)
        {
          /* Removing from the pool */
          ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
                                                        ximagesink->image_pool);
          
          if ( (ximage->width != ximagesink->width) ||
               (ximage->height != ximagesink->height) )
            { /* This image is unusable. Destroying... */
              gst_ximagesink_ximage_destroy (ximagesink, ximage);
              ximage = NULL;
            }
          else /* We found a suitable image */
            break;
        }
    }
   
  g_mutex_unlock (ximagesink->pool_lock);
  
  if (!ximage) /* We found no suitable image in the pool. Creating... */
    ximage = gst_ximagesink_ximage_new (ximagesink,
                                        ximagesink->width,
                                        ximagesink->height);
  
  if (ximage)
    {
      buffer = gst_buffer_new ();
      GST_BUFFER_POOL_PRIVATE (buffer) = ximage;
      GST_BUFFER_DATA (buffer) = ximage->ximage->data;
      GST_BUFFER_SIZE (buffer) = ximage->size;
      return buffer;
    }
  else
    return NULL;
}

static void
gst_ximagesink_buffer_free (GstBufferPool *pool,
                            GstBuffer *buffer, gpointer user_data)
{
  GstXImageSink *ximagesink;
  GstXImage *ximage;
  
  ximagesink = GST_XIMAGESINK (user_data);
  
  ximage = GST_BUFFER_POOL_PRIVATE (buffer);
  
  /* If our geometry changed we can't reuse that image. */
  if ( (ximage->width != ximagesink->width) ||
       (ximage->height != ximagesink->height) )
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  else /* In that case we can reuse the image and add it to our image pool. */
    {
      g_mutex_lock (ximagesink->pool_lock);
      ximagesink->image_pool = g_slist_prepend (ximagesink->image_pool, ximage);
      g_mutex_unlock (ximagesink->pool_lock);
    }
    
  GST_BUFFER_DATA (buffer) = NULL;

  gst_buffer_default_free (buffer);
}

static void
gst_ximagesink_imagepool_clear (GstXImageSink *ximagesink)
{
  g_mutex_lock(ximagesink->pool_lock);
  
  while (ximagesink->image_pool)
    {
      GstXImage *ximage = ximagesink->image_pool->data;
      ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
                                                    ximagesink->image_pool);
      gst_ximagesink_ximage_destroy (ximagesink, ximage);
    }
  
  g_mutex_unlock(ximagesink->pool_lock);
}

static GstBufferPool*
gst_ximagesink_get_bufferpool (GstPad *pad)
{
  GstXImageSink *ximagesink;
  
  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  if (!ximagesink->bufferpool) {
    ximagesink->bufferpool = gst_buffer_pool_new (
                NULL,		/* free */
                NULL,		/* copy */
                (GstBufferPoolBufferNewFunction) gst_ximagesink_buffer_new,
                NULL,		/* buffer copy, the default is fine */
                (GstBufferPoolBufferFreeFunction) gst_ximagesink_buffer_free,
                ximagesink);

    ximagesink->image_pool = NULL;
  }

  gst_buffer_pool_ref (ximagesink->bufferpool);

  return ximagesink->bufferpool;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_ximagesink_dispose (GObject *object)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (object);

  if (ximagesink->ximage)
    {
      gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
      ximagesink->ximage = NULL;
    }
    
  if (ximagesink->image_pool)
    gst_ximagesink_imagepool_clear (ximagesink);
  
  if (ximagesink->xwindow)
    {
      gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
      ximagesink->xwindow = NULL;
    }
  
  if (ximagesink->xcontext)
    gst_ximagesink_xcontext_clear (ximagesink);
    
  g_mutex_free (ximagesink->x_lock);
  g_mutex_free (ximagesink->pool_lock);

  if (ximagesink->bufferpool) 
    gst_buffer_pool_free (ximagesink->bufferpool);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_ximagesink_init (GstXImageSink *ximagesink)
{
  ximagesink->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (
                                         gst_ximagesink_sink_template_factory),
                                         "sink");
  gst_element_add_pad (GST_ELEMENT (ximagesink), ximagesink->sinkpad);

  gst_pad_set_chain_function (ximagesink->sinkpad, gst_ximagesink_chain);
  gst_pad_set_link_function (ximagesink->sinkpad, gst_ximagesink_sinkconnect);
  gst_pad_set_getcaps_function (ximagesink->sinkpad, gst_ximagesink_getcaps);
  gst_pad_set_bufferpool_function (ximagesink->sinkpad,
                                   gst_ximagesink_get_bufferpool);

  ximagesink->xcontext = NULL;
  ximagesink->xwindow = NULL;
  ximagesink->ximage = NULL;
  ximagesink->clock = NULL;
  
  ximagesink->width = ximagesink->height = 0;
  
  ximagesink->framerate = 0;
  
  ximagesink->x_lock = g_mutex_new ();
  
  ximagesink->pixel_width = ximagesink->pixel_height = 1;
  
  ximagesink->image_pool = NULL;
  ximagesink->pool_lock = g_mutex_new ();

  GST_FLAG_SET(ximagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET(ximagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_ximagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_ximagesink_details);

  gst_element_class_add_pad_template (element_class, 
    GST_PAD_TEMPLATE_GET (gst_ximagesink_sink_template_factory));
}

static void
gst_ximagesink_class_init (GstXImageSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_ximagesink_dispose;
  
  gstelement_class->change_state = gst_ximagesink_change_state;
  gstelement_class->set_clock    = gst_ximagesink_set_clock;
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
gst_ximagesink_get_type (void)
{
  static GType ximagesink_type = 0;

  if (!ximagesink_type)
    {
      static const GTypeInfo ximagesink_info = {
        sizeof(GstXImageSinkClass),
        gst_ximagesink_base_init,
        NULL,
        (GClassInitFunc) gst_ximagesink_class_init,
        NULL,
        NULL,
        sizeof(GstXImageSink),
        0,
        (GInstanceInitFunc) gst_ximagesink_init,
      };
      static const GInterfaceInfo iface_info = {
        (GInterfaceInitFunc) gst_ximagesink_interface_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo navigation_info = {
        (GInterfaceInitFunc) gst_ximagesink_navigation_init,
        NULL,
        NULL,
      };
      /*static const GInterfaceInfo xoverlay_info = {
        (GInterfaceInitFunc) gst_ximagesink_xoverlay_init,
        NULL,
        NULL,
      };*/
      
      ximagesink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                                "GstXImageSink",
                                                &ximagesink_info, 0);
      
      g_type_add_interface_static (ximagesink_type, GST_TYPE_INTERFACE,
        &iface_info);
      g_type_add_interface_static (ximagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
      /*g_type_add_interface_static (ximagesink_type, GST_TYPE_X_OVERLAY,
        &xoverlay_info);*/
    }
    
  return ximagesink_type;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "ximagesink",
                             GST_RANK_SECONDARY, GST_TYPE_XIMAGESINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ximagesink",
  "XFree86 video output plugin based on standard Xlib calls",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_COPYRIGHT,
  GST_PACKAGE,
  GST_ORIGIN)
