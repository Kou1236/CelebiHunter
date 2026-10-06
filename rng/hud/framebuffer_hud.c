#include "framebuffer_hud.h"
#include <string.h>

/* Compact original 5x7 row glyphs; unlisted/non-ASCII bytes use '?'. */
static const uint8_t font[95][7]={
 ['!'-32]={4,4,4,4,4,0,4},['"'-32]={10,10,10,0,0,0,0},
 ['#'-32]={10,31,10,10,31,10,0},['$'-32]={4,15,20,14,5,30,4},
 ['%'-32]={25,26,2,4,8,11,19},['&'-32]={12,18,20,8,21,18,13},
 ['\''-32]={4,4,8,0,0,0,0},['('-32]={2,4,8,8,8,4,2},
 [')'-32]={8,4,2,2,2,4,8},['*'-32]={0,21,14,31,14,21,0},
 ['+'-32]={0,4,4,31,4,4,0},[','-32]={0,0,0,0,0,4,8},
 ['-'-32]={0,0,0,31,0,0,0},['.'-32]={0,0,0,0,0,0,4},
 ['/'-32]={1,2,2,4,8,8,16},['0'-32]={14,17,19,21,25,17,14},
 ['1'-32]={4,12,4,4,4,4,14},['2'-32]={14,17,1,2,4,8,31},
 ['3'-32]={30,1,1,14,1,1,30},['4'-32]={2,6,10,18,31,2,2},
 ['5'-32]={31,16,16,30,1,1,30},['6'-32]={14,16,16,30,17,17,14},
 ['7'-32]={31,1,2,4,8,8,8},['8'-32]={14,17,17,14,17,17,14},
 ['9'-32]={14,17,17,15,1,1,14},[':'-32]={0,4,0,0,4,0,0},
 [';'-32]={0,4,0,0,4,4,8},['<'-32]={1,2,4,8,4,2,1},
 ['='-32]={0,0,31,0,31,0,0},['>'-32]={16,8,4,2,4,8,16},
 ['?'-32]={14,17,1,2,4,0,4},['@'-32]={14,17,23,21,23,16,14},
 ['A'-32]={14,17,17,31,17,17,17},['B'-32]={30,17,17,30,17,17,30},
 ['C'-32]={14,17,16,16,16,17,14},['D'-32]={30,17,17,17,17,17,30},
 ['E'-32]={31,16,16,30,16,16,31},['F'-32]={31,16,16,30,16,16,16},
 ['G'-32]={14,17,16,23,17,17,15},['H'-32]={17,17,17,31,17,17,17},
 ['I'-32]={14,4,4,4,4,4,14},['J'-32]={7,2,2,2,2,18,12},
 ['K'-32]={17,18,20,24,20,18,17},['L'-32]={16,16,16,16,16,16,31},
 ['M'-32]={17,27,21,21,17,17,17},['N'-32]={17,25,25,21,19,19,17},
 ['O'-32]={14,17,17,17,17,17,14},['P'-32]={30,17,17,30,16,16,16},
 ['Q'-32]={14,17,17,17,21,18,13},['R'-32]={30,17,17,30,20,18,17},
 ['S'-32]={15,16,16,14,1,1,30},['T'-32]={31,4,4,4,4,4,4},
 ['U'-32]={17,17,17,17,17,17,14},['V'-32]={17,17,17,17,17,10,4},
 ['W'-32]={17,17,17,21,21,21,10},['X'-32]={17,17,10,4,10,17,17},
 ['Y'-32]={17,17,10,4,4,4,4},['Z'-32]={31,1,2,4,8,16,31},
 ['['-32]={14,8,8,8,8,8,14},['\\'-32]={16,8,8,4,2,2,1},
 [']'-32]={14,2,2,2,2,2,14},['^'-32]={4,10,17,0,0,0,0},
 ['_'-32]={0,0,0,0,0,0,31},['`'-32]={8,4,0,0,0,0,0},
 ['a'-32]={0,0,14,1,15,17,15},['b'-32]={16,16,30,17,17,17,30},
 ['c'-32]={0,0,14,17,16,17,14},['d'-32]={1,1,15,17,17,17,15},
 ['e'-32]={0,0,14,17,31,16,14},['f'-32]={6,9,8,28,8,8,8},
 ['g'-32]={0,14,17,17,15,1,14},['h'-32]={16,16,30,17,17,17,17},
 ['i'-32]={4,0,12,4,4,4,14},['j'-32]={2,0,6,2,2,18,12},
 ['k'-32]={16,16,18,20,24,20,18},['l'-32]={12,4,4,4,4,4,14},
 ['m'-32]={0,0,26,21,21,21,21},['n'-32]={0,0,30,17,17,17,17},
 ['o'-32]={0,0,14,17,17,17,14},['p'-32]={0,30,17,17,30,16,16},
 ['q'-32]={0,15,17,17,15,1,1},['r'-32]={0,0,22,25,16,16,16},
 ['s'-32]={0,0,15,16,14,1,30},['t'-32]={8,8,28,8,8,9,6},
 ['u'-32]={0,0,17,17,17,19,13},['v'-32]={0,0,17,17,17,10,4},
 ['w'-32]={0,0,17,17,21,21,10},['x'-32]={0,0,17,10,4,10,17},
 ['y'-32]={0,17,17,17,15,1,14},['z'-32]={0,0,31,2,4,8,31},
 ['{'-32]={2,4,4,8,4,4,2},['|'-32]={4,4,4,4,4,4,4},
 ['}'-32]={8,4,4,2,4,4,8},['~'-32]={0,0,9,22,0,0,0}
};
/* The same font in column form. Each bit is one display row. Glyph pitch
 * separates these 6x8 foreground/shadow cells, so final foreground has
 * priority and each covered pixel needs only one VRAM read and write. */
static const uint8_t font_columns[95][5]={
 [1]={0,0,95,0,0},
 [2]={0,7,0,7,0},
 [3]={18,63,18,63,18},
 [4]={36,42,127,42,18},
 [5]={67,51,8,102,97},
 [6]={54,73,85,34,80},
 [7]={0,4,3,0,0},
 [8]={0,28,34,65,0},
 [9]={0,65,34,28,0},
 [10]={42,28,62,28,42},
 [11]={8,8,62,8,8},
 [12]={0,64,32,0,0},
 [13]={8,8,8,8,8},
 [14]={0,0,64,0,0},
 [15]={64,48,8,6,1},
 [16]={62,81,73,69,62},
 [17]={0,66,127,64,0},
 [18]={66,97,81,73,70},
 [19]={65,73,73,73,54},
 [20]={24,20,18,127,16},
 [21]={79,73,73,73,49},
 [22]={62,73,73,73,48},
 [23]={1,113,9,5,3},
 [24]={54,73,73,73,54},
 [25]={6,73,73,73,62},
 [26]={0,0,18,0,0},
 [27]={0,64,50,0,0},
 [28]={0,8,20,34,65},
 [29]={20,20,20,20,20},
 [30]={65,34,20,8,0},
 [31]={2,1,81,9,6},
 [32]={62,65,93,85,30},
 [33]={126,9,9,9,126},
 [34]={127,73,73,73,54},
 [35]={62,65,65,65,34},
 [36]={127,65,65,65,62},
 [37]={127,73,73,73,65},
 [38]={127,9,9,9,1},
 [39]={62,65,73,73,122},
 [40]={127,8,8,8,127},
 [41]={0,65,127,65,0},
 [42]={32,64,65,63,1},
 [43]={127,8,20,34,65},
 [44]={127,64,64,64,64},
 [45]={127,2,12,2,127},
 [46]={127,6,8,48,127},
 [47]={62,65,65,65,62},
 [48]={127,9,9,9,6},
 [49]={62,65,81,33,94},
 [50]={127,9,25,41,70},
 [51]={70,73,73,73,49},
 [52]={1,1,127,1,1},
 [53]={63,64,64,64,63},
 [54]={31,32,64,32,31},
 [55]={63,64,56,64,63},
 [56]={99,20,8,20,99},
 [57]={3,4,120,4,3},
 [58]={97,81,73,69,67},
 [59]={0,127,65,65,0},
 [60]={1,6,8,48,64},
 [61]={0,65,65,127,0},
 [62]={4,2,1,2,4},
 [63]={64,64,64,64,64},
 [64]={0,1,2,0,0},
 [65]={32,84,84,84,120},
 [66]={127,68,68,68,56},
 [67]={56,68,68,68,40},
 [68]={56,68,68,68,127},
 [69]={56,84,84,84,24},
 [70]={8,126,9,1,2},
 [71]={12,82,82,82,60},
 [72]={127,4,4,4,120},
 [73]={0,68,125,64,0},
 [74]={32,64,68,61,0},
 [75]={127,16,40,68,0},
 [76]={0,65,127,64,0},
 [77]={124,4,120,4,120},
 [78]={124,4,4,4,120},
 [79]={56,68,68,68,56},
 [80]={126,18,18,18,12},
 [81]={12,18,18,18,126},
 [82]={124,8,4,4,8},
 [83]={72,84,84,84,36},
 [84]={4,63,68,64,32},
 [85]={60,64,64,32,124},
 [86]={28,32,64,32,28},
 [87]={60,64,48,64,60},
 [88]={68,40,16,40,68},
 [89]={14,80,80,80,62},
 [90]={68,100,84,76,68},
 [91]={0,8,54,65,0},
 [92]={0,0,127,0,0},
 [93]={0,65,54,8,0},
 [94]={8,4,8,8,4},
};

int raw_hud_surface_span(uint32_t width,uint32_t height,uint32_t stride,
    uint32_t format,size_t *span){
    uint32_t f=format&15u,bpp=f==1?3u:2u;uint64_t n;
    if(!span||(width!=320&&width!=400)||height!=240||f<1||f>3||stride<height*bpp)return 0;
    n=(uint64_t)(width-1)*stride+(uint64_t)height*bpp;
    if(n>SIZE_MAX||n>UINT32_MAX)return 0;
    *span=(size_t)n;return 1;
}
static void pixel(const RawHudSurface *s,uint32_t x,uint32_t y,uint32_t color,RawHudCapture *capture){
    uint32_t f=s->format&15u,r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
    uint8_t *p=s->pixels+(size_t)x*s->stride+(239u-y)*(f==1?3u:2u);
    if(capture){uint32_t index=(x-capture->x)*capture->height+y-capture->y;
        uint8_t bit=(uint8_t)(1u<<(index&7));
        if(!(capture->mask[index>>3]&bit)){uint8_t *saved=capture->pixels+index*(f==1?3u:2u);
            if(f!=1&&!(((uintptr_t)p|(uintptr_t)saved)&1u))
                *(uint16_t *)(void *)saved=*(const volatile uint16_t *)(const void *)p;
            else {saved[0]=p[0];saved[1]=p[1];if(f==1)saved[2]=p[2];}
            capture->mask[index>>3]|=bit;capture->saved_pixels++;
        }
    }
    if(f==1){p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r;}
    else {uint16_t v=f==2?(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)):
        (uint16_t)(((r>>3)<<11)|((g>>3)<<6)|((b>>3)<<1)|1u);
        if(!((uintptr_t)p&1u))*(volatile uint16_t *)(void *)p=v;
        else {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}}
}
/* Interior cells cover disjoint pixel positions. Record the original
 * bytes once, then store the final color directly. Clipped or unaligned
 * surfaces retain the general painter below. */
static void cell16(const RawHudSurface *s,const uint8_t *glyph,int32_t gx,int32_t gy,
    uint16_t ink,uint16_t shadow,RawHudCapture *capture){
    uint32_t k,j,previous=0;
    for(k=0;k<6;k++){
        uint32_t foreground=k<5?glyph[k]:0,covered=foreground|(previous<<1);
        if(!covered){previous=foreground;continue;}
        uint16_t *dst=(uint16_t *)(void *)(s->pixels+(size_t)(gx+(int32_t)k)*s->stride+(232u-(uint32_t)gy)*2u);
        uint32_t index=0;uint16_t *saved=0;
        if(capture){index=(uint32_t)(gx+(int32_t)k-(int32_t)capture->x)*capture->height+
            (uint32_t)(gy+7-(int32_t)capture->y);
            saved=(uint16_t *)(void *)(capture->pixels+index*2u);}
        for(j=128u;j;j>>=1,dst++,index--){
            if(covered&j){
                if(capture){*saved=*(const volatile uint16_t *)dst;
                    capture->mask[index>>3]|=(uint8_t)(1u<<(index&7));capture->saved_pixels++;}
                *(volatile uint16_t *)dst=foreground&j?ink:shadow;
            }
            if(capture&&j!=1u)saved--;
        }
        previous=foreground;
    }
}
static void cell24(const RawHudSurface *s,const uint8_t *glyph,int32_t gx,int32_t gy,
    uint32_t color,RawHudCapture *capture){
    uint32_t k,j,previous=0;uint8_t r=(uint8_t)(color>>16),g=(uint8_t)(color>>8),b=(uint8_t)color;
    for(k=0;k<6;k++){
        uint32_t foreground=k<5?glyph[k]:0,covered=foreground|(previous<<1);
        if(!covered){previous=foreground;continue;}
        uint8_t *dst=s->pixels+(size_t)(gx+(int32_t)k)*s->stride+(232u-(uint32_t)gy)*3u,*saved=0;
        uint32_t index=0;
        if(capture){index=(uint32_t)(gx+(int32_t)k-(int32_t)capture->x)*capture->height+
            (uint32_t)(gy+7-(int32_t)capture->y);saved=capture->pixels+index*3u;}
        for(j=128u;j;j>>=1,dst+=3,index--){
            if(covered&j){
                if(capture){saved[0]=dst[0];saved[1]=dst[1];saved[2]=dst[2];
                    capture->mask[index>>3]|=(uint8_t)(1u<<(index&7));capture->saved_pixels++;}
                if(foreground&j){dst[0]=b;dst[1]=g;dst[2]=r;}
                else {dst[0]=0;dst[1]=0;dst[2]=0;}
            }
            if(capture&&j!=1u)saved-=3;
        }
        previous=foreground;
    }
}

static void background(const RawHudSurface *s,uint32_t left,uint32_t top,
    uint32_t right,uint32_t bottom){
    uint32_t f=s->format&15u,height=bottom-top,x,n;
    uint32_t color=RAW_HUD_BACKGROUND,r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
    size_t row_offset=(size_t)(240u-bottom)*(f==1?3u:2u);
    /* Framebuffers store each column contiguously, bottom row first. Encode
     * the fixed background once and fill only this clipped column interval. */
    if(f==1){
        for(x=left;x<right;x++){
            uint8_t *p=s->pixels+(size_t)x*s->stride+row_offset;
            for(n=height;n;n--){p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r;p+=3;}
        }
    }else{
        uint16_t v=f==2?(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)):
            (uint16_t)(((r>>3)<<11)|((g>>3)<<6)|((b>>3)<<1)|1u);
        uint8_t lo=(uint8_t)v,hi=(uint8_t)(v>>8);
        for(x=left;x<right;x++){
            uint8_t *p=s->pixels+(size_t)x*s->stride+row_offset;
            for(n=height;n;n--){p[0]=lo;p[1]=hi;p+=2;}
        }
    }
}
static int paint(const RawHud *h,const RawHudSurface *s,const RawHudLayout *layout,RawHudPaint *out,int transparent,RawHudCapture *capture){
    RawHudLayout l={8,10,RAW_HUD_COLUMNS};RawHudPaint p={0};size_t span;
    int32_t left,top,right,bottom,x,y,gx,gy;uint32_t row,col,k,j,count,f,columns;uint16_t ink=0,shadow=0;
    if(out)memset(out,0,sizeof(*out));
    if(!h)return RAW_HUD_INVALID;
    if(!h->visible||!h->count)return RAW_HUD_SKIPPED;
    if(layout)l=*layout;
    if(!s||!s->pixels||!raw_hud_surface_span(s->width,s->height,s->stride,s->format,&span)||
        s->bytes<span||span>UINTPTR_MAX-(uintptr_t)s->pixels||l.columns<1||l.columns>RAW_HUD_COLUMNS||l.x< -1024||l.x>1024||l.y< -1024||l.y>1024)
        return RAW_HUD_INVALID;
    count=h->count<RAW_HUD_ROWS?h->count:RAW_HUD_ROWS;
    columns=l.columns;
    if(transparent){
        /* A transparent receipt owns only the bounded displayed rows and
           columns. All glyphs and their shadows still fit inside its padding;
           unused rows have no pixels to save or restore. Opaque redraws keep
           their fixed footprint so they continue to clear stale text. */
        columns=0;
        for(row=0;row<count;row++){
            for(col=0;col<l.columns&&col<sizeof(h->line[row])&&h->line[row][col];col++){}
            if(col>columns)columns=col;
        }
    }
    left=l.x<0?0:l.x;top=l.y<0?0:l.y;
    right=l.x+(int32_t)(2*RAW_HUD_PADDING+columns*RAW_HUD_GLYPH_PITCH);
    bottom=l.y+(int32_t)(2*RAW_HUD_PADDING+(transparent?count:RAW_HUD_ROWS)*RAW_HUD_ROW_PITCH);
    if(right>(int32_t)s->width)right=(int32_t)s->width;
    if(bottom>(int32_t)s->height)bottom=(int32_t)s->height;
    if(left>=right||top>=bottom)return RAW_HUD_SKIPPED;
    if(capture){uint32_t pixels=(uint32_t)(right-left)*(uint32_t)(bottom-top),bpp=(s->format&15u)==1?3u:2u;
        if(!capture->pixels||!capture->mask||capture->bytes<(size_t)pixels*bpp||
           capture->mask_bytes<(pixels+7u)/8u)return RAW_HUD_INVALID;
        capture->x=(uint32_t)left;capture->y=(uint32_t)top;
        capture->width=(uint32_t)(right-left);capture->height=(uint32_t)(bottom-top);
        capture->saved_pixels=0;memset(capture->mask,0,(pixels+7u)/8u);
    }
    /* Fixed footprint replaces stale rows on owner-authorized paused redraws.
     * Opaque background is idempotent; repeated draws never keep dimming. */
    if(!transparent)background(s,(uint32_t)left,(uint32_t)top,(uint32_t)right,(uint32_t)bottom);
    f=s->format&15u;shadow=f==3u?1u:0u;
    for(row=0;row<count;row++){
        uint32_t color=h->color[row],r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
        ink=f==2u?(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)):
            (uint16_t)(((r>>3)<<11)|((g>>3)<<6)|((b>>3)<<1)|1u);
        for(col=0;col<l.columns&&col<sizeof(h->line[row]);col++){
        uint8_t c=(uint8_t)h->line[row][col];const uint8_t *glyph;
        if(!c)break;
        if(c<32||c>126)c='?';
        glyph=font[c-32];gx=l.x+(int32_t)RAW_HUD_PADDING+(int32_t)(col*RAW_HUD_GLYPH_PITCH);
        gy=l.y+(int32_t)RAW_HUD_PADDING+(int32_t)(row*RAW_HUD_ROW_PITCH);
        if(transparent&&gx>=left&&gx+5<right&&gy>=top&&gy+7<bottom&&
           (f==1u||!(((uintptr_t)s->pixels|s->stride|
                (capture?(uintptr_t)capture->pixels:0u))&1u))){
            if(f==1u)cell24(s,font_columns[c-32],gx,gy,h->color[row],capture);
            else cell16(s,font_columns[c-32],gx,gy,ink,shadow,capture);
        }else for(j=0;j<7;j++)for(k=0;k<5;k++)if(glyph[j]&(16u>>k)){
            x=gx+(int32_t)k;y=gy+(int32_t)j;
            if(transparent&&x+1>=left&&x+1<right&&y+1>=top&&y+1<bottom)
                pixel(s,(uint32_t)(x+1),(uint32_t)(y+1),0,capture);
            if(x>=left&&x<right&&y>=top&&y<bottom)pixel(s,(uint32_t)x,(uint32_t)y,h->color[row],capture);
        }
        p.glyphs++;
        }
    }
    p.x=(uint32_t)left;p.y=(uint32_t)top;p.width=(uint32_t)(right-left);p.height=(uint32_t)(bottom-top);
    p.first_byte=(size_t)left*s->stride+(239u-(uint32_t)(bottom-1))*((s->format&15u)==1?3u:2u);
    p.end_byte=(size_t)(right-1)*s->stride+(240u-(uint32_t)top)*((s->format&15u)==1?3u:2u);
    if(out)*out=p;
    return RAW_HUD_PAINTED;
}
int raw_hud_paint(const RawHud *h,const RawHudSurface *s,const RawHudLayout *l,RawHudPaint *out){
    return paint(h,s,l,out,0,0);
}
int raw_hud_paint_transparent(const RawHud *h,const RawHudSurface *s,const RawHudLayout *l,RawHudPaint *out){
    return paint(h,s,l,out,1,0);
}
int raw_hud_paint_capture(const RawHud *h,const RawHudSurface *s,const RawHudLayout *l,RawHudPaint *out,RawHudCapture *capture){
    return capture?paint(h,s,l,out,1,capture):RAW_HUD_INVALID;
}
