#include <stdio.h>
#include "rca.hpp"
#include "rcavt.hpp"
#include "pico/time.h"

void proc_cin();

int main() {
    stdio_init_all();
    /*
    // core0 で映像出力を駆動する場合は以下を呼ぶ
    init_framedata();
    init_dma();
    */
   
    // core1 で映像出力を駆動する場合は以下を呼ぶ。250ms くらい待たされる
    init_video_on_core1();
    
    palcolor(1);    
    
    clrgraph(1);
    while(1){
        proc_cin();
        
        wait_for_vsync();
    }
}

void swimming_triangle(){
    uint32_t c = 0;
    uint32_t f = 0;
    
    int16_t x[3] = {20, 120, 70};
    int16_t y[3] = {20, 120, 180};
    int8_t ys[3] = {3, 3, 3};
    int8_t xs[3] = {3, 3, 3};
    
    f = frame % (64 * 224);
    c += 1;
    palcolor(0);
    for(uint8_t i = 0; i < 3; i += 1){
        x[i] += xs[i];
        y[i] += ys[i];
        if(x[i] <= 0 && xs[i] < 0){
            xs[i] = -xs[i];
        }
        if(y[i] <= 0 && ys[i] < 0){
            ys[i] = -ys[i];
        }
        if(x[i] >= DISP_RES_X - 1 && xs[i] > 0){
            xs[i] = -xs[i];
        }
        if(y[i] >= DISP_RES_Y - 1 && ys[i] > 0){
            ys[i] = -ys[i];
        }
    }
    triangle(x[0], y[0], x[1], y[1], x[2], y[2]);
}

void proc_cin(){
    while(1){
        int c = getchar_timeout_us(0);
        if(c == PICO_ERROR_TIMEOUT){
            break;
        }
        palcolor(0);
        c_putc((char)c);
        if(c == 13){
            c_putc(10);
        }
    }
}