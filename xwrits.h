#ifndef XWRITS_H
#define XWRITS_H

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#ifndef FD_SET
#include <sys/select.h>
#endif
#include "gif.h"

typedef struct Port Port;
typedef struct Options Options;
typedef struct Hand Hand;
typedef struct Picture Picture;
typedef struct Alarm Alarm;

#ifdef __cplusplus
#define EXTERNFUNCTION		extern "C"
#else
#define EXTERNFUNCTION		extern
#endif

#define xwNEW(typ)		(typ *)xmalloc(sizeof(typ))
#define xwNEWARR(typ,num)	(typ *)xmalloc(sizeof(typ) * (num))
#define xwREARRAY(var,typ,num)	var = (typ *)xrealloc(var, sizeof(typ) * (num))


/*****************************************************************************/
/*  Global X stuff							     */

struct Port {
  
  Display *display;
  int x_socket;
  
  int screen_number;
  Window root_window;
  int width;
  int height;

  Drawable drawable;
  Visual *visual;
  int depth;
  Colormap colormap;
  unsigned long black;
  unsigned long white;
  
  XFontStruct *font;
  
};

extern Display *display;
extern Port port;

int default_x_processing(XEvent *);
int x_error_handler(Display *, XErrorEvent *);


/*****************************************************************************/
/*  Options								     */

struct Options {
  
  struct timeval multiply_delay;
  struct timeval lock_bounce_delay;
  double flash_rate_ratio;
  
  unsigned never_iconify: 1;
  unsigned top: 1;
  unsigned clock: 1;
  unsigned break_clock: 1;
  unsigned multiply: 1;
  unsigned beep: 1;
  unsigned appear_iconified: 1;
  unsigned lock: 1;
  int max_hands;
  
  Gif_Stream *slideshow;
  char *slideshow_text;
  Gif_Stream *icon_slideshow;
  char *icon_slideshow_text;
  
  struct timeval next_delay;
  Options *next;
  Options *prev;
  
};

extern Options *ocurrent;

extern struct timeval type_delay;
extern struct timeval break_delay;

#define MaxPasswordSize 256
extern struct timeval lock_message_delay;
extern char *lock_password;


/*****************************************************************************/
/*  Clocks								     */

extern struct timeval clock_zero_time;
extern struct timeval clock_tick;

void init_clock(Drawable);
void draw_clock(Hand *, struct timeval *);
void draw_all_clocks(struct timeval *);
void erase_clock(Hand *);
void erase_all_clocks(void);


/*****************************************************************************/
/*  Alarms								     */

#define A_FLASH			0x0001
#define A_AWAKE			0x0002
#define A_CLOCK			0x0004
#define A_MULTIPLY		0x0008
#define A_NEXT_OPTIONS		0x0010
#define A_LOCK_BOUNCE		0x0020
#define A_LOCK_CLOCK		0x0040
#define A_LOCK_MESS_ERASE	0x0080
#define A_IDLE_SELECT		0x0100
#define A_IDLE_CHECK		0x0200
#define A_MOUSE			0x0400

struct Alarm {
  
  Alarm *next;
  struct timeval timer;
  int action;
  void *data;
  
  unsigned scheduled: 1;
  
};

typedef int (*Alarmloopfunc)(Alarm *, struct timeval *);
typedef int (*Xloopfunc)(XEvent *, struct timeval *);

extern Alarm *alarms;

#define new_alarm(i)	new_alarm_data((i), 0)
Alarm *new_alarm_data(int, void *);
#define grab_alarm(i)	grab_alarm_data((i), 0)
Alarm *grab_alarm_data(int, void *);
void destroy_alarm(Alarm *);

void schedule(Alarm *);
#define unschedule(i)	unschedule_data((i), 0)
void unschedule_data(int, void *);

int loopmaster(Alarmloopfunc, Xloopfunc);


/*****************************************************************************/
/*  Hands								     */

#define MaxHands	137

struct Hand {
  
  Hand *next;
  Hand *prev;
  Hand *icon;
  
  Window w;
  int x;
  int y;
  int width;
  int height;
  
  Window root_child;
  
  Gif_Stream *slideshow;
  int slide;
  int loopcount;
  
  unsigned is_icon: 1;
  unsigned mapped: 1;
  unsigned configured: 1;
  unsigned obscured: 1;
  unsigned clock: 1;
  
};


extern Hand *hands;
extern Hand *icon_hands;
int active_hands(void);

#define NHCenter	0x8000
#define NHRandom	0x7FFF
Hand *new_hand(int x, int y);
void destroy_hand(Hand *);

Hand *window_to_hand(Window, int allow_icon);

void draw_slide(Hand *);


/*****************************************************************************/
/*  Slideshow								     */

extern Gif_Stream *current_slideshow;
extern Gif_Stream *resting_slideshow, *resting_icon_slideshow;
extern Gif_Stream *ready_slideshow, *ready_icon_slideshow;
#define DEFAULT_FLASH_DELAY_SEC 2

Gif_Image *get_built_in_image(const char *, int mono);
Gif_Stream *parse_slideshow(const char *, double, int mono);
void set_slideshow(Hand *, Gif_Stream *, struct timeval *);
void set_all_slideshows(Hand *, Gif_Stream *);


/*****************************************************************************/
/*  Pictures								     */

struct Picture {
  Pixmap pix;
  int clock_x_off;
  int clock_y_off;
};

extern Pixmap bars_pixmap;
extern Pixmap lock_pixmap;

void default_pictures(void);
void load_needed_pictures(Window, int, int force_mono);


/*****************************************************************************/
/*  Idle processing							     */

extern struct timeval register_keystrokes_delay;
extern struct timeval register_keystrokes_gap;

extern int check_idle;
extern struct timeval idle_time;
extern struct timeval last_key_time;

extern int check_mouse;
extern struct timeval check_mouse_time;
extern int last_mouse_x, last_mouse_y;
extern Window last_mouse_root;

void watch_keystrokes(Window, struct timeval *);
void register_keystrokes(Window);


/*****************************************************************************/
/*  The high-level procedures						     */

void error(const char *, ...);

void wait_for_break(void);

#define TRAN_WARN	1
#define TRAN_CANCEL	2
#define TRAN_FAIL	3
#define TRAN_REST	4
#define TRAN_LOCK	5
#define TRAN_AWAKE	6

extern struct timeval first_warning_time;

int warning(int was_lock);
int rest(void);
int lock(void);

void ready(void);
void unmap_all(void);


/*****************************************************************************/
/*  Time functions							     */

#define MICRO_PER_SEC 1000000
#define SEC_PER_MIN 60
#define MIN_PER_HOUR 60
#define HOUR_PER_CYCLE 12

#define xwADDTIME(result, a, b) do { \
	(result).tv_sec = (a).tv_sec + (b).tv_sec; \
	if (((result).tv_usec = (a).tv_usec+(b).tv_usec) >= MICRO_PER_SEC) { \
		(result).tv_sec++; \
		(result).tv_usec -= MICRO_PER_SEC; \
	} } while (0)

#define xwSUBTIME(result, a, b) do { \
	(result).tv_sec = (a).tv_sec - (b).tv_sec; \
	if (((result).tv_usec = (a).tv_usec - (b).tv_usec) < 0) { \
		(result).tv_sec--; \
		(result).tv_usec += MICRO_PER_SEC; \
	} } while (0)

#define xwSETMINTIME(a, b) do { \
	if ((b).tv_sec < (a).tv_sec || \
	    ((b).tv_sec == (a).tv_sec && (b).tv_usec < (a).tv_usec)) \
		(a) = (b); \
	} while (0)

#define xwTIMEGEQ(a, b) ((a).tv_sec > (b).tv_sec || \
	((a).tv_sec == (b).tv_sec && (a).tv_usec >= (b).tv_usec))

#define xwTIMEGT(a, b) ((a).tv_sec > (b).tv_sec || \
	((a).tv_sec == (b).tv_sec && (a).tv_usec > (b).tv_usec))

#define xwTIMELEQ0(a) ((a).tv_sec < 0 || ((a).tv_sec == 0 && (a).tv_usec <= 0))


#ifdef X_GETTIMEOFDAY
# define xwGETTIMEOFDAY(a) X_GETTIMEOFDAY(a)
#elif GETTIMEOFDAY_PROTO == 0
EXTERNFUNCTION int gettimeofday(struct timeval *, struct timezone *);
# define xwGETTIMEOFDAY(a) gettimeofday((a), 0)
#elif GETTIMEOFDAY_PROTO == 1
# define xwGETTIMEOFDAY(a) gettimeofday((a))
#else
# define xwGETTIMEOFDAY(a) gettimeofday((a), 0)
#endif

#define xwGETTIME(a) do { xwGETTIMEOFDAY(&(a)); xwSUBTIME((a), (a), genesis_time); } while (0)
extern struct timeval genesis_time;

#define xwADDDELAY(result, a, d) do { \
	(result).tv_sec = (a).tv_sec + ((d)/100); \
	if (((result).tv_usec = (a).tv_usec + ((d)%100)*10000) >= MICRO_PER_SEC) { \
		(result).tv_sec++; \
		(result).tv_usec -= MICRO_PER_SEC; \
	} } while (0)

#define xwSUBDELAY(result, a, d) do { \
	(result).tv_sec = (a).tv_sec - ((d)/100); \
	if (((result).tv_usec = (a).tv_usec - ((d)%100)*10000) < 0) { \
		(result).tv_sec--; \
		(result).tv_usec += MICRO_PER_SEC; \
	} } while (0)

#endif
