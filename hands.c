/* -*- c-basic-offset: 2 -*- */
#include <config.h>
#include "xwrits.h"
#include "logopic.c"
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <X11/Xatom.h>
#ifdef HAVE_GETHOSTNAME
# include <unistd.h>
#elif HAVE_UNAME
# include <sys/utsname.h>
#endif

#define NEW_HAND_TRIES 6

/* creating a new hand */

static void
get_icon_size(Port *port)
{
  XIconSize *ic;
  int nic;
  int w = ocurrent->icon_slideshow->screen_width;
  int h = ocurrent->icon_slideshow->screen_height;
  if (XGetIconSizes(port->display, port->root_window, &ic, &nic) != 0) {
    if (nic != 0) {
      if (w < ic->min_width) w = ic->min_width;
      if (h < ic->min_height) h = ic->min_height;
      if (w > ic->max_width) w = ic->max_width;
      if (h > ic->max_height) h = ic->max_height;
    }
    XFree(ic);
  }
  port->icon_width = w;
  port->icon_height = h;
}

#define xwMAX(i, j) ((i) > (j) ? (i) : (j))
#define xwMIN(i, j) ((i) < (j) ? (i) : (j))

/* get_best_position: gets the best (x, y) pair from the list of pairs stored
     in xlist and ylist (num pairs overall). Best means 'covering smallest
     number of existing hands.' Returns it in *retx and *rety */

static void
get_best_position(Port *port, int *xlist, int *ylist, int num,
		  int width, int height, int *retx, int *rety)
{
  unsigned int best_penalty = 0x8000U;
  unsigned int penalty;
  int i, overw, overh, best = 0;
  Hand *h;
  for (i = 0; i < num; i++) {
    int x1 = xlist[i], y1 = ylist[i];
    int x2 = x1 + width, y2 = y1 + height;
    penalty = 0;
    for (h = port->hands; h; h = h->next)
      if (h->mapped) {
	overw = xwMIN(x2, h->x + h->width) - xwMAX(x1, h->x);
	overh = xwMIN(y2, h->y + h->height) - xwMAX(y1, h->y);
	if (overw > 0 && overh > 0) penalty += overw * overh;
      }
    if (penalty < best_penalty) {
      best_penalty = penalty;
      best = i;
    }
  }
  *retx = xlist[best];
  *rety = ylist[best];
}

static char *
net_get_hostname(char *buf, size_t maxlen)
{
#ifdef HAVE_GETHOSTNAME
  gethostname(buf, maxlen);
  buf[maxlen - 1] = '\0';
  return buf;
#elif defined(HAVE_UNAME)
  struct utsname name;
  size_t len;

  uname(&name);
  len = strlen(name.nodename);
  if (len >= maxlen)
    len = maxlen - 1;
  strncpy(buf, name.nodename, len);
  buf[len] = '\0';

  return buf;
#else
  return 0;
#endif
}

/* June 2006 -- Jeff Layton, and an examination of Xm/MwmUtil.h, points out
   that these properties are stored as long on the client. */
static struct {
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long inputMode;
  unsigned long status;
} mwm_hints;

Hand *
new_hand(Port *slave_port, int x, int y)
{
  static XClassHint classh;
  static XSizeHints *xsh;
  static XWMHints *xwmh;
  static XTextProperty window_name, icon_name, hostname;
  static char hostname_buf[256];
  Hand *nh = xwNEW(Hand);
  Hand *nh_icon = xwNEW(Hand);
  int width = ocurrent->slideshow->screen_width;
  int height = ocurrent->slideshow->screen_height;
  unsigned long property[2];
  Port *port;

  /* check for random port, patch by Peter Maydell <maydell@tao-group.com> */
  if (slave_port == NEW_HAND_RANDOM_PORT)
      slave_port = ports[(rand() >> 4) % nports];
  port = slave_port->master;

  /* is this the permanent hand? */
  if (!port->permanent_hand) {
    nh->permanent = 1;
    port->permanent_hand = nh;
  } else
    nh->permanent = 0;

  /* set position and size */
  if (x == NEW_HAND_CENTER)
    x = slave_port->left + (slave_port->width - width) / 2;
  if (y == NEW_HAND_CENTER)
    y = slave_port->top + (slave_port->height - height) / 2;

  if (x == NEW_HAND_RANDOM || y == NEW_HAND_RANDOM) {
    int xs[NEW_HAND_TRIES], ys[NEW_HAND_TRIES], i;
    int xdist = slave_port->width - width;
    int ydist = slave_port->height - height;
    int xrand = (x == NEW_HAND_RANDOM);
    int yrand = (y == NEW_HAND_RANDOM);
    for (i = 0; i < NEW_HAND_TRIES; i++) {
	xs[i] = (xrand ? slave_port->left + ((rand() >> 4) % xdist) : x);
	ys[i] = (yrand ? slave_port->top + ((rand() >> 4) % ydist) : y);
    }
    get_best_position(port, xs, ys, NEW_HAND_TRIES, width, height, &x, &y);
  }

  if (!port->icon_width)
    get_icon_size(port);

  if (!xwmh) {
    const char *stringlist[2];
    stringlist[0] = ocurrent->window_title;
    stringlist[1] = NULL;
    XStringListToTextProperty((char **)stringlist, 1, &window_name);
    XStringListToTextProperty((char **)stringlist, 1, &icon_name);
    classh.res_name = "xwrits";
    classh.res_class = "XWrits";

    xsh = XAllocSizeHints();
    xsh->flags = USPosition | PMinSize | PMaxSize;
    xsh->min_width = xsh->max_width = width;
    xsh->min_height = xsh->max_height = height;

    xwmh = XAllocWMHints();
    xwmh->flags = InputHint | StateHint | IconWindowHint;
    xwmh->input = True;

    /* Silly hackery to get the MWM appearance *just right*: ie., no resize
       handles or maximize button, no Resize or Maximize entries in window
       menu. The constitution of the property itself was inferred from data
       in <Xm/MwmUtil.h> and output of xprop. */
    mwm_hints.flags = (1L << 0) | (1L << 1);
    /* flags = MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS */
    if( !ocurrent->never_close )
        mwm_hints.functions |= (1L << 5);
    if( !ocurrent->never_move )
        mwm_hints.functions |= (1L << 2);
    mwm_hints.decorations = (1L << 1) | (1L << 3) | (1L << 4);
    /* decorations = MWM_DECOR_BORDER | MWM_DECOR_TITLE | MWM_DECOR_MENU */
    mwm_hints.inputMode = ~(0L);
    mwm_hints.status = 0;

    /* Add MINIMIZE options only if the window might be iconifiable and no noclose*/
    if (!ocurrent->never_iconify && !ocurrent->never_close) {
      mwm_hints.functions |= (1L << 3); /* MWM_FUNC_MINIMIZE */
      mwm_hints.decorations |= (1L << 5); /* MWM_DECOR_MINIMIZE */
    }

    /* Get current hostname. */
    stringlist[0] = net_get_hostname(hostname_buf, 256);
    if (stringlist[0])
      XStringListToTextProperty((char **)stringlist, 1, &hostname);
    else
      hostname.value = 0;
  }

  /* create windows */
  {
    XSetWindowAttributes setattr;
    unsigned long setattr_mask;
    setattr.colormap = port->colormap;
    setattr.backing_store = NotUseful;
    setattr.save_under = False;
    setattr.border_pixel = 0;
    setattr.background_pixel = 0;
    setattr_mask = CWColormap | CWBorderPixel | CWBackPixel | CWBackingStore
      | CWSaveUnder;

    nh->w = XCreateWindow
      (port->display, port->root_window,
       x, y, width, height, 0,
       port->depth, InputOutput, port->visual, setattr_mask, &setattr);

    // TODO: learn and check what is icon_window for?
    xwmh->icon_window = nh_icon->w = XCreateWindow
      (port->display, port->root_window,
       x, y, port->icon_width, port->icon_height, 0,
       port->depth, InputOutput, port->visual, setattr_mask, &setattr);
  }

  /* logo icon hack :-o */
  {
      Atom propicon;
      propicon = XInternAtom(port->display, "_NET_WM_ICON", 0); // 0 = create if not exists
      XChangeProperty(port->display, nh->w, propicon, XA_CARDINAL, 32, PropModeReplace,
      (unsigned char*)logo_c32_data, logo_c32_data[0] * logo_c32_data[1] + 2);
  }

  /* beep on every new hand warning window created, if beep */
  if( port->hands && ocurrent->beep ) XBell(port->display, 0);

  /* set XWRITS_WINDOW property early to minimize races */
  mark_xwrits_window(port, nh->w);

  xsh->x = x;
  xsh->y = y;
  xwmh->initial_state = ocurrent->appear_iconified ? IconicState : NormalState;
  XSetWMProperties(port->display, nh->w, &window_name, &icon_name,
		   NULL, 0, xsh, xwmh, &classh);
  XSetWMProtocols(port->display, nh->w, &port->wm_delete_window_atom, 1);

  /* window manager properties, including GNOME/KDE hints */
  XChangeProperty(port->display, nh->w, port->mwm_hints_atom,
		  port->mwm_hints_atom, 32, PropModeReplace,
		  (unsigned char *)&mwm_hints, sizeof(mwm_hints) / sizeof(long));
  property[0] = port->wm_delete_window_atom;
  property[1] = port->net_wm_ping_atom;
  XChangeProperty(port->display, nh->w, port->wm_protocols_atom,
		  XA_ATOM, 32, PropModeReplace,
		  (unsigned char *)property, 2);
  /* 9.Jul.2006 -- see also hand_map_raised below */
  property[0] = 0xFFFFFFFFU;
  XChangeProperty(port->display, nh->w, port->net_wm_desktop_atom,
		  XA_CARDINAL, 32, PropModeReplace,
		  (unsigned char *)property, 1);
#if 0
  property[0] = port->net_wm_window_type_utility_atom;
  XChangeProperty(port->display, nh->w, port->net_wm_window_type_atom,
		  XA_ATOM, 32, PropModeReplace,
		  (unsigned char *)property, 1);
#endif
  if (hostname.value)
    XSetWMClientMachine(port->display, nh->w, &hostname);
  property[0] = getpid();
  XChangeProperty(port->display, nh->w, port->net_wm_pid_atom,
		  XA_CARDINAL, 32, PropModeReplace,
		  (unsigned char *)property, 1);


  XSelectInput(port->display, nh->w, ButtonPressMask | StructureNotifyMask
	       | KeyPressMask | VisibilityChangeMask | ExposureMask);
  XSelectInput(port->display, nh_icon->w, StructureNotifyMask);

  nh->port = port;
  nh->icon = nh_icon;
  nh->x = x;			/* will be set correctly */
  nh->y = y;			/* by next ConfigureNotify */
  nh->width = width;
  nh->height = height;
  nh->root_child = nh->w;
  nh->is_icon = 0;
  nh->mapped = 0;
  nh->withdrawn = 0;
  nh->configured = 0;
  nh->slideshow = 0;
  nh->clock = 0;
  nh->toplevel = 1;

  if (port->hands)
    port->hands->prev = nh;
  nh->next = port->hands;
  nh->prev = 0;
  port->hands = nh;

  nh_icon->port = port;
  nh_icon->icon = nh;
  nh_icon->root_child = nh->w;
  nh_icon->width = nh_icon->height = 0;
  nh_icon->is_icon = 1;
  nh_icon->mapped = 0;
  nh_icon->withdrawn = 0;
  nh_icon->configured = 0;
  nh_icon->slideshow = 0;
  nh_icon->clock = 0;
  nh_icon->permanent = 0;
  nh_icon->toplevel = 1;
  if (port->icon_hands)
    port->icon_hands->prev = nh_icon;
  nh_icon->next = port->icon_hands;
  nh_icon->prev = 0;
  port->icon_hands = nh_icon;

  return nh;
}

Hand *
new_hand_subwindow(Port *port, Window parent, int x, int y)
{
  Hand *nh = xwNEW(Hand);
  int width = ocurrent->slideshow->screen_width;
  int height = ocurrent->slideshow->screen_height;
  unsigned parent_width, parent_height;
  port = port->master;

  if (x == NEW_HAND_CENTER || y == NEW_HAND_CENTER || x == NEW_HAND_RANDOM
      || y == NEW_HAND_RANDOM) {
      /* get dimensions of 'parent' */
      Window root;
      int x, y;
      unsigned bw, depth;
      (void) XGetGeometry(port->display, parent, &root, &x, &y, &parent_width, &parent_height, &bw, &depth);
  }

  if (x == NEW_HAND_CENTER)
    x = (parent_width - width) / 2;
  if (y == NEW_HAND_CENTER)
    y = (parent_height - height) / 2;

  if (x == NEW_HAND_RANDOM)
    x = (rand() >> 4) % (parent_width - width);
  if (y == NEW_HAND_RANDOM)
    y = (rand() >> 4) % (parent_height - height);

  {
    XSetWindowAttributes setattr;
    unsigned long setattr_mask;
    setattr.colormap = port->colormap;
    setattr.backing_store = NotUseful;
    setattr.save_under = False;
    setattr.border_pixel = 0;
    setattr.background_pixel = 0;
    setattr_mask = CWColormap | CWBorderPixel | CWBackPixel | CWBackingStore
      | CWSaveUnder;

    nh->w = XCreateWindow
      (port->display, parent,
       x, y, width, height, 0,
       port->depth, InputOutput, port->visual, setattr_mask, &setattr);
  }

  mark_xwrits_window(port, nh->w);

  XSelectInput(port->display, nh->w, ButtonPressMask | StructureNotifyMask
	       | KeyPressMask | VisibilityChangeMask | ExposureMask);

  nh->port = port;
  nh->icon = 0;
  nh->x = x;			/* will be set correctly */
  nh->y = y;			/* by next ConfigureNotify */
  nh->width = width;
  nh->height = height;
  nh->root_child = nh->w;
  nh->is_icon = 0;
  nh->mapped = 0;
  nh->withdrawn = 0;
  nh->configured = 0;
  nh->slideshow = 0;
  nh->clock = 0;
  nh->permanent = 0;
  nh->toplevel = 0;
  if (port->hands) port->hands->prev = nh;
  nh->next = port->hands;
  nh->prev = 0;
  port->hands = nh;

  return nh;
}


/* destroy a hand */

void
destroy_hand(Hand *h)
{
  Port *port = h->port;
  assert(!h->is_icon);

  unschedule_data(A_FLASH, h);
  /* 29.Jan.2000 oops -- forgot to do this, it caused segfaults */
  if (h->icon)
    unschedule_data(A_FLASH, h->icon);

  if (h->permanent) {
    XEvent event;
    /* last remaining hand; don't destroy it, unmap it */
    XUnmapWindow(port->display, h->w);
    /* Synthetic UnmapNotify required by ICCCM to withdraw the window */
    event.type = UnmapNotify;
    event.xunmap.event = port->root_window;
    event.xunmap.window = h->w;
    event.xunmap.from_configure = False;
    XSendEvent(port->display, port->root_window, False,
	       SubstructureRedirectMask | SubstructureNotifyMask, &event);
    /* mark hand as unmapped now */
    h->mapped = h->icon->mapped = 0;
    /* 9.Jul.2006 -- _NET_WM_DESKTOP must be reset after the window is
         withdrawn! The freedesktop.org standards require this. So mark the
         window as withdrawn as well. */
    h->withdrawn = 1;
  } else {
    if (h->icon) {
      Hand *ih = h->icon;
      XDestroyWindow(port->display, ih->w);
      if (ih->prev) ih->prev->next = ih->next;
      else port->icon_hands = ih->next;
      if (ih->next) ih->next->prev = ih->prev;
      xfree(ih);
    }
    XDestroyWindow(port->display, h->w);
    if (h->prev) h->prev->next = h->next;
    else port->hands = h->next;
    if (h->next) h->next->prev = h->prev;
    xfree(h);
  }
}


/* count active hands (mapped or iconified) */

int
active_hands(void)
{
  Hand *h;
  int i, n = 0;
  for (i = 0; i < nports; i++)
    for (h = ports[i]->hands; h; h = h->next)
      if (h->mapped || h->icon->mapped)
	n++;
  return n;
}


/* translate a Window to a Hand */

Hand *
window_to_hand(Port *port, Window w, int allow_icons)
{
  Hand *h;
  for (h = port->hands; h; h = h->next)
    if (h->w == w)
      return h;
  if (allow_icons)
    for (h = port->icon_hands; h; h = h->next)
      if (h->w == w)
	return h;
  return 0;
}


/* draw a picture on a hand */

void
draw_slide(Hand *h)
{
  Gif_Stream *gfs;
  Gif_Image *gfi;
  Port *port;
  PictureList *pl;

  if (!h || !h->slideshow)
    return;

  gfs = h->slideshow;
  gfi = gfs->images[h->slide];
  pl = (PictureList *)gfi->user_data;
  port = h->port;

  if (!pl->frames[port->port_number][h->slide].pixmap)
    (void) Gif_XNextImage(port->gfx, gfs, h->slide,
			  pl->frames[port->port_number]);

  XSetWindowBackgroundPixmap(port->display, h->w, pl->frames[port->port_number][h->slide].pixmap);
  XClearWindow(port->display, h->w);

  if (h->clock)
    draw_clock(h, 0);
}


/* unmap all windows */

void
unmap_all(void)
{
  int i;
  for (i = 0; i < nports; i++) {
    Hand *prev = 0, *trav = ports[i]->hands;
    while (trav) {
      if (trav->permanent) {
	prev = trav;
	trav->slideshow = 0;
      }
      destroy_hand(trav);
      trav = (prev == 0 ? ports[i]->hands : prev->next);
    }
    XFlush(ports[i]->display);
  }
}


/* map a hand window, keeping track of the _NET_WM_DESKTOP property */

void
hand_map_raised(Hand *h)
{
    if (h->withdrawn) {
	/* 9.Jul.2006 -- freedesktop.org says that the all-desktops property
             gets reset every withdraw, so reset it */
	unsigned long property[1];
	property[0] = 0xFFFFFFFFU;
	XChangeProperty(h->port->display, h->w, h->port->net_wm_desktop_atom,
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)property, 1);
	h->withdrawn = 0;
    }
    XMapRaised(h->port->display, h->w);
}



/* search for some hand on a given port, which might be a slave, or have
   slaves */

Hand *
find_one_hand(Port *port, int mapped)
{
    Hand *h, *acceptable;

    if (port->master == port) {
	if (!mapped)
	    return port->hands;
	else
	    for (h = acceptable = port->hands; h; h = h->next)
		if (h->mapped)
		    return h;
    } else {
	for (h = port->master->hands, acceptable = 0; h; h = h->next)
	    if (h->x >= port->left && h->x < port->left + port->width
		&& h->y >= port->top && h->y < port->top + port->height) {
		acceptable = h;
		if (mapped && h->mapped)
		    return h;
	    }
	if (!acceptable)
	    acceptable = new_hand(port, NEW_HAND_CENTER, NEW_HAND_CENTER);
    }

    if (mapped && !acceptable->mapped)
	hand_map_raised(acceptable);
    return acceptable;
}
