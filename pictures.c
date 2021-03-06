#include <config.h>
#include "xwrits.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include "colorpic.c"
#include "monopic.c"

Gif_Stream *current_slideshow = 0;

#define NPICTURES 14

struct named_record {
  const char *name;
  const Gif_Record *record;
  Gif_Stream *gfs;
  const char *synonym;
};

struct named_record built_in_pictures[] = {
  /* normal pictures */
  {"clench", &clenchl_gif, 0},		{"clenchicon", &clenchi_gif, 0},
  {"spread", &spreadl_gif, 0},		{"spreadicon", &spreadi_gif, 0},
  {"american", &fingerl_gif, 0},	{"americanicon", &fingeri_gif, 0},
  {"resting", &restl_gif, 0},		{"restingicon", &resti_gif, 0},
  {"ready", &okl_gif, 0},		{"readyicon", &oki_gif, 0},
  {"locked", &lock_gif, 0},
  {"bars", &bars_gif, 0},

  /* monochrome pictures */
  {"clenchmono", &clenchlm_gif, 0},	{"clenchiconmono", &clenchim_gif, 0},
  {"spreadmono", &spreadlm_gif, 0},	{"spreadiconmono", &spreadim_gif, 0},
  {"americanmono", &fingerlm_gif, 0},	{"americaniconmono", &fingerim_gif, 0},
  {"restingmono", &restlm_gif, 0},	{"restingiconmono", &restim_gif, 0},
  {"readymono", &oklm_gif, 0},		{"readyiconmono", &okim_gif, 0},
  {"lockedmono", &lockm_gif, 0},
  {"barsmono", &barsm_gif, 0},

  /* other cultures' finger gestures */
  {"korean", &koreanl_gif, 0},		{"koreanicon", &koreani_gif, 0},
  {"german", &germanl_gif, 0},		{"germanicon", &germani_gif, 0},
  {"japanese", 0, 0, "korean"},		{"japaneseicon", 0, 0, "koreanicon"},
  {"russian", 0, 0, "korean"},		{"russianicon", 0, 0, "koreanicon"},

  /* last */
  {0, 0, 0}
};


static void
free_picturelist(void *v)
{
  PictureList *pl = (PictureList *)v;
  int i;
  if (--pl->refcount == 0) {
    for (i = 0; i < nports; i++)
      Gif_DeleteXFrames(ports[i]->gfx, pl->gfs, pl->frames[i]);
    xfree(pl);
  }
}

static int
add_picturelist(Gif_Stream *gfs, int clock_x_off, int clock_y_off)
{
  int i;
  PictureList *pl = (PictureList *)
    xmalloc(sizeof(PictureList) + (nports-1) * sizeof(Gif_XFrame *));
  if (!pl)
    return -1;

  pl->clock_x_off = clock_x_off;
  pl->clock_y_off = clock_y_off;
  pl->gfs = gfs;
  pl->refcount = 0;
  for (i = 0; i < nports; i++)
    if (!(pl->frames[i] = Gif_NewXFrames(gfs)))
      return 0;
  for (i = 0; i < gfs->nimages; ++i) {
    gfs->images[i]->user_data = pl;
    gfs->images[i]->free_user_data = free_picturelist;
    ++pl->refcount;
  }
  return 0;
}


static Gif_Stream *
get_built_in_image(const char *name)
{
  struct named_record *nr;
  Gif_Stream *gfs;
  int i;

  for (nr = built_in_pictures; nr->name; nr++)
    if (strcmp(nr->name, name) == 0)
      goto found;
  return 0;

 found:
  if (nr->gfs)
    return nr->gfs;
  if (!nr->record && nr->synonym) {
    nr->gfs = get_built_in_image(nr->synonym);
    return nr->gfs;
  }

  nr->gfs = gfs =
    Gif_FullReadRecord(nr->record, GIF_READ_COMPRESSED | GIF_READ_CONST_RECORD,
		       0, 0);
  if (!gfs)
    return 0;

  /* built-in images are all loop-forever. don't change the GIFs because it
     makes the executable bigger */
  if (gfs->loopcount < 0) gfs->loopcount = 0;

  for (i = 0; i < gfs->nimages; ++i)
    gfs->images[i]->user_data = (void *) gfs;

  return gfs;
}


#define MIN_DELAY 4

static Gif_Image *
clone_image_skeleton(Gif_Image *gfi)
{
  Gif_Image *ngfi = Gif_NewImage();
  assert(!gfi->image_data && gfi->compressed);
  ngfi->local = gfi->local;
  if (ngfi->local) ngfi->local->refcount++;
  ngfi->transparent = gfi->transparent;
  ngfi->left = gfi->left;
  ngfi->top = gfi->top;
  ngfi->width = gfi->width;
  ngfi->height = gfi->height;
  ngfi->compressed = gfi->compressed;
  ngfi->compressed_len = gfi->compressed_len;
  ngfi->free_compressed = 0;
  ngfi->delay = gfi->delay;
  gfi->refcount++;
  return ngfi;
}

static void
add_stream_to_slideshow(Gif_Stream *add, Gif_Stream *gfs,
			double flash_rate_ratio)
{
  Gif_Image *gfi;
  int i;
  double d;

  /* adapt delays for 1-frame images */
  if (add->nimages == 1)
    add->images[0]->delay = DEFAULT_FLASH_DELAY_SEC * 100;

  /* account for background and loopcount */
  if (gfs->nimages == 0) {
    if (add->global) {
      gfs->global = add->global;
      add->global->refcount++;
      gfs->background = add->background;
    }
    gfs->loopcount = add->loopcount;
  }

  /* adapt screen size */
  if (add->screen_width > gfs->screen_width)
    gfs->screen_width = add->screen_width;
  if (add->screen_height > gfs->screen_height)
    gfs->screen_height = add->screen_height;

  /* add images from add to gfs */
  for (i = 0; i < add->nimages; i++) {
    gfi = add->images[i];
    /* don't use an image directly if it is shared */
    if (gfi->user_data)
      gfi = clone_image_skeleton(gfi);
    /* ensure local colormap */
    if (!gfi->local && add->global) {
      gfi->local = add->global;
      add->global->refcount++;
    }
    /* adapt delay */
    d = gfi->delay * flash_rate_ratio;
    if (d < 0 || d >= 0xFFFF)
      gfi->delay = 0xFFFF;
    else
      gfi->delay = (d < MIN_DELAY ? MIN_DELAY : (uint16_t)d);
    /* add image */
    Gif_AddImage(gfs, gfi);
  }

  /* add multiple times if it has a loop count, up to a max of 20 loops */
  if (add->nimages > 1 && add->loopcount >= 0) {
    int loop = (add->loopcount <= 20 ? add->loopcount : 20);
    int first = gfs->nimages - add->nimages;
    int j;
    if (loop == 0 && first > 0) {
      for (i = j = 0; i < add->nimages; i++)
	j += gfs->images[first + i]->delay;
      loop =
	(int)((DEFAULT_FLASH_DELAY_SEC * 100) * flash_rate_ratio / j);
    }
    for (i = 0; i < loop; i++)
      for (j = 0; j < add->nimages; j++)
	Gif_AddImage(gfs, gfs->images[first + j]);
  }
}

Gif_Stream *
parse_slideshow(const char *slideshowtext, double flash_rate_ratio, int mono)
{
  char buf[BUFSIZ];
  char name[BUFSIZ + 4];
  char *s;
  Gif_Stream *gfs, *add;
  Gif_Image *gfi;
  int i, clock_xoff = -1;

  if (strlen(slideshowtext) >= BUFSIZ) return 0;
  strcpy(buf, slideshowtext);
  s = buf;

  gfs = Gif_NewStream();
  gfs->loopcount = 0;

  while (*s) {
    char *n, save;
    FILE *f;

    while (isspace(*s))
	s++;
    n = s;
    while (!isspace(*s) && *s != ';' && *s)
	s++;
    save = *s;
    *s = 0;

    if (n[0] == '&' || n[0] == '*') {
      /* built-in image */
      strcpy(name, n + 1);
      if (mono)
	  strcat(name, "mono");
      i = strlen(name);
      add = get_built_in_image(name);
      /* some images don't have monochromatic versions; fall back on color */
      if (!add && i > 4 && strcmp(name + i - 4, "mono") == 0) {
	name[i-4] = 0;
	add = get_built_in_image(name);
	if (add)
	  warning("no monochrome version of built-in picture '%s'", name);
	name[i-4] = 'm';
      }
      /* set clock X offset */
      if (clock_xoff < 0)
	  clock_xoff = (strncmp(name, "locked", 6) == 0 ? 65 : 10);
      /* add images */
      if (add) {
	  add_stream_to_slideshow(add, gfs, flash_rate_ratio);
	  goto done;
      } else if (n[0] == '*')
	  goto done;
      else
	  n++;
    }

    /* load file from disk */
    f = fopen(n, "rb");
    add = Gif_FullReadFile(f, GIF_READ_COMPRESSED, 0, 0);
    if (!f)
	error("%s: %s", n, strerror(errno));
    else if (!add || (add->nimages == 0 && add->errors > 0))
	error("%s: not a GIF", n);
    else
	add_stream_to_slideshow(add, gfs, flash_rate_ratio);
    if (add)
	Gif_DeleteStream(add);
    if (f)
	fclose(f);

  done:
    *s = save;
    if (*s)
	s++;
  }

  /* make sure screen_width and screen_height aren't 0 */
  if (gfs->screen_width == 0 || gfs->screen_height == 0 || gfs->nimages == 1) {
    gfs->screen_width = gfs->screen_height = 0;
    for (i = 0; i < gfs->nimages; i++) {
      gfi = gfs->images[i];
      if (gfi->width > gfs->screen_width)
	gfs->screen_width = gfi->width;
      if (gfi->height > gfs->screen_height)
	gfs->screen_height = gfi->height;
    }
  }

  /* create picture list */
  if (gfs->nimages > 0
      && add_picturelist(gfs, (clock_xoff < 0 ? 10 : clock_xoff), 10) < 0)
    return 0;

  return gfs;
}


void
set_slideshow(Hand *h, Gif_Stream *gfs, const struct timeval *now_ptr)
{
  int which_im = 0;
  Alarm *a;
  struct timeval t;
  Port *port = h->port;

  if (h->slideshow == gfs)
    return;
  else if (!gfs) {
    h->slideshow = 0;
    unschedule_data(A_FLASH, h);
    return;
  }

  if (now_ptr)
    t = *now_ptr;
  else
    xwGETTIME(t);

  a = grab_alarm_data(A_FLASH, h, 0);

  if (h->slideshow) {
    Gif_Image *cur_im = h->slideshow->images[h->slide];
    if (a)
      xwSUBDELAY(t, a->timer, cur_im->delay);
    /* Leave which_im with a picture that matches the old one, or 0 if no
       picture matches the old one. */
    for (which_im = gfs->nimages - 1; which_im > 0; which_im--)
      if (gfs->images[which_im] == cur_im)
	break;
  }

  /* fprintf(stderr, "%p %p %d\n", h, gfs, which_im); */
  if (gfs->nimages > 1) {
    if (!a)
      a = new_alarm_data(A_FLASH, h, 0);
    xwADDDELAY(a->timer, t, gfs->images[which_im]->delay);
    schedule(a);
  } else {
    if (a)
      destroy_alarm(a);
  }

  h->slideshow = gfs;

  if ((gfs->screen_width != h->width || gfs->screen_height != h->height)
      && !h->is_icon) {
    XWindowChanges wmch;
    XSizeHints *xsh = XAllocSizeHints();
    xsh->flags = USPosition | PMinSize | PMaxSize;
    xsh->min_width = xsh->max_width = gfs->screen_width;
    xsh->min_height = xsh->max_height = gfs->screen_height;
    wmch.width = gfs->screen_width;
    wmch.height = gfs->screen_height;
    XSetWMNormalHints(port->display, h->w, xsh);
    XReconfigureWMWindow(port->display, h->w, port->screen_number,
			 CWWidth | CWHeight, &wmch);
    XFree(xsh);
  }

  h->loopcount = 0;
  h->slide = which_im;
  draw_slide(h);
}

void
set_all_slideshows(Hand *hands, Gif_Stream *gfs)
{
  Hand *h;
  struct timeval now;
  xwGETTIME(now);
  for (h = hands; h; h = h->next)
    set_slideshow(h, gfs, &now);
  current_slideshow = gfs;
}
