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
#include <gst/navigation/navigation.h>
#include <gst/xoverlay/xoverlay.h>

/* Object header */
#include "ximagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>
GST_DEBUG_CATEGORY_STATIC (gst_debug_ximagesink);
#define GST_CAT_DEFAULT gst_debug_ximagesink

typedef struct
{
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long input_mode;
  unsigned long status;
}
MotifWmHints, MwmHints;

#define MWM_HINTS_DECORATIONS   (1L << 1)

static void gst_ximagesink_buffer_free (GstBuffer * buffer);
static void gst_ximagesink_ximage_destroy (GstXImageSink * ximagesink,
    GstXImage * ximage);

/* ElementFactory information */
static GstElementDetails gst_ximagesink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "A standard X based videosink",
    "Julien Moutte <julien@moutte.net>");

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_ximagesink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (double) [ 0.0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

enum
{
  ARG_0,
  ARG_DISPLAY,
  ARG_SYNCHRONOUS
      /* FILL ME */
};

static GstVideoSinkClass *parent_class = NULL;
static gboolean error_caught = FALSE;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* X11 stuff */

static int
gst_ximagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("ximagesink failed to use XShm calls. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

/* This function checks that it is actually really possible to create an image
   using XShm */
static gboolean
gst_ximagesink_check_xshm_calls (GstXContext * xcontext)
{
#ifndef HAVE_XSHM
  return FALSE;
#else
  GstXImage *ximage = NULL;
  int (*handler) (Display *, XErrorEvent *);
  gboolean result = FALSE;

  g_return_val_if_fail (xcontext != NULL, FALSE);

  ximage = g_new0 (GstXImage, 1);
  g_return_val_if_fail (ximage != NULL, FALSE);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

  /* Trying to create a 1x1 picture */
  GST_DEBUG ("XShmCreateImage of 1x1");

  ximage->ximage = XShmCreateImage (xcontext->disp, xcontext->visual,
      xcontext->depth, ZPixmap, NULL, &ximage->SHMInfo, 1, 1);
  if (!ximage->ximage) {
    GST_WARNING ("could not XShmCreateImage a 1x1 image");
    goto beach;
  }
  ximage->size = ximage->ximage->height * ximage->ximage->bytes_per_line;

  ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size, IPC_CREAT | 0777);
  if (ximage->SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %d bytes", ximage->size);
    goto beach;
  }

  ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
  if ((int) ximage->SHMInfo.shmaddr == -1) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    goto beach;
  }

  ximage->ximage->data = ximage->SHMInfo.shmaddr;
  ximage->SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &ximage->SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    goto beach;
  }

  XSync (xcontext->disp, 0);

  XShmDetach (xcontext->disp, &ximage->SHMInfo);
  shmdt (ximage->SHMInfo.shmaddr);
  shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);

  /* store whether we succeeded in result and reset error_caught */
  result = !error_caught;
  error_caught = FALSE;

beach:
  XSetErrorHandler (handler);
  if (ximage->ximage)
    XFree (ximage->ximage);
  g_free (ximage);
  XSync (xcontext->disp, FALSE);
  return result;
#endif /* HAVE_XSHM */
}

/* This function handles GstXImage creation depending on XShm availability */
static GstXImage *
gst_ximagesink_ximage_new (GstXImageSink * ximagesink, gint width, gint height)
{
  GstXImage *ximage = NULL;
  gboolean succeeded = FALSE;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  GST_DEBUG_OBJECT (ximagesink, "creating %dx%d", width, height);

  ximage = g_new0 (GstXImage, 1);

  ximage->width = width;
  ximage->height = height;
  ximage->ximagesink = ximagesink;

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    ximage->ximage = XShmCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, NULL, &ximage->SHMInfo, ximage->width, ximage->height);
    if (!ximage->ximage) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not XShmCreateImage a %dx%d image"));
      goto beach;
    }

    /* we have to use the returned bytes_per_line for our shm size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    GST_DEBUG_OBJECT (ximagesink, "XShm image size is %d, width %d, stride %d",
        ximage->size, ximage->width, ximage->ximage->bytes_per_line);

    ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
        IPC_CREAT | 0777);
    if (ximage->SHMInfo.shmid == -1) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not get shared memory of %d bytes", ximage->size));
      goto beach;
    }

    ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
    if ((int) ximage->SHMInfo.shmaddr == -1) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("Failed to shmat: %s", g_strerror (errno)));
      goto beach;
    }

    ximage->ximage->data = ximage->SHMInfo.shmaddr;
    ximage->SHMInfo.readOnly = FALSE;

    if (XShmAttach (ximagesink->xcontext->disp, &ximage->SHMInfo) == 0) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("Failed to XShmAttach"));
      goto beach;
    }

    XSync (ximagesink->xcontext->disp, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, 0, NULL,
        ximage->width, ximage->height, ximagesink->xcontext->bpp, 0);
    if (!ximage->ximage) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not XCreateImage a %dx%d image"));
      goto beach;
    }

    /* we have to use the returned bytes_per_line for our image size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    ximage->ximage->data = g_malloc (ximage->size);

    XSync (ximagesink->xcontext->disp, FALSE);
  }
  succeeded = TRUE;
  g_mutex_unlock (ximagesink->x_lock);

beach:
  if (!succeeded) {
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
    ximage = NULL;
  }

  return ximage;
}

/* This function destroys a GstXImage handling XShm availability */
static void
gst_ximagesink_ximage_destroy (GstXImageSink * ximagesink, GstXImage * ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* If the destroyed image is the current one we destroy our reference too */
  if (ximagesink->cur_image == ximage)
    ximagesink->cur_image = NULL;

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    if ((int) ximage->SHMInfo.shmaddr != -1) {
      XShmDetach (ximagesink->xcontext->disp, &ximage->SHMInfo);
      shmdt (ximage->SHMInfo.shmaddr);
    }
    if (ximage->SHMInfo.shmid > 0)
      shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);
    if (ximage->ximage)
      XDestroyImage (ximage->ximage);

  } else
#endif /* HAVE_XSHM */
  {
    if (ximage->ximage) {
      if (ximage->ximage->data) {
        g_free (ximage->ximage->data);
      }
      XDestroyImage (ximage->ximage);
    }
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (ximage);
}

/* This function puts a GstXImage on a GstXImageSink's window */
static void
gst_ximagesink_ximage_put (GstXImageSink * ximagesink, GstXImage * ximage)
{
  gint x, y;
  gint w, h;

  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Store a reference to the last image we put */
  if (ximagesink->cur_image != ximage)
    ximagesink->cur_image = ximage;

  /* We center the image in the window; so calculate top left corner location */
  x = MAX (0, (ximagesink->xwindow->width - ximage->width) / 2);
  y = MAX (0, (ximagesink->xwindow->height - ximage->height) / 2);

  w = ximage->width;
  h = ximage->height;

  g_mutex_lock (ximagesink->x_lock);
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    GST_LOG_OBJECT (ximagesink,
        "XShmPutImage, src: %d, %d - dest: %d, %d, dim: %dx%d",
        0, 0, x, y, w, h);
    XShmPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, x, y, w, h, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, x, y, w, h);
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static gboolean
gst_ximagesink_xwindow_decorate (GstXImageSink * ximagesink,
    GstXWindow * window)
{
  Atom hints_atom = None;
  MotifWmHints *hints;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), FALSE);
  g_return_val_if_fail (window != NULL, FALSE);

  g_mutex_lock (ximagesink->x_lock);

  hints_atom = XInternAtom (ximagesink->xcontext->disp, "_MOTIF_WM_HINTS", 1);
  if (hints_atom == None) {
    g_mutex_unlock (ximagesink->x_lock);
    return FALSE;
  }

  hints = g_malloc0 (sizeof (MotifWmHints));

  hints->flags |= MWM_HINTS_DECORATIONS;
  hints->decorations = 1 << 0;

  XChangeProperty (ximagesink->xcontext->disp, window->win,
      hints_atom, hints_atom, 32, PropModeReplace,
      (guchar *) hints, sizeof (MotifWmHints) / sizeof (long));

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (hints);

  return TRUE;
}

/* This function handles a GstXWindow creation */
static GstXWindow *
gst_ximagesink_xwindow_new (GstXImageSink * ximagesink, gint width, gint height)
{
  GstXWindow *xwindow = NULL;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  xwindow = g_new0 (GstXWindow, 1);

  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;

  g_mutex_lock (ximagesink->x_lock);

  xwindow->win = XCreateSimpleWindow (ximagesink->xcontext->disp,
      ximagesink->xcontext->root,
      0, 0, xwindow->width, xwindow->height, 0, 0, ximagesink->xcontext->black);

  XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
      StructureNotifyMask | PointerMotionMask | KeyPressMask |
      KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);

  xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win, 0, NULL);

  XMapRaised (ximagesink->xcontext->disp, xwindow->win);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  gst_ximagesink_xwindow_decorate (ximagesink, xwindow);

  gst_x_overlay_got_xwindow_id (GST_X_OVERLAY (ximagesink), xwindow->win);

  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_ximagesink_xwindow_destroy (GstXImageSink * ximagesink,
    GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (ximagesink->xcontext->disp, xwindow->win);
  else
    XSelectInput (ximagesink->xcontext->disp, xwindow->win, 0);

  XFreeGC (ximagesink->xcontext->disp, xwindow->gc);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (xwindow);
}

/* This function resizes a GstXWindow */
static void
gst_ximagesink_xwindow_resize (GstXImageSink * ximagesink, GstXWindow * xwindow,
    guint width, guint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  xwindow->width = width;
  xwindow->height = height;

  XResizeWindow (ximagesink->xcontext->disp, xwindow->win,
      xwindow->width, xwindow->height);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static void
gst_ximagesink_xwindow_clear (GstXImageSink * ximagesink, GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  XSetForeground (ximagesink->xcontext->disp, xwindow->gc,
      ximagesink->xcontext->black);

  XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
      0, 0, xwindow->width, xwindow->height);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static void
gst_ximagesink_xwindow_update_geometry (GstXImageSink * ximagesink,
    GstXWindow * xwindow)
{
  XWindowAttributes attr;

  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Update the window geometry */
  g_mutex_lock (ximagesink->x_lock);
  XGetWindowAttributes (ximagesink->xcontext->disp,
      ximagesink->xwindow->win, &attr);
  g_mutex_unlock (ximagesink->x_lock);

  ximagesink->xwindow->width = attr.width;
  ximagesink->xwindow->height = attr.height;
}

static void
gst_ximagesink_renegotiate_size (GstXImageSink * ximagesink)
{
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  if (!ximagesink->xwindow)
    return;

  gst_ximagesink_xwindow_update_geometry (ximagesink, ximagesink->xwindow);

  if (ximagesink->sw_scaling_failed)
    return;

  if (ximagesink->xwindow->width <= 1 || ximagesink->xwindow->height <= 1)
    return;

  if (GST_PAD_IS_NEGOTIATING (GST_VIDEOSINK_PAD (ximagesink)) ||
      !gst_pad_is_negotiated (GST_VIDEOSINK_PAD (ximagesink)))
    return;

  /* Window got resized or moved. We do caps negotiation again to get video
     scaler to fit that new size only if size of the window differs from our
     size. */

  if (GST_VIDEOSINK_WIDTH (ximagesink) != ximagesink->xwindow->width ||
      GST_VIDEOSINK_HEIGHT (ximagesink) != ximagesink->xwindow->height) {
    GstPadLinkReturn r;

    r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (ximagesink),
        gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, ximagesink->xcontext->bpp,
            "depth", G_TYPE_INT, ximagesink->xcontext->depth,
            "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
            "red_mask", G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
            "green_mask", G_TYPE_INT, ximagesink->xcontext->visual->green_mask,
            "blue_mask", G_TYPE_INT, ximagesink->xcontext->visual->blue_mask,
            "width", G_TYPE_INT, ximagesink->xwindow->width,
            "height", G_TYPE_INT, ximagesink->xwindow->height,
            "framerate", G_TYPE_DOUBLE, ximagesink->framerate, NULL));

    if ((r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE)) {
      /* Renegotiation succeeded, we update our size and image */
      GST_VIDEOSINK_WIDTH (ximagesink) = ximagesink->xwindow->width;
      GST_VIDEOSINK_HEIGHT (ximagesink) = ximagesink->xwindow->height;

      if ((ximagesink->ximage) &&
          ((GST_VIDEOSINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
              (GST_VIDEOSINK_HEIGHT (ximagesink) !=
                  ximagesink->ximage->height))) {
        /* We renew our ximage only if size changed */
        GST_DEBUG_OBJECT (ximagesink, "destroying and recreating our ximage");
        gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
        ximagesink->ximage = NULL;
      }
    } else {
      ximagesink->sw_scaling_failed = TRUE;
    }
  }
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_ximagesink_handle_xevents (GstXImageSink * ximagesink, GstPad * pad)
{
  XEvent e;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  gst_ximagesink_renegotiate_size (ximagesink);

  /* Then we get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win, PointerMotionMask, &e)) {
    g_mutex_unlock (ximagesink->x_lock);

    switch (e.type) {
      case MotionNotify:
        pointer_x = e.xmotion.x;
        pointer_y = e.xmotion.y;
        pointer_moved = TRUE;
        break;
      default:
        break;
    }

    g_mutex_lock (ximagesink->x_lock);
  }
  g_mutex_unlock (ximagesink->x_lock);

  if (pointer_moved) {
    GST_DEBUG ("ximagesink pointer moved over window at %d,%d",
        pointer_x, pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
        "mouse-move", 0, pointer_x, pointer_y);
  }

  /* We get all remaining events on our window to throw them upstream */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win,
          KeyPressMask | KeyReleaseMask |
          ButtonPressMask | ButtonReleaseMask, &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (ximagesink->x_lock);

    switch (e.type) {
      case ButtonPress:
        /* Mouse button pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("ximagesink button %d pressed over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
            "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case ButtonRelease:
        GST_DEBUG ("ximagesink button %d release over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
            "mouse-button-release", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case KeyPress:
      case KeyRelease:
        /* Key pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("ximagesink key %d pressed over window at %d,%d",
            e.xkey.keycode, e.xkey.x, e.xkey.x);
        keysym = XKeycodeToKeysym (ximagesink->xcontext->disp,
            e.xkey.keycode, 0);
        if (keysym != NoSymbol) {
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ?
              "key-press" : "key-release", XKeysymToString (keysym));
        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG ("ximagesink unhandled X event (%d)", e.type);
    }

    g_mutex_lock (ximagesink->x_lock);
  }
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function gets the X Display and global info about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or
   image creation */
static GstXContext *
gst_ximagesink_xcontext_get (GstXImageSink * ximagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  xcontext = g_new0 (GstXContext, 1);

  g_mutex_lock (ximagesink->x_lock);

  xcontext->disp = XOpenDisplay (ximagesink->display_name);

  if (!xcontext->disp) {
    g_mutex_unlock (ximagesink->x_lock);
    g_free (xcontext);
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, TOO_LAZY, (NULL),
        ("Could not open display"));
    return NULL;
  }

  xcontext->screen = DefaultScreenOfDisplay (xcontext->disp);
  xcontext->screen_num = DefaultScreen (xcontext->disp);
  xcontext->visual = DefaultVisual (xcontext->disp, xcontext->screen_num);
  xcontext->root = DefaultRootWindow (xcontext->disp);
  xcontext->white = XWhitePixel (xcontext->disp, xcontext->screen_num);
  xcontext->black = XBlackPixel (xcontext->disp, xcontext->screen_num);
  xcontext->depth = DefaultDepthOfScreen (xcontext->screen);

  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);

  if (!px_formats) {
    XCloseDisplay (xcontext->disp);
    g_mutex_unlock (ximagesink->x_lock);
    g_free (xcontext);
    return NULL;
  }

  /* We get bpp value corresponding to our running depth */
  for (i = 0; i < nb_formats; i++) {
    if (px_formats[i].depth == xcontext->depth)
      xcontext->bpp = px_formats[i].bits_per_pixel;
  }

  XFree (px_formats);

  xcontext->endianness =
      (ImageByteOrder (xcontext->disp) ==
      LSBFirst) ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;

#ifdef HAVE_XSHM
  /* Search for XShm extension support */
  if (XShmQueryExtension (xcontext->disp) &&
      gst_ximagesink_check_xshm_calls (xcontext)) {
    xcontext->use_xshm = TRUE;
    GST_DEBUG ("ximagesink is using XShm extension");
  } else {
    xcontext->use_xshm = FALSE;
    GST_DEBUG ("ximagesink is not using XShm extension");
  }
#endif /* HAVE_XSHM */

  /* our caps system handles 24/32bpp RGB as big-endian. */
  if ((xcontext->bpp == 24 || xcontext->bpp == 32) &&
      xcontext->endianness == G_LITTLE_ENDIAN) {
    xcontext->endianness = G_BIG_ENDIAN;
    xcontext->visual->red_mask = GUINT32_TO_BE (xcontext->visual->red_mask);
    xcontext->visual->green_mask = GUINT32_TO_BE (xcontext->visual->green_mask);
    xcontext->visual->blue_mask = GUINT32_TO_BE (xcontext->visual->blue_mask);
    if (xcontext->bpp == 24) {
      xcontext->visual->red_mask >>= 8;
      xcontext->visual->green_mask >>= 8;
      xcontext->visual->blue_mask >>= 8;
    }
  }

  xcontext->caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, xcontext->bpp,
      "depth", G_TYPE_INT, xcontext->depth,
      "endianness", G_TYPE_INT, xcontext->endianness,
      "red_mask", G_TYPE_INT, xcontext->visual->red_mask,
      "green_mask", G_TYPE_INT, xcontext->visual->green_mask,
      "blue_mask", G_TYPE_INT, xcontext->visual->blue_mask,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);

  g_mutex_unlock (ximagesink->x_lock);

  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
static void
gst_ximagesink_xcontext_clear (GstXImageSink * ximagesink)
{
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  gst_caps_free (ximagesink->xcontext->caps);

  g_mutex_lock (ximagesink->x_lock);

  XCloseDisplay (ximagesink->xcontext->disp);

  g_mutex_unlock (ximagesink->x_lock);

  ximagesink->xcontext = NULL;
}

static void
gst_ximagesink_imagepool_clear (GstXImageSink * ximagesink)
{
  g_mutex_lock (ximagesink->pool_lock);

  while (ximagesink->image_pool) {
    GstXImage *ximage = ximagesink->image_pool->data;

    ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
        ximagesink->image_pool);
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  }

  g_mutex_unlock (ximagesink->pool_lock);
}

/* Element stuff */

static GstCaps *
gst_ximagesink_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1)
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static GstCaps *
gst_ximagesink_getcaps (GstPad * pad)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  if (ximagesink->xcontext)
    return gst_caps_copy (ximagesink->xcontext->caps);

  return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
}

static GstPadLinkReturn
gst_ximagesink_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstXImageSink *ximagesink;
  gboolean ret;
  GstStructure *structure;

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  if (!ximagesink->xcontext)
    return GST_PAD_LINK_DELAYED;

  GST_DEBUG_OBJECT (ximagesink,
      "sinkconnect possible caps %" GST_PTR_FORMAT " with given caps %"
      GST_PTR_FORMAT, ximagesink->xcontext->caps, caps);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width",
      &(GST_VIDEOSINK_WIDTH (ximagesink)));
  ret &= gst_structure_get_int (structure, "height",
      &(GST_VIDEOSINK_HEIGHT (ximagesink)));
  ret &= gst_structure_get_double (structure,
      "framerate", &ximagesink->framerate);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  ximagesink->pixel_width = 1;
  gst_structure_get_int (structure, "pixel_width", &ximagesink->pixel_width);

  ximagesink->pixel_height = 1;
  gst_structure_get_int (structure, "pixel_height", &ximagesink->pixel_height);

  /* Creating our window and our image */
  g_assert (GST_VIDEOSINK_WIDTH (ximagesink) > 0);
  g_assert (GST_VIDEOSINK_HEIGHT (ximagesink) > 0);
  if (!ximagesink->xwindow) {
    ximagesink->xwindow = gst_ximagesink_xwindow_new (ximagesink,
        GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));
  } else {
    if (ximagesink->xwindow->internal) {
      gst_ximagesink_xwindow_resize (ximagesink, ximagesink->xwindow,
          GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));
    }
  }

  /* If our ximage has changed we destroy it, next chain iteration will create
     a new one */
  if ((ximagesink->ximage) &&
      ((GST_VIDEOSINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
          (GST_VIDEOSINK_HEIGHT (ximagesink) != ximagesink->ximage->height))) {
    gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
    ximagesink->ximage = NULL;
  }

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (ximagesink),
      GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));

  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_ximagesink_change_state (GstElement * element)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      if (!ximagesink->xcontext)
        ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);
      if (!ximagesink->xcontext)
        return GST_STATE_FAILURE;
      /* call XSynchronize with the current value of synchronous */
      GST_DEBUG_OBJECT (ximagesink, "XSynchronize called with %s",
          ximagesink->synchronous ? "TRUE" : "FALSE");
      XSynchronize (ximagesink->xcontext->disp, ximagesink->synchronous);
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (ximagesink->xwindow)
        gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);
      ximagesink->time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      ximagesink->framerate = 0;
      ximagesink->sw_scaling_failed = FALSE;
      GST_VIDEOSINK_WIDTH (ximagesink) = 0;
      GST_VIDEOSINK_HEIGHT (ximagesink) = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      if (ximagesink->ximage) {
        gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
        ximagesink->ximage = NULL;
      }

      if (ximagesink->image_pool)
        gst_ximagesink_imagepool_clear (ximagesink);

      if (ximagesink->xwindow) {
        gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
        ximagesink->xwindow = NULL;
      }

      if (ximagesink->xcontext) {
        gst_ximagesink_xcontext_clear (ximagesink);
        ximagesink->xcontext = NULL;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ximagesink_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstXImageSink *ximagesink;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (data)) {
    gst_pad_event_default (pad, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* update time */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    ximagesink->time = GST_BUFFER_TIMESTAMP (buf);
  }
  GST_LOG_OBJECT (ximagesink, "clock wait: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ximagesink->time));

  if (GST_VIDEOSINK_CLOCK (ximagesink)) {
    gst_element_wait (GST_ELEMENT (ximagesink), ximagesink->time);
  }

  /* If this buffer has been allocated using our buffer management we simply
     put the ximage which is in the PRIVATE pointer */
  if (GST_BUFFER_FREE_DATA_FUNC (buf) == gst_ximagesink_buffer_free) {
    gst_ximagesink_ximage_put (ximagesink, GST_BUFFER_PRIVATE (buf));
  } else {
    /* Else we have to copy the data into our private image, */
    /* if we have one... */
    if (!ximagesink->ximage) {
      GST_DEBUG_OBJECT (ximagesink, "creating our ximage");
      ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
          GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));
      if (!ximagesink->ximage) {
        /* No image available. That's very bad ! */
        gst_buffer_unref (buf);
        GST_ELEMENT_ERROR (ximagesink, CORE, NEGOTIATION, (NULL),
            ("Failed creating an XImage in ximagesink chain function."));
        return;
      }
    }

    memcpy (ximagesink->ximage->ximage->data,
        GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (buf), ximagesink->ximage->size));
    gst_ximagesink_ximage_put (ximagesink, ximagesink->ximage);
  }

  /* set correct time for next buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf) && ximagesink->framerate > 0) {
    ximagesink->time += GST_SECOND / ximagesink->framerate;
  }

  gst_buffer_unref (buf);

  gst_ximagesink_handle_xevents (ximagesink, pad);
}

/* Buffer management */

static void
gst_ximagesink_buffer_free (GstBuffer * buffer)
{
  GstXImageSink *ximagesink;
  GstXImage *ximage;

  ximage = GST_BUFFER_PRIVATE (buffer);

  g_assert (GST_IS_XIMAGESINK (ximage->ximagesink));
  ximagesink = ximage->ximagesink;

  /* If our geometry changed we can't reuse that image. */
  if ((ximage->width != GST_VIDEOSINK_WIDTH (ximagesink)) ||
      (ximage->height != GST_VIDEOSINK_HEIGHT (ximagesink)))
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  else {
    /* In that case we can reuse the image and add it to our image pool. */
    g_mutex_lock (ximagesink->pool_lock);
    ximagesink->image_pool = g_slist_prepend (ximagesink->image_pool, ximage);
    g_mutex_unlock (ximagesink->pool_lock);
  }
}

static GstBuffer *
gst_ximagesink_buffer_alloc (GstPad * pad, guint64 offset, guint size)
{
  GstXImageSink *ximagesink;
  GstBuffer *buffer;
  GstXImage *ximage = NULL;
  gboolean not_found = TRUE;

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  g_mutex_lock (ximagesink->pool_lock);

  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && ximagesink->image_pool) {
    ximage = ximagesink->image_pool->data;

    if (ximage) {
      /* Removing from the pool */
      ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
          ximagesink->image_pool);

      if ((ximage->width != GST_VIDEOSINK_WIDTH (ximagesink)) ||
          (ximage->height != GST_VIDEOSINK_HEIGHT (ximagesink))) {
        /* This image is unusable. Destroying... */
        gst_ximagesink_ximage_destroy (ximagesink, ximage);
        ximage = NULL;
      } else {
        /* We found a suitable image */
        break;
      }
    }
  }

  g_mutex_unlock (ximagesink->pool_lock);

  if (!ximage) {
    /* We found no suitable image in the pool. Creating... */
    GST_DEBUG_OBJECT (ximagesink, "no usable image in pool, creating ximage");
    ximage = gst_ximagesink_ximage_new (ximagesink,
        GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));
  }

  if (ximage) {
    buffer = gst_buffer_new ();

    /* Storing some pointers in the buffer */
    GST_BUFFER_PRIVATE (buffer) = ximage;

    GST_BUFFER_DATA (buffer) = ximage->ximage->data;
    GST_BUFFER_FREE_DATA_FUNC (buffer) = gst_ximagesink_buffer_free;
    GST_BUFFER_SIZE (buffer) = ximage->size;
    return buffer;
  } else
    return NULL;
}

/* Interfaces stuff */

static gboolean
gst_ximagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_ximagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_ximagesink_interface_supported;
}

static void
gst_ximagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (navigation);
  GstEvent *event;
  gint x_offset, y_offset;
  double x, y;

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* We are not converting the pointer coordinates as there's no hardware
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling. So here we just add the offset if the image
     is centered in the window.  */

  x_offset = ximagesink->xwindow->width - GST_VIDEOSINK_WIDTH (ximagesink);
  y_offset = ximagesink->xwindow->height - GST_VIDEOSINK_HEIGHT (ximagesink);

  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x += x_offset;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y += y_offset;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD (ximagesink)), event);
}

static void
gst_ximagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_ximagesink_navigation_send_event;
}

static void
gst_ximagesink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);
  GstXWindow *xwindow = NULL;
  XWindowAttributes attr;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* If we already use that window return */
  if (ximagesink->xwindow && (xwindow_id == ximagesink->xwindow->win))
    return;

  /* If the element has not initialized the X11 context try to do so */
  if (!ximagesink->xcontext)
    ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);

  if (!ximagesink->xcontext) {
    g_warning ("ximagesink was unable to obtain the X11 context.");
    return;
  }

  /* Clear image pool as the images are unusable anyway */
  gst_ximagesink_imagepool_clear (ximagesink);

  /* Clear the ximage */
  if (ximagesink->ximage) {
    gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
    ximagesink->ximage = NULL;
  }

  /* If a window is there already we destroy it */
  if (ximagesink->xwindow) {
    gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
    ximagesink->xwindow = NULL;
  }

  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0) {
    /* If no width/height caps nego did not happen window will be created
       during caps nego then */
    if (GST_VIDEOSINK_WIDTH (ximagesink) && GST_VIDEOSINK_HEIGHT (ximagesink)) {
      xwindow = gst_ximagesink_xwindow_new (ximagesink,
          GST_VIDEOSINK_WIDTH (ximagesink), GST_VIDEOSINK_HEIGHT (ximagesink));
    }
  } else {
    xwindow = g_new0 (GstXWindow, 1);

    xwindow->win = xwindow_id;

    /* We get window geometry, set the event we want to receive,
       and create a GC */
    g_mutex_lock (ximagesink->x_lock);
    XGetWindowAttributes (ximagesink->xcontext->disp, xwindow->win, &attr);
    xwindow->width = attr.width;
    xwindow->height = attr.height;
    xwindow->internal = FALSE;
    XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask);

    xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win, 0, NULL);
    g_mutex_unlock (ximagesink->x_lock);

    /* If that new window geometry differs from our one we try to
       renegotiate caps */
    if (gst_pad_is_negotiated (GST_VIDEOSINK_PAD (ximagesink)) &&
        (xwindow->width != GST_VIDEOSINK_WIDTH (ximagesink) ||
            xwindow->height != GST_VIDEOSINK_HEIGHT (ximagesink))) {
      GstPadLinkReturn r;

      r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (ximagesink),
          gst_caps_new_simple ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, ximagesink->xcontext->bpp,
              "depth", G_TYPE_INT, ximagesink->xcontext->depth,
              "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
              "red_mask", G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
              "green_mask", G_TYPE_INT,
              ximagesink->xcontext->visual->green_mask, "blue_mask", G_TYPE_INT,
              ximagesink->xcontext->visual->blue_mask, "width", G_TYPE_INT,
              xwindow->width, "height", G_TYPE_INT, xwindow->height,
              "framerate", G_TYPE_DOUBLE, ximagesink->framerate, NULL));

      /* If caps nego succeded updating our size */
      if ((r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE)) {
        GST_VIDEOSINK_WIDTH (ximagesink) = xwindow->width;
        GST_VIDEOSINK_HEIGHT (ximagesink) = xwindow->height;
      }
    }
  }

  if (xwindow)
    ximagesink->xwindow = xwindow;
}

static void
gst_ximagesink_get_desired_size (GstXOverlay * overlay,
    guint * width, guint * height)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  *width = GST_VIDEOSINK_WIDTH (ximagesink);
  *height = GST_VIDEOSINK_HEIGHT (ximagesink);
}

static void
gst_ximagesink_expose (GstXOverlay * overlay)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  if (!ximagesink->xwindow)
    return;

  gst_ximagesink_xwindow_update_geometry (ximagesink, ximagesink->xwindow);

  /* We don't act on internal window from outside that could cause some thread
     race with the video sink own thread checking for configure event */
  if (ximagesink->xwindow->internal)
    return;

  gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);

  if (ximagesink->cur_image)
    gst_ximagesink_ximage_put (ximagesink, ximagesink->cur_image);
}

static void
gst_ximagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_ximagesink_set_xwindow_id;
  iface->get_desired_size = gst_ximagesink_get_desired_size;
  iface->expose = gst_ximagesink_expose;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_ximagesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXImageSink *ximagesink;

  g_return_if_fail (GST_IS_XIMAGESINK (object));

  ximagesink = GST_XIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      ximagesink->display_name = g_strdup (g_value_get_string (value));
      break;
    case ARG_SYNCHRONOUS:
      ximagesink->synchronous = g_value_get_boolean (value);
      if (ximagesink->xcontext) {
        GST_DEBUG_OBJECT (ximagesink, "XSynchronize called with %s",
            ximagesink->synchronous ? "TRUE" : "FALSE");
        XSynchronize (ximagesink->xcontext->disp, ximagesink->synchronous);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstXImageSink *ximagesink;

  g_return_if_fail (GST_IS_XIMAGESINK (object));

  ximagesink = GST_XIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, g_strdup (ximagesink->display_name));
      break;
    case ARG_SYNCHRONOUS:
      g_value_set_boolean (value, ximagesink->synchronous);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesink_finalize (GObject * object)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (object);

  if (ximagesink->display_name) {
    g_free (ximagesink->display_name);
    ximagesink->display_name = NULL;
  }

  g_mutex_free (ximagesink->x_lock);
  g_mutex_free (ximagesink->pool_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ximagesink_init (GstXImageSink * ximagesink)
{
  GST_VIDEOSINK_PAD (ximagesink) =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_ximagesink_sink_template_factory), "sink");

  gst_element_add_pad (GST_ELEMENT (ximagesink),
      GST_VIDEOSINK_PAD (ximagesink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (ximagesink),
      gst_ximagesink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (ximagesink),
      gst_ximagesink_sink_link);
  gst_pad_set_getcaps_function (GST_VIDEOSINK_PAD (ximagesink),
      gst_ximagesink_getcaps);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (ximagesink),
      gst_ximagesink_fixate);
  gst_pad_set_bufferalloc_function (GST_VIDEOSINK_PAD (ximagesink),
      gst_ximagesink_buffer_alloc);

  ximagesink->display_name = NULL;
  ximagesink->xcontext = NULL;
  ximagesink->xwindow = NULL;
  ximagesink->ximage = NULL;
  ximagesink->cur_image = NULL;

  ximagesink->framerate = 0;

  ximagesink->x_lock = g_mutex_new ();

  ximagesink->pixel_width = ximagesink->pixel_height = 1;

  ximagesink->image_pool = NULL;
  ximagesink->pool_lock = g_mutex_new ();

  ximagesink->sw_scaling_failed = FALSE;
  ximagesink->synchronous = FALSE;

  GST_FLAG_SET (ximagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (ximagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_ximagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_ximagesink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ximagesink_sink_template_factory));
}

static void
gst_ximagesink_class_init (GstXImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEOSINK);

  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous", "When enabled, runs "
          "the X display in synchronous mode. (used only for debugging)", FALSE,
          G_PARAM_READWRITE));

  gobject_class->finalize = gst_ximagesink_finalize;
  gobject_class->set_property = gst_ximagesink_set_property;
  gobject_class->get_property = gst_ximagesink_get_property;

  gstelement_class->change_state = gst_ximagesink_change_state;
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

  if (!ximagesink_type) {
    static const GTypeInfo ximagesink_info = {
      sizeof (GstXImageSinkClass),
      gst_ximagesink_base_init,
      NULL,
      (GClassInitFunc) gst_ximagesink_class_init,
      NULL,
      NULL,
      sizeof (GstXImageSink),
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
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_ximagesink_xoverlay_init,
      NULL,
      NULL,
    };

    ximagesink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
        "GstXImageSink", &ximagesink_info, 0);

    g_type_add_interface_static (ximagesink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &iface_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return ximagesink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  if (!gst_element_register (plugin, "ximagesink",
          GST_RANK_SECONDARY, GST_TYPE_XIMAGESINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_ximagesink, "ximagesink", 0,
      "ximagesink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ximagesink",
    "XFree86 video output plugin based on standard Xlib calls",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
