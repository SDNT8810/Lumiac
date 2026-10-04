#include "media_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t bitfield[] = {
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x05,
    0x09,0xe9,0x09,0xea,0x09,0xb5,0x09,0xb6,0x09,0xcd,0x81,0x02,
    0x75,0x03,0x95,0x01,0x81,0x01,0xc0
};
static const uint8_t array[] = {
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,
    0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,
    0x75,0x10,0x95,0x02,0x81,0x00,0xc0
};
static media_action_t actions[32];
static uint32_t durations[32];
static unsigned emitted;
static void capture(unsigned key, media_action_t action, uint32_t ms, void *ctx) {
    (void)key;(void)ctx;assert(emitted<32);
    actions[emitted]=action;durations[emitted++]=ms;
}

int main(void) {
    uint8_t mac[6], expected[] = {0xdc,0x2c,0x26,0x08,0x87,0xb8};
    assert(media_parse_mac_query("DC%3A2C%3A26%3A08%3A87%3AB8",mac));
    assert(!memcmp(mac,expected,6));
    assert(media_parse_mac_query("dc:2c:26:08:87:b8",mac));
    assert(!media_parse_mac_query("DC:2C:26:08:87:B8extra",mac));
    assert(!media_parse_mac_query("DC%3",mac));
    assert(!media_parse_mac_query("DC%GG2C:26:08:87:B8",mac));
    assert(!media_parse_mac_query("DC:2C:26:08:87:BZ",mac));
    assert(!media_parse_mac_query("DC:2C:26:08:87:B8%00",mac));
    puts("PASS encoded and strict Bluetooth addresses");

    media_map_t map; uint8_t down, covered;
    assert(media_parse_descriptor(&map,bitfield,sizeof(bitfield)));
    assert(map.valid && map.count==5 && map.report_ids);
    for(unsigned i=0;i<5;i++){
        uint8_t packet[]={3,1u<<i};
        assert(media_decode(&map,packet,sizeof(packet),&down,&covered));
        assert(down==(1u<<i) && covered==31);
    }
    const uint8_t release[]={3,0}, chord[]={3,3}, wrong_id[]={7,0xff}, short_packet[]={3};
    assert(media_decode(&map,release,2,&down,&covered)&&down==0);
    assert(media_decode(&map,chord,2,&down,&covered)&&down==3);
    assert(!media_decode(&map,wrong_id,2,&down,&covered));
    assert(!media_decode(&map,short_packet,1,&down,&covered));
    puts("PASS five bitfield buttons, releases, simultaneous keys, report IDs, truncation");

    assert(media_parse_descriptor(&map,array,sizeof(array)));
    const uint8_t usage_array[]={2,0xe9,0,0xb6,0}, usage_release[]={2,0,0,0,0};
    assert(media_decode(&map,usage_array,5,&down,&covered)&&down==9&&covered==31);
    assert(media_decode(&map,usage_release,5,&down,&covered)&&down==0);
    const uint8_t ff[]={2,0xb3,0,0,0};
    assert(media_decode(&map,ff,5,&down,&covered)&&down==4);
    /* An array with an explicit list uses a logical index, not a raw usage. */
    const uint8_t indexed[]={0x05,0x0c,0x15,0,0x25,2,0x75,8,0x95,1,0x09,0,0x09,0xe9,0x09,0xea,0x81,0};
    const uint8_t index_packet[]={2};
    assert(media_parse_descriptor(&map,indexed,sizeof(indexed)));
    assert(media_decode(&map,index_packet,1,&down,&covered)&&down==2);
    puts("PASS consumer arrays, explicit usage lists, hold transport aliases");

    const uint8_t multi[]={0x05,0x0c,0x15,0,0x25,1,0x75,1,0x95,1,0x85,1,0x09,0xe9,0x81,2,
        0xa4,0x85,2,0x09,0xea,0x81,2,0xb4,0x09,0xcd,0x81,2};
    assert(media_parse_descriptor(&map,multi,sizeof(multi)));
    const uint8_t report1[]={1,2}, report2[]={2,1};
    assert(media_decode(&map,report1,2,&down,&covered)&&down==16&&covered==17);
    assert(media_decode(&map,report2,2,&down,&covered)&&down==2&&covered==2);
    const uint8_t truncated[]={0x05};
    assert(!media_parse_descriptor(&map,truncated,1));
    assert(!map.valid);
    const uint8_t bad_pop[]={0xb4};
    assert(!media_parse_descriptor(&map,bad_pop,1));
    const uint8_t huge[]={0x75,32,0x96,0xff,0xff,0x81,2};
    assert(!media_parse_descriptor(&map,huge,sizeof(huge)));
    puts("PASS independent report offsets, global push/pop, malformed descriptors");

    media_tracker_t t={0};
    media_update(&t,1,31,100,capture,NULL);
    media_update(&t,0,31,220,capture,NULL);
    assert(emitted==2&&actions[0]==MEDIA_DOWN&&actions[1]==MEDIA_PRESS&&durations[1]==120);
    assert(t.keys[0].presses==1&&!t.keys[0].down);
    media_tick(&t,2000,capture,NULL);assert(emitted==2);
    puts("PASS quick tap persists as a PRESS event, no phantom hold after release");

    media_update(&t,1,31,3000,capture,NULL);
    media_update(&t,1,31,3400,capture,NULL);
    media_tick(&t,3649,capture,NULL);assert(!t.keys[0].held);
    media_tick(&t,3650,capture,NULL);assert(t.keys[0].held&&t.keys[0].holds==1);
    assert(actions[3]==MEDIA_HOLD&&durations[3]==650);
    media_update(&t,1,31,4000,capture,NULL);assert(t.keys[0].holds==1);
    media_update(&t,0,31,4200,capture,NULL);
    assert(actions[4]==MEDIA_HOLD_END&&durations[4]==1200&&!t.keys[0].down);
    assert(t.keys[0].presses==1);
    puts("PASS timed hold without repeats, repeated reports do not restart timer, release duration");

    emitted=0;memset(&t,0,sizeof(t));
    media_update(&t,3,31,5000,capture,NULL);
    media_update(&t,0,2,5100,capture,NULL);
    assert(t.keys[0].down&&!t.keys[1].down);
    media_cancel(&t,5200,capture,NULL);
    assert(!t.keys[0].down&&actions[3]==MEDIA_CANCEL&&t.keys[0].presses==0);
    emitted=0;memset(&t,0,sizeof(t));
    media_update(&t,1,31,100,capture,NULL);
    media_tick(&t,15100,capture,NULL);
    assert(!t.keys[0].down&&actions[1]==MEDIA_CANCEL);
    emitted=0;memset(&t,0,sizeof(t));
    media_update(&t,1,31,UINT32_MAX-100,capture,NULL);
    media_update(&t,0,31,49,capture,NULL);
    assert(durations[1]==150&&actions[1]==MEDIA_PRESS);
    puts("PASS partial reports, disconnect cancellation, stale key timeout, timer wrap");
    puts("All media tests passed");
}
