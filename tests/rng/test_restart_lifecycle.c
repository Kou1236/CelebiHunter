/* Reuse SDK-only boundaries, compiling the actual device/service coordinator.
   Its normal waiting scenarios remain a separately run regression group. */
#define WAITING_DEVICE_MAIN waiting_regressions_main
#include "test_waiting_device.c"
#undef WAITING_DEVICE_MAIN
typedef struct {uint32_t counter,mode,gate,drift,bad_word,failed,reads;
    uint32_t caller_lr,menu_state,caller_drift,state_drift,caller_reads,state_reads,reset_nonzero;
    uint32_t caller_r4,gui_callback,owner_drift,callback_drift,owner_reads,callback_reads;} RestartFixture;
static const uint32_t gui_fixture_code[][2]={
    {0x10347cu,0xe59f10b8u},{0x103480u,0xe59f0048u},{0x103484u,0xeb00224fu},
    {0x1034d0u,0x2da1a0u},{0x10353cu,0x18588cu},
    {0x10bdc8u,0xe2800b05u},{0x10bdccu,0xe2800f66u},{0x10bdd0u,0xe5801000u},
    {0x144238u,0xe92d4010u},{0x14423cu,0xe1a04000u},{0x144240u,0xe2800b05u},
    {0x144244u,0xe2800f66u},{0x144248u,0xe5900000u},{0x144254u,0xe12fff30u}
};
static RestartFixture restart_fixture;
static int restart_read(void *u,uint32_t a,void *out,uint32_t n){RestartFixture *f=u;uint32_t v=0,i;
    f->reads++;if(f->failed&&f->reads==f->failed)return 0;
    if(a==RAW_FRONTEND_MODE_ADDRESS&&n==1u){*(uint8_t *)out=(uint8_t)f->mode;return 1;}
    if(a==RAW_FRONTEND_SCAN_GATE_ADDRESS&&n==1u){*(uint8_t *)out=(uint8_t)f->gate;return 1;}
    if(n!=4u)return 0;
    if(a==0x10ed48u)v=f->bad_word?0u:0xebfffb42u;
    else if(a==0x10da78u)v=0xe5801140u;
    else if(a==0x18588cu)v=f->bad_word==0x18588cu?0u:0xe92d4010u;
    else if(a==0x10eba8u)v=f->bad_word==0x10eba8u?0u:0xeb01db37u;
    else if(a==0x1858b4u)v=f->bad_word==0x1858b4u?0u:0xe5801140u;
    else if(a==0x185900u)v=f->bad_word==0x185900u?0u:0x27be7cu;
    else if(a==0x08000100u)v=f->caller_r4+(f->owner_drift&&++f->owner_reads>1u);
    else if(a==0x08000104u)v=f->caller_lr+(f->caller_drift&&++f->caller_reads>1u);
    else if(a==RAW_GUI_RESET_CALLBACK_ADDRESS)v=f->gui_callback+(f->callback_drift&&++f->callback_reads>1u);
    else if(a==RAW_FRONTEND_MENU_STATE_ADDRESS)v=f->menu_state+(f->state_drift&&++f->state_reads>1u);
    else if(a==0x10daa4u||a==CH_ENGINE_POINTER_ADDRESS)v=0x27be7cu;
    else if(a==0x27bfbcu)v=f->counter+(f->drift&&f->reads>11u);
    else if(a==0x27bfc0u||a==0x27bfc4u||a==0x27bfc8u||a==0x27bfecu)v=f->reset_nonzero==a;
    else {for(i=0u;i<sizeof(gui_fixture_code)/sizeof(gui_fixture_code[0]);i++)if(a==gui_fixture_code[i][0])break;
        if(i==sizeof(gui_fixture_code)/sizeof(gui_fixture_code[0]))return 0;
        v=f->bad_word==a?0u:gui_fixture_code[i][1];}
    for(i=0;i<4u;i++)((uint8_t *)out)[i]=(uint8_t)(v>>(8u*i));return 1;
}
static void restart_setup(void){RawRuntime *r;
    reset();r=&ch_raw_service.runtime;memset(&restart_fixture,0,sizeof(restart_fixture));
    restart_fixture.mode=8u;restart_fixture.caller_lr=0x10ebacu;restart_fixture.menu_state=21u;
    restart_fixture.caller_r4=RAW_GUI_RESET_OWNER_ADDRESS;restart_fixture.gui_callback=0x18588cu;
    device.binding.read=(ChReadOps){&restart_fixture,restart_read,0};
    ch_raw_service.ops.restart_session=restart_session;
    r->fault=RAW_FAULT_ORDER;r->controls.fault=CH_FAULT_RUNTIME_FAILED;
    r->encounter_started=r->actual_seen=r->actual_raw_press_seen=1u;
    device.actual_result.complete=1u;device.source_diagnostic.closed=1u;
    device.environment.generation=6u;device.refresh_attempted=1u;
    device.waiting_observation_valid=1u;ch_raw_service.pending_marker=1u;
    raw_mailbox_init(&device.mailbox);backend_ok=1;restore_status=RAW_ENV_OK;
    worker_read_ok=1;expected_sample_epoch=0u;
}
static uint32_t restart_route(void){ChNativeContext c={0};c.lr=RAW_RESTART_RECEIPT_LR;
    return raw_service_route(&ch_raw_service,&c);
}
static uint32_t menu_reset_route(void){ChNativeContext c={0};
    c.lr=RAW_MENU_RESET_RECEIPT_LR;c.original_sp=0x08000100u;
    return raw_service_route(&ch_raw_service,&c);
}
static void completed_restart_retires_resources(void){RawRuntime *r;RawJob old={0},running,new_job={0},received;
    ManualPrediction p={0},received_result;uint32_t keys_mask=0;
    restart_setup();r=&ch_raw_service.runtime;
    old.token.query_id=23u;old.source.source_epoch=77u;
    CHECK(raw_mailbox_submit(&device.mailbox,&old));CHECK(raw_mailbox_take(&device.mailbox,&running));
    old.token.query_id=24u;CHECK(raw_mailbox_submit(&device.mailbox,&old));
    CHECK(restart_route()==0x18c670u);
    CHECK(device.restart_receipts==1u&&device.binding.source_epoch==78u&&r->scene_epoch==78u);
    CHECK(restore_calls==1u&&entered==1u&&left==1u&&!device.environment_backend.active);
    CHECK(!device.environment.state.rtc_owned&&!device.environment.state.initial_applied&&!device.environment.generation);
    CHECK(!device.source_available&&!device.source_diagnostic.closed&&!device.refresh_attempted);
    CHECK(!device.actual_result.complete&&!device.result_gate.abi&&!device.waiting_observation_valid);
    CHECK(!r->fault&&!r->controls.fault&&!r->encounter_started&&!r->actual_seen&&!r->source_bound);
    CHECK(!r->controls.active_query.query_id&&!ch_raw_service.pending_marker);
    CHECK(device.mailbox.running&&!device.mailbox.pending_valid&&!device.mailbox.ready_valid);
    CHECK(device.mailbox.minimum_epoch==78u&&!raw_mailbox_submit(&device.mailbox,&old));
    new_job.token.query_id=25u;new_job.source.source_epoch=78u;
    CHECK(raw_mailbox_submit(&device.mailbox,&new_job));
    CHECK(!raw_mailbox_take(&device.mailbox,&received));
    CHECK(!raw_mailbox_finish(&device.mailbox,&running,&p));
    CHECK(!device.mailbox.running&&!raw_mailbox_receive(&device.mailbox,&received,&received_result));
    CHECK(raw_mailbox_take(&device.mailbox,&received)&&received.token.query_id==25u);
    CHECK(raw_mailbox_finish(&device.mailbox,&received,&p));
    CHECK(raw_mailbox_receive(&device.mailbox,&received,&received_result)&&received.source.source_epoch==78u);
    fixture_sample.counter=0u;fixture_sample.scene_epoch=78u;fixture_sample.script_final_prompt=0u;
    CHECK(raw_platform_keys(&device.binding,&keys_mask));
    raw_runtime_before_scan(r,&fixture_sample);raw_runtime_poll_player(r,keys_mask,&fixture_sample);
    CHECK(!r->fault&&r->runtime==CH_RUNTIME_RUNNING&&!r->search_started);
    CHECK(r->restart_bootstrap_pending&&!r->controls.initialized_scene);
    raw_runtime_begin_scan(r,&fixture_sample);raw_runtime_after_scan(r,&fixture_sample);
    raw_runtime_engine_entry(r,&fixture_sample);fixture_sample.counter=1u;
    raw_runtime_before_scan(r,&fixture_sample);raw_runtime_poll_player(r,0,&fixture_sample);
    CHECK(!r->restart_bootstrap_pending&&!r->fault&&r->controls.last_counter==1u);
    fixture_sample.script_final_prompt=1u;raw_runtime_before_scan(r,&fixture_sample);raw_runtime_poll_player(r,0,&fixture_sample);
    CHECK(r->search_started&&r->search_requested&&!r->fault);
}
static void lifecycle_route(uint32_t lr){ChNativeContext c={0};c.lr=lr;
    CHECK(raw_service_route(&ch_raw_service,&c)==raw_original_target(lr));
}
static void lifecycle_pre(uint32_t mode,uint32_t ordinary,uint32_t counter){
    fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=mode;fixture_sample.frontend_scan_gate=0u;
    fixture_sample.scene_epoch=78u;fixture_sample.script_final_prompt=0u;
    fixture_sample.ordinary_supported=ordinary;fixture_sample.counter=counter;
    lifecycle_route(0x1042f4u);
}
static void lifecycle_complete_scan(void){
    lifecycle_route(0x104378u);lifecycle_route(0x1a82e0u);lifecycle_route(0x1a8340u);
}
static void restarted_menu_loading_play_baseline(void){RawRuntime *r;uint32_t n;
    restart_setup();r=&ch_raw_service.runtime;CHECK(restart_route()==0x18c670u);
    for(n=0;n<3u;n++){
        lifecycle_pre(n==0u?8u:n==1u?10u:9u,0u,50000u-n*100u);
        CHECK(r->frontend_suspended&&r->restart_bootstrap_pending&&!r->scan_open&&!r->controls.initialized_scene);
        CHECK(!r->fault&&!r->search_started&&!r->controls.active_query.query_id);
    }
    /* Real pre/post scan calls can precede ordinary gameplay while the new
       engine loads. Their provisional counters must not become a control
       baseline, even when the final loading operation initializes it to 0. */
    lifecycle_pre(1u,0u,40000u);CHECK(r->scan_open);lifecycle_complete_scan();
    CHECK(r->restart_bootstrap_pending&&!r->fault&&!r->controls.initialized_scene);
    lifecycle_pre(1u,0u,0u);lifecycle_complete_scan();
    CHECK(r->restart_bootstrap_pending&&!r->fault&&!r->search_started);
    lifecycle_pre(1u,1u,0u);CHECK(r->restart_bootstrap_pending);lifecycle_complete_scan();
    lifecycle_pre(1u,1u,1u);
    CHECK(!r->restart_bootstrap_pending&&!r->fault&&r->controls.initialized_scene&&r->controls.last_counter==1u);
    lifecycle_complete_scan();lifecycle_pre(1u,1u,2u);
    CHECK(!r->fault&&r->controls.last_counter==2u&&r->runtime==CH_RUNTIME_RUNNING);
    lifecycle_complete_scan();lifecycle_pre(1u,1u,0u);
    CHECK(r->fault==RAW_FAULT_ORDER&&r->controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
    CHECK(!r->restart_bootstrap_pending&&!r->source_bound&&!r->controls.candidate_valid);
    /* Read and call-order failures cannot be hidden by a checked restart. */
    restart_setup();r=&ch_raw_service.runtime;CHECK(restart_route()==0x18c670u);
    lifecycle_pre(1u,1u,0u);lifecycle_pre(1u,1u,0u);
    CHECK(r->fault==RAW_FAULT_ORDER&&r->restart_bootstrap_pending);
    restart_setup();r=&ch_raw_service.runtime;CHECK(restart_route()==0x18c670u);
    fixture_sample.sample_complete=0u;lifecycle_route(0x1042f4u);
    CHECK(r->fault==RAW_FAULT_READ&&r->restart_bootstrap_pending);
    /* Every receipt of the settling unit must be ordinary. A good pre-HID
       read cannot certify an unsupported post-HID or engine-marker sample. */
    for(n=0u;n<2u;n++){
        restart_setup();r=&ch_raw_service.runtime;CHECK(restart_route()==0x18c670u);
        lifecycle_pre(1u,1u,0u);
        if(n==0u)fixture_sample.ordinary_supported=0u;
        lifecycle_route(0x104378u);
        fixture_sample.ordinary_supported=n==0u?1u:0u;
        lifecycle_route(0x1a82e0u);lifecycle_route(0x1a8340u);
        lifecycle_pre(1u,1u,1u);CHECK(r->restart_bootstrap_pending&&!r->controls.initialized_scene&&!r->fault);
        lifecycle_complete_scan();lifecycle_pre(1u,1u,2u);
        CHECK(!r->restart_bootstrap_pending&&r->controls.last_counter==2u&&!r->fault);
    }
}
static void confirmed_reset_routes_after_native_clear(int gui_worker){RawRuntime *r;
    restart_setup();r=&ch_raw_service.runtime;r->fault=0u;r->controls.fault=0u;
    if(gui_worker){restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;restart_fixture.menu_state=0u;}
    device.environment.terminal_attempted=1u;
    fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=1u;
    fixture_sample.frontend_scan_gate=0u;fixture_sample.script_final_prompt=0u;
    fixture_sample.scene_epoch=77u;fixture_sample.counter=70000u;
    lifecycle_route(0x1042f4u);lifecycle_complete_scan();
    CHECK(r->controls.initialized_scene&&r->controls.last_counter==70000u&&!r->fault);
    fixture_sample.frontend_mode=8u;fixture_sample.counter=70001u;
    lifecycle_route(0x1042f4u);
    CHECK(r->frontend_suspended&&!r->scan_open&&r->controls.last_counter==70000u);
    /* The ordinary touchscreen Yes exits its animation and starts144238:
       GUI+1598 -> BLX18588C -> saved r4=GUI/caller144258 -> counter clear.
       Debug Menu Reset separately uses caller10EBAC/state21. The same
       installed1858E4 route must admit either confirmed source. */
    CHECK(menu_reset_route()==0x1928a8u);
    if(gui_worker){
        CHECK(ch_raw_service.restart_pending&&device.restart_notice.valid);
        CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&r->scene_epoch==77u);
        CHECK(!restore_calls&&r->controls.last_counter==70000u&&device.environment.state.rtc_owned);
        /* The native worker has returned: its saved stack/counter evidence is
           now gone. Only the captured receipt is consumed on the main caller. */
        restart_fixture.caller_lr=0xdeadbeefu;restart_fixture.counter=17u;restart_fixture.failed=1u;
        fixture_sample.frontend_mode=8u;fixture_sample.scene_epoch=78u;
        expected_sample_epoch=78u;lifecycle_route(0x1042f4u);expected_sample_epoch=0u;
        CHECK(!ch_raw_service.restart_pending&&!device.restart_notice.valid);
    }
    CHECK(device.restart_receipts==1u&&device.binding.source_epoch==78u&&r->scene_epoch==78u);
    CHECK(restore_calls==1u&&!device.environment.state.rtc_owned&&!ch_raw_service.pending_marker);
    lifecycle_pre(1u,1u,0u);CHECK(r->restart_bootstrap_pending);lifecycle_complete_scan();
    lifecycle_pre(1u,1u,1u);
    CHECK(!r->restart_bootstrap_pending&&!r->fault&&r->controls.last_counter==1u);
    lifecycle_complete_scan();lifecycle_pre(1u,1u,2u);
    CHECK(!r->fault&&r->controls.last_counter==2u);
    lifecycle_complete_scan();lifecycle_pre(1u,1u,0u);
    CHECK(r->controls.fault==CH_FAULT_COUNTER_DISCONTINUITY&&r->fault==RAW_FAULT_ORDER);
    /* A second confirmed native Reset is a new receipt, including recovery
       from the genuine discontinuity just observed in the preceding epoch. */
    if(gui_worker){restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;
        restart_fixture.counter=restart_fixture.failed=restart_fixture.reads=0u;}
    CHECK(menu_reset_route()==0x1928a8u);
    if(gui_worker){
        CHECK(ch_raw_service.restart_pending&&device.restart_receipts==1u&&r->fault==RAW_FAULT_ORDER);
        fixture_sample.frontend_mode=8u;fixture_sample.scene_epoch=79u;
        expected_sample_epoch=79u;lifecycle_route(0x1042f4u);expected_sample_epoch=0u;
        CHECK(!ch_raw_service.restart_pending&&!r->fault);
    }
    CHECK(device.restart_receipts==2u&&device.binding.source_epoch==79u&&r->scene_epoch==79u);
    fixture_sample.frontend_mode=1u;fixture_sample.scene_epoch=79u;fixture_sample.counter=0u;
    lifecycle_route(0x1042f4u);lifecycle_complete_scan();
    fixture_sample.counter=1u;lifecycle_route(0x1042f4u);
    CHECK(!r->fault&&!r->controls.fault&&!r->restart_bootstrap_pending&&r->controls.last_counter==1u);
}
static void cancelled_menu_reset_keeps_session(void){RawRuntime *r;
    restart_setup();r=&ch_raw_service.runtime;r->fault=0u;r->controls.fault=0u;
    device.environment.terminal_attempted=1u;
    fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=1u;
    fixture_sample.frontend_scan_gate=0u;fixture_sample.script_final_prompt=0u;
    fixture_sample.scene_epoch=77u;fixture_sample.counter=45000u;
    lifecycle_route(0x1042f4u);lifecycle_complete_scan();
    fixture_sample.frontend_mode=8u;fixture_sample.counter=45001u;
    lifecycle_route(0x1042f4u);
    CHECK(r->frontend_suspended&&r->controls.last_counter==45000u);
    /* Cancel returns from the original menu without entering 18588C. */
    fixture_sample.frontend_mode=1u;lifecycle_route(0x1042f4u);
    CHECK(!r->fault&&r->controls.last_counter==45001u&&r->scene_epoch==77u);
    CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&!restore_calls);
    CHECK(device.environment.state.rtc_owned&&!device.mailbox.minimum_epoch);
}
static void menu_reset_provenance_is_required(void){ChNativeContext c={0};uint32_t n;
    const uint32_t changed_code[]={0x18588cu,0x10eba8u,0x1858b4u,0x185900u};
    const uint32_t reset_words[]={0x27bfc0u,0x27bfc4u,0x27bfc8u,0x27bfecu};
    for(n=0;n<17u;n++){
        restart_setup();c.lr=RAW_MENU_RESET_RECEIPT_LR;c.original_sp=0x08000100u;
        if(n==0u)restart_fixture.caller_lr=0x109298u; /* Unconfirmed native caller. */
        if(n==1u)restart_fixture.menu_state=20u;
        if(n==2u)restart_fixture.caller_drift=1u;
        if(n==3u)restart_fixture.state_drift=1u;
        if(n==4u)c.original_sp=0u;
        if(n==5u)c.original_sp|=1u;
        if(n==6u)c.original_sp=0xfffffffcu;
        if(n>=7u&&n<11u)restart_fixture.bad_word=changed_code[n-7u];
        if(n>=11u&&n<15u)restart_fixture.reset_nonzero=reset_words[n-11u];
        if(n==15u)restart_fixture.counter=1u;
        if(n==16u)restart_fixture.mode=13u;
        CHECK(!raw_restart_receipt(&device.binding.read,&c));
        CHECK(raw_service_route(&ch_raw_service,&c)==0x1928a8u);
        CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&!restore_calls);
    }
    /* Proven in-function Reset remains valid even if the general frontend
       scan gate already permits its next gameplay visit. */
    restart_setup();restart_fixture.mode=1u;restart_fixture.gate=0u;
    CHECK(menu_reset_route()==0x1928a8u&&device.restart_receipts==1u);
}
static void touchscreen_reset_provenance_is_required(void){ChNativeContext c={0};uint32_t n;
    for(n=0u;n<20u;n++){
        restart_setup();c.lr=RAW_MENU_RESET_RECEIPT_LR;c.original_sp=0x08000100u;
        restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;restart_fixture.menu_state=0u;
        if(n==0u)restart_fixture.caller_r4++;
        if(n==1u)restart_fixture.gui_callback=0x10da58u; /* Interruption Load callback is a different field. */
        if(n==2u)restart_fixture.owner_drift=1u;
        if(n==3u)restart_fixture.callback_drift=1u;
        if(n==4u)restart_fixture.caller_drift=1u;
        if(n==5u)restart_fixture.counter=1u;
        if(n>=6u)restart_fixture.bad_word=gui_fixture_code[n-6u][0];
        CHECK(!raw_restart_receipt(&device.binding.read,&c));
        CHECK(raw_service_route(&ch_raw_service,&c)==0x1928a8u);
        CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&!restore_calls);
    }
    /* Both native GUI Yes branches share this exact callback worker. They
       need no debug-menu state21 and retain the general frontend bounds. */
    restart_setup();restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;restart_fixture.menu_state=0u;
    CHECK(raw_restart_receipt(&device.binding.read,&c));
    restart_setup();restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;
    restart_fixture.mode=1u;restart_fixture.gate=0u;
    CHECK(raw_restart_receipt(&device.binding.read,&c));
}
static void touchscreen_notice_is_main_thread_owned(void){RawRuntime before,*r;Ch3dsBackend backend;
    ManualSourceBinding binding={0};RawJob old={0},running,received_job;ManualPrediction p={0},received_p;
    ChNativeContext c={0};uint64_t generation=0u;uint32_t n,old_reads,old_draws,old_received;
    restart_setup();r=&ch_raw_service.runtime;
    restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;restart_fixture.menu_state=0u;
    before=*r;device.backend.scoped_read_calls=123u;backend=device.backend;
    old.source.source_epoch=77u;old.token.query_id=23u;
    CHECK(raw_mailbox_submit(&device.mailbox,&old)&&raw_mailbox_take(&device.mailbox,&running));
    CHECK(menu_reset_route()==0x1928a8u);
    CHECK(!memcmp(&before,r,sizeof(before))&&!memcmp(&backend,&device.backend,sizeof(backend)));
    CHECK(ch_raw_service.restart_pending&&device.restart_notice.source_epoch==77u&&
        device.restart_notice.target_epoch==78u&&!device.restart_receipts&&!restore_calls);
    old_reads=restart_fixture.reads;old_draws=drawn;old_received=received;
    for(n=0u;n<3u;n++)lifecycle_route(n==0u?0x104378u:n==1u?0x1a82e0u:0x1a8340u);
    CHECK(restart_fixture.reads==old_reads&&drawn==old_draws&&received==old_received);
    CHECK(!memcmp(&before,r,sizeof(before))&&ch_raw_service.pending_marker);
    /* Acquiring the source lock after publication still rejects old writers,
       even if their service route had passed its earlier pending check. */
    c.lr=0x1a8340u;CHECK(!source(&device,&c,&binding,&generation));
    CHECK(!first(&device,&c,r));completed(&device,&c,r,1u);
    CHECK(!entered&&!restore_calls&&!memcmp(&before,r,sizeof(before)));
    /* Duplicate callback cannot overwrite an already captured epoch. */
    CHECK(menu_reset_route()==0x1928a8u&&restart_fixture.reads==old_reads);
    restart_fixture.failed=1u;restart_fixture.reads=0u;restart_fixture.caller_lr=0xdeadbeefu;
    fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=8u;fixture_sample.scene_epoch=78u;
    expected_sample_epoch=78u;lifecycle_route(0x1042f4u);expected_sample_epoch=0u;
    CHECK(device.restart_receipts==1u&&!ch_raw_service.restart_pending&&!device.restart_notice.valid);
    CHECK(restore_calls==1u&&device.mailbox.minimum_epoch==78u&&device.mailbox.running);
    CHECK(!raw_mailbox_finish(&device.mailbox,&running,&p));
    CHECK(!raw_mailbox_receive(&device.mailbox,&received_job,&received_p));
    /* No independently valid receipt means no queued notification. */
    restart_setup();restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;worker_read_ok=0;
    CHECK(menu_reset_route()==0x1928a8u&&!ch_raw_service.restart_pending&&!device.restart_notice.valid);
}
static void touchscreen_notice_rejects_unsafe_consumption(void){RawRuntime *r;uint32_t n,old_draws,old_received;
    for(n=0u;n<11u;n++){
        restart_setup();r=&ch_raw_service.runtime;restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;
        r->fault=0u;r->controls.fault=0u;
        CHECK(menu_reset_route()==0x1928a8u&&ch_raw_service.restart_pending);
        if(n==0u)device.restart_notice.source_epoch++;
        if(n==1u)device.restart_notice.target_epoch++;
        if(n==2u)device.restart_notice.valid=0u;
        if(n==3u)backend_ok=0;
        if(n==4u)restore_status=RAW_ENV_POISONED;
        if(n==5u){r->controls.pending.kind=CH_COMMAND_RESUME;r->controls.pending.request_id=7u;
            r->last_request=7u;r->completed_request=6u;}
        if(n==6u)r->controls.pending.kind=CH_COMMAND_STEP_ONE_BOUNDARY;
        if(n==7u)device.pause_audio.owned=1u;
        if(n==8u)ch_raw_service.display_owned=1u;
        if(n==9u)device.pause_display.state=RAW_PAUSE_STEP_WAIT;
        if(n==10u)r->display_error=1u;
        fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=8u;fixture_sample.scene_epoch=77u;
        old_draws=drawn;old_received=received;lifecycle_route(0x1042f4u);
        CHECK(!ch_raw_service.restart_pending&&!device.restart_receipts&&device.binding.source_epoch==77u);
        CHECK(r->fault==RAW_FAULT_ENVIRONMENT&&drawn==old_draws&&received==old_received);
        CHECK(!device.mailbox.minimum_epoch&&device.environment.state.rtc_owned);
    }
    /* The already acknowledged Resume is bookkeeping, not an owned command.
       A mismatched acknowledgement above still refuses the restart. */
    restart_setup();r=&ch_raw_service.runtime;restart_fixture.caller_lr=RAW_GUI_RESET_CALLER_LR;
    r->controls.pending.kind=CH_COMMAND_RESUME;r->controls.pending.request_id=7u;
    r->last_request=r->completed_request=7u;
    CHECK(menu_reset_route()==0x1928a8u&&ch_raw_service.restart_pending);
    fixture_sample.frontend_valid=1u;fixture_sample.frontend_mode=8u;fixture_sample.scene_epoch=78u;
    lifecycle_route(0x1042f4u);
    CHECK(!ch_raw_service.restart_pending&&!r->fault&&device.restart_receipts==1u&&r->scene_epoch==78u);
    CHECK(!r->controls.pending.kind&&restore_calls==1u);
}
static void rejected_receipts_preserve_old_session(void){uint32_t n;RawRuntime *r;ChNativeContext c={0};
    for(n=0u;n<5u;n++){
        restart_setup();r=&ch_raw_service.runtime;
        if(n==0u)restart_fixture.counter=1u;
        if(n==1u)restart_fixture.bad_word=1u;
        if(n==2u)restart_fixture.drift=1u;
        if(n==3u)restart_fixture.failed=4u;
        if(n==4u)restart_fixture.mode=1u;
        CHECK(restart_route()==0x18c670u);
        CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&r->scene_epoch==77u);
        CHECK(r->fault==RAW_FAULT_ORDER&&r->actual_seen&&device.actual_result.complete);
        CHECK(device.environment.state.rtc_owned&&!restore_calls&&!device.mailbox.minimum_epoch);
    }
    restart_setup();c.lr=0x10ed4cu;CHECK(!raw_restart_receipt(&device.binding.read,&c));
    CHECK(raw_service_route(&ch_raw_service,&c)==0u&&!device.restart_receipts);
    restart_setup();r=&ch_raw_service.runtime;r->runtime=CH_RUNTIME_PAUSED;
    CHECK(restart_route()==0x18c670u&&!device.restart_receipts&&!restore_calls);
    restart_setup();device.pause_audio.owned=1u;
    CHECK(restart_route()==0x18c670u&&!device.restart_receipts&&!restore_calls);
    restart_setup();r=&ch_raw_service.runtime;r->fault=RAW_FAULT_ENVIRONMENT;
    CHECK(restart_route()==0x18c670u&&!device.restart_receipts&&!restore_calls);
    restart_setup();ch_raw_service.runtime.fault=0u;backend_ok=0;CHECK(restart_route()==0x18c670u);
    CHECK(ch_raw_service.runtime.fault==RAW_FAULT_ENVIRONMENT&&!device.restart_receipts&&!restore_calls);
    restart_setup();ch_raw_service.runtime.fault=0u;restore_status=RAW_ENV_POISONED;CHECK(restart_route()==0x18c670u);
    CHECK(restore_calls==1u&&device.environment.state.poisoned&&device.environment.state.rtc_owned);
    CHECK(!device.restart_receipts&&device.binding.source_epoch==77u&&device.actual_result.complete);
    CHECK(ch_raw_service.runtime.fault==RAW_FAULT_ENVIRONMENT&&!device.mailbox.minimum_epoch);
}
int main(void){completed_restart_retires_resources();rejected_receipts_preserve_old_session();
    restarted_menu_loading_play_baseline();
    confirmed_reset_routes_after_native_clear(0);confirmed_reset_routes_after_native_clear(1);
    menu_reset_provenance_is_required();touchscreen_reset_provenance_is_required();
    touchscreen_notice_is_main_thread_owned();touchscreen_notice_rejects_unsafe_consumption();
    cancelled_menu_reset_keeps_session();
    printf("passed: %u checks; completed Restart, RTC cleanup, epoch retirement and stale worker rejection\n",checks);return 0;
}
