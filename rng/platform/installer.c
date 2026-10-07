#include "platform.h"
#include <string.h>
static const uint32_t allowed_sites[CH_MAX_INSTALL_POINTS]={CH_RAW_PRE_SITE,CH_POST_SCAN_SITE,CH_MARKER_SITE,CH_SOURCE_SITE,CH_PRESENT_SITE,CH_RESTART_RECEIPT_SITE,CH_MENU_RESET_RECEIPT_SITE};
static const uint32_t pristine_words[CH_MAX_INSTALL_POINTS]={0xeb00332au,0xeb00182du,0xeb000a45u,0xeb000ac7u,0xeb001568u,0xeb01f643u,0xeb0033efu};
static const uint32_t aliases[4]={0x02000000u,0x01f00000u,0x01e00000u,0x01d00000u};

int ch_encode_bl(uint32_t site,uint32_t target,uint32_t *word) {
    int64_t delta=(int64_t)target-(int64_t)site-8;
    if(!word||(site&3u)||(target&3u)||delta<-(INT64_C(1)<<25)||
        delta>=(INT64_C(1)<<25))return 0;
    *word=0xeb000000u|((uint32_t)(delta/4)&0x00ffffffu);return 1;
}
static int valid_point(const ChInstallPoint *p) {
    uint32_t i;
    if((p->bridge_offset&3u)||p->bridge_offset>=CH_ALIAS_PAGE_SIZE)return 0;
    for(i=0;i<CH_MAX_INSTALL_POINTS;i++)if(p->site==allowed_sites[i])
        return p->expected_original==pristine_words[i];
    return 0;
}
static int owned(const ChInstallOps *o) {return o->startup_owned(o->user)==1;}
static int poisoned(ChInstall *s) {s->poisoned=1u;return CH_INSTALL_POISONED;}
static int rollback(ChInstall *s,const ChInstallOps *o) {
    uint32_t i,v;
    if(!owned(o))return poisoned(s);
    /* Include the last attempted store, even if its callback returned failure.
       Only this install's original/owned BL values may be reconciled. */
    for(i=s->attempted_count;i>0u;i--) {
        uint32_t j=i-1u;
        /* A failed store or publication can leave an uncached alias write
           hidden by the original VA's old data-cache line. Refresh that VA
           before deciding whether its actual word is ours or foreign. */
        if(!owned(o)||!o->publish_code(o->user,s->points[j].site,4u)||
            !o->read_word(o->user,s->points[j].site,&v))return poisoned(s);
        if(v==s->installed_words[j]) {
            if(!owned(o)||!o->write_word(o->user,s->points[j].site,
                s->points[j].expected_original)||
                !owned(o)||!o->publish_code(o->user,s->points[j].site,4u)||
                !o->read_word(o->user,s->points[j].site,&v)||
                v!=s->points[j].expected_original)return poisoned(s);
        } else if(v!=s->points[j].expected_original)return poisoned(s);
    }
    for(i=0;i<s->attempted_count;i++) {
        if(!o->publish_code(o->user,s->points[i].site,4u)||
            !o->read_word(o->user,s->points[i].site,&v)||
            v!=s->points[i].expected_original)return poisoned(s);
    }
    s->attempted_count=0u;
    if(s->alias_mapped) {
        if(!owned(o)||!o->unmap_alias(o->user,s->alias_page))return poisoned(s);
        s->alias_mapped=0u;
    }
    return CH_INSTALL_REJECTED;
}
int ch_install_startup(ChInstall *s,const ChInstallOps *o,uint32_t page,
    uint32_t probe_offset,const ChInstallPoint *points,uint32_t count) {
    uint32_t i,j,v,dest=0u,raw=0u;int mapped;
    if(!s||!o||!points||!count||count>CH_MAX_INSTALL_POINTS||
        !o->startup_owned||!o->pristine_identity||!o->free_page||!o->map_alias||
        !o->unmap_alias||!o->read_word||!o->write_word||!o->publish_code||!o->probe_alias||
        s->poisoned||s->alias_mapped||s->attempted_count||s->point_count||
        !page||(page&4095u)||(probe_offset&3u)||probe_offset>4088u||
        !owned(o))return CH_INSTALL_REJECTED;
    for(i=0;i<count;i++) {
        if(!valid_point(&points[i]))return CH_INSTALL_REJECTED;
        for(j=0;j<i;j++)if(points[i].site==points[j].site)return CH_INSTALL_REJECTED;
        if(points[i].site==CH_RAW_PRE_SITE)raw=1u;
    }
    if(!raw||!o->pristine_identity(o->user))return CH_INSTALL_REJECTED;
    for(i=0;i<count;i++)if(!o->read_word(o->user,points[i].site,&v)||
        v!=points[i].expected_original)return CH_INSTALL_REJECTED;
    for(i=0;i<4u;i++)if(o->free_page(o->user,aliases[i])){dest=aliases[i];break;}
    if(!dest)return CH_INSTALL_REJECTED;
    for(i=0;i<count;i++)if(!ch_encode_bl(points[i].site,dest+points[i].bridge_offset,
        &s->installed_words[i]))return CH_INSTALL_REJECTED;
    s->source_page=page;s->alias_page=dest;s->point_count=count;
    memcpy(s->points,points,count*sizeof(*points));
    if(!owned(o))return CH_INSTALL_REJECTED;
    mapped=o->map_alias(o->user,dest,page);
    if(mapped<0)return poisoned(s);
    if(!mapped){s->point_count=0u;return CH_INSTALL_REJECTED;}
    s->alias_mapped=1u;
    if(!o->publish_code(o->user,dest,4096u)||
        !o->probe_alias(o->user,dest+probe_offset))return rollback(s,o);
    for(i=0;i<count;i++) {
        if(!owned(o)||!o->read_word(o->user,points[i].site,&v)||
            v!=points[i].expected_original)return rollback(s,o);
        s->attempted_count=i+1u;
        if(!o->write_word(o->user,points[i].site,s->installed_words[i])||
            !owned(o)||!o->publish_code(o->user,points[i].site,4u)||
            !o->read_word(o->user,points[i].site,&v)||v!=s->installed_words[i])
            return rollback(s,o);
    }
    for(i=0;i<count;i++)if(!owned(o)||
        !o->publish_code(o->user,points[i].site,4u)||
        !o->read_word(o->user,points[i].site,&v)||v!=s->installed_words[i])
        return rollback(s,o);
    return CH_INSTALL_OK;
}
