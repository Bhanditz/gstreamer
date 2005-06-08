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
#include <gst/gstelementfactory.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstindex.h>
#include <gst/gstiterator.h>
#include <gst/gstmessage.h>
#include <gst/gsttag.h>

G_BEGIN_DECLS

GST_EXPORT GType _gst_element_type;

#define GST_TYPE_ELEMENT		(_gst_element_type)
#define GST_IS_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ELEMENT))
#define GST_ELEMENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ELEMENT, GstElementClass))
#define GST_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ELEMENT, GstElement))
#define GST_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ELEMENT, GstElementClass))
#define GST_ELEMENT_CAST(obj)		((GstElement*)(obj))

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

/* convenience functions */
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

typedef enum
{
  /* private flags that can be used by the scheduler */
  GST_ELEMENT_SCHEDULER_PRIVATE1,
  GST_ELEMENT_SCHEDULER_PRIVATE2,

  /* ignore state changes from parent */
  GST_ELEMENT_LOCKED_STATE,

  /* the element is a sink */
  GST_ELEMENT_IS_SINK,

  /* use some padding for future expansion */
  GST_ELEMENT_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 16
} GstElementFlags;

#define GST_ELEMENT_IS_LOCKED_STATE(obj)        (GST_FLAG_IS_SET(obj,GST_ELEMENT_LOCKED_STATE))

#define GST_ELEMENT_NAME(obj)			(GST_OBJECT_NAME(obj))
#define GST_ELEMENT_PARENT(obj)			(GST_ELEMENT_CAST(GST_OBJECT_PARENT(obj)))
#define GST_ELEMENT_MANAGER(obj)		(GST_ELEMENT_CAST(obj)->manager)
#define GST_ELEMENT_BUS(obj)			(GST_ELEMENT_CAST(obj)->bus)
#define GST_ELEMENT_SCHEDULER(obj)		(GST_ELEMENT_CAST(obj)->scheduler)
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
#define GST_ELEMENT_ERROR(el, domain, code, text, debug)		\
G_STMT_START {								\
  gchar *__txt = _gst_element_error_printf text;			\
  gchar *__dbg = _gst_element_error_printf debug;			\
  if (__txt)								\
    GST_ERROR_OBJECT (el, "%s", __txt);					\
  if (__dbg)								\
    GST_ERROR_OBJECT (el, "%s", __dbg);					\
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_ERROR,		\
    GST_ ## domain ## _ERROR, GST_ ## domain ## _ERROR_ ## code,	\
    __txt, __dbg, __FILE__, GST_FUNCTION, __LINE__);			\
} G_STMT_END

/* log a (non-fatal) warning message and post it on the bus */
#define GST_ELEMENT_WARNING(el, domain, code, text, debug)		\
G_STMT_START {								\
  gchar *__txt = _gst_element_error_printf text;			\
  gchar *__dbg = _gst_element_error_printf debug;			\
  if (__txt)								\
    GST_WARNING_OBJECT (el, "%s", __txt);				\
  if (__dbg)								\
    GST_WARNING_OBJECT (el, "%s", __dbg);				\
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_WARNING,	\
    GST_ ## domain ## _ERROR, GST_ ## domain ## _ERROR_ ## code,	\
  __txt, __dbg, __FILE__, GST_FUNCTION, __LINE__);			\
} G_STMT_END

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
  /* element manager */
  GstPipeline  	       *manager;
  GstBus       	       *bus;
  GstScheduler 	       *scheduler;

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

  /*< protected >*/
  /* vtable */

  /* request/release pads */
  GstPad*		(*request_new_pad)	(GstElement *element, GstPadTemplate *templ, const gchar* name);
  void			(*release_pad)		(GstElement *element, GstPad *pad);

  /* state changes */
  GstElementStateReturn (*get_state) 		(GstElement * element, GstElementState * state,
      						 GstElementState * pending, GTimeVal * timeout);
  GstElementStateReturn (*change_state)		(GstElement *element);

  /* manager */
  void 			(*set_manager)      	(GstElement * element, GstPipeline * pipeline);
  void 			(*set_bus)          	(GstElement * element, GstBus * bus);
  void			(*set_scheduler)	(GstElement *element, GstScheduler *scheduler);

  /* set/get clocks */
  GstClock*		(*get_clock)		(GstElement *element);
  void			(*set_clock)		(GstElement *element, GstClock *clock);

  /* index */
  GstIndex*		(*get_index)		(GstElement *element);
  void			(*set_index)		(GstElement *element, GstIndex *index);

  /* query functions */
  gboolean		(*send_event)		(GstElement *element, GstEvent *event);

  const GstQueryType* 	(*get_query_types)    	(GstElement *element);
  gboolean		(*query)		(GstElement *element, GstQuery *query);

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

/* indexes */
gboolean		gst_element_is_indexable	(GstElement *element);
void			gst_element_set_index		(GstElement *element, GstIndex *index);
GstIndex*		gst_element_get_index		(GstElement *element);

/* manager and tasks */
void 			gst_element_set_manager 	(GstElement * element, GstPipeline * pipeline);
GstPipeline *		gst_element_get_manager 	(GstElement * element);
void 			gst_element_set_bus 		(GstElement * element, GstBus * bus);
GstBus *		gst_element_get_bus 		(GstElement * element);
void			gst_element_set_scheduler	(GstElement *element, GstScheduler *sched);
GstScheduler*		gst_element_get_scheduler	(GstElement *element);

/* pad management */
gboolean		gst_element_add_pad		(GstElement *element, GstPad *pad);
gboolean		gst_element_remove_pad		(GstElement *element, GstPad *pad);
void			gst_element_no_more_pads	(GstElement *element);

GstPad*			gst_element_get_pad		(GstElement *element, const gchar *name);
GstPad*			gst_element_get_static_pad	(GstElement *element, const gchar *name);
GstPad*			gst_element_get_request_pad	(GstElement *element, const gchar *name);
void			gst_element_release_request_pad	(GstElement *element, GstPad *pad);

GstIterator *		gst_element_iterate_pads 	(GstElement * element);

/* event/query/format stuff */
gboolean		gst_element_send_event		(GstElement *element, GstEvent *event);
gboolean		gst_element_seek		(GstElement *element, GstSeekType seek_type,
							 guint64 offset);
G_CONST_RETURN GstQueryType*
			gst_element_get_query_types	(GstElement *element);
gboolean		gst_element_query		(GstElement *element, GstQuery *query);

/* messages */
gboolean 		gst_element_post_message 	(GstElement * element, GstMessage * message);

/* error handling */
gchar *			_gst_element_error_printf	(const gchar *format, ...);
void 			gst_element_message_full 	(GstElement * element, GstMessageType type,
   							 GQuark domain, gint code, gchar * text, 
							 gchar * debug, const gchar * file,
   							 const gchar * function, gint line);

/* state management */
gboolean		gst_element_is_locked_state	(GstElement *element);
gboolean		gst_element_set_locked_state	(GstElement *element, gboolean locked_state);
gboolean		gst_element_sync_state_with_parent (GstElement *element);

GstElementStateReturn 	gst_element_get_state 		(GstElement * element, 
							 GstElementState * state,
    							 GstElementState * pending, 
							 GTimeVal * timeout);
GstElementStateReturn	gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_abort_state 	(GstElement * element);
void 			gst_element_commit_state 	(GstElement * element);
void 			gst_element_lost_state 	        (GstElement * element);

/* factory management */
GstElementFactory*	gst_element_get_factory		(GstElement *element);

G_END_DECLS

#endif /* __GST_ELEMENT_H__ */
