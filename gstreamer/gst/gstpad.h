/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstpad.h: Header for GstPad object
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


#ifndef __GST_PAD_H__
#define __GST_PAD_H__

#include <gst/gstconfig.h>

#include <gst/gstobject.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstevent.h>
#include <gst/gstprobe.h>
#include <gst/gstquery.h>


G_BEGIN_DECLS

extern GType _gst_pad_type;
extern GType _gst_real_pad_type;
extern GType _gst_ghost_pad_type;

#define GST_TYPE_PARANOID 

/* 
 * Pad base class
 */
#define GST_TYPE_PAD			(_gst_pad_type) 

#define GST_PAD_CAST(obj)		((GstPad*)(obj))
#define GST_PAD_CLASS_CAST(klass)	((GstPadClass*)(klass))
#define GST_IS_PAD(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PAD))
#define GST_IS_PAD_FAST(obj)		(G_OBJECT_TYPE(obj) == GST_TYPE_REAL_PAD || \
					 G_OBJECT_TYPE(obj) == GST_TYPE_GHOST_PAD)
#define GST_IS_PAD_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_PAD(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PAD, GstPad))
# define GST_PAD_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD, GstPadClass))
#else
# define GST_PAD			GST_PAD_CAST
# define GST_PAD_CLASS			GST_PAD_CLASS_CAST
#endif

/* 
 * Real Pads
 */
#define GST_TYPE_REAL_PAD		(_gst_real_pad_type)

#define GST_REAL_PAD_CAST(obj)		((GstRealPad*)(obj))
#define GST_REAL_PAD_CLASS_CAST(klass)	((GstRealPadClass*)(klass))
#define GST_IS_REAL_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_REAL_PAD))
#define GST_IS_REAL_PAD_FAST(obj)	(G_OBJECT_TYPE(obj) == GST_TYPE_REAL_PAD)
#define GST_IS_REAL_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_REAL_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_REAL_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_REAL_PAD, GstRealPad))
# define GST_REAL_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_REAL_PAD, GstRealPadClass))
#else
# define GST_REAL_PAD			GST_REAL_PAD_CAST
# define GST_REAL_PAD_CLASS		GST_REAL_PAD_CLASS_CAST
#endif

/* 
 * Ghost Pads
 */
#define GST_TYPE_GHOST_PAD		(_gst_ghost_pad_type)

#define GST_GHOST_PAD_CAST(obj)		((GstGhostPad*)(obj))
#define GST_GHOST_PAD_CLASS_CAST(klass)	((GstGhostPadClass*)(klass))
#define GST_IS_GHOST_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GHOST_PAD))
#define GST_IS_GHOST_PAD_FAST(obj)	(G_OBJECT_TYPE(obj) == GST_TYPE_GHOST_PAD)
#define GST_IS_GHOST_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GHOST_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_GHOST_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GHOST_PAD, GstGhostPad))
# define GST_GHOST_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GHOST_PAD, GstGhostPadClass))
#else
# define GST_GHOST_PAD			GST_GHOST_PAD_CAST
# define GST_GHOST_PAD_CLASS		GST_GHOST_PAD_CLASS_CAST
#endif


/*typedef struct _GstPad GstPad; */
/*typedef struct _GstPadClass GstPadClass;*/
typedef struct _GstRealPad GstRealPad;
typedef struct _GstRealPadClass GstRealPadClass;
typedef struct _GstGhostPad GstGhostPad;
typedef struct _GstGhostPadClass GstGhostPadClass;
/*typedef struct _GstPadTemplate GstPadTemplate;*/
/*typedef struct _GstPadTemplateClass GstPadTemplateClass;*/


typedef enum {
  GST_PAD_LINK_REFUSED = -1,
  GST_PAD_LINK_DELAYED =  0,
  GST_PAD_LINK_OK      =  1,
  GST_PAD_LINK_DONE    =  2
} GstPadLinkReturn;

/* convenience functions */
#ifdef G_HAVE_ISO_VARARGS
#define GST_PAD_QUERY_TYPE_FUNCTION(functionname, ...)  GST_QUERY_TYPE_FUNCTION (GstPad *, functionname, __VA_ARGS__);
#define GST_PAD_FORMATS_FUNCTION(functionname, ...)  	GST_FORMATS_FUNCTION (GstPad *, functionname, __VA_ARGS__);
#define GST_PAD_EVENT_MASK_FUNCTION(functionname, ...) 	GST_EVENT_MASK_FUNCTION (GstPad *, functionname, __VA_ARGS__);
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_PAD_QUERY_TYPE_FUNCTION(functionname, a...) GST_QUERY_TYPE_FUNCTION (GstPad *, functionname, a);
#define GST_PAD_FORMATS_FUNCTION(functionname, a...)  	GST_FORMATS_FUNCTION (GstPad *, functionname, a);
#define GST_PAD_EVENT_MASK_FUNCTION(functionname, a...) GST_EVENT_MASK_FUNCTION (GstPad *, functionname, a);
#endif

 
/* this defines the functions used to chain buffers
 * pad is the sink pad (so the same chain function can be used for N pads)
 * buf is the buffer being passed */
typedef void 			(*GstPadChainFunction) 		(GstPad *pad,GstBuffer *buf);
typedef GstBuffer*		(*GstPadGetFunction) 		(GstPad *pad);
typedef gboolean		(*GstPadEventFunction)		(GstPad *pad, GstEvent *event);
typedef gboolean		(*GstPadConvertFunction)	(GstPad *pad, 
		 						 GstFormat src_format,  gint64  src_value,
								 GstFormat *dest_format, gint64 *dest_value);
typedef gboolean		(*GstPadQueryFunction)		(GstPad *pad, GstQueryType type,
		 						 GstFormat *format, gint64  *value);
typedef GList*			(*GstPadIntLinkFunction)	(GstPad *pad);
typedef const GstFormat*	(*GstPadFormatsFunction)	(GstPad *pad);
typedef const GstEventMask*	(*GstPadEventMaskFunction)	(GstPad *pad);
typedef const GstQueryType*	(*GstPadQueryTypeFunction)	(GstPad *pad);

typedef GstPadLinkReturn	(*GstPadLinkFunction) 	(GstPad *pad, GstCaps *caps);
typedef GstCaps*		(*GstPadGetCapsFunction) 	(GstPad *pad, GstCaps *caps);
typedef GstBufferPool*		(*GstPadBufferPoolFunction) 	(GstPad *pad);

typedef gboolean 		(*GstPadDispatcherFunction) 	(GstPad *pad, gpointer data);

typedef enum {
  GST_PAD_UNKNOWN,
  GST_PAD_SRC,
  GST_PAD_SINK
} GstPadDirection;

typedef enum {
  GST_PAD_DISABLED		= GST_OBJECT_FLAG_LAST,
  GST_PAD_NEGOTIATING,

  GST_PAD_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 4
} GstPadFlags;

struct _GstPad {
  GstObject 		object;

  gpointer 		element_private;

  GstPadTemplate 	*padtemplate;	/* the template for this pad */
};

struct _GstPadClass {
  GstObjectClass parent_class;
};

struct _GstRealPad {
  GstPad 			 pad;

  /* the pad capabilities */
  GstCaps 			*caps;
  GstCaps 			*filter;
  GstCaps 			*appfilter;
  GstPadGetCapsFunction 	 getcapsfunc;
  
  GstPadDirection 		 direction;

  GstPadLinkFunction 		 linkfunc;
  GstRealPad 			*peer;

  gpointer 			 sched_private;

  /* data transport functions */
  GstPadChainFunction 		 chainfunc;
  GstPadChainFunction 		 chainhandler;
  GstPadGetFunction 		 getfunc;
  GstPadGetFunction		 gethandler;
  GstPadEventFunction		 eventfunc;
  GstPadEventFunction		 eventhandler;
  GstPadEventMaskFunction	 eventmaskfunc;

  GList 			*ghostpads;

  /* query/convert/formats functions */
  GstPadConvertFunction		 convertfunc;
  GstPadQueryFunction		 queryfunc;
  GstPadFormatsFunction		 formatsfunc;
  GstPadQueryTypeFunction	 querytypefunc;
  GstPadIntLinkFunction		 intlinkfunc;

  GstPadBufferPoolFunction 	 bufferpoolfunc;

  GstProbeDispatcher 		 probedisp;
};

struct _GstRealPadClass {
  GstPadClass parent_class;

  /* signal callbacks */
  void (*caps_nego_failed)	(GstPad *pad);
  void (*linked)		(GstPad *pad, GstPad *peer);
  void (*unlinked)		(GstPad *pad, GstPad *peer);
};

struct _GstGhostPad {
  GstPad pad;

  GstRealPad *realpad;
};

struct _GstGhostPadClass {
  GstPadClass parent_class;
};


/***** helper macros *****/
/* GstPad */
#define GST_PAD_NAME(pad)		(GST_OBJECT_NAME(pad))
#define GST_PAD_PARENT(pad)		((GstElement *)(GST_OBJECT_PARENT(pad)))
#define GST_PAD_ELEMENT_PRIVATE(pad)	(((GstPad *)(pad))->element_private)
#define GST_PAD_PAD_TEMPLATE(pad)	(((GstPad *)(pad))->padtemplate)

/* GstRealPad */
#define GST_RPAD_DIRECTION(pad)		(((GstRealPad *)(pad))->direction)
#define GST_RPAD_CAPS(pad)		(((GstRealPad *)(pad))->caps)
#define GST_RPAD_FILTER(pad)		(((GstRealPad *)(pad))->filter)
#define GST_RPAD_APPFILTER(pad)		(((GstRealPad *)(pad))->appfilter)
#define GST_RPAD_PEER(pad)		(((GstRealPad *)(pad))->peer)
#define GST_RPAD_CHAINFUNC(pad)		(((GstRealPad *)(pad))->chainfunc)
#define GST_RPAD_CHAINHANDLER(pad)	(((GstRealPad *)(pad))->chainhandler)
#define GST_RPAD_GETFUNC(pad)		(((GstRealPad *)(pad))->getfunc)
#define GST_RPAD_GETHANDLER(pad)	(((GstRealPad *)(pad))->gethandler)
#define GST_RPAD_EVENTFUNC(pad)		(((GstRealPad *)(pad))->eventfunc)
#define GST_RPAD_EVENTHANDLER(pad)	(((GstRealPad *)(pad))->eventhandler)
#define GST_RPAD_CONVERTFUNC(pad)	(((GstRealPad *)(pad))->convertfunc)
#define GST_RPAD_QUERYFUNC(pad)		(((GstRealPad *)(pad))->queryfunc)
#define GST_RPAD_INTLINKFUNC(pad)	(((GstRealPad *)(pad))->intlinkfunc)
#define GST_RPAD_FORMATSFUNC(pad)	(((GstRealPad *)(pad))->formatsfunc)
#define GST_RPAD_QUERYTYPEFUNC(pad)	(((GstRealPad *)(pad))->querytypefunc)
#define GST_RPAD_EVENTMASKFUNC(pad)	(((GstRealPad *)(pad))->eventmaskfunc)

#define GST_RPAD_LINKFUNC(pad)		(((GstRealPad *)(pad))->linkfunc)
#define GST_RPAD_GETCAPSFUNC(pad)	(((GstRealPad *)(pad))->getcapsfunc)
#define GST_RPAD_BUFFERPOOLFUNC(pad)	(((GstRealPad *)(pad))->bufferpoolfunc)

/* GstGhostPad */
#define GST_GPAD_REALPAD(pad)		(((GstGhostPad *)(pad))->realpad)

/* Generic */
#define GST_PAD_REALIZE(pad)		(GST_IS_REAL_PAD(pad) ? ((GstRealPad *)(pad)) : GST_GPAD_REALPAD(pad))
#define GST_PAD_DIRECTION(pad)		GST_RPAD_DIRECTION(GST_PAD_REALIZE(pad))
#define GST_PAD_CAPS(pad)		GST_RPAD_CAPS(GST_PAD_REALIZE(pad))
#define GST_PAD_PEER(pad)		GST_PAD_CAST(GST_RPAD_PEER(GST_PAD_REALIZE(pad)))

/* Some check functions (unused?) */
#define GST_PAD_IS_LINKED(pad)	(GST_PAD_PEER(pad) != NULL)
#define GST_PAD_IS_ACTIVE(pad)		(!GST_FLAG_IS_SET(GST_PAD_REALIZE(pad), GST_PAD_DISABLED))
#define GST_PAD_IS_USABLE(pad)		(GST_PAD_IS_LINKED (pad) && \
		                         GST_PAD_IS_ACTIVE(pad) && GST_PAD_IS_ACTIVE(GST_PAD_PEER (pad)))
#define GST_PAD_CAN_PULL(pad)		(GST_IS_REAL_PAD(pad) && GST_REAL_PAD(pad)->gethandler != NULL)
#define GST_PAD_IS_SRC(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SRC)
#define GST_PAD_IS_SINK(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SINK)

/***** PadTemplate *****/
#define GST_TYPE_PAD_TEMPLATE		(gst_pad_template_get_type ())
#define GST_PAD_TEMPLATE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PAD_TEMPLATE,GstPadTemplate))
#define GST_PAD_TEMPLATE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD_TEMPLATE,GstPadTemplateClass))
#define GST_IS_PAD_TEMPLATE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PAD_TEMPLATE))
#define GST_IS_PAD_TEMPLATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD_TEMPLATE))

typedef enum {
  GST_PAD_ALWAYS,
  GST_PAD_SOMETIMES,
  GST_PAD_REQUEST
} GstPadPresence;

#define GST_PAD_TEMPLATE_NAME_TEMPLATE(templ)	(((GstPadTemplate *)(templ))->name_template)
#define GST_PAD_TEMPLATE_DIRECTION(templ)	(((GstPadTemplate *)(templ))->direction)
#define GST_PAD_TEMPLATE_PRESENCE(templ)	(((GstPadTemplate *)(templ))->presence)
#define GST_PAD_TEMPLATE_CAPS(templ)		(((GstPadTemplate *)(templ))->caps)
#define GST_PAD_TEMPLATE_FIXED(templ)		(((GstPadTemplate *)(templ))->fixed)

#define GST_PAD_TEMPLATE_IS_FIXED(templ)	(GST_PAD_TEMPLATE_FIXED(templ) == TRUE)

struct _GstPadTemplate {
  GstObject	  object;

  gchar           *name_template;
  GstPadDirection direction;
  GstPadPresence  presence;
  GstCaps	  *caps;
  gboolean	  fixed;
};

struct _GstPadTemplateClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*pad_created)	(GstPadTemplate *templ, GstPad *pad);
};

#ifdef G_HAVE_ISO_VARARGS
#define GST_PAD_TEMPLATE_NEW(padname, dir, pres, ...) \
  gst_pad_template_new (                        \
    padname,                                    \
    dir,                                        \
    pres,                                       \
    __VA_ARGS__ ,				\
    NULL)

#define GST_PAD_TEMPLATE_FACTORY(name, padname, dir, pres, ...) \
static GstPadTemplate*                          \
name (void)                                     \
{                                               \
  static GstPadTemplate *templ = NULL;       	\
  if (!templ) {                              	\
    templ = GST_PAD_TEMPLATE_NEW (            	\
      padname,                          	\
      dir,                                      \
      pres,                                     \
      __VA_ARGS__ );                            \
  }                                             \
  return templ;                              	\
}
#elif defined(G_HAVE_GNUC_VARARGS)
/* CR1: the space after 'a' is necessary because of preprocessing in gcc */
#define GST_PAD_TEMPLATE_NEW(padname, dir, pres, a...) \
  gst_pad_template_new (                        \
    padname,                                    \
    dir,                                        \
    pres,                                       \
    a ,						\
    NULL)

#define GST_PAD_TEMPLATE_FACTORY(name, padname, dir, pres, a...)         \
static GstPadTemplate*                          \
name (void)                                     \
{                                               \
  static GstPadTemplate *templ = NULL;       	\
  if (!templ) {                              	\
    templ = GST_PAD_TEMPLATE_NEW (            	\
      padname,                          	\
      dir,                                      \
      pres,                                     \
      a );                                      \
  }                                             \
  return templ;                              	\
}
#endif

#define GST_PAD_TEMPLATE_GET(fact) (fact)()

GType			gst_pad_get_type			(void);
GType			gst_real_pad_get_type			(void);
GType			gst_ghost_pad_get_type			(void);

/* creating pads */
GstPad*			gst_pad_new				(const gchar *name, GstPadDirection direction);
#define			gst_pad_destroy(pad)			gst_object_destroy (GST_OBJECT (pad))
GstPad*			gst_pad_new_from_template		(GstPadTemplate *templ, const gchar *name);
GstPad*			gst_pad_custom_new			(GType type, const gchar *name, GstPadDirection direction);
GstPad*			gst_pad_custom_new_from_template	(GType type, GstPadTemplate *templ, const gchar *name);

void			gst_pad_set_name			(GstPad *pad, const gchar *name);
const gchar*		gst_pad_get_name			(GstPad *pad);

GstPadDirection		gst_pad_get_direction			(GstPad *pad);

void			gst_pad_set_active			(GstPad *pad, gboolean active);
gboolean		gst_pad_is_active			(GstPad *pad);

void			gst_pad_set_element_private		(GstPad *pad, gpointer priv);
gpointer		gst_pad_get_element_private		(GstPad *pad);

void			gst_pad_set_parent			(GstPad *pad, GstElement *parent);
GstElement*		gst_pad_get_parent			(GstPad *pad);
GstElement*		gst_pad_get_real_parent			(GstPad *pad);

GstScheduler*		gst_pad_get_scheduler			(GstPad *pad);

void			gst_pad_add_ghost_pad			(GstPad *pad, GstPad *ghostpad);
void			gst_pad_remove_ghost_pad		(GstPad *pad, GstPad *ghostpad);
GList*			gst_pad_get_ghost_pad_list		(GstPad *pad);

GstPadTemplate*		gst_pad_get_pad_template		(GstPad *pad);

void			gst_pad_set_bufferpool_function		(GstPad *pad, GstPadBufferPoolFunction bufpool);
GstBufferPool*		gst_pad_get_bufferpool			(GstPad *pad);

/* data passing setup functions */
void			gst_pad_set_chain_function		(GstPad *pad, GstPadChainFunction chain);
void			gst_pad_set_get_function		(GstPad *pad, GstPadGetFunction get);
void			gst_pad_set_event_function		(GstPad *pad, GstPadEventFunction event);
void			gst_pad_set_event_mask_function		(GstPad *pad, GstPadEventMaskFunction mask_func);
const GstEventMask*	gst_pad_get_event_masks			(GstPad *pad);
const GstEventMask*	gst_pad_get_event_masks_default		(GstPad *pad);

/* pad links */
void			gst_pad_set_link_function		(GstPad *pad, GstPadLinkFunction link);
gboolean                gst_pad_can_link            		(GstPad *srcpad, GstPad *sinkpad);
gboolean                gst_pad_can_link_filtered   		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);

gboolean                gst_pad_link             		(GstPad *srcpad, GstPad *sinkpad);
gboolean                gst_pad_link_filtered       		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
void			gst_pad_unlink			(GstPad *srcpad, GstPad *sinkpad);

GstPad*			gst_pad_get_peer			(GstPad *pad);

/* capsnego functions */
GstCaps*		gst_pad_get_caps			(GstPad *pad);
GstCaps*		gst_pad_get_pad_template_caps		(GstPad *pad);
GstPadLinkReturn	gst_pad_try_set_caps			(GstPad *pad, GstCaps *caps);
gboolean		gst_pad_check_compatibility		(GstPad *srcpad, GstPad *sinkpad);

void			gst_pad_set_getcaps_function		(GstPad *pad, GstPadGetCapsFunction getcaps);
GstPadLinkReturn	gst_pad_proxy_link          		(GstPad *pad, GstCaps *caps);
gboolean		gst_pad_relink_filtered		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
gboolean		gst_pad_perform_negotiate		(GstPad *srcpad, GstPad *sinkpad);
gboolean		gst_pad_try_relink_filtered		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
GstCaps*	     	gst_pad_get_allowed_caps       		(GstPad *pad);
gboolean	     	gst_pad_recalc_allowed_caps    		(GstPad *pad);

/* data passing functions */
void			gst_pad_push				(GstPad *pad, GstBuffer *buf);
GstBuffer*		gst_pad_pull				(GstPad *pad);
gboolean		gst_pad_send_event			(GstPad *pad, GstEvent *event);
gboolean		gst_pad_event_default			(GstPad *pad, GstEvent *event);
GstPad*			gst_pad_select				(GList *padlist);
GstPad*			gst_pad_selectv				(GstPad *pad, ...);


/* convert/query/format functions */
void			gst_pad_set_formats_function		(GstPad *pad, 
								 GstPadFormatsFunction formats);
const GstFormat*	gst_pad_get_formats			(GstPad *pad);
const GstFormat*	gst_pad_get_formats_default		(GstPad *pad);

void			gst_pad_set_convert_function		(GstPad *pad, GstPadConvertFunction convert);
gboolean		gst_pad_convert				(GstPad *pad, 
		 						 GstFormat src_format,  gint64  src_value,
								 GstFormat *dest_format, gint64 *dest_value);
gboolean		gst_pad_convert_default 		(GstPad *pad,
		                        			 GstFormat src_format,  gint64  src_value,
					                         GstFormat *dest_format, gint64 *dest_value);

void			gst_pad_set_query_function		(GstPad *pad, GstPadQueryFunction query);
void			gst_pad_set_query_type_function		(GstPad *pad, GstPadQueryTypeFunction type_func);
const GstQueryType*	gst_pad_get_query_types			(GstPad *pad);
const GstQueryType*	gst_pad_get_query_types_default		(GstPad *pad);
gboolean		gst_pad_query				(GstPad *pad, GstQueryType type,
								 GstFormat *format, gint64 *value);
gboolean 		gst_pad_query_default 			(GstPad *pad, GstQueryType type,
		                      				 GstFormat *format, gint64 *value);

void			gst_pad_set_internal_link_function(GstPad *pad, GstPadIntLinkFunction intlink);
GList*			gst_pad_get_internal_links	(GstPad *pad);
GList*	 		gst_pad_get_internal_links_default (GstPad *pad);
	
/* misc helper functions */
gboolean 		gst_pad_dispatcher 			(GstPad *pad, GstPadDispatcherFunction dispatch, 
								 gpointer data);

#define			gst_pad_add_probe(pad, probe) \
			(gst_probe_dispatcher_add_probe (&(GST_REAL_PAD (pad)-probedisp), probe))
#define			gst_pad_remove_probe(pad, probe) \
			(gst_probe_dispatcher_remove_probe (&(GST_REAL_PAD (pad)-probedisp), probe))

#ifndef GST_DISABLE_LOADSAVE
void			gst_pad_load_and_link		(xmlNodePtr self, GstObject *parent);
#endif


/* ghostpads */
GstPad*			gst_ghost_pad_new			(const gchar *name, GstPad *pad);


/* templates and factories */
GType			gst_pad_template_get_type		(void);

GstPadTemplate*		gst_pad_template_new			(const gchar *name_template,
		                                        	 GstPadDirection direction, GstPadPresence presence,
								 GstCaps *caps, ...);

GstCaps*		gst_pad_template_get_caps		(GstPadTemplate *templ);
GstCaps*		gst_pad_template_get_caps_by_name	(GstPadTemplate *templ, const gchar *name);

xmlNodePtr              gst_ghost_pad_save_thyself   		(GstPad *pad,
						     		 xmlNodePtr parent);

G_END_DECLS


#endif /* __GST_PAD_H__ */

