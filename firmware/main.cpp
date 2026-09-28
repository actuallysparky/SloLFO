#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"

// Electrical contract: the board's divider ratio is 1:2 on both ADC inputs.
// These thresholds are prototype defaults; scope the rails and recalibrate.
namespace {
constexpr uint PWM_SINE = 10, PWM_TRI = 11, PWM_SQUARE = 12;
constexpr uint ENC_A = 13, ENC_B = 14, ENC_PUSH = 15;
constexpr uint BUCK_ADC = 26, CAP_ADC = 27, LED_PIN = 25;
constexpr uint32_t PWM_TOP = 4095;
constexpr uint64_t SECOND_US = 1000000ULL;
constexpr uint64_t DAY_US = 86400ULL * SECOND_US;
constexpr uint64_t MONTH_US = 30ULL * DAY_US;
constexpr uint64_t PERIOD_MIN_US = 60ULL * SECOND_US;
constexpr uint64_t PERIOD_MAX_US = 64ULL * MONTH_US;
constexpr uint64_t MINIMUM_US[4] = {PERIOD_MIN_US, 3600ULL * SECOND_US, DAY_US, MONTH_US};
constexpr uint64_t MAXIMUM_US[4] = {3600ULL * SECOND_US, DAY_US, 64ULL * DAY_US, PERIOD_MAX_US};
constexpr uint64_t STEP_US[4] = {60ULL * SECOND_US, 3600ULL * SECOND_US, DAY_US / 2, MONTH_US};
constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
constexpr uint32_t JOURNAL_BYTES = 64U * 1024U;
constexpr uint32_t JOURNAL_OFFSET = FLASH_BYTES - JOURNAL_BYTES;
constexpr uint32_t PAGE_BYTES = 256, SECTOR_BYTES = 4096;
constexpr uint32_t PAGE_COUNT = JOURNAL_BYTES / PAGE_BYTES;
constexpr uint32_t MAGIC = 0x534C4632; // SLF2; incompatible with the older RP2040 journal.
constexpr uint32_t FORMAT = 2;
constexpr uint32_t BUCK_FAIL_MV = 4400;
constexpr uint32_t BUCK_RECOVER_MV = 4750;
constexpr uint32_t CAP_READY_MV = 4550;
constexpr uint32_t CAP_READY_HYST_MV = 4450;
constexpr uint64_t HOUR_US = 3600ULL * 1000000ULL;
constexpr double TAU = 6.283185307179586;
constexpr int MATRIX_SIDE = 8, MATRIX_CELLS = 64;
constexpr int TRIANGLE_PATH_BI[] = {32,24,17,9,2,11,19,28,36,45,53,62,55,47,39};
constexpr int TRIANGLE_PATH_UNI[] = {16,8,9,1,2,3,11,12,20,21,29,30,31,23};

struct Color { uint8_t r, g, b; };
constexpr Color UNIT_COLOR[4] = {
    {4, 19, 25},   // minutes: cyan
    {6, 25, 11},   // hours: green
    {25, 16, 3},   // days: amber
    {23, 7, 25},   // months: violet
};

struct __attribute__((packed)) Record {
    uint32_t magic, format, seq;
    uint64_t period_us;
    uint32_t phase_q32;
    uint8_t bipolar;
    uint8_t selected_unit;
    uint8_t reserved[226];
    uint32_t crc;
};
static_assert(sizeof(Record) == PAGE_BYTES);

uint64_t period_us = PERIOD_MIN_US;
uint32_t base_phase = 0;
uint64_t anchor_us = 0;
bool bipolar = false;
bool reserve_ready = false;
bool flash_error = false;
bool fallback_used = false;
bool power_failed = false;
uint8_t selected_unit = 0;
uint8_t display_view = 0; // 0 waveform, 1 selector, 2 period
uint64_t view_deadline_us = 0;
bool mode_hold_preview = false;
uint32_t journal_seq = 0;
int newest_page = -1;
uint64_t next_checkpoint_us = 0;
PIO led_pio = pio0;
uint led_sm = 0;
constexpr uint WS_T1=2, WS_T2=5, WS_T3=3;
const uint16_t ws_instructions[] = {
    static_cast<uint16_t>(pio_encode_out(pio_x,1)|pio_encode_sideset(1,0)|pio_encode_delay(WS_T3-1)),
    static_cast<uint16_t>(pio_encode_jmp_not_x(3)|pio_encode_sideset(1,1)|pio_encode_delay(WS_T1-1)),
    static_cast<uint16_t>(pio_encode_jmp(0)|pio_encode_sideset(1,1)|pio_encode_delay(WS_T2-1)),
    static_cast<uint16_t>(pio_encode_nop()|pio_encode_sideset(1,0)|pio_encode_delay(WS_T2-1)),
};
const pio_program ws_program={ws_instructions,4,-1};

uint32_t crc32(const uint8_t *data, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i=0; i<n; ++i) {
        crc ^= data[i];
        for (int bit=0; bit<8; ++bit) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}

const uint8_t *page_address(uint32_t page) {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + JOURNAL_OFFSET + page*PAGE_BYTES);
}
bool erased(uint32_t page) {
    const uint8_t *p=page_address(page);
    for (uint32_t i=0;i<PAGE_BYTES;++i) if (p[i]!=0xff) return false;
    return true;
}
bool valid(const Record &r) {
    return r.magic==MAGIC && r.format==FORMAT && r.period_us>=PERIOD_MIN_US &&
        r.period_us<=PERIOD_MAX_US && r.bipolar<=1 && r.selected_unit<4 &&
        crc32(reinterpret_cast<const uint8_t*>(&r),PAGE_BYTES-4)==r.crc;
}
void scan_journal() {
    uint32_t nonempty=0;
    for (uint32_t p=0;p<PAGE_COUNT;++p) {
        if (erased(p)) continue;
        ++nonempty;
        const Record &r=*reinterpret_cast<const Record*>(page_address(p));
        if (!valid(r)) {fallback_used=true; continue;}
        if (newest_page<0 || static_cast<int32_t>(r.seq-journal_seq)>0) {
            newest_page=static_cast<int>(p); journal_seq=r.seq;
            period_us=r.period_us; base_phase=r.phase_q32; bipolar=r.bipolar!=0;
            selected_unit=r.selected_unit;
        }
    }
    if (newest_page<0 && nonempty) fallback_used=true;
    anchor_us=time_us_64();
}

uint32_t phase_at(uint64_t now) {
    // Modulo bounds the quotient for periods as long as 64 fixed months.
    uint64_t remainder=(now-anchor_us)%period_us;
    uint32_t advance=static_cast<uint32_t>((double)remainder*4294967296.0/(double)period_us);
    return base_phase+advance;
}
void reanchor(uint64_t now) { base_phase=phase_at(now); anchor_us=now; }

void flash_program_cb(void *arg) {
    auto *pair=static_cast<std::pair<uint32_t,const uint8_t*>*>(arg);
    flash_range_program(pair->first,pair->second,PAGE_BYTES);
}
void flash_erase_cb(void *arg) {
    uint32_t offset=*static_cast<uint32_t*>(arg);
    flash_range_erase(offset,SECTOR_BYTES);
}

bool prepare_next_sector() {
    if (newest_page<0) return true;
    uint32_t next=(static_cast<uint32_t>(newest_page)+1)%PAGE_COUNT;
    // Erase the sector ahead while current valid records remain in the current
    // sector. Do this only under stable power, outside the emergency path.
    uint32_t page_in_sector=static_cast<uint32_t>(newest_page)%16;
    if (page_in_sector<8) return true;
    uint32_t sector_page=((static_cast<uint32_t>(newest_page)/16+1)%16)*16;
    if (erased(sector_page)) return true;
    uint32_t offset=JOURNAL_OFFSET+sector_page*PAGE_BYTES;
    int rc=flash_safe_execute(flash_erase_cb,&offset,1000);
    if (rc!=PICO_OK) {flash_error=true; return false;}
    return erased(sector_page);
}

bool append_record(uint64_t now, bool emergency) {
    uint32_t next= newest_page<0 ? 0 : (static_cast<uint32_t>(newest_page)+1)%PAGE_COUNT;
    // A brownout may leave a partly programmed page. Skip it rather than
    // making the journal permanently unwritable on the next boot.
    uint32_t searched=0;
    while (!erased(next) && searched++<PAGE_COUNT) next=(next+1)%PAGE_COUNT;
    if (searched>PAGE_COUNT || !erased(next)) { flash_error=true; return false; }
    alignas(PAGE_BYTES) Record r{};
    std::memset(&r,0xff,sizeof(r));
    r.magic=MAGIC; r.format=FORMAT; r.seq=journal_seq+1;
    r.period_us=period_us; r.phase_q32=phase_at(now); r.bipolar=bipolar?1:0;
    r.selected_unit=selected_unit;
    r.crc=crc32(reinterpret_cast<const uint8_t*>(&r),PAGE_BYTES-4);
    std::pair<uint32_t,const uint8_t*> args{JOURNAL_OFFSET+next*PAGE_BYTES,
        reinterpret_cast<const uint8_t*>(&r)};
    int rc=flash_safe_execute(flash_program_cb,&args,100);
    if (rc!=PICO_OK || !valid(*reinterpret_cast<const Record*>(page_address(next)))) {
        flash_error=true; return false;
    }
    newest_page=static_cast<int>(next); journal_seq=r.seq;
    if (!emergency) prepare_next_sector();
    return true;
}

uint32_t adc_mv(uint channel) {
    adc_select_input(channel);
    (void)adc_read(); // let the mux/source settle
    uint32_t sum=0;
    for (int i=0;i<4;++i) sum+=adc_read();
    return (sum*3300u*2u)/(4u*4095u);
}
void init_adc() {adc_init(); adc_gpio_init(BUCK_ADC); adc_gpio_init(CAP_ADC);}

void init_pwm() {
    for (uint gpio: {PWM_SINE,PWM_TRI,PWM_SQUARE}) gpio_set_function(gpio,GPIO_FUNC_PWM);
    uint slice_sine=pwm_gpio_to_slice_num(PWM_SINE);
    uint slice_square=pwm_gpio_to_slice_num(PWM_SQUARE);
    pwm_config cfg=pwm_get_default_config(); pwm_config_set_wrap(&cfg,PWM_TOP);
    pwm_init(slice_sine,&cfg,true); pwm_init(slice_square,&cfg,true);
}
void outputs(uint32_t phase) {
    double p=(double)phase/4294967296.0;
    double sine=0.5+0.5*std::sin(TAU*p);
    double tri=p<0.25 ? 0.5+2*p : (p<0.75 ? 1.5-2*p : 2*p-1.5);
    auto duty=[&](double value)->uint16_t {
        double d=bipolar?value:(0.5+0.5*value);
        return static_cast<uint16_t>(std::clamp(std::lround(d*PWM_TOP),0L,(long)PWM_TOP));
    };
    pwm_set_gpio_level(PWM_SINE,duty(sine));
    pwm_set_gpio_level(PWM_TRI,duty(tri));
    pwm_set_gpio_level(PWM_SQUARE,duty(p<0.5?1.0:0.0));
}

void init_leds() {
    uint offset=pio_add_program(led_pio,&ws_program);
    pio_sm_config c=pio_get_default_sm_config();
    sm_config_set_wrap(&c,offset,offset+3);
    sm_config_set_sideset(&c,1,false,false);
    sm_config_set_sideset_pins(&c,LED_PIN);
    sm_config_set_out_shift(&c,false,true,24);
    sm_config_set_fifo_join(&c,PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c,(float)clock_get_hz(clk_sys)/(800000.0f*(WS_T1+WS_T2+WS_T3)));
    pio_gpio_init(led_pio,LED_PIN); pio_sm_set_consecutive_pindirs(led_pio,led_sm,LED_PIN,1,true);
    pio_sm_init(led_pio,led_sm,offset,&c); pio_sm_set_enabled(led_pio,led_sm,true);
}
using Frame = std::array<Color, MATRIX_CELLS>;

Color dim(Color c, double level) {
    level=std::clamp(level,0.0,1.0);
    return {static_cast<uint8_t>(std::lround(c.r*level)),
            static_cast<uint8_t>(std::lround(c.g*level)),
            static_cast<uint8_t>(std::lround(c.b*level))};
}

std::array<int, MATRIX_CELLS> charge_spiral() {
    std::array<int, MATRIX_CELLS> path{};
    int left=0,right=7,top=0,bottom=7,index=0;
    while (left<=right && top<=bottom) {
        for (int x=left;x<=right;++x) path[index++]=top*8+x;
        ++top;
        for (int y=top;y<=bottom;++y) path[index++]=y*8+right;
        --right;
        if (top<=bottom) {
            for (int x=right;x>=left;--x) path[index++]=bottom*8+x;
            --bottom;
        }
        if (left<=right) {
            for (int y=bottom;y>=top;--y) path[index++]=y*8+left;
            ++left;
        }
    }
    return path;
}

std::array<int, MATRIX_CELLS> pie_order() {
    std::array<int, MATRIX_CELLS> order{};
    for (int i=0;i<MATRIX_CELLS;++i) order[i]=i;
    auto angle=[](int i) {
        double a=std::atan2(static_cast<double>(i%8)-3.5,3.5-static_cast<double>(i/8));
        return a<0 ? a+TAU : a;
    };
    std::sort(order.begin(),order.end(),[&](int a,int b) {
        const double aa=angle(a),bb=angle(b);
        if (aa!=bb) return aa<bb;
        return std::hypot(a%8-3.5,a/8-3.5)<std::hypot(b%8-3.5,b/8-3.5);
    });
    return order;
}

uint32_t grb(Color c) {return (uint32_t(c.g)<<16)|(uint32_t(c.r)<<8)|c.b;}
void pixels(const Frame &frame) {
    // The matrix chain is routed in alternating rows. Keep logical art row-major.
    for (int i=0;i<MATRIX_CELLS;++i) {
        const int y=i/8, x=(y&1) ? 7-i%8 : i%8;
        pio_sm_put_blocking(led_pio,led_sm,grb(frame[y*8+x])<<8u);
    }
    sleep_us(80);
}
void blank() {pixels(Frame{});}

void render(uint64_t now, uint32_t cap_mv) {
    Frame frame{};
    const Color color=UNIT_COLOR[selected_unit];
    if (!reserve_ready) {
        static const auto spiral=charge_spiral();
        const double filled=std::clamp(static_cast<double>(cap_mv)/CAP_READY_MV,0.0,1.0)*64.0;
        const int complete=std::min(63,static_cast<int>(filled));
        for (int i=0;i<complete;++i) frame[spiral[i]]={2,8,14};
        frame[spiral[complete]]={23,27,28};
    } else if (mode_hold_preview) {
        for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
            if (y<4) frame[y*8+x]={2,8,25};
            else if (bipolar) frame[y*8+x]={25,2,2};
        }
    } else if (display_view==1) {
        for (int u=0;u<4;++u) {
            int bx=(u&1)*4,by=(u/2)*4;
            for (int y=0;y<4;++y) for (int x=0;x<4;++x)
                frame[(by+y)*8+bx+x]=dim(UNIT_COLOR[u],u==selected_unit?1.0:0.10);
        }
    } else if (display_view==2 && selected_unit>=2) {
        const double filled=static_cast<double>(period_us)/(selected_unit==2?DAY_US:MONTH_US);
        for (int i=0;i<64;++i) frame[i]=dim(color,filled-i);
    } else if (display_view==2) {
        static const auto order=pie_order();
        const double filled=std::min(1.0,static_cast<double>(period_us)/MAXIMUM_US[selected_unit])*64.0;
        for (int i=0;i<64;++i) frame[order[i]]=dim(color,filled-i);
    } else {
        const int *path=bipolar?TRIANGLE_PATH_BI:TRIANGLE_PATH_UNI;
        const int count=bipolar?15:14;
        for (int i=0;i<count;++i) frame[path[i]]=dim(color,0.48);
        const int marker=std::min(count-1,static_cast<int>(static_cast<double>(phase_at(now))/4294967296.0*count));
        frame[path[marker]]={28,28,28};
    }
    if (flash_error || fallback_used) frame[63]={28,0,0};
    pixels(frame);
}

struct Encoder {
    uint8_t prev=3;
    int accum=0;
    bool raw_press=false, pressed=false, press_eligible=false;
    bool turned_during_press=false, mode_switched=false;
    uint64_t raw_change_us=0, press_us=0;

    void tick(uint64_t now) {
        uint8_t ab=(gpio_get(ENC_A)<<1)|gpio_get(ENC_B);
        static constexpr int8_t transitions[16]={0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
        accum+=transitions[(prev<<2)|ab]; prev=ab;
        if (accum>=4 || accum<=-4) {
            int dir=accum>0?1:-1; accum=0;
            if (pressed) {turned_during_press=true; mode_hold_preview=false;}
            if (reserve_ready && !power_failed) turn(dir,now);
        }
        bool raw=!gpio_get(ENC_PUSH);
        if (raw!=raw_press) {raw_press=raw; raw_change_us=now;}
        if (raw_press!=pressed && now-raw_change_us>=30000) {
            pressed=raw_press;
            if (pressed) {
                press_us=now; press_eligible=reserve_ready && !power_failed;
                turned_during_press=false; mode_switched=false;
            } else {
                mode_hold_preview=false;
                if (press_eligible && !turned_during_press && !power_failed &&
                    now-press_us<500000) {
                    selected_unit=(selected_unit+1)%4;
                    display_view=1; view_deadline_us=now+2000000;
                }
            }
        }
        if (pressed && press_eligible && !turned_during_press && reserve_ready && !power_failed) {
            const uint64_t held=now-press_us;
            mode_hold_preview=held>=500000;
            if (held>=2000000 && !mode_switched) {
                bipolar=!bipolar; mode_switched=true;
            }
        }
    }
    void turn(int dir,uint64_t now) {
        const int u=selected_unit;
        reanchor(now);
        if (period_us<MINIMUM_US[u]) period_us=MINIMUM_US[u];
        else if (period_us>MAXIMUM_US[u]) period_us=MAXIMUM_US[u];
        else {
            int64_t next=static_cast<int64_t>(period_us)+dir*static_cast<int64_t>(STEP_US[u]);
            period_us=static_cast<uint64_t>(std::clamp(next,static_cast<int64_t>(MINIMUM_US[u]),
                                                      static_cast<int64_t>(MAXIMUM_US[u])));
        }
        display_view=2; view_deadline_us=now+2000000;
    }
};
} // namespace

int main() {
    stdio_init_all();
    init_adc(); scan_journal();
    for (uint pin: {ENC_A,ENC_B,ENC_PUSH}) {gpio_init(pin); gpio_set_dir(pin,false); gpio_pull_up(pin);}
    init_pwm(); init_leds();
    Encoder encoder; next_checkpoint_us=time_us_64()+HOUR_US;
    uint64_t next_sample=0,next_display=0,next_wave=0;
    uint32_t cap_mv=0,buck_mv=0; int fail_count=0;
    while (true) {
        uint64_t now=time_us_64();
        if (now>=next_sample) {
            next_sample=now+2000;
            buck_mv=adc_mv(0); cap_mv=adc_mv(1);
            if (buck_mv<BUCK_FAIL_MV) ++fail_count; else fail_count=0;
            if (!power_failed && fail_count>=2) {
                power_failed=true;
                blank();
                if (reserve_ready) (void)append_record(time_us_64(),true);
                next_display=UINT64_MAX;
            }
            if (!reserve_ready && cap_mv>=CAP_READY_MV && buck_mv>=BUCK_RECOVER_MV) {
                reserve_ready=true; anchor_us=now;
                if (newest_page<0) (void)append_record(now,false);
                next_checkpoint_us=now+HOUR_US;
            }
            if (reserve_ready && cap_mv<CAP_READY_HYST_MV) {
                reanchor(now);
                reserve_ready=false;
            }
        }
        if (power_failed) {
            // A returning upstream supply is handled as a clean reboot; the
            // stored phase remains authoritative during the brownout window.
            if (buck_mv>BUCK_RECOVER_MV) watchdog_reboot(0,0,1);
            tight_loop_contents(); continue;
        }
        if (now>=next_wave) {
            next_wave=now+10000;
            outputs(reserve_ready?phase_at(now):base_phase);
        }
        encoder.tick(now);
        if (display_view!=0 && now>=view_deadline_us) display_view=0;
        if (now>=next_display) {next_display=now+100000; render(now,cap_mv);}
        if (reserve_ready && now>=next_checkpoint_us) {
            (void)append_record(now,false); next_checkpoint_us=now+HOUR_US;
        }
        tight_loop_contents();
    }
}
