#include <stdio.h>
#include "rca.hpp"

int main() {
    stdio_init_all();
    
    init_framedata();
    init_dma();
    

    uint16_t counter = 0;
    //uint32_t pat = 0b0000111110110100;
    uint32_t pat = 0b1111111111111010;
    
    
    uint32_t c = 0;
    
    int16_t x = 0;
    int16_t y = 0;
    int8_t xs = 1;
    int8_t ys = 1;
    while(1){
        //while(frame % (64 * 224) == c){}
        //c = frame % (64 * 224);
        c += 1;
        if(c % 2000 == 1999){
            palcolor(c / 2000 % 2);
        }
        x += xs;
        y += ys;
        if(x == 0 && xs < 0){
            xs = -xs;
        }
        if(y == 0 && ys < 0){
            ys = -ys;
        }
        if(x == DISP_RES_X - 1 && xs > 0){
            xs = -xs;
        }
        if(y == DISP_RES_Y - 1 && ys > 0){
            ys = -ys;
        }
        pset(x, y);
    }
}


