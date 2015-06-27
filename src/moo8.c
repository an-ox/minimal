#include <pebble.h>

  
// Simple half-abstract watchface

#define XREZ 144
#define YREZ 168 
#define CENTREX 72
#define CENTREY 84

  
static Window *window;
static Layer *canvas;

// using an apptimer allows us to easily change the update rate

static AppTimer *timer;
static uint32_t delta = 60000;

// let's declare a struct for pixel bitmaps in general
// as I intend to be using them quite a bit.

struct pixMap
{
// information about the bitmap format
  uint8_t width;
  uint8_t height;
  uint8_t index;
  void *raw;        // the raw bitmap data
// information about the position and scale
  uint16_t dotPitchX;	// Step per glyph X pixel
  uint16_t dotWidth;	// Actual size of X pixel
  uint16_t dotPitchY;	// Step per glyph Y pixel
  uint16_t dotHeight;   // Size of y pixel
  uint16_t x;
  uint16_t y;
  int col;		// Colour
  const int *pal;	// Palette, NULL if 1 bpp
  const int *plpal;     // Per line palette, usually NULL 
// stuff that will be used to animate a pixmap
  uint8_t minFrame;
  uint8_t maxFrame;
  int8_t frameStep;
  uint8_t animFlags;
};

// this struct lets me easily define paletted sprites in the source

typedef struct
{
	char *map[64];
	char colourMap[16];
	unsigned int palette[16];
	uint8_t totalWidth;
	uint8_t totalHeight;
}spriteDef;

// this is a struct for a sprite.

typedef struct
{
  uint8_t width;
  uint8_t height;
  uint8_t *raw;
}sprite;

// let's have a struct for a sprite entity that mooves

typedef struct 
{
  int16_t px;
  int16_t py;
  int16_t vx;
  int16_t vy;
  uint8_t scalex;
  uint8_t scaley;
  uint8_t tint;
  sprite *sp;
  uint8_t type;  // So I can have different behaviour types.
  void *next;  // for a linked list of entities we need to be able to link them together
  uint8_t index;
  bool killMe;
}entity;



// here are some vars defining a clip window for the sprite to clip against
// I'll set some defaults here to test with

int16_t xclip_lo=0;
int16_t xclip_hi=XREZ;
int16_t yclip_lo=16;
int16_t yclip_hi=YREZ-1;

uint8_t clockDigits[6];  // I will unpack the time into here HHMMSS

// info about the time

uint8_t lastMinute=64;
uint8_t lastHour=64;
uint16_t currentSecond; 
uint16_t currentMinute;
uint16_t currentHour;

// need to be able to see the time so..
struct tm *t;  // Structure holding easily read components of the time.
time_t temp;	 // Raw epoch time.

// Let's make some routines for directly accessing a raw bitmap.
// All of them reference a bitmap pointer which will have been
// obtained in the render procedure.

// Little helper functions to get chunks of colour for fill data
// converting from proper hex colours

uint8_t getcol1(int col)
{
#ifndef PBL_COLOR
  return 0xff;
#else
  return GColorFromHEX(col).argb;
#endif
}

uint16_t getcol2(int col)
{
#ifndef PBL_COLOR
  return 0xffff;
#else
  uint16_t res=GColorFromHEX(col).argb;
  return res|(res<<8); 
#endif
}

uint32_t getcol4(int col)
{
#ifndef PBL_COLOR
  return 0xffffffff;
#else
  uint32_t res=GColorFromHEX(col).argb;
  return res|(res<<8)|(res<<16)|(res<<24);

#endif
}


// Box functions, use to fill larger areas
// use the 2 and 4 versions for faster fills on aligned bounds


static void box(uint8_t *bits,uint8_t col,uint8_t x,uint8_t y,uint8_t w,uint8_t h)
{
  // Raw box draw. Note this is NOT bounds checked; use box_c for clipped
  // version.
  int i,j;
  uint8_t *lp=bits+y*XREZ+x;
  int stride=XREZ-w;
  for(j=0;j<h;j++)
  {
    for(i=0;i<w;i++)
     *lp++=col;
    lp+=stride;
  }
}

static void box2(uint8_t *bits,uint16_t col,uint8_t x,uint8_t y,uint8_t w,uint8_t h)
{
  // Raw box draw word aligned. Note this is NOT bounds checked; use box_c for clipped
  // version.
  int i,j;
  w>>=1;
  x>>=1;
  uint16_t *lp=(uint16_t *)bits+y*(XREZ/2)+x;
  int stride=(XREZ/2)-w;
  for(j=0;j<h;j++)
  {
    for(i=0;i<w;i++)
     *lp++=col;
    lp+=stride;
  }
}

static void box4(uint8_t *bits,uint32_t col,uint8_t x,uint8_t y,uint8_t w,uint8_t h)
{
  // Raw box draw. Note this is NOT bounds checked; use box_c for clipped
  // version.
  int i,j;
  w>>=2;
  x>>=2;
  uint32_t *lp=(uint32_t *)bits+y*(XREZ/4)+x;
  int stride=(XREZ/4)-w;
  for(j=0;j<h;j++)
  {
    for(i=0;i<w;i++)
     *lp++=col;
    lp+=stride;
  }
}

static void box_c(uint8_t *bits,int col,int16_t x,int16_t y,int16_t w,int16_t h,uint8_t chunk)
{
  // Clipped box draw, anyway aligned. This does real clipping instead of
  // just rejecting out of bounds stuff like the pix_c calls.

  int16_t bb;
  if(x>=XREZ||y>=YREZ) return;
  if(x<0) 
  {
    w+=x;
    x=0;
  }
  if(w<0) return;
  bb=x+w;
  if(bb>=XREZ) w-=(bb-XREZ)+1;
  if(w<0||w>=XREZ) return;
  if(y<0) 
  {
    h+=y;
    y=0;
  }
  if(h<0) return; 
  bb=y+h;
  if(bb>=YREZ) h-=(bb-YREZ)+1;
  if(h<0||h>=YREZ) return;
  switch(chunk)
  {
  default: // any align, byte data
    box(bits,getcol1(col),x,y,w,h);
    break;
  case 2: // word align, word data
    box2(bits,getcol2(col),x,y,w,h);
    break;
  case 4: // long align, long data
    box4(bits,getcol4(col),x,y,w,h);
    break;
  }
}

// Converting the pixmap draw to use our direct write "Box" routine

// added flags to modify some options
// don't mind making this a bit slower with more options as
// most moving/updated stuff will be done as sprites now

#define BYTEREV 1  // reverse pixel order in each byte (mono only at the moment)
#define LINEREV 2  // reverse line order (reflect vertically)
static void drawPixMap(struct pixMap *pm,uint8_t *bits,sprite *sp,uint8_t flags)
{

// Instead of a graphic context we have a pointer to the raw screen "bits".
// This stuff is basalt only so we can drop the ifdefs for PBL_COLOR.

  int spp=0;
  if(sp)
  {
    sp->height=pm->height;
    sp->width=pm->width;
    sp->raw=malloc(sp->height*sp->width);
  }
  int col=pm->col;  // we'll hold the fill colour in col

  const int *pal=pm->pal;
  int pindex,i;
  uint8_t *map=(uint8_t *)pm->raw;
  uint16_t szx=pm->width*pm->dotPitchX;
  uint16_t szy=pm->height*pm->dotPitchY;
  uint16_t bytesPerLine=pm->width>>3;
  uint16_t base=bytesPerLine*pm->height*pm->index;
  uint16_t offBase=bytesPerLine*(pm->height-1);
  uint16_t oy=pm->y-(pm->dotHeight>>1)-(szy>>1);
  if(flags&LINEREV) base+=offBase;
  for(int j=0;j<pm->height;j++)
  {
    uint16_t ox=pm->x-(pm->dotWidth>>1)-(szx>>1);
    uint16_t ubase=base;
    if(pm->plpal) // per line palette
    {
      col=pm->plpal[j];
    }
    for(int k=0;k<bytesPerLine;k++)
    {
      uint8_t ccline=map[base];
      if(pal)  // non-NULL pal means multiple bits per pixel
      {
        // byte unpacking loop for 2 bits per pixel
        uint16_t ox2=ox+pm->dotPitchX*6;
        for(i=0;i<4;i++)
        {
          pindex=ccline&3;
          ccline>>=2;
          col=(int)pal[pindex];
          if(sp)
            sp->raw[spp++]=getcol1(col);
          else
            box_c(bits,col,ox2>>8,oy>>8,pm->dotWidth>>7,pm->dotHeight>>8,1);
          ox2-=pm->dotPitchX<<1;
        }
        ox+=pm->dotPitchX<<3;
      }
      else // byte unpacking loop for 1 bit per pixel
      {
        for(i=0;i<8;i++)
        {
          bool pixcnd;
          if(flags&BYTEREV)  // reverse byte order
            pixcnd=((1<<i)&ccline);
          else
            pixcnd=((1<<(7-i))&ccline);
           if(pixcnd)
           {
              if(sp)
                sp->raw[spp++]=getcol1(col);
              else
                box_c(bits,col,ox>>8,oy>>8,pm->dotWidth>>8,pm->dotHeight>>8,1);
           }
           else
           {
               if(sp)
                sp->raw[spp++]=0;
           }
          ox+=pm->dotPitchX;
        }
      }
      
      base++;
    }
    base=ubase;
    if(flags&LINEREV)
      base-=bytesPerLine;
    else
      base+=bytesPerLine;
    oy+=pm->dotPitchY;
  }
}

void createSpriteFromDef(spriteDef *def,sprite *s)
{
  // creates the raw data for a sprite from the def
  
  s->height=def->totalHeight;
  s->width=def->totalWidth;
  s->raw=malloc(s->height*s->width);
  
  uint8_t i,j,k,ch;
  uint8_t *dp=s->raw;
  char **sp=def->map;
  for(i=0;i<def->totalHeight;i++)
  {
    for(j=0;j<def->totalWidth;j++)
    {
      ch=sp[i][j];
      for(k=0;k<16;k++)
      {
        if(def->colourMap[k]==ch)
        {
          *dp++=getcol1(def->palette[k]);
          k=16;
        }
      }
    }
  }
}

// right, let's make drawSprite now that we (should) have a faster sprite bitmap format done.

void drawSprite(int16_t x,int16_t y,uint16_t sx,uint16_t sy,sprite *sp,uint8_t tint,uint8_t *bits)
{
  // x and y are position in signed 16bit ints
  // sx and sy are scale in 4:4 fixed point
  // sp is a pointer to an initialised sprite bitmap  
  // tint is ANDed with each pixel in the sprite, so if you want mono sprites you
  // can colour, unpack the sprite as white and put the desired colour byte in tint.
  // Otherwise put coloured pixels in the sprite and set tint to 255.
  // bits is the raw dest bitmap
  
  uint8_t ch;
  uint8_t *src=sp->raw;
  
  // now let#s make some fixed point offsets into the sprite's raw bitmap
  
  uint16_t u=0;  // let these be 12:4 fixed point
  uint16_t v=0;
  
  // sx and sy are the size of one pixel, in 4:4 

  uint8_t lines=(sp->height*sy)>>4;
  uint8_t pixels=(sp->width*sx)>>4;
  uint16_t lp;
  uint16_t ui=0x8000/sx; // this result is 1:15 divided by 4:4 so is 0:11
  uint16_t vi=0x8000/sy;
  
  // I could speed things up by pre-shifting those and using less precision
  // in the main loop, but let's see how things go, more precision might
  // come in useful some time.
  
  // So, clipping.  First let's trivially reject anything that is completely outside of the clip window.
  // first trivially reject completely off screen sprites:
  
  if(y>=yclip_hi) return;
  if(y+lines<=yclip_lo) return;
  if(x>=xclip_hi) return;
  if(x+pixels<=xclip_lo) return;
  
  // now find out where clipping is needed and adjust lengths and pointers
  // accordingly.
  
  int16_t cly=0;
  if(y<yclip_lo) // There is clopping to be done at the low edge
  {
    cly=yclip_lo-y;  // amount of low edge clip
    lines-=cly;      // reduce the line count by that much clip
    y+=cly;          // y origin is now clip point
    // we also have to advance the v pointer by that many pixel fractions
    v+=cly*vi;
  }
  if(y+lines>yclip_hi) // There is clipping at the high edge
  {
    cly=(y+lines)-yclip_hi;  // amount of high edge clop
    lines-=cly;              // reduce height by clip amount
  }
  
  // I'll xclip here for now
  
 int16_t clx=0;
  if(x<xclip_lo) // There is clopping to be done at the low edge
  {
    clx=xclip_lo-x;  // amount of low edge clip
    pixels-=clx;      // reduce the line count by that much clip
    x+=clx;          // x origin is now clip point
    // we also have to advance the u pointer by that many pixel fractions
    u+=clx*ui;
  }
  if(x+pixels>xclip_hi) // There is clipping at the high edge
  {
    clx=(x+pixels)-xclip_hi;  // amount of high edge clop
    pixels-=clx;              // reduce height by clip amount
  }  
  
  // get our ducks in a row before the mainloop starts
  
  uint16_t u0=u;  // preserve the clipped u coord
  uint8_t stride=XREZ-pixels;
  uint16_t pp=((y*XREZ)+x);

  // here is the main loop
  
  for(int j=0;j<lines;j++)
  {
    u=u0;
    lp=(v>>11)*sp->width;
    for(int i=0;i<pixels;i++)
    {
      ch=src[lp+(u>>11)];
      if(ch>0) bits[pp]=ch&tint;
      u+=ui;
      pp++;
    }
    v+=vi;
    pp+=stride;
  }
}

// Numeral bitmaps 

const uint8_t numerals[]=
{
  0xfc,0x84,0x84,0x84,0x84,0xfc,  // 0
  0x30,0x10,0x10,0x10,0x10,0x78, // 1
  0x7c,0x04,0xfc,0x80,0x80,0xf8,  // 2
  0x7c,0x04,0x7c,0x04,0x04,0xfc,  // 3
  0x80,0x88,0x88,0xfc,0x08,0x08,  // 4
  0xf8,0x80,0xfc,0x04,0x04,0xfc,  // 5
  0xf8,0x80,0xfc,0x84,0x84,0xfc,  // 6
  0xf8,0x08,0x10,0x20,0x40,0x80,  // 7
  0xfc,0x84,0xfc,0x84,0x84,0xfc,  // 8
  0xfc,0x84,0xfc,0x04,0x04,0x7c,  // 9
};

const uint32_t palette[]=
{
  0x770777,0x004dff,0x00ff00,0xffff00,0xff8000,0xff0000,000000,
};


  

sprite *numberShapes[10];


static void hourChanged()
{
  // this will be called every time the hour changes
  // use it to change any state per hour on the hour
  clockDigits[0]=currentHour/10;
  clockDigits[1]=currentHour%10;
}

static void minuteChanged()
{
  // this will be called every time the hour changes
  // use it to change any state per hour on the hour  
 
}

// make stuff needed during run time

static void makeCrap()
{
  int i;

// fetch the time so I can set everything up right at first run

  temp = time(NULL);	   // get raw time
  t = localtime(&temp);  // break it up into readable bits
  // I'll initialise this even though it'll get filled with the time
  // before first draw happens anyway
  for(i=0;i<6;i++) clockDigits[i]=i;
  
 struct pixMap *pm=malloc(sizeof(struct pixMap));  // use this to make the sprites
  pm->width=8;
  pm->height=6;
  pm->index=0;
  pm->col=0xffffff;
  pm->pal=NULL;
  pm->plpal=NULL; 
  
  // make digit shapes, these are all 8x5

  for(i=0;i<10;i++) 
  {
    numberShapes[i]=malloc(sizeof(sprite));
    pm->raw=(void *)&numerals[i*6];
    drawPixMap(pm,NULL,numberShapes[i],0);
  }
  // throw away the pixmap as I'm done with it  
  free(pm);
}



  

// throw away all the smeg when done

static void destroyCrap()
{
  int i;
  for(i=0;i<10;i++)
  {
    free(numberShapes[i]->raw);    
    free(numberShapes[i]);
  } 
}
                

	
/****************************** Renderer Lifecycle *****************************/

static void timeAt(int16_t xp,int16_t yp,int col,uint8_t *bits)
{
    // draw a copy of the hour time at position xp,yp in colour col
    // (xp and yp are with 4 bits of frac)
 
  // set scales
  uint8_t xs=6;
  uint8_t ys=8;
  uint16_t gap=2<<6;
  
  
  int16_t txp=xp-(6<<xs)-gap;
  int16_t typ=yp-(3<<7);
  for(uint8_t i=0;i<2;i++)
  {
    drawSprite((txp>>4)-4,typ>>4,xs<<4,ys<<4,numberShapes[clockDigits[i]],getcol1(col),bits);
    txp+=(gap<<1)+(6<<xs);
  }  
}

static uint16_t mag(int16_t v)
{
 if(v<0) return -v; else return v;  
}

static void directAccess(GContext *ctx)
{
  // In which I experiment with directly accessing the frame buffer

#ifndef PBL_COLOR
  return;
#else

  GBitmap *b=graphics_capture_frame_buffer_format(ctx,GBitmapFormat8Bit);
  uint8_t *bits=gbitmap_get_data(b);

  uint16_t rzang=(0x8000/60)*currentMinute;
  int16_t tx=sin_lookup(rzang*2)*0x1000/TRIG_MAX_RATIO;
  int16_t ty=-cos_lookup(rzang*2)*0x1000/TRIG_MAX_RATIO;
  int16_t px=XREZ<<3;
  int16_t py=(YREZ<<3);
  // 'bits' now should point to the raw frame buffer data

  int i;

  if(mag(tx)>mag(ty))
  {
    ty/=mag(tx)>>4;
    tx/=mag(tx)>>4;
  
  }
  else
  {
    tx/=mag(ty)>>4;
    ty/=mag(ty)>>4;
 
  }
  
  int16_t ox=-56*tx;  // there must always be a variable called 'ox'.
  int16_t oy=-56*ty;
  yclip_lo=0;
  
  for(i=0;i<56;i++)
  {
    ox+=tx;
    oy+=ty;
    timeAt(px-ox,py-oy,palette[i>>3],bits);
  }
  
  // draw the clock digits
  
  timeAt(px,py,0xffffffff,bits);
  
  graphics_release_frame_buffer(ctx,b);
#endif
}

/*
 * Render 
 */
static void timer_callback(void *data)
{	
  //Render
  layer_mark_dirty(canvas);
  timer=app_timer_register(delta,(AppTimerCallback) timer_callback,0);
}

/*
 * Start rendering loop
 */
static void start()
{
  timer=app_timer_register(delta,(AppTimerCallback) timer_callback,0);
}



/*
 * Rendering
 */



static void render(Layer *layer, GContext* ctx) 
{
  int i;

  // Get the current bits of the time

  temp = time(NULL);	   // get raw time
  t = localtime(&temp);  // break it up into readable bits
  currentSecond=t->tm_sec;  // the current second
  currentMinute=t->tm_min;
  currentHour=t->tm_hour;
  if(currentHour>11) currentHour-=12;

  if(currentHour==0) currentHour=12;

// Upon first run copy the current hour and sec ro last hour and sec

  if(lastMinute>60)  // will never naturally occur, set that way at first run
  {
    lastMinute=currentMinute;
    lastHour=currentHour;
  }
  else // in normal runs we can check for changed min or hour and do something
  {
    if(currentHour!=lastHour)
    {
      lastHour=currentHour;
      hourChanged();
    }
    if(currentMinute!=lastMinute)
    {
      lastMinute=currentMinute;
      minuteChanged();
    }    
  }
  if(currentSecond>59) currentSecond=59;  // Can happen, apparently, if there is a leap second. 
  
  // put time in time digits

  clockDigits[0]=currentHour/10;
  clockDigits[1]=currentHour%10;
  clockDigits[2]=t->tm_min/10;
  clockDigits[3]=t->tm_min%10;
  clockDigits[4]=t->tm_sec/10;
  clockDigits[5]=t->tm_sec%10;
  
  // everything happens in framebuffer direct access

  directAccess(ctx);

}

/****************************** Window Lifecycle *****************************/


static void window_load(Window *window)
{
	//Setup window
	window_set_background_color(window, GColorBlack);
	
	//Setup canvas
	canvas = layer_create(GRect(0, 0, 144, 168));
	layer_set_update_proc(canvas, (LayerUpdateProc) render);
	layer_add_child(window_get_root_layer(window), canvas);
	
	//Start rendering
	start();
}

static void window_unload(Window *window) 
{
	//Cancel timer
	app_timer_cancel(timer);

	//Destroy canvas
	layer_destroy(canvas);
}

/****************************** App Lifecycle *****************************/


static void init(void) {
	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) 
	{
		.load = window_load,
		.unload = window_unload,
	});
	
	//Prepare everything
	makeCrap();
	//Finally
	window_stack_push(window, true);
}

static void deinit(void) 
{
	//De-init everything
	destroyCrap();
	//Finally
	window_destroy(window);
}

int main(void) 
{
	init();
	app_event_loop();
	deinit();
}
