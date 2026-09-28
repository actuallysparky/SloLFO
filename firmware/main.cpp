#include <algorithm>
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
constexpr uint PWM_SINE = 2, PWM_TRI = 3, PWM_SQUARE = 4;
constexpr uint ENC_A = 5, ENC_B = 6, ENC_PUSH = 7;
constexpr uint BUCK_ADC = 26, CAP_ADC = 27, LED_PIN = 29;
constexpr uint32_t PWM_TOP = 4095;
constexpr uint64_t PERIOD_MIN_US = 60ULL * 1000000ULL;
constexpr uint64_t PERIOD_MAX_US = 2592000ULL * 1000000ULL;
constexpr uint32_t FLASH_BYTES = 2U * 1024U * 1024U;
constexpr uint32_t JOURNAL_BYTES = 64U * 1024U;
constexpr uint32_t JOURNAL_OFFSET = FLASH_BYTES - JOURNAL_BYTES;
constexpr uint32_t PAGE_BYTES = 256, SECTOR_BYTES = 4096;
constexpr uint32_t PAGE_COUNT = JOURNAL_BYTES / PAGE_BYTES;
constexpr uint32_t MAGIC = 0x534C4642; // SLFB
constexpr uint32_t FORMAT = 1;
constexpr uint32_t BUCK_FAIL_MV = 4400;
constexpr uint32_t BUCK_RECOVER_MV = 4750;
constexpr uint32_t CAP_READY_MV = 4550;
constexpr uint32_t CAP_READY_HYST_MV = 4450;
constexpr uint64_t HOUR_US = 3600ULL * 1000000ULL;
constexpr double TAU = 6.283185307179586;
constexpr uint64_t RANGE_US[4] = {PERIOD_MIN_US, 3600ULL*1000000ULL,
    86400ULL*1000000ULL, PERIOD_MAX_US};

struct __attribute__((packed)) Record {
    uint32_t magic, format, seq;
    uint64_t period_us;
    uint32_t phase_q32;
    uint8_t bipolar;
    uint8_t reserved[227];
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
        r.period_us<=PERIOD_MAX_US && r.bipolar<=1 &&
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
        }
    }
    if (newest_page<0 && nonempty) fallback_used=true;
    anchor_us=time_us_64();
}

uint32_t phase_at(uint64_t now) {
    // Double has enough precision for every integer microsecond of a 30-day
    // period. Modulo bounds the quotient and avoids tiny cumulative additions.
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
void pixels(const uint32_t frame[25]) {
    for (int i=0;i<25;++i) pio_sm_put_blocking(led_pio,led_sm,frame[i]<<8u);
    sleep_us(80);
}
void blank() {uint32_t frame[25]{}; pixels(frame);}
void render(uint64_t now, bool charging) {
    uint32_t frame[25]{};
    uint32_t color=charging ? 0x000004 : (flash_error||fallback_used ? 0x040000 :
        bipolar ? 0x000400 : 0x040400); // GRB order
    if (charging) {
        frame[(now/200000)%25]=color;
    } else if ((now/4000000)%2==0) {
        // One of three phase-aligned waveform previews, changing each second.
        int wave=(now/1000000)%3;
        for (int x=0;x<5;++x) {
            double p=(double)x/4.0;
            double v=wave==0 ? 0.5+0.5*std::sin(TAU*p) :
                wave==1 ? (p<0.5 ? 2*p : 2-2*p) : (p<0.5 ? 1.0 : 0.0);
            int y=4-std::clamp((int)std::lround(4*v),0,4);
            frame[5*y+x]=color;
        }
        frame[(phase_at(now)>>29)%5]=0x040404; // phase marker on top row
    } else {
        // Top row marks minute, hour, day, or month scale. Remaining rows
        // show logarithmic progress within the selected scale.
        int range=period_us<3600ULL*1000000ULL ? 0 :
            period_us<86400ULL*1000000ULL ? 1 :
            period_us<604800ULL*1000000ULL ? 2 : 3;
        frame[range]=color;
        double lower=(double)(range==3 ? 604800ULL*1000000ULL : RANGE_US[range]);
        double upper=(double)(range==0 ? RANGE_US[1] :
            range==1 ? RANGE_US[2] : range==2 ? 604800ULL*1000000ULL : PERIOD_MAX_US);
        int n=std::clamp(1+(int)std::lround(19*std::log((double)period_us/lower)/std::log(upper/lower)),1,20);
        for (int i=0;i<n;++i) frame[5+i]=color;
    }
    pixels(frame);
}

struct Encoder {
    uint8_t prev=3; int accum=0; bool pressed=false; uint64_t change_us=0, press_us=0;
    bool long_done=false; bool range_mode=false; int range=0;
    void tick(uint64_t now) {
        uint8_t ab=(gpio_get(ENC_A)<<1)|gpio_get(ENC_B);
        static constexpr int8_t transitions[16]={0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
        accum+=transitions[(prev<<2)|ab]; prev=ab;
        if (accum>=4 || accum<=-4) {
            int dir=accum>0?1:-1; accum=0;
            if (reserve_ready && !power_failed) turn(dir,now);
        }
        bool raw=!gpio_get(ENC_PUSH);
        if (raw!=pressed && now-change_us>=30000) {
            pressed=raw; change_us=now;
            if (pressed) {press_us=now; long_done=false;}
            else if (!long_done && reserve_ready && !power_failed) range_mode=!range_mode;
        }
        if (pressed && !long_done && now-press_us>=800000) {
            long_done=true;
            if (reserve_ready && !power_failed) bipolar=!bipolar;
        }
    }
    void turn(int dir,uint64_t now) {
        if (range_mode) {
            range=std::clamp(range+dir,0,3);
            reanchor(now); period_us=RANGE_US[range];
        } else {
            reanchor(now);
            double value=(double)period_us*std::exp((double)dir*std::log((double)PERIOD_MAX_US/(double)PERIOD_MIN_US)/192.0);
            period_us=(uint64_t)std::clamp(value,(double)PERIOD_MIN_US,(double)PERIOD_MAX_US);
        }
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
        if (now>=next_display) {next_display=now+100000; render(now,!reserve_ready);}
        if (reserve_ready && now>=next_checkpoint_us) {
            (void)append_record(now,false); next_checkpoint_us=now+HOUR_US;
        }
        tight_loop_contents();
    }
}
