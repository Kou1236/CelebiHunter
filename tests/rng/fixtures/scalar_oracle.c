#include "inverse_frontier.h"
#include "profile_data.h"
#include <stdio.h>
#include <string.h>

static RawInverseWorkspace inverse;
static Sol61Frontier actual,expected,saved;
static unsigned checks,seeds,shiny_pairs,branch_coverage[3];
static uint32_t prefix_add[256],prefix_sub[256];
static const uint8_t scalar_offsets[2][3][4][2]={
    {{{254,254},{3,4},{6,6}},{{254,254},{0,0},{6,6},{8,8}},{{254,254},{0,0},{6,6},{8,8}}},
    {{{254,255},{4,4},{6,6}},{{254,255},{1,1},{6,6},{8,8}},{{254,255},{1,1},{6,6},{8,8}}}
};
#define CHECK(x) do{checks++;if(!(x)){printf("BG inverse check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t sum_at(uint32_t side,uint32_t n){
    uint32_t bit,sum=0;
    for(bit=0;bit<17u;bit++){
        uint32_t p=sol61_sum_roots[side][bit];
        while(p>1u){const Sol61BaseNode *v=&sol61_base_nodes[p];p=(n&(1u<<(8u-v->variable)))?v->high:v->low;}
        sum|=p<<bit;
    }
    return sum;
}
static uint32_t random_pair(uint32_t *a,uint32_t *s,uint32_t first,uint32_t second){
    uint32_t carry=*a+first;*a=carry&255u;*s=(*s-second-(carry>>8))&255u;return *s;
}
static void scalar_oracle(uint32_t seed_a,uint32_t seed_s,uint32_t bg){
    uint32_t n,x,branch;
    uint32_t a1=(seed_a-165u)&255u,s1=(seed_s+166u+((a1+165u)>>8))&255u;
    memset(&expected,0,sizeof(expected));
    for(n=2;n<=507u;n++){
        uint32_t waiting_total=a1+sum_at(0,n),wa=waiting_total&255u;
        uint32_t ws=(s1-sum_at(1,n)-(waiting_total>>8))&255u;
        uint32_t target=(bg+(n-2u)%3u)%3u,axis=target==2u?0u:1u;
        for(x=0;x<256u;x++)for(branch=0;branch<3u;branch++){
            uint32_t wide=wa+prefix_add[x],a=wide&255u,s=(ws-prefix_sub[x]-(wide>>8))&255u;
            uint32_t i,count=branch==0u?3u:4u,values[4],chosen,dv;
            for(i=0;i<count;i++)values[i]=random_pair(&a,&s,
                (x+scalar_offsets[axis][branch][i][0])&255u,
                (x+scalar_offsets[axis][branch][i][1])&255u);
            chosen=values[0]<192u?0u:values[1]<20u?2u:1u;
            if(chosen!=branch)continue;branch_coverage[branch]++;
            dv=(values[count-2u]<<8)|values[count-1u];
            if((dv&0xfffu)!=0xaaau||!(dv&0x2000u))continue;
            {Sol61FrontierEntry *e=&expected.entries[n];e->branch_pairs[branch]++;shiny_pairs++;
                if(!e->found||x<e->div_x||(x==e->div_x&&branch<e->branch)){
                    e->found=1;e->div_x=(uint16_t)x;e->branch=(uint8_t)branch;e->predicted_dv=(uint16_t)dv;
                    e->expected_wait_add=(uint8_t)wa;e->expected_wait_sub=(uint8_t)ws;
                }
            }
        }
    }
}
int main(void){
    uint32_t k,x,side,offset,bg,n,random=0x17ba612u;Sol61Result p;
    /* Expand the certified DIV operand multiset by scalar addition; neither
       inverse shiny equations nor a solved candidate table is used here. */
    for(x=0;x<256u;x++)for(side=0;side<2u;side++){
        uint32_t total=0;
        for(offset=0;offset<256u;offset++)for(k=0;k<sol61_prefix_hist[side][offset];k++)total+=(x+offset)&255u;
        if(side)prefix_sub[x]=total;else prefix_add[x]=total;
    }
    for(k=0;k<32u;k++){
        uint32_t a,s;
        random=random*1664525u+1013904223u;a=k<8u?k:(random>>16)&255u;
        random=random*1664525u+1013904223u;s=k<8u?255u-k:(random>>16)&255u;
        for(bg=0;bg<3u;bg++){
            CHECK(!raw_inverse_frontier_bg(a,s,bg,&inverse,&actual));
            CHECK(actual.complete==1u&&actual.abi==RAW_BG_FRONTIER_ABI&&actual.algebraic_x_cases==96u&&inverse.waiting_pairs==506u);
            for(x=0;x<256u;x++)CHECK(inverse.prefix_sums[0][x]==prefix_add[x]&&inverse.prefix_sums[1][x]==prefix_sub[x]);
            scalar_oracle(a,s,bg);
            for(n=0;n<508u;n++)CHECK(!memcmp(&actual.entries[n],&expected.entries[n],sizeof(actual.entries[n])));
            CHECK(!raw_inverse_frontier_select_bg(a,s,bg,2u,&inverse,&actual,&p));
            CHECK(raw_inverse_frontier_select_bg(a,s,(bg+1u)%3u,2u,&inverse,&actual,&p)==2);
            CHECK(raw_inverse_frontier_select_bg(a,s,3u,2u,&inverse,&actual,&p)==2);
            CHECK(sol61_frontier_select(a,s,2u,&actual,&p)==2); /* cannot use historical selector */
            saved=actual;actual.entries[507].expected_wait_add^=1u;
            CHECK(raw_inverse_frontier_select_bg(a,s,bg,2u,&inverse,&actual,&p)==2);actual=saved;
            inverse.source_bg=(bg+1u)%3u;
            CHECK(raw_inverse_frontier_select_bg(a,s,bg,2u,&inverse,&actual,&p)==2);
            seeds++;
        }
    }
    CHECK(raw_inverse_frontier_bg(0,0,3,&inverse,&actual)==2&&!actual.complete);
    CHECK(raw_inverse_frontier_bg(256,0,0,&inverse,&actual)==2&&!actual.complete);
    CHECK(raw_inverse_target_bg(3,2)==UINT32_MAX&&raw_inverse_target_bg(0,1)==UINT32_MAX&&raw_inverse_target_bg(0,508)==UINT32_MAX);
    CHECK(branch_coverage[0]&&branch_coverage[1]&&branch_coverage[2]);
    printf("BG inverse passed: %u seed/phase combinations, 506 slots x256 DIV x3 branches; %u exact/corruption checks; %u shiny pairs, branches %u/%u/%u\n",
        seeds,checks,shiny_pairs,branch_coverage[0],branch_coverage[1],branch_coverage[2]);return 0;
}
