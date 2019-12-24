#pragma once
/*#
    # m6561.h

    MOS Technology 6561 emulator (aka VIC for PAL)

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the 
    implementation.

    Optionally provide the following macros with your own implementation
    ~~~C    
    CHIPS_ASSERT(c)
    ~~~

    ## Emulated Pins
    TODO

    TODO: Documentation

    ## zlib/libpng license

    Copyright (c) 2019 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution. 
#*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* address pins (see control pins for chip-select condition) */
#define M6561_A0    (1ULL<<0)
#define M6561_A1    (1ULL<<1)
#define M6561_A2    (1ULL<<2)
#define M6561_A3    (1ULL<<3)
#define M6561_A4    (1ULL<<4)
#define M6561_A5    (1ULL<<5)
#define M6561_A6    (1ULL<<6)
#define M6561_A7    (1ULL<<7)
#define M6561_A8    (1ULL<<8)
#define M6561_A9    (1ULL<<9)
#define M6561_A10   (1ULL<<10)
#define M6561_A11   (1ULL<<11)
#define M6561_A12   (1ULL<<12)
#define M6561_A13   (1ULL<<13)

/* data pins (the 4 additional color data pins are not emulated as pins) */
#define M6561_D0    (1ULL<<16)
#define M6561_D1    (1ULL<<17)
#define M6561_D2    (1ULL<<18)
#define M6561_D3    (1ULL<<19)
#define M6561_D4    (1ULL<<20)
#define M6561_D5    (1ULL<<21)
#define M6561_D6    (1ULL<<22)
#define M6561_D7    (1ULL<<23)

/* control pins shared with CPU 
    NOTE that there is no dedicated chip-select pin, instead
    registers are read and written during the CPU's 'shared bus
    half-cycle', and the following address bus pin mask is
    active:

    A13 A12 A11 A10 A9 A8
      0   1   0   0  0  0

    When this is true, the pins A3..A0 select the chip register.

    Use the M6561_SELECTED() macro to decide whether to call
    m6561_iorq()
*/
#define M6561_RW    (1ULL<<24)      /* same as M6502_RW */

#define M6561_CS_MASK           (M6561_A13|M6561_A12|M6561_A11|M6561_A10|M6561_A9|M6561_A8)
#define M6561_CS_VALUE          (M6561_A12)
#define M6561_SELECTED(pins)    ((pins&M6561_CS_MASK)==M6561_CS_VALUE)

#define M6561_NUM_REGS (16)
#define M6561_REG_MASK (M6561_NUM_REGS-1)

/* sound DC adjustment buffer length */
#define M6561_DCADJ_BUFLEN (512)

/* extract 8-bit data bus from 64-bit pins */
#define M6561_GET_DATA(p) ((uint8_t)((p&0xFF0000ULL)>>16))
/* merge 8-bit data bus value into 64-bit pins */
#define M6561_SET_DATA(p,d) {p=(((p)&~0xFF0000ULL)|(((d)<<16)&0xFF0000ULL));}

/* memory fetch callback, used to feed pixel- and color-data into the m6561 */
typedef uint16_t (*m6561_fetch_t)(uint16_t addr, void* user_data);

/* setup parameters for m6561_init() function */
typedef struct {
    /* pointer to RGBA8 framebuffer for generated image (optional) */
    uint32_t* rgba8_buffer;
    /* size of the RGBA framebuffer (optional) */
    uint32_t rgba8_buffer_size;
    /* visible CRT area blitted to rgba8_buffer (in pixels) */
    uint16_t vis_x, vis_y, vis_w, vis_h;
    /* the memory-fetch callback */
    m6561_fetch_t fetch_cb;
    /* optional user-data for fetch callback */
    void* user_data;
    /* frequency at which the tick function is called (for audio generation) */
    int tick_hz;
    /* sound sample frequency */
    int sound_hz;
    /* sound sample magnitude/volume (0.0..1.0)*/
    float sound_magnitude;
} m6561_desc_t;

/* raster unit state */
typedef struct {
    uint8_t h_count;        /* horizontal tick counter */
    uint16_t v_count;       /* line counter */
    uint16_t vc;            /* video matrix counter */
    uint16_t vc_base;       /* vc reload value at start of line */
    uint8_t vc_disabled;    /* if != 0, video counter inactive */
    uint8_t rc;             /* 4-bit raster counter (0..7 or 0..15) */
    uint8_t row_height;     /* either 8 or 16 */
    uint8_t row_count;      /* character row count */
} m6561_raster_unit_t;

/* memory unit state */
typedef struct {
    uint16_t c_addr_base;   /* character access base address */
    uint16_t g_addr_base;   /* graphics access base address */
    uint16_t c_value;       /* last fetched character access value */
} m6561_memory_unit_t;

/* graphics unit state */
typedef struct {
    uint8_t shift;          /* current pixel shifter */
    uint8_t color;          /* last fetched color value */
    bool inv_color;         /* true when bit 3 of CRF is clear */
    uint32_t bg_color;      /* current background color RGBA */
    uint32_t brd_color;     /* border color RGBA */
    uint32_t aux_color;     /* auxiliary color RGBA */
} m6561_graphics_unit_t;

/* border unit state */
typedef struct {
    uint8_t left, right;
    uint16_t top, bottom;
    uint8_t enabled;        /* if != 0, in border area */
} m6561_border_unit_t;

/* CRT state tracking */
typedef struct {
    uint16_t x, y;              /* beam pos */
    uint16_t vis_x0, vis_y0, vis_x1, vis_y1;  /* the visible area */
    uint16_t vis_w, vis_h;      /* width of visible area */
    uint32_t* rgba8_buffer;
} m6561_crt_t;

/* sound generator state */
typedef struct {
    uint16_t count;     /* count down with clock frequency */
    uint16_t period;    /* counter reload value */
    uint8_t bit;        /* toggled between 0 and 1 */
    uint8_t enabled;    /* 1 if enabled */
} m6561_voice_t;

typedef struct {
    uint16_t count;
    uint16_t period;
    uint32_t shift;
    uint8_t bit;
    uint8_t enabled;
} m6561_noise_t;

typedef struct {
    m6561_voice_t voice[3];
    m6561_noise_t noise;
    uint8_t volume;
    int sample_period;
    int sample_counter;
    float sample_accum;
    float sample_accum_count;
    float sample_mag;
    float sample;
    float dcadj_sum;
    uint32_t dcadj_pos;
    float dcadj_buf[M6561_DCADJ_BUFLEN];
} m6561_sound_t;

/* the m6561_t state struct */
typedef struct {
    uint64_t pins;
    m6561_fetch_t fetch_cb; /* memory fetch callback */
    void* user_data;        /* memory fetch callback user data */
    bool debug_vis;
    uint8_t regs[M6561_NUM_REGS];
    m6561_raster_unit_t rs;
    m6561_memory_unit_t mem;
    m6561_border_unit_t border;
    m6561_graphics_unit_t gunit;
    m6561_crt_t crt;
    m6561_sound_t sound;
} m6561_t;

/* initialize a new m6561_t instance */
void m6561_init(m6561_t* vic, const m6561_desc_t* desc);
/* reset a m6561_t instance */
void m6561_reset(m6561_t* vic);
/* get the visible display width in pixels */
int m6561_display_width(m6561_t* vic);
/* get the visible display height in pixels */
int m6561_display_height(m6561_t* vic);
/* read/write m6561 registers */
uint64_t m6561_iorq(m6561_t* vic, uint64_t pins);
/* tick the m6561_t instance to generate video output */
void m6561_tick_video(m6561_t* vic);
/* tick the m6561_t instance to generate audio, returns true when sample is ready */
bool m6561_tick_audio(m6561_t* vic);
/* get 32-bit RGBA8 value from color index (0..15) */
uint32_t m6561_color(int i);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*--- IMPLEMENTATION ---------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

/* internal constants */
#define _M6561_HTOTAL       (71)
#define _M6561_VTOTAL       (312)
#define _M6561_VRETRACEPOS  (303)
#define _M6561_PIXELS_PER_TICK  (4)

#define _M6561_HBORDER (1<<0)
#define _M6561_VBORDER (1<<1)
#define _M6561_HVC_DISABLE (1<<0)
#define _M6561_VVC_DISABLE (1<<1)

/* fixed point precision for audio sample period */
#define _M6561_FIXEDPOINT_SCALE (16)

#define _M6561_RGBA8(r,g,b) (0xFF000000|(b<<16)|(g<<8)|(r))

/* see VICE sources under vice/data/vic20/mike-pal.vpl */
static const uint32_t _m6561_colors[16] = {
    _M6561_RGBA8(0x00, 0x00, 0x00),     /* black */
    _M6561_RGBA8(0xFF, 0xFF, 0xFF),     /* white */
    _M6561_RGBA8(0xB6, 0x1F, 0x21),     /* red */
    _M6561_RGBA8(0x4D, 0xF0, 0xFF),     /* cyan */
    _M6561_RGBA8(0xB4, 0x3F, 0xFF),     /* purple */
    _M6561_RGBA8(0x44, 0xE2, 0x37),     /* green */
    _M6561_RGBA8(0x1A, 0x34, 0xFF),     /* blue */
    _M6561_RGBA8(0xDC, 0xD7, 0x1B),     /* yellow */
    _M6561_RGBA8(0xCA, 0x54, 0x00),     /* orange */
    _M6561_RGBA8(0xE9, 0xB0, 0x72),     /* light orange */
    _M6561_RGBA8(0xE7, 0x92, 0x93),     /* light red */
    _M6561_RGBA8(0x9A, 0xF7, 0xFD),     /* light cyan */
    _M6561_RGBA8(0xE0, 0x9F, 0xFF),     /* light purple */
    _M6561_RGBA8(0x8F, 0xE4, 0x93),     /* light green */
    _M6561_RGBA8(0x82, 0x90, 0xFF),     /* ligth blue */
    _M6561_RGBA8(0xE5, 0xDE, 0x85)      /* light yellow */
};

static void _m6561_init_crt(m6561_crt_t* crt, const m6561_desc_t* desc) {
    /* vis area horizontal coords must be multiple of 8 */
    CHIPS_ASSERT((desc->vis_x & 7) == 0);
    CHIPS_ASSERT((desc->vis_w & 7) == 0);
    crt->rgba8_buffer = desc->rgba8_buffer;
    crt->vis_x0 = desc->vis_x/_M6561_PIXELS_PER_TICK;
    crt->vis_y0 = desc->vis_y;
    crt->vis_w = desc->vis_w/_M6561_PIXELS_PER_TICK;
    crt->vis_h = desc->vis_h;
    crt->vis_x1 = crt->vis_x0 + crt->vis_w;
    crt->vis_y1 = crt->vis_y0 + crt->vis_h;
}

void m6561_init(m6561_t* vic, const m6561_desc_t* desc) {
    CHIPS_ASSERT(vic && desc && desc->fetch_cb);
    CHIPS_ASSERT((0 == desc->rgba8_buffer) || (desc->rgba8_buffer_size >= (_M6561_HTOTAL*8*_M6561_VTOTAL*sizeof(uint32_t))));
    memset(vic, 0, sizeof(*vic));
    _m6561_init_crt(&vic->crt, desc);
    vic->border.enabled = _M6561_HBORDER|_M6561_VBORDER;
    vic->fetch_cb = desc->fetch_cb;
    vic->user_data = desc->user_data;
    vic->sound.sample_period = (desc->tick_hz * _M6561_FIXEDPOINT_SCALE) / desc->sound_hz;
    vic->sound.sample_counter = vic->sound.sample_period;
    vic->sound.sample_mag = desc->sound_magnitude;
    vic->sound.noise.shift = 0x7FFFFC;
}

static void _m6561_reset_crt(m6561_t* vic) {
    vic->crt.x = vic->crt.y = 0;
}

static void _m6561_reset_raster_unit(m6561_t* vic) {
    memset(&vic->rs, 0, sizeof(vic->rs));
}

static void _m6561_reset_border_unit(m6561_t* vic) {
    memset(&vic->border, 0, sizeof(vic->border));
    vic->border.enabled = _M6561_HBORDER|_M6561_VBORDER;
}

static void _m6561_reset_memory_unit(m6561_t* vic) {
    memset(&vic->mem, 0, sizeof(vic->mem));
}

static void _m6561_reset_graphics_unit(m6561_t* vic) {
    memset(&vic->gunit, 0, sizeof(vic->gunit));
}

static void _m6561_reset_audio(m6561_t* vic) {
    for (int i = 0; i < 3; i++) {
        memset(&vic->sound.voice[i], 0, sizeof(m6561_voice_t));
    }
    memset(&vic->sound.noise, 0, sizeof(m6561_noise_t));
    vic->sound.volume = 0;
    vic->sound.noise.shift = 0x7FFFF8;
}

void m6561_reset(m6561_t* vic) {
    CHIPS_ASSERT(vic);
    memset(&vic->regs, 0, sizeof(vic->regs));
    _m6561_reset_raster_unit(vic);
    _m6561_reset_border_unit(vic);
    _m6561_reset_memory_unit(vic);
    _m6561_reset_graphics_unit(vic);
    _m6561_reset_crt(vic);
    _m6561_reset_audio(vic);
}

int m6561_display_width(m6561_t* vic) {
    CHIPS_ASSERT(vic);
    return _M6561_PIXELS_PER_TICK * (vic->debug_vis ? _M6561_HTOTAL : vic->crt.vis_w);
}

int m6561_display_height(m6561_t* vic) {
    CHIPS_ASSERT(vic);
    return vic->debug_vis ? _M6561_VTOTAL : vic->crt.vis_h;
}

uint32_t m6561_color(int i) {
    CHIPS_ASSERT((i >= 0) && (i < 16));
    return _m6561_colors[i];
}

/* update precomputed values when disp-related registers changed */
static void _m6561_regs_dirty(m6561_t* vic) {
    /* each column is 2 ticks */
    vic->rs.row_height = (vic->regs[3] & 1) ? 16 : 8;
    vic->border.left = vic->regs[0] & 0x7F;
    vic->border.right = vic->border.left + (vic->regs[2] & 0x7F) * 2;
    vic->border.top = vic->regs[1];
    vic->border.bottom = vic->border.top + ((vic->regs[3]>>1) & 0x3F) * vic->rs.row_height;
    vic->gunit.inv_color = (vic->regs[15] & 8) == 0;
    vic->gunit.bg_color = _m6561_colors[(vic->regs[15]>>4) & 0xF];
    vic->gunit.brd_color = _m6561_colors[vic->regs[15] & 7];
    vic->gunit.aux_color = _m6561_colors[(vic->regs[14]>>4) & 0xF];
    vic->mem.g_addr_base = ((vic->regs[5] & 0xF)<<10);  // A13..A10
    vic->mem.c_addr_base = (((vic->regs[5]>>4)&0xF)<<10) | // A13..A10
                           (((vic->regs[2]>>7)&1)<<9);    // A9
    vic->sound.voice[0].enabled = 0 != (vic->regs[10] & 0x80);
    vic->sound.voice[0].period = 0x80 * (0x80 - (vic->regs[10] & 0x7F));
    vic->sound.voice[1].enabled = 0 != (vic->regs[11] & 0x80);
    vic->sound.voice[1].period = 0x40 * (0x80 - (vic->regs[11] & 0x7F));
    vic->sound.voice[2].enabled = 0 != (vic->regs[12] & 0x80);
    vic->sound.voice[2].period = 0x20 * (0x80 - (vic->regs[12] & 0x7F));
    vic->sound.noise.enabled = 0 != (vic->regs[13] & 0x80);
    /* 0x40 factor is not a bug, tweaked to 'sound right' */
    vic->sound.noise.period = 0x40 * (0x80 - (vic->regs[13] & 0x7F));
    vic->sound.volume = vic->regs[14] & 0xF;
}

uint64_t m6561_iorq(m6561_t* vic, uint64_t pins) {
    // FIXME: read from 'unmapped' areas returns last
    // fetched graphics data
    CHIPS_ASSERT(vic);
    uint8_t addr = pins & M6561_REG_MASK;
    if (pins & M6561_RW) {
        /* read */
        uint8_t data;
        switch (addr) {
            case 3:
                data = ((vic->rs.v_count & 1)<<7) | (vic->regs[addr] & 0x7F);
                break;
            case 4:
                data = (vic->rs.v_count>>1) & 0xFF;
                break;
            /* not implemented: light pen and potentiometers */
            default:
                data = vic->regs[addr];
                break;
        }
        M6561_SET_DATA(pins, data);
    }
    else {
        /* write */
        const uint8_t data = M6561_GET_DATA(pins);
        vic->regs[addr] = data;
        _m6561_regs_dirty(vic);
    }
    vic->pins = pins;
    return pins;
}

static inline void _m6561_decode_pixels(m6561_t* vic, uint32_t* dst) {
    if (vic->border.enabled) {
        for (int i = 0; i < 4; i++) {
            *dst++ = vic->gunit.brd_color;
        }
    }
    else {
        uint8_t p = vic->gunit.shift;
        if (vic->gunit.color & 8) {
            /* multi-color mode */
            uint32_t fg_color = _m6561_colors[vic->gunit.color & 7];
            switch (p & 0xC0) {
                case 0x00: dst[0] = dst[1] = vic->gunit.bg_color; break;
                case 0x40: dst[0] = dst[1] = vic->gunit.brd_color; break;
                case 0x80: dst[0] = dst[1] = fg_color; break;
                case 0xC0: dst[0] = dst[1] = vic->gunit.aux_color; break;
            }
            switch (p & 0x30) {
                case 0x00: dst[2] = dst[3] = vic->gunit.bg_color; break;
                case 0x10: dst[2] = dst[3] = vic->gunit.brd_color; break;
                case 0x20: dst[2] = dst[3] = fg_color; break;
                case 0x30: dst[2] = dst[3] = vic->gunit.aux_color; break;
            }
        }
        else {
            /* hires mode */
            uint32_t bg, fg;
            if (vic->gunit.inv_color) {
                bg = _m6561_colors[vic->gunit.color & 7];
                fg = vic->gunit.bg_color;
            }
            else {
                bg = vic->gunit.bg_color;
                fg = _m6561_colors[vic->gunit.color & 7];
            }
            dst[0] = (p & (1<<7)) ? fg : bg;
            dst[1] = (p & (1<<6)) ? fg : bg;
            dst[2] = (p & (1<<5)) ? fg : bg;
            dst[3] = (p & (1<<4)) ? fg : bg;
        }
        vic->gunit.shift = p<<4;
    }
}

/* tick function for video output */
void m6561_tick_video(m6561_t* vic) {

    /* decode pixels, each tick is 4 pixels */
    if (vic->crt.rgba8_buffer) {
        int x, y, w;
        if (vic->debug_vis) {
            x = vic->rs.h_count;
            y = vic->rs.v_count;
            w = _M6561_HTOTAL;
            uint32_t* dst = vic->crt.rgba8_buffer + (y * w + x) * _M6561_PIXELS_PER_TICK;
            _m6561_decode_pixels(vic, dst);
        }
        else if ((vic->crt.x >= vic->crt.vis_x0) && (vic->crt.x < vic->crt.vis_x1) &&
                 (vic->crt.y >= vic->crt.vis_y0) && (vic->crt.y < vic->crt.vis_y1))
        {
            const int x = vic->crt.x - vic->crt.vis_x0;
            const int y = vic->crt.y - vic->crt.vis_y0;
            const int w = vic->crt.vis_w;
            uint32_t* dst = vic->crt.rgba8_buffer + (y * w + x) * _M6561_PIXELS_PER_TICK;
            _m6561_decode_pixels(vic, dst);
        }
    }

    /* display-enabled area? */
    if (vic->rs.h_count == vic->border.left) {
        /* enable fetching, but border still active for 1 tick */
        vic->rs.vc_disabled &= ~_M6561_HVC_DISABLE;
        vic->rs.vc = vic->rs.vc_base;
    }
    if (vic->rs.h_count == (vic->border.left+1)) {
        /* switch off horizontal border */
        vic->border.enabled &= ~_M6561_HBORDER;
    }
    if (vic->rs.h_count == (vic->border.right+1)) {
        /* switch on horizontal border */
        vic->border.enabled |= _M6561_HBORDER;
        vic->rs.vc_disabled |= _M6561_HVC_DISABLE;
    }

    /* fetch data */
    if (vic->rs.vc & 1) {
        /* a g-access (graphics data) into pixel shifter */
        uint16_t addr = vic->mem.g_addr_base |
                        ((vic->mem.c_value & 0xFF) * vic->rs.row_height) |
                        vic->rs.rc;
        vic->gunit.shift = (uint8_t) vic->fetch_cb(addr, vic->user_data);
        vic->gunit.color = (vic->mem.c_value>>8) & 0xF;
    }
    else {
        /* a c-access (character code and color) */
        uint16_t addr = vic->mem.c_addr_base | (vic->rs.vc>>1);
        vic->mem.c_value = vic->fetch_cb(addr, vic->user_data);
    }
    if (!vic->rs.vc_disabled) {
        vic->rs.vc = (vic->rs.vc + 1) & ((1<<11)-1);
    }

    /* tick horizontal and vertical counters */
    vic->rs.h_count++;
    vic->crt.x++;
    if (vic->rs.h_count == _M6561_HTOTAL) {
        vic->rs.h_count = 0;
        vic->crt.x = 0;
        vic->rs.v_count++;
        vic->rs.rc++;
        if (vic->rs.rc == vic->rs.row_height) {
            vic->rs.rc = 0;
            vic->rs.row_count++;
            vic->rs.vc_base = vic->rs.vc & 0xFFFE;
        }
        if (vic->rs.v_count == vic->border.top) {
            vic->border.enabled &= ~_M6561_VBORDER;
            vic->rs.vc_disabled &= ~_M6561_VVC_DISABLE;
            vic->rs.vc = 0;
            vic->rs.vc_base = 0;
            vic->rs.row_count = 0;
            vic->rs.rc = 0;
        }
        if (vic->rs.v_count == vic->border.bottom) {
            vic->border.enabled |= _M6561_VBORDER;
            vic->rs.vc_disabled |= _M6561_VVC_DISABLE;
        }
        if (vic->rs.v_count == _M6561_VRETRACEPOS) {
            vic->crt.y = 0;
        }
        else {
            vic->crt.y++;
        }
        if (vic->rs.v_count == _M6561_VTOTAL) {
            vic->rs.v_count = 0;
            vic->border.enabled |= _M6561_VBORDER;
        }
    }
}

/*--- audio engine code ---*/
#define _M6561_BIT(val,bitnr) ((val>>bitnr)&1)
static inline float _m6561_noise_ampl(uint32_t noise_shift) {
    uint32_t amp = (_M6561_BIT(noise_shift,22)<<7) |
                   (_M6561_BIT(noise_shift,20)<<6) |
                   (_M6561_BIT(noise_shift,16)<<5) |
                   (_M6561_BIT(noise_shift,13)<<4) |
                   (_M6561_BIT(noise_shift,11)<<3) |
                   (_M6561_BIT(noise_shift,7)<<2) |
                   (_M6561_BIT(noise_shift,4)<<1) |
                   (_M6561_BIT(noise_shift,2)<<0);
    return ((float)amp) / 256.0f;
}

/* center positive volume value around zero */
static inline float _m6561_dcadjust(m6561_sound_t* snd, float s) {
    snd->dcadj_sum -= snd->dcadj_buf[snd->dcadj_pos];
    snd->dcadj_sum += s;
    snd->dcadj_buf[snd->dcadj_pos] = s;
    snd->dcadj_pos = (snd->dcadj_pos + 1) & (M6561_DCADJ_BUFLEN-1);
    return s - (snd->dcadj_sum / M6561_DCADJ_BUFLEN);
}

/* tick the audio engine, return true if a new sample if ready */
bool m6561_tick_audio(m6561_t* vic) {
    m6561_sound_t* snd = &vic->sound;
    /* tick tone voices */
    for (int i = 0; i < 3; i++) {
        m6561_voice_t* voice = &snd->voice[i];
        if (voice->count == 0) {
            voice->count = voice->period;
            voice->bit = !voice->bit;
        }
        else {
            voice->count--;
        }
        if (voice->bit && voice->enabled) {
            snd->sample_accum += 1.0f;
        }
    }
    /* tick noice channel */
    {
        m6561_noise_t* noise = &snd->noise;
        if (noise->count == 0) {
            noise->count = noise->period;
            noise->bit = !noise->bit;
            if (noise->bit) {
                uint32_t s = noise->shift;
                /* FIXME(?): m6581 uses (s>>17), MAME is (s>>13) */
                uint32_t new_bit = ((s>>22)^(s>>13)) & 1;
                noise->shift = ((s<<1)|new_bit) & 0x007FFFFF;
            }
        }
        else {
            noise->count--;
        }
        if (noise->bit && noise->enabled) {
            snd->sample_accum += _m6561_noise_ampl(noise->shift);
        }
    }
    snd->sample_accum_count += 1.0f;

    /* output a new sample */
    snd->sample_counter -= _M6561_FIXEDPOINT_SCALE;
    if (snd->sample_counter <= 0) {
        snd->sample_counter += snd->sample_period;
        float sm = (snd->sample_accum / snd->sample_accum_count) * (snd->volume / 15.0f);
        snd->sample_accum = 0.0f;
        snd->sample_accum_count = 0.0f;
        snd->sample = _m6561_dcadjust(snd, sm) * snd->sample_mag;
        return true;
    }
    else {
        return false;
    }
}
#endif
