/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2004 Wim Taymans <wim@fluendo.com>
 *
 * gstelement.h: Header for GstElement
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


#ifndef __GST_ELEMENT_H__
#define __GST_ELEMENT_H__

#include <gst/gstconfig.h>
#include <gst/gsttypes.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstclock.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstindex.h>
#include <gst/gstiterator.h>
#include <gst/gsttag.h>

G_BEGIN_DECLS

typedef struct _GstElementDetails GstElementDetails;

/* FIXME: need translatable stuff in here (how handle in registry)? */
struct _GstElementDetails
{
  /*< public > */
  gchar *longname;              /* long, english name */
  gchar *klass;                 /* type of element, as hierarchy */
  gchar *description;           /* insights of one form or another */
  gchar *author;                /* who wrote this thing? */

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

#define GST_ELEMENT_DETAILS(longname,klass,description,author)		\
  { longname, klass, description, author, GST_PADDING_INIT }
#define GST_IS_ELEMENT_DETAILS(details) (					\
  (details) && ((details)->longname != NULL) && ((details)->klass != NULL)	\
  && ((details)->description != NULL) && ((details)->author != NULL))

#define GST_NUM_STATES 4

/* NOTE: this probably should be done with an #ifdef to decide 
 * whether to safe-cast or to just do the non-checking cast.
 */
#define GST_STATE(obj)			(GST_ELEMENT(obj)->current_state)
#define GST_STATE_PENDING(obj)		(GST_ELEMENT(obj)->pending_state)
#define GST_STATE_ERROR(obj)		(GST_ELEMENT(obj)->state_error)

/* Note: using 8 bit shift mostly "just because", it leaves us enough room to grow <g> */
#define GST_STATE_TRANSITION(obj)	((GST_STATE(obj)<<8) | GST_STATE_PENDING(obj))
#define GST_STATE_NULL_TO_READY		((GST_STATE_NULL<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_PAUSED	((GST_STATE_READY<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_PLAYING	((GST_STATE_PAUSED<<8) | GST_STATE_PLAYING)
#define GST_STATE_PLAYING_TO_PAUSED	((GST_STATE_PLAYING<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_READY	((GST_STATE_PAUSED<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_NULL		((GST_STATE_READY<<8) | GST_STATE_NULL)

GST_EXPORT GType _gst_element_type;

#define GST_TYPE_ELEMENT		(_gst_element_type)
#define GST_IS_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ELEMENT))
#define GST_ELEMENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ELEMENT, GstElementClass))
#define GST_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ELEMENT, GstElement))
#define GST_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ELEMENT, GstElementClass))
#define GST_ELEMENT_CAST(obj)		((GstElement*)(obj))

/* convenience functions */
#ifndef GST_DISABLE_DEPRECATED
#ifdef G_HAVE_ISO_VARARGS
#define GST_ELEMENT_QUERY_TYPE_FUNCTION(functionname, ...) \
	GST_QUERY_TYPE_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#define GST_ELEMENT_FORMATS_FUNCTION(functionname, ...)    \
	GST_FORMATS_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#define GST_ELEMENT_EVENT_MASK_FUNCTION(functionname, ...) \
	GST_EVENT_MASK_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_ELEMENT_QUERY_TYPE_FUNCTION(functionname, a...) \
	GST_QUERY_TYPE_FUNCTION (GstElement*, functionname, a);
#define GST_ELEMENT_FORMATS_FUNCTION(functionname, a...)    \
	GST_FORMATS_FUNCTION (GstElement*, functionname, a);
#define GST_ELEMENT_EVENT_MASK_FUNCTION(functionname, a...) \
	GST_EVENT_MASK_FUNCTION (GstElement*, functionname, a);
#endif
#endif

typedef enum
{
  /* input and output pads aren't directly coupled to each other
     examples: queues, multi-output async readers, etc. */
  GST_ELEMENT_DECOUPLED,
  /* this element, for some reason, has a loop function that performs
   * an infinite loop without calls to gst_element_yield () */
  GST_ELEMENT_INFINITE_LOOP,
  /* there is a new loopfunction ready for placement */
  GST_ELEMENT_NEW_LOOPFUNC,
  /* if this element can handle events */
  GST_ELEMENT_EVENT_AWARE,

  /* private flags that can be used by the scheduler */
  GST_ELEMENT_SCHEDULER_PRIVATE1,
  GST_ELEMENT_SCHEDULER_PRIVATE2,

  /* ignore state changes from parent */
  GST_ELEMENT_LOCKED_STATE,

  /* element is in error */
  GST_ELEMENT_IN_ERROR,

  /* use some padding for future expansion */
  GST_ELEMENT_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 16
} GstElementFlags;

#define GST_ELEMENT_IS_THREAD_SUGGESTED(obj)	(GST_FLAG_IS_SET(obj,GST_ELEMENT_THREAD_SUGGESTED))
#define GST_ELEMENT_IS_EVENT_AWARE(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_EVENT_AWARE))
#define GST_ELEMENT_IS_DECOUPLED(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_DECOUPLED))

#define GST_ELEMENT_NAME(obj)			(GST_OBJECT_NAME(obj))
#define GST_ELEMENT_PARENT(obj)			(GST_OBJECT_PARENT(obj))
#define GST_ELEMENT_SCHEDULER(obj)		(GST_ELEMENT_CAST(obj)->sched)
#define GST_ELEMENT_CLOCK(obj)			(GST_ELEMENT_CAST(obj)->clock)
#define GST_ELEMENT_PADS(obj)			(GST_ELEMENT_CAST(obj)->pads)

/**
 * GST_ELEMENT_ERROR:
 * @el: the element that throws the error
 * @domain: like CORE, LIBRARY, RESOURCE or STREAM (see #GstError)
 * @code: error code defined for that domain (see #GstError)
 * @message: the message to display (format string and args enclosed in round brackets)
 * @debug: debugging information for the message (format string and args enclosed in round brackets)
 *
 * Utility function that elements can use in case they encountered a fatal
 * data processing error. The pipeline will throw an error signal and the
 * application will be requested to stop further media processing.
 */
#define GST_ELEMENT_ERROR(el, domain, code, message, debug) 		\
G_STMT_START { 								\
  gchar *__msg = _gst_element_error_printf message; 			\
  gchar *__dbg = _gst_element_error_printf debug; 			\
  if (__msg) 								\
    GST_ERROR_OBJECT (el, "%s", __msg); 				\
  if (__dbg) 								\
    GST_ERROR_OBJECT (el, "%s", __dbg); 				\
  gst_element_error_full (GST_ELEMENT(el), 				\
    GST_ ## domain ## _ERROR, GST_ ## domain ## _ERROR_ ## code, 	\
    __msg, __dbg, __FILE__, GST_FUNCTION, __LINE__); 			\
} G_STMT_END

typedef struct _GstElementFactory GstElementFactory;
typedef struct _GstElementFactoryClass GstElementFactoryClass;

typedef void 		(*GstElementLoopFunction) 	(GstElement *element);

/* the state change mutexes and conds */
#define GST_STATE_GET_LOCK(elem)               (GST_ELEMENT_CAST(elem)->state_lock)
#define GST_STATE_LOCK(elem)                   g_mutex_lock(GST_STATE_GET_LOCK(elem))
#define GST_STATE_TRYLOCK(elem)                g_mutex_trylock(GST_STATE_GET_LOCK(elem))
#define GST_STATE_UNLOCK(elem)                 g_mutex_unlock(GST_STATE_GET_LOCK(elem))
#define GST_STATE_GET_COND(elem)               (GST_ELEMENT_CAST(elem)->state_cond)
#define GST_STATE_WAIT(elem)                   g_cond_wait (GST_STATE_GET_COND (elem), GST_STATE_GET_LOCK (elem))
#define GST_STATE_TIMED_WAIT(elem, timeval)    g_cond_timed_wait (GST_STATE_GET_COND (elem), GST_STATE_GET_LOCK (elem),\
		                                                timeval)
#define GST_STATE_SIGNAL(elem)                 g_cond_signal (GST_STATE_GET_COND (elem));
#define GST_STATE_BROADCAST(elem)              g_cond_broadcast (GST_STATE_GET_COND (elem));

struct _GstElement 
{
  GstObject 		object;

  /*< public >*/ /* with STATE_LOCK */
  /* element state */
  GMutex               *state_lock;
  GCond                *state_cond;
  guint8                current_state;
  guint8                pending_state;
  gboolean              state_error; /* flag is set when the element has an error in the last state
                                        change. it is cleared when doing another state change. */
  /*< public >*/ /* with LOCK */
  /* scheduling */
  GstElementLoopFunction loopfunc;
  GstScheduler 	       *sched;
  /* private pointer for the scheduler */
  gpointer		sched_private;

  /* allocated clock */
  GstClock	       *clock;
  GstClockTimeDiff    	base_time; /* NULL/READY: 0 - PAUSED: current time - PLAYING: difference to clock */

  /* element pads, these lists can only be iterated while holding
   * the LOCK or checking the cookie after each LOCK. */
  guint16               numpads;
  GList                *pads;
  guint16               numsrcpads;
  GList                *srcpads;
  guint16               numsinkpads;
  GList                *sinkpads;
  guint32               pads_cookie;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstElementClass
{
  GstObjectClass         parent_class;

  /*< public >*/
  /* the element details */
  GstElementDetails 	 details;

  /* factory that the element was created from */
  GstElementFactory	*elementfactory;

  /* templates for our pads */
  GList                 *padtemplates;
  gint                   numpadtemplates;
  guint32                pad_templ_cookie;

  /* signal callbacks */
  void (*state_change)	(GstElement *element, GstElementState old, GstElementState state);
  void (*new_pad)	(GstElement *element, GstPad *pad);
  void (*pad_removed)	(GstElement *element, GstPad *pad);
  void (*no_more_pads)	(GstElement *element);
  void (*error)		(GstElement *element, GstElement *source, GError *error, gchar *debug);
  void (*eos)		(GstElement *element);
  void (*found_tag)	(GstElement *element, GstElement *source, const GstTagList *tag_list);

  /*< protected >*/
  /* vtable*/

  /* request/release pads */
  GstPad*		(*request_new_pad)	(GstElement *element, GstPadTemplate *templ, const gchar* name);
  void			(*release_pad)		(GstElement *element, GstPad *pad);

  /* state changes */
  GstElementStateReturn (*change_state)		(GstElement *element);
  GstElementStateReturn	(*set_state)		(GstElement *element, GstElementState state);

  /* scheduling */
  gboolean		(*release_locks)	(GstElement *element);
  void			(*set_scheduler)	(GstElement *element, GstScheduler *scheduler);

  /* set/get clocks */
  GstClock*		(*get_clock)		(GstElement *element);
  void			(*set_clock)		(GstElement *element, GstClock *clock);

  /* index */
  GstIndex*		(*get_index)		(GstElement *element);
  void			(*set_index)		(GstElement *element, GstIndex *index);

  /* query/convert/events functions */
  const GstEventMask*   (*get_event_masks)     	(GstElement *element);
  gboolean		(*send_event)		(GstElement *element, GstEvent *event);
  const GstFormat*      (*get_formats)        	(GstElement *element);
  gboolean              (*convert)        	(GstElement *element,
		                                 GstFormat  src_format,  gint64  src_value,
						 GstFormat *dest_format, gint64 *dest_value);
  const GstQueryType* 	(*get_query_types)    	(GstElement *element);
  gboolean		(*query)		(GstElement *element, GstQueryType type,
		  				 GstFormat *format, gint64 *value);
  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* element class pad templates */
void			gst_element_class_add_pad_template	(GstElementClass *klass, GstPadTemplate *templ);
GstPadTemplate*		gst_element_class_get_pad_template	(GstElementClass *element_class, const gchar *name);
GList*                  gst_element_class_get_pad_template_list (GstElementClass *element_class);
void			gst_element_class_set_details		(GstElementClass *klass,
								 const GstElementDetails *details);

/* element instance */
GType			gst_element_get_type		(void);

/* basic name and parentage stuff from GstObject */
#define			gst_element_get_name(elem)	gst_object_get_name(GST_OBJECT(elem))
#define			gst_element_set_name(elem,name)	gst_object_set_name(GST_OBJECT(elem),name)
#define			gst_element_get_parent(elem)	gst_object_get_parent(GST_OBJECT(elem))
#define			gst_element_set_parent(elem,parent)	gst_object_set_parent(GST_OBJECT(elem),parent)

/* clocking */
gboolean		gst_element_requires_clock	(GstElement *element);
gboolean		gst_element_provides_clock	(GstElement *element);
GstClock*		gst_element_get_clock 		(GstElement *element);
void			gst_element_set_clock 		(GstElement *element, GstClock *clock);
GstClockReturn		gst_element_clock_wait 		(GstElement *element, 
							 GstClockID id, GstClockTimeDiff *jitter);
GstClockTime		gst_element_get_time		(GstElement *element);
gboolean		gst_element_wait		(GstElement *element, GstClockTime timestamp);
void			gst_element_set_time		(GstElement *element, GstClockTime time);
void			gst_element_set_time_delay	(GstElement *element, GstClockTime time, GstClockTime delay);

void			gst_element_adjust_time		(GstElement *element, GstClockTimeDiff diff);

/* indexes */
gboolean		gst_element_is_indexable	(GstElement *element);
void			gst_element_set_index		(GstElement *element, GstIndex *index);
GstIndex*		gst_element_get_index		(GstElement *element);


/* scheduling */
void			gst_element_set_loop_function	(GstElement *element,
							 GstElementLoopFunction loop);
gboolean		gst_element_release_locks	(GstElement *element);
void			gst_element_yield		(GstElement *element);
gboolean		gst_element_interrupt		(GstElement *element);
void			gst_element_set_scheduler	(GstElement *element, GstScheduler *sched);
GstScheduler*		gst_element_get_scheduler	(GstElement *element);
GstBin*			gst_element_get_managing_bin	(GstElement *element);

/* pad management */
gboolean		gst_element_add_pad		(GstElement *element, GstPad *pad);
gboolean		gst_element_remove_pad		(GstElement *element, GstPad *pad);
GstPad *		gst_element_add_ghost_pad	(GstElement *element, GstPad *pad, const gchar *name);
void			gst_element_no_more_pads	(GstElement *element);

GstPad*			gst_element_get_pad		(GstElement *element, const gchar *name);
GstPad*			gst_element_get_static_pad	(GstElement *element, const gchar *name);
GstPad*			gst_element_get_request_pad	(GstElement *element, const gchar *name);
void			gst_element_release_request_pad	(GstElement *element, GstPad *pad);

GstIterator *		gst_element_iterate_pads 	(GstElement * element);

/* event/query/format stuff */
G_CONST_RETURN GstEventMask*
			gst_element_get_event_masks	(GstElement *element);
gboolean		gst_element_send_event		(GstElement *element, GstEvent *event);
gboolean		gst_element_seek		(GstElement *element, GstSeekType seek_type,
							 guint64 offset);
G_CONST_RETURN GstQueryType*
			gst_element_get_query_types	(GstElement *element);
gboolean		gst_element_query		(GstElement *element, GstQueryType type,
			                                 GstFormat *format, gint64 *value);
G_CONST_RETURN GstFormat*
			gst_element_get_formats		(GstElement *element);
gboolean		gst_element_convert		(GstElement *element, 
		 					 GstFormat  src_format,  gint64  src_value,
							 GstFormat *dest_format, gint64 *dest_value);

/* error handling */
gchar *			_gst_element_error_printf	(const gchar *format, ...);
void			gst_element_error_full		(GstElement *element, GQuark domain, gint code, 
							 gchar *message, gchar *debug, 
							 const gchar *file, const gchar *function, gint line);
void 			gst_element_default_error	(GObject *object, GstObject *orig, GError *error, gchar *debug);
#define 		gst_element_default_deep_notify gst_object_default_deep_notify

/* state management */
void			gst_element_set_eos		(GstElement *element);

gboolean		gst_element_is_locked_state	(GstElement *element);
gboolean		gst_element_set_locked_state	(GstElement *element, gboolean locked_state);
gboolean		gst_element_sync_state_with_parent (GstElement *element);

GstElementState         gst_element_get_state           (GstElement *element);
GstElementStateReturn	gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_wait_state_change 	(GstElement *element);

/* factory management */
GstElementFactory*	gst_element_get_factory		(GstElement *element);


/* misc */
void			gst_element_found_tags		(GstElement *element, const GstTagList *tag_list);
void			gst_element_found_tags_for_pad	(GstElement *element, GstPad *pad, GstClockTime timestamp, 
							 GstTagList *list);
/*
 *
 * factories stuff
 *
 **/

#define GST_TYPE_ELEMENT_FACTORY 		(gst_element_factory_get_type())
#define GST_ELEMENT_FACTORY(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactory))
#define GST_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactoryClass))
#define GST_IS_ELEMENT_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ELEMENT_FACTORY))
#define GST_IS_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENT_FACTORY))

struct _GstElementFactory {
  GstPluginFeature	parent;

  GType			type;			/* unique GType of element or 0 if not loaded */

  GstElementDetails	details;

  GList *		padtemplates;
  guint			numpadtemplates;

  /* URI interface stuff */
  guint			uri_type;
  gchar **		uri_protocols;
  
  GList *		interfaces;		/* interfaces this element implements */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstElementFactoryClass {
  GstPluginFeatureClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

GType 			gst_element_factory_get_type 		(void);

gboolean		gst_element_register			(GstPlugin *plugin,
								 const gchar *name,
								 guint rank,
								 GType type);

GstElementFactory *	gst_element_factory_find		(const gchar *name);
GType			gst_element_factory_get_element_type	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_longname	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_klass		(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_description  	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_author		(GstElementFactory *factory);
guint			gst_element_factory_get_num_pad_templates (GstElementFactory *factory);
G_CONST_RETURN GList *	gst_element_factory_get_pad_templates	(GstElementFactory *factory);
guint			gst_element_factory_get_uri_type	(GstElementFactory *factory);		
gchar **		gst_element_factory_get_uri_protocols	(GstElementFactory *factory);		

GstElement*		gst_element_factory_create		(GstElementFactory *factory,
								 const gchar *name);
GstElement*		gst_element_factory_make		(const gchar *factoryname, const gchar *name);

gboolean		gst_element_factory_can_src_caps	(GstElementFactory *factory,
								 const GstCaps *caps);
gboolean		gst_element_factory_can_sink_caps	(GstElementFactory *factory,
								 const GstCaps *caps);

void			__gst_element_factory_add_pad_template	(GstElementFactory *elementfactory,
								 GstPadTemplate *templ);
void			__gst_element_factory_add_interface	(GstElementFactory *elementfactory,
								 const gchar *interfacename);

G_END_DECLS

#endif /* __GST_ELEMENT_H__ */

