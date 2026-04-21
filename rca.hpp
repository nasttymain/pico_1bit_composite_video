#include <cstdint>
void init_framedata();
void init_dma();
void pset(int16_t x, int16_t y);
void palcolor(uint8_t palno);
void line(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
void wait_for_vsync();
void triangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3);
void trianglef(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3);

/*
void boxf(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
void cls(uint8_t cls_mode);
*/


#ifndef __NASTTY_RCA_1BIT__
#define __NASTTY_RCA_1BIT__

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/dma.h"
#include "main.pio.h"
#include "pico/stdlib.h"
#include <cmath>
#include <utility>
#include "pico/time.h"
#include "pico/multicore.h"

#define START_PIN 16
#define ROW_PINS 4

// PIO program is a simple pull and shift out
// simply copy & paste the following into main.pio file
// .program main
//     pull
//     out pins, 4
int dma_chan;
volatile uint16_t lineno = 0;
volatile uint32_t frame = 0;
constexpr const uint16_t LINEBUF_LEN = 159;
uint8_t linebuf_a[LINEBUF_LEN] __attribute__((aligned(4)));
uint8_t linebuf_b[LINEBUF_LEN] __attribute__((aligned(4)));
uint8_t* ptr_linebuf[2] = {linebuf_a, linebuf_b};
uint8_t linebuf_vblank[LINEBUF_LEN] __attribute__((aligned(4)));
uint8_t linebuf_vsync[LINEBUF_LEN] __attribute__((aligned(4)));


// X 方向に 512 pixel で 単色なので、ラインあたり 64 バイト必要。縦は 224 ラインを使う
uint8_t framebuf[64 * 224] __attribute__((aligned(4)));
uint8_t framebuf_c[64 * 224] __attribute__((aligned(4)));
constexpr const uint16_t DISP_RES_X = 512;
constexpr const uint16_t DISP_RES_Y = 224;

PIO pio = pio0;
uint sm;
dma_channel_config dc;
uint8_t current_color = 1;

void prepare_next_line(uint8_t* buf, uint16_t line_number);

// サンキュー OpenAI
inline uint8_t expand4to8(uint8_t x) {
    x &= 0x0F;                // 下位4ビットだけ使う（ABCD）
    x = (x | (x << 2)) & 0x33; // A B C D → AB00CD00 → A0B0C0D0途中
    x = (x | (x << 1)) & 0x55; // 最終形 A0B0C0D0
    return x;
}

uint8_t bittable[16] = {
    0x00,
    0x02,
    0x08,
    0x0A,
    0x20,
    0x22,
    0x28,
    0x2A,
    0x80,
    0x82,
    0x88,
    0x8A,
    0xA0,
    0xA2,
    0xA8,
    0xAA,
};

uint8_t flip = 0;
void hndirq0(void){
    
    dma_hw->ints0 = 1u << dma_chan;
    dma_channel_abort(dma_chan);
    
    //   3 Before Porch 
    //   3 Vsync
    //  14 After Porch
    // 242 Video
    if(lineno <= 3){
        //   3 Before Porch
        dma_channel_set_read_addr(dma_chan, linebuf_vblank, false);
    }else if(lineno <= 6){
        //   3 Vsync
        dma_channel_set_read_addr(dma_chan, linebuf_vsync, false);
    }else if(lineno <= 20 + 8){
        //  14 After Porch + 8 no video
        dma_channel_set_read_addr(dma_chan, linebuf_vblank, false);
    }else if(lineno <= 20 + 8 + 224){
        // 224 Video
        dma_channel_set_read_addr(dma_chan, ptr_linebuf[flip], false);
    }else{
        // 10 after video
        dma_channel_set_read_addr(dma_chan, linebuf_vblank, false);
    }
    dma_channel_set_trans_count(dma_chan, LINEBUF_LEN / sizeof(uint8_t), false);
        
    dma_channel_start(dma_chan);
    
    flip = (flip + 1) & 1;
    if(lineno > (28 - 1) && lineno <= 20 + 8 + 224){
        // 次の flip に対して書込処理を行う
        // 29, (224): active video. translating from vram
        const uint16_t linenum = (lineno - 28);
        //const uint16_t lineoffset = linenum * 64;
        const uint16_t lineoffset = linenum << 6;
        // 映像として有効な x 方向の添字は、28 ～ 251 までの 224 pixel。
        for(uint16_t i = 0; i < 64; i += 1){
            (ptr_linebuf[flip])[28 + (i << 1) + 0] = 0b01010101 | bittable[framebuf[lineoffset + i] >> 4];
            (ptr_linebuf[flip])[28 + (i << 1) + 1] = 0b01010101 | bittable[framebuf[lineoffset + i] & 15];
        }
    }
    
    lineno = (lineno + 1) % 262;
    if(lineno == 0){
        frame += 1;
    }
}


void main_program_init(PIO pio, uint sm, uint offset, uint pin) {
    // Initialize ROW_PINS amount of pins, starting from function input value pin
    for(int i = 0; i < ROW_PINS; i++) {
        pio_gpio_init(pio, pin + i);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, pin, ROW_PINS, true);
    
    // Start building PIO config
    pio_sm_config c = main_program_get_default_config(offset);
    
    // NOTE: You have to config out and set pins separately, or they just won't write anything
    sm_config_set_out_pins(&c, pin, ROW_PINS);
    sm_config_set_out_shift(&c, false, true, 8);
    
    // Begin PIO state machine
    pio_sm_init(pio, sm, offset, &c);
}

void pset(int16_t x, int16_t y){
    if(x < 0 || x >= DISP_RES_X){
        return;
    }
    if(y < 0 || y >= DISP_RES_Y){
        return;
    }
    int32_t c = (int32_t)y * DISP_RES_X + x;
    if(current_color == 1){
        framebuf[c >> 3] |= (0x80 >> (c % 8));
    }else if(current_color == 0){
        framebuf[c >> 3] &= 0xFF ^ (0x80 >> (c % 8));
    }
}

void palcolor(uint8_t palno){
    current_color = palno;
}


// core0 でも init_framedata と init_dma を呼べば動く。USB割り込みで荒れるけど
void init_framedata(){    
    // ラインあたり63.5usが正規。4で割り切れたいので63.6usにしよう。10MHzならラインあたり636pixel。159y2te(1/4)
    // 2(17) White
    // 1(16) Sync
    // BLACK: 01 White: 11 Sync: 00
    
    //  15p front porch
    //  47p hsync
    //  47p back porch
    // 526p active video
    // めんどくせーので、左の3px、右の11pxは捨てます。つまり、画面左端は28、右端は...計算めんどくさいや
    for(uint16_t i = 0; i < LINEBUF_LEN; i += 1){
        if(i < 3){
            // front porch
            linebuf_a[i] = 0b01010101;
            linebuf_b[i] = 0b01010101;
            linebuf_vblank[i] = 0b01010101;
            linebuf_vsync[i] = 0b01010101;
        }else if(i == 3){
            // front porch + hsync
            linebuf_a[i] = 0b01000000;
            linebuf_b[i] = 0b01000000;
            linebuf_vblank[i] = 0b01000000;
            linebuf_vsync[i] = 0b01000000;
        }else if(i <= 15){
            // hsync
            linebuf_a[i] = 0b00000000;
            linebuf_b[i] = 0b00000000;
            linebuf_vblank[i] = 0b00000000;
            linebuf_vsync[i] = 0b00000000;
        }else if(i == 15){
            // hsync + back porch
            linebuf_a[i] = 0b00000101;
            linebuf_b[i] = 0b00000101;
            linebuf_vblank[i] = 0b00000101;
            linebuf_vsync[i] = 0b00000000;
        }else if(i <= 27){
            // back porch
            linebuf_a[i] = 0b01010101;
            linebuf_b[i] = 0b01010101;
            linebuf_vblank[i] = 0b01010101;
            linebuf_vsync[i] = 0b00000000;
        }else if(i == 27){
            // back porch(1) + active video(3)
            linebuf_a[i] = 0b01010101;
            linebuf_b[i] = 0b01010101;
            linebuf_vblank[i] = 0b01010101;
        }else{
            // active video
            //if(i % 8 == 0){
            //    linebuf_a[i] = 0b11111111;
            //}else{
            //    linebuf_a[i] = 0b01010101;
            //}
            linebuf_a[i] = 0b01010101;
            linebuf_b[i] = 0b01010101;
            linebuf_vblank[i] = 0b01010101;
            
            if(i < 150){
                linebuf_vsync[i] = 0b00000000;
            }else{
                linebuf_vsync[i] = 0b01010101;
            }

        }
    }
}

void init_dma(){
    // PIO の設定
    uint offset = pio_add_program(pio, &main_program);
    sm = pio_claim_unused_sm(pio, true);
    main_program_init(pio, sm, offset, START_PIN);
    // 10MHz。なのでSYS_CLKが150MHzなら15
    pio_sm_set_clkdiv(pio, sm, 15.0);
    // 以下、VGA 25.175MHz
    //pio_sm_set_clkdiv_int_frac8(pio, sm, 11, 234);
    pio_sm_set_enabled(pio, sm, true);
    
    // DMA の設定
    dma_chan = dma_claim_unused_channel(true);
    dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    dma_channel_configure(
        dma_chan,
        &dc,
        &pio->txf[sm],
        framebuf,
        sizeof(linebuf_a) / sizeof(uint8_t),
        true
    );
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, hndirq0);
    irq_set_enabled(DMA_IRQ_0, true);
}


void line(int16_t x1, int16_t y1, int16_t x2, int16_t y2){
    bool steep = std::abs(x1 - x2) < std::abs(y1 - y2);
    if(steep){
        std::swap(x1, y1);
        std::swap(x2, y2);
    }
    
    if (x1 > x2){
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    int16_t y = y1;
    int ierror = 0;
    for(int16_t x = x1; x <= x2; x += 1){
        if(steep){
            pset(y, x);
        }else{
            pset(x, y);
        }
        
        ierror += 2 * std::abs(y2 - y1);
        if (ierror > x2 - x1){
            y += y2 > y1 ? 1 : -1;
            ierror -= 2 * (x2 - x1);
        }
        
    }
}

// ま、正確には待ってる対象は vblank なんだけどね
void wait_for_vsync(){
    const auto f = frame;
    while(f == frame){ /*asm("wfi"); ←core1 で動かしてる以上core0にライン割り込みは飛ばないため*/ }
    return;
}

// 画面クリア
void clrgraph(uint8_t color_code){
    const uint8_t c = (color_code == 0) ? 0 : 255;
    for(int16_t i = 0; i < sizeof(framebuf) / sizeof(framebuf[0]); i += 1){
        framebuf[i] = c;
    }
}

void triangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3) {
    line(x1, y1, x2, y2);
    line(x2, y2, x3, y3);
    line(x3, y3, x1, y1);
}

void trianglef(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3) {
    
    // bubble sort
    if(y2 > y3){
        std::swap(x2, x3);
        std::swap(y2, y3);
    }
    
    if(y1 > y2){
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    
    if(y2 > y3){
        std::swap(x2, x3);
        std::swap(y2, y3);
    }
    
    for(int16_t ycnt = y1; ycnt < y3; ycnt += 1){
        int16_t xleft;
        if (ycnt < y2){
            xleft = x1 + (((x2 - x1) * 16) * ((ycnt - y1) * 16) / ((y2 - y1) * 16) + 8) / 16;
        }else{
            xleft = x2 + (((x3 - x2) * 16) * ((ycnt - y2) * 16) / ((y3 - y2) * 16) + 8) / 16;
        }
        int xright = x1 + (((x3 - x1) * 16) * ((ycnt - y1) * 16) / ((y3 - y1) * 16) + 8) / 16;
        
        line(xleft, ycnt, xright, ycnt);
    }
    
}


uint8_t is_core1_initialized = 0;
void core1_main(){
    sleep_ms(10);
    init_framedata();
    init_dma();
    is_core1_initialized = 1;
    while(1){
        asm("wfi");
    }
}

// core1 で映像を駆動したい場合はこれ「のみを」呼ぶ
void init_video_on_core1(){
    sleep_ms(100);
    multicore_launch_core1(core1_main);
    sleep_ms(100);
    while(is_core1_initialized == 0){}
}


#endif // __NASTTY_RCA_1BIT__