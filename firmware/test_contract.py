#!/usr/bin/env python3
"""Focused source contract for the RP2350 SloLFO control and recovery path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "firmware" / "main.cpp").read_text()
BOARD = (ROOT / "firmware" / "boards" / "slow_lfo_rp2350_matrix.h").read_text()
CMAKE = (ROOT / "firmware" / "CMakeLists.txt").read_text()


def between(start: str, end: str) -> str:
    first = SOURCE.index(start)
    return SOURCE[first:SOURCE.index(end, first)]


assert "pico_board_cmake_set(PICO_PLATFORM, rp2350)" in BOARD
assert "PICO_DEFAULT_WS2812_PIN 25" in BOARD
assert "set(PICO_BOARD slow_lfo_rp2350_matrix" in CMAKE
assert "constexpr uint PWM_SINE = 10, PWM_TRI = 11, PWM_SQUARE = 12;" in SOURCE
assert "constexpr uint ENC_A = 13, ENC_B = 14, ENC_PUSH = 15;" in SOURCE
assert "constexpr uint BUCK_ADC = 26, CAP_ADC = 27, LED_PIN = 25;" in SOURCE
assert "constexpr uint64_t MONTH_US = 30ULL * DAY_US;" in SOURCE
assert "constexpr uint64_t PERIOD_MIN_US = 60ULL * SECOND_US;" in SOURCE
assert "constexpr uint64_t PERIOD_MAX_US = 64ULL * MONTH_US;" in SOURCE
assert "constexpr uint64_t STEP_US[4] = {60ULL * SECOND_US, 3600ULL * SECOND_US, DAY_US / 2, MONTH_US};" in SOURCE

encoder = between("struct Encoder", "} // namespace")
assert "if (!reserve_ready || power_failed)" in encoder
assert "press_eligible=false;" in encoder and "mode_hold_preview=false;" in encoder
assert "mode_hold_preview=held>=500000;" in encoder
assert "if (held>=2000000 && !mode_switched)" in encoder
assert "if (pressed) {turned_during_press=true; mode_hold_preview=false;}" in encoder
assert "selected_unit=(selected_unit+1)%4;" in encoder

main = SOURCE[SOURCE.index("int main()") :]
assert "next_sample=now+2000;" in main
assert "next_wave=now+10000;" in main
assert main.index("blank();") < main.index("append_record(time_us_64(),true)")
assert "cap_mv>=CAP_READY_MV && buck_mv>=BUCK_RECOVER_MV" in main
assert "if (reserve_ready && cap_mv<CAP_READY_HYST_MV)" in main
assert "outputs(reserve_ready?phase_at(now):base_phase);" in main
assert "remainder=(now-anchor_us)%period_us;" in SOURCE

print("RP2350 SloLFO timing, reserve, and encoder contracts passed")
