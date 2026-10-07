/* Exercise the actual seven-site startup transaction, including ambiguous
   failure of the new reset receipt store and rollback of all earlier sites. */
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define REQUIRE(c) do{checks++;if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static const uint32_t sites[CH_MAX_INSTALL_POINTS]={CH_RAW_PRE_SITE,CH_POST_SCAN_SITE,CH_MARKER_SITE,CH_SOURCE_SITE,CH_PRESENT_SITE,CH_RESTART_RECEIPT_SITE,CH_MENU_RESET_RECEIPT_SITE};
static const uint32_t original[CH_MAX_INSTALL_POINTS]={0xeb00332au,0xeb00182du,0xeb000a45u,0xeb000ac7u,0xeb001568u,0xeb01f643u,0xeb0033efu};
typedef struct{uint32_t words[CH_MAX_INSTALL_POINTS],mapped,maps,unmaps,writes,failed_store,fail_index,owned;} Fixture;
static int ownership(void *u){return (int)((Fixture *)u)->owned;}
static int identity(void *u){(void)u;return 1;}
static int free_page(void *u,uint32_t a){(void)u;return a==0x02000000u;}
static int map_alias(void *u,uint32_t a,uint32_t p){Fixture *f=u;REQUIRE(a==0x02000000u&&p==0x07010000u);f->maps++;f->mapped=a;return 1;}
static int unmap_alias(void *u,uint32_t a){Fixture *f=u;REQUIRE(a==f->mapped);f->unmaps++;f->mapped=0;return 1;}
static int read_word(void *u,uint32_t a,uint32_t *v){Fixture *f=u;unsigned i;for(i=0;i<CH_MAX_INSTALL_POINTS;i++)if(a==sites[i]){*v=f->words[i];return 1;}return 0;}
static int write_word(void *u,uint32_t a,uint32_t v){Fixture *f=u;unsigned i;
    for(i=0;i<CH_MAX_INSTALL_POINTS;i++) {
        if(a==sites[i]){f->writes++;f->words[i]=v;
            if(i==f->fail_index&&!f->failed_store&&v!=original[i]){f->failed_store=1;return 0;}
            return 1;
        }
    }
    return 0;
}
static int publish(void *u,uint32_t a,uint32_t n){Fixture *f=u;(void)a;return f->owned&&n;}
static int probe(void *u,uint32_t a){Fixture *f=u;return a==f->mapped+128u;}
static void init(Fixture *f,ChInstall *s,ChInstallPoint p[CH_MAX_INSTALL_POINTS]){unsigned i;memset(f,0,sizeof(*f));memset(s,0,sizeof(*s));
    f->owned=1;f->fail_index=UINT32_MAX;memcpy(f->words,original,sizeof(original));
    for(i=0;i<CH_MAX_INSTALL_POINTS;i++)p[i]=(ChInstallPoint){sites[i],original[i],0};
}
int main(void){Fixture f;ChInstall s;ChInstallPoint p[CH_MAX_INSTALL_POINTS];unsigned i,k;uint32_t encoded;
    ChInstallOps o={&f,ownership,identity,free_page,map_alias,unmap_alias,read_word,write_word,publish,probe};
    init(&f,&s,p);
    REQUIRE(ch_install_startup(&s,&o,0x07010000u,128u,p,CH_MAX_INSTALL_POINTS)==CH_INSTALL_OK);
    REQUIRE(s.point_count==CH_MAX_INSTALL_POINTS&&s.alias_mapped&&f.maps==1u&&!f.unmaps);
    for(i=0;i<CH_MAX_INSTALL_POINTS;i++){REQUIRE(ch_encode_bl(sites[i],f.mapped,&encoded));REQUIRE(f.words[i]==encoded);}
    /* Every store, including the seventh, may report failure after committing. */
    for(k=0;k<CH_MAX_INSTALL_POINTS;k++){init(&f,&s,p);f.fail_index=k;
        REQUIRE(ch_install_startup(&s,&o,0x07010000u,128u,p,CH_MAX_INSTALL_POINTS)==CH_INSTALL_REJECTED);
        REQUIRE(f.failed_store&&f.maps==1u&&f.unmaps==1u&&!f.mapped&&!s.poisoned);
        for(i=0;i<CH_MAX_INSTALL_POINTS;i++)REQUIRE(f.words[i]==original[i]);
    }
    for(k=5u;k<CH_MAX_INSTALL_POINTS;k++){
        init(&f,&s,p);p[k].expected_original^=1u;
        REQUIRE(ch_install_startup(&s,&o,0x07010000u,128u,p,CH_MAX_INSTALL_POINTS)==CH_INSTALL_REJECTED);REQUIRE(!f.maps&&!f.writes);
        init(&f,&s,p);f.words[k]^=1u;
        REQUIRE(ch_install_startup(&s,&o,0x07010000u,128u,p,CH_MAX_INSTALL_POINTS)==CH_INSTALL_REJECTED);REQUIRE(!f.maps&&!f.writes);
        init(&f,&s,p);p[k].site=CH_RAW_PRE_SITE;p[k].expected_original=original[0];
        REQUIRE(ch_install_startup(&s,&o,0x07010000u,128u,p,CH_MAX_INSTALL_POINTS)==CH_INSTALL_REJECTED);REQUIRE(!f.maps&&!f.writes);
    }
    printf("passed: %u seven-site installer/rollback checks\n",checks);return 0;
}
