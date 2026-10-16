/* fn_menu.cpp -- Function menu (2.8" CYD, LovyanGFX) */
#include "fn_menu.h"
#include "display.h"
#include "config.h"
#include "mesh.h"
#include "frame.h"
#include "radio_esp32.h"
#include <Arduino.h>
#include <esp_wifi.h>

extern int mesh_transmit(const wichat_frame_t *frame);

#define MENU_X  30
#define MENU_Y  10
#define MENU_W  260
#define MENU_H  210
#define ITEM_H  24
#define ITEM_GAP 3
#define NUM_ITEMS 7
#define COL_MENU_BG    0x2104
#define COL_MENU_BORDER 0x07E0
#define COL_MENU_ITEM  0x4208

static const char *menu_labels[NUM_ITEMS] = {
    "Ping Network", "Node List", "Change Channel",
    "Change Name", "Stats", "Scan Channels", "Clear Chat"
};

static void draw_menu(void) {
    tft.fillRect(MENU_X-2,MENU_Y-2,MENU_W+4,MENU_H+4,COL_MENU_BORDER);
    tft.fillRect(MENU_X,MENU_Y,MENU_W,MENU_H,COL_MENU_BG);
    tft.setTextSize(2); tft.setTextColor(0x07FF,COL_MENU_BG);
    tft.setCursor(MENU_X+50,MENU_Y+4); tft.print("FUNCTIONS");
    tft.setTextSize(1);
    for(int i=0;i<NUM_ITEMS;i++){
        int iy=MENU_Y+26+i*(ITEM_H+ITEM_GAP);
        tft.fillRect(MENU_X+8,iy,MENU_W-16,ITEM_H,COL_MENU_ITEM);
        tft.setTextColor(0xFFFF,COL_MENU_ITEM);
        tft.setCursor(MENU_X+14,iy+8); tft.print(menu_labels[i]);
    }
}

static int wait_sel(void) {
    int16_t tx,ty;
    while(tft.getTouch(&tx,&ty))delay(10); delay(100);
    while(true) {
        if(tft.getTouch(&tx,&ty)){
            if(tx<MENU_X||tx>MENU_X+MENU_W||ty<MENU_Y||ty>MENU_Y+MENU_H){while(tft.getTouch(&tx,&ty))delay(10);return -1;}
            for(int i=0;i<NUM_ITEMS;i++){int iy=MENU_Y+26+i*(ITEM_H+ITEM_GAP);
                if(ty>=iy&&ty<iy+ITEM_H&&tx>=MENU_X+8&&tx<MENU_X+MENU_W-8){while(tft.getTouch(&tx,&ty))delay(10);return i;}}
            while(tft.getTouch(&tx,&ty))delay(10);
        } delay(20);
    }
}

static void show_msg(const char *t,const char *m,uint16_t c){
    tft.fillRect(MENU_X-2,MENU_Y-2,MENU_W+4,MENU_H+4,COL_MENU_BORDER);
    tft.fillRect(MENU_X,MENU_Y,MENU_W,MENU_H,COL_MENU_BG);
    tft.setTextSize(2);tft.setTextColor(c,COL_MENU_BG);tft.setCursor(MENU_X+16,MENU_Y+16);tft.print(t);
    tft.setTextSize(1);tft.setTextColor(0xFFFF,COL_MENU_BG);tft.setCursor(MENU_X+16,MENU_Y+42);tft.print(m);
}

static void wait_tap(void){
    int16_t tx,ty; while(tft.getTouch(&tx,&ty))delay(10); delay(200);
    tft.setTextSize(1);tft.setTextColor(0xFFE0,COL_MENU_BG);
    tft.setCursor(MENU_X+50,MENU_Y+MENU_H-14);tft.print("Tap to close");
    while(!tft.getTouch(&tx,&ty))delay(20);
    while(tft.getTouch(&tx,&ty))delay(10);
}

static void fn_ping(void){
    show_msg("PING","Sending PING...",0x07FF);
    wichat_frame_t p;memset(&p,0,sizeof p);
    p.hdr.magic=WICHAT_MAGIC;p.hdr.msg_id=frame_gen_msg_id();
    p.hdr.ttl=WICHAT_DEFAULT_TTL;p.hdr.type=WICHAT_TYPE_PING;
    memcpy(p.hdr.sender,g_mesh.local_mac,6);
    cyd_mesh_mark_seen(p.hdr.msg_id);mesh_transmit(&p);cyd_mesh_send_beacon();
}

static void fn_nodes(void){
    tft.fillRect(MENU_X-2,MENU_Y-2,MENU_W+4,MENU_H+4,COL_MENU_BORDER);
    tft.fillRect(MENU_X,MENU_Y,MENU_W,MENU_H,COL_MENU_BG);
    tft.setTextSize(2);tft.setTextColor(0x07FF,COL_MENU_BG);
    tft.setCursor(MENU_X+50,MENU_Y+4);tft.print("NODE LIST");
    tft.setTextSize(1);tft.setTextColor(0xFFFF,COL_MENU_BG);
    if(g_mesh.node_count==0){tft.setCursor(MENU_X+16,MENU_Y+35);tft.print("No nodes yet.");}
    else{for(int i=0;i<g_mesh.node_count&&i<7;i++){
        tft.setCursor(MENU_X+8,MENU_Y+30+i*20);
        tft.printf("%d.%-8s R:%d %s",i+1,g_mesh.nodes[i].callsign,g_mesh.nodes[i].rssi,
            (millis()-g_mesh.nodes[i].last_seen_ms<30000)?"OK":"OLD");
    }} wait_tap();
}

static void fn_channel(void){
    show_msg("CHANNEL","",0x07FF);
    char buf[32];snprintf(buf,sizeof buf,"Current: CH %d",g_mesh.channel);
    tft.setCursor(MENU_X+16,MENU_Y+42);tft.setTextSize(2);tft.setTextColor(0x07E0,COL_MENU_BG);tft.print(buf);
    tft.fillRect(MENU_X+20,MENU_Y+75,50,30,0xF800);tft.fillRect(MENU_X+160,MENU_Y+75,50,30,0x07E0);
    tft.setTextColor(0xFFFF,0xF800);tft.setCursor(MENU_X+38,MENU_Y+82);tft.print("-");
    tft.setTextColor(0xFFFF,0x07E0);tft.setCursor(MENU_X+178,MENU_Y+82);tft.print("+");
    tft.setTextSize(1);tft.setTextColor(0xFFE0,COL_MENU_BG);
    tft.setCursor(MENU_X+40,MENU_Y+120);tft.print("Tap outside to close");
    int16_t tx,ty;while(tft.getTouch(&tx,&ty))delay(10);delay(200);
    while(true){if(tft.getTouch(&tx,&ty)){
        if(tx<MENU_X||tx>MENU_X+MENU_W||ty<MENU_Y||ty>MENU_Y+MENU_H){while(tft.getTouch(&tx,&ty))delay(10);break;}
        int by=MENU_Y+75;if(ty>=by&&ty<by+30){
            if(tx>=MENU_X+20&&tx<MENU_X+70&&g_mesh.channel>1){g_mesh.channel--;esp_wifi_set_channel(g_mesh.channel,WIFI_SECOND_CHAN_NONE);}
            else if(tx>=MENU_X+160&&tx<MENU_X+210&&g_mesh.channel<14){g_mesh.channel++;esp_wifi_set_channel(g_mesh.channel,WIFI_SECOND_CHAN_NONE);}
            tft.fillRect(MENU_X+16,MENU_Y+38,MENU_W-32,24,COL_MENU_BG);
            snprintf(buf,sizeof buf,"Current: CH %d",g_mesh.channel);
            tft.setCursor(MENU_X+16,MENU_Y+42);tft.setTextSize(2);tft.setTextColor(0x07E0,COL_MENU_BG);tft.print(buf);
        }while(tft.getTouch(&tx,&ty))delay(10);delay(200);
    }delay(20);}
}

static void fn_name(void){show_msg("NAME","",0x07FF);
    tft.setCursor(MENU_X+16,MENU_Y+42);tft.setTextSize(2);tft.setTextColor(0x07E0,COL_MENU_BG);
    tft.printf("Current: %s",g_mesh.callsign);
    tft.setTextSize(1);tft.setTextColor(0xFFE0,COL_MENU_BG);
    tft.setCursor(MENU_X+16,MENU_Y+70);tft.print("Type name, then SEND.");wait_tap();}

static void fn_stats(void){
    tft.fillRect(MENU_X-2,MENU_Y-2,MENU_W+4,MENU_H+4,COL_MENU_BORDER);
    tft.fillRect(MENU_X,MENU_Y,MENU_W,MENU_H,COL_MENU_BG);
    tft.setTextSize(2);tft.setTextColor(0x07FF,COL_MENU_BG);
    tft.setCursor(MENU_X+70,MENU_Y+4);tft.print("STATS");
    tft.setTextSize(1);tft.setTextColor(0xFFFF,COL_MENU_BG);int y=MENU_Y+30;
    tft.setCursor(MENU_X+12,y);tft.printf("Callsign: %s",g_mesh.callsign);y+=16;
    tft.setCursor(MENU_X+12,y);tft.printf("Channel:  %d",g_mesh.channel);y+=16;
    tft.setCursor(MENU_X+12,y);tft.printf("Nodes:    %d",g_mesh.node_count);y+=16;
    tft.setCursor(MENU_X+12,y);tft.printf("Messages: %d",g_mesh.msg_count);y+=16;
    tft.setCursor(MENU_X+12,y);tft.printf("Uptime:   %lu min",millis()/60000);y+=16;
    tft.setCursor(MENU_X+12,y);tft.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
        g_mesh.local_mac[0],g_mesh.local_mac[1],g_mesh.local_mac[2],
        g_mesh.local_mac[3],g_mesh.local_mac[4],g_mesh.local_mac[5]);
    wait_tap();}

static void fn_scan(void){show_msg("SCAN","Scanning...",0x07FF);
    uint8_t orig=g_mesh.channel;tft.setTextSize(1);tft.setTextColor(0xFFFF,COL_MENU_BG);
    for(int ch=1;ch<=14;ch++){esp_wifi_set_channel(ch,WIFI_SECOND_CHAN_NONE);
        int y=MENU_Y+42+(ch-1)*11;if(y>MENU_Y+MENU_H-14)break;
        tft.setCursor(MENU_X+12,y);tft.printf("CH%2d: ...",ch);
        uint8_t buf[512];int8_t rssi;int heard=0;uint32_t st=millis();
        while(millis()-st<300){if(radio_esp32_rx(buf,sizeof buf,&rssi)>0)heard++;}
        tft.fillRect(MENU_X+12,y,MENU_W-24,11,COL_MENU_BG);
        tft.setCursor(MENU_X+12,y);tft.printf("CH%2d: %d pkts",ch,heard);
    }esp_wifi_set_channel(orig,WIFI_SECOND_CHAN_NONE);g_mesh.channel=orig;wait_tap();}

static void fn_clear(void){g_mesh.msg_count=0;g_mesh.msg_head=0;g_mesh.msg_version++;
    show_msg("CLEAR","Chat cleared.",0x07E0);wait_tap();}

void fn_menu_show(void){
    draw_menu(); int sel=wait_sel();
    switch(sel){case 0:fn_ping();break;case 1:fn_nodes();break;case 2:fn_channel();break;
    case 3:fn_name();break;case 4:fn_stats();break;case 5:fn_scan();break;case 6:fn_clear();break;default:break;}
    display_clear();
    display_status_bar(g_mesh.callsign,g_mesh.channel,g_mesh.node_count,millis());
}
