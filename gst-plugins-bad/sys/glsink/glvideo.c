/* glvideo.c
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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

#include "glvideo.h"
/* only use gst for debugging */
#include <gst/gst.h>

#include <string.h>


static gboolean glv_display_check_features (GLVideoDisplay * display);


GLVideoDisplay *
glv_display_new (const char *display_name)
{
  GLVideoDisplay *display;
  gboolean usable;

  display = g_malloc0 (sizeof (GLVideoDisplay));

  display->display = XOpenDisplay (display_name);
  if (display->display == NULL) {
    g_free (display);
    return NULL;
  }

  usable = glv_display_check_features (display);
  if (!usable) {
    g_free (display);
    return NULL;
  }

  display->lock = g_mutex_new ();

  return display;
}

static gboolean
glv_display_check_features (GLVideoDisplay * display)
{
  gboolean ret;
  XVisualInfo *visinfo;
  Screen *screen;
  Window root;
  int scrnum;
  int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None
  };
  XSetWindowAttributes attr;
  int error_base;
  int event_base;
  int mask;
  const char *extstring;
  Window window;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (display->display, scrnum);

  ret = glXQueryExtension (display->display, &error_base, &event_base);
  if (!ret) {
    GST_DEBUG ("No GLX extension");
    return FALSE;
  }

  visinfo = glXChooseVisual (display->display, scrnum, attrib);
  if (visinfo == NULL) {
    GST_DEBUG ("No usable visual");
    return FALSE;
  }

  display->visinfo = visinfo;

  display->context = glXCreateContext (display->display, visinfo, NULL, True);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (display->display, root,
      visinfo->visual, AllocNone);
  attr.event_mask = StructureNotifyMask | ExposureMask;
  attr.override_redirect = True;

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  window = XCreateWindow (display->display, root, 0, 0,
      100, 100, 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);

  XSync (display->display, FALSE);

  glXMakeCurrent (display->display, window, display->context);

  glGetIntegerv (GL_MAX_TEXTURE_SIZE, &display->max_texture_size);

  extstring = (const char *) glGetString (GL_EXTENSIONS);
#ifdef GL_YCBCR_MESA
  if (strstr (extstring, "GL_MESA_ycbcr_texture")) {
    display->have_ycbcr_texture = TRUE;
  } else {
    display->have_ycbcr_texture = FALSE;
  }
#else
  display->have_ycbcr_texture = FALSE;
#endif

  glXMakeCurrent (display->display, None, NULL);
  XDestroyWindow (display->display, window);

  return TRUE;
}

void
glv_display_free (GLVideoDisplay * display)
{
  /* sure hope nobody is using it as it's being freed */
  g_mutex_lock (display->lock);
  g_mutex_unlock (display->lock);

  if (display->context) {
    glXDestroyContext (display->display, display->context);
  }
  if (display->visinfo) {
    XFree (display->visinfo);
  }
  if (display->display) {
    XCloseDisplay (display->display);
  }

  g_mutex_free (display->lock);

  g_free (display);
}


/* drawable */

GLVideoDrawable *
glv_drawable_new_window (GLVideoDisplay * display)
{
  GLVideoDrawable *drawable;
  XSetWindowAttributes attr = { 0 };
  int scrnum;
  int mask;
  Window root;
  Screen *screen;

  drawable = g_malloc0 (sizeof (GLVideoDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (display->display, scrnum);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (display->display, root,
      display->visinfo->visual, AllocNone);
  attr.override_redirect = False;
#if 0
  if (display->parent_window) {
    attr.override_redirect = True;
  }
#endif

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  drawable->window = XCreateWindow (display->display,
      root, 0, 0, 100, 100,
      0, display->visinfo->depth, InputOutput,
      display->visinfo->visual, mask, &attr);
  XMapWindow (display->display, drawable->window);
  drawable->destroy_on_free = TRUE;

  g_mutex_unlock (display->lock);

  return drawable;
}

GLVideoDrawable *
glv_drawable_new_root_window (GLVideoDisplay * display)
{
  GLVideoDrawable *drawable;
  int scrnum;
  Screen *screen;

  drawable = g_malloc0 (sizeof (GLVideoDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);

  drawable->window = XRootWindow (display->display, scrnum);
  drawable->destroy_on_free = FALSE;
  g_mutex_unlock (display->lock);

  return drawable;
}

GLVideoDrawable *
glv_drawable_new_from_window (GLVideoDisplay * display, Window window)
{
  GLVideoDrawable *drawable;

  drawable = g_malloc0 (sizeof (GLVideoDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  drawable->window = window;
  drawable->destroy_on_free = FALSE;

  g_mutex_unlock (display->lock);
  return drawable;
}

void
glv_drawable_free (GLVideoDrawable * drawable)
{

  g_mutex_lock (drawable->display->lock);
  if (drawable->destroy_on_free) {
    XDestroyWindow (drawable->display->display, drawable->window);
  }
  g_mutex_unlock (drawable->display->lock);

  g_free (drawable);
}

void
glv_drawable_lock (GLVideoDrawable * drawable)
{
  g_mutex_lock (drawable->display->lock);
  glXMakeCurrent (drawable->display->display, drawable->window,
      drawable->display->context);
}

void
glv_drawable_unlock (GLVideoDrawable * drawable)
{
  glXMakeCurrent (drawable->display->display, None, NULL);
  g_mutex_unlock (drawable->display->lock);
}

void
glv_drawable_update_attributes (GLVideoDrawable * drawable)
{
  XWindowAttributes attr;

  XGetWindowAttributes (drawable->display->display, drawable->window, &attr);
  drawable->win_width = attr.width;
  drawable->win_height = attr.height;

}

void
glv_drawable_clear (GLVideoDrawable * drawable)
{

  glv_drawable_lock (drawable);

  glDepthFunc (GL_LESS);
  glEnable (GL_DEPTH_TEST);
  glClearColor (0.2, 0.2, 0.2, 1.0);
  glViewport (0, 0, drawable->win_width, drawable->win_height);

  glv_drawable_unlock (drawable);
}



void
glv_drawable_draw_image (GLVideoDrawable * drawable, GLVideoImageType type,
    void *data, int width, int height)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);

  glv_drawable_lock (drawable);

  glv_drawable_update_attributes (drawable);

  glViewport (0, 0, drawable->win_width, drawable->win_height);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 1);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  switch (type) {
    case GLVIDEO_IMAGE_TYPE_RGBx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GLVIDEO_IMAGE_TYPE_BGRx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GLVIDEO_IMAGE_TYPE_YUY2:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GLVIDEO_IMAGE_TYPE_UYVY:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    default:
      g_assert_not_reached ();
  }

  glColor4f (1, 0, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  glTexCoord2f (width, 0);
  glVertex3f (1.0, 1.0, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-1.0, 1.0, 0);
  glTexCoord2f (0, height);
  glVertex3f (-1.0, -1.0, 0);
  glTexCoord2f (width, height);
  glVertex3f (1.0, -1.0, 0);
  glEnd ();

  glFlush ();
  glXSwapBuffers (drawable->display->display, drawable->window);

  glv_drawable_unlock (drawable);
}
