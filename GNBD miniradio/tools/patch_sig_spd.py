# -*- coding: utf-8 -*-
"""SIG DBG -> SIG spd (LOW/HIGH 속도 전용) 로 변경."""
import io

# 1) radio.c
p = 'main/radio.c'
t = io.open(p, encoding='utf-8').read()

t = t.replace('"SIG DBG", "BOTTOM",', '"SIG spd", "BOTTOM",')

# rsq_poll 주기: LOW=250ms, HIGH=30ms
old = '''    bool fast = (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK);
    uint32_t period = fast ? 30u : (uint32_t)RSQ_PERIOD;
    if (fast && s_dbg_sig == 3) {
        period = 250u;          /* 진단: 갱신 주기만 늦춰 비교 */
    }
'''
new = '''    bool fast = (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK);
    /* SIG spd: LOW=250ms(부드러움), HIGH=30ms(빠름) */
    uint32_t period = fast ? (s_dbg_sig == 0 ? 250u : 30u)
                           : (uint32_t)RSQ_PERIOD;
'''
assert old in t
t = t.replace(old, new)

# sigdbg_add 계측 함수/블록 삭제
start = t.index('/* --------------------------------------------------------- signal dbg')
end = t.index('void rsq_poll(uint32_t now)')
t = t[:start] + t[end:]

# HOLD 분기 및 계측 호출 제거
t = t.replace('''        if (s_dbg_sig == 2) {
            return;             /* 진단: 리드로우 정지 */
        }
        int64_t t0 = esp_timer_get_time();
        ui_draw_adv_signal4b(&s_ui);
        if (s_dbg_sig) {
            sigdbg_add((uint32_t)(esp_timer_get_time() - t0), r.rssi, r.snr);
        }
        return;''', '''        ui_draw_adv_signal4b(&s_ui);
        return;''')

old2 = '''    if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) {
        if (s_dbg_sig == 2) {
            /* 진단: 패널 리드로우 정지 (값/스켈치는 계속 갱신) */
        } else {
            int64_t t0 = esp_timer_get_time();
            if (mute_changed) {
                ui_draw_all(&s_ui);   /* SQ ?? ?? */
            } else {
                ui_draw_adv_signal4b(&s_ui);
            }
            if (s_dbg_sig) {
                sigdbg_add((uint32_t)(esp_timer_get_time() - t0), r.rssi, r.snr);
            }
        }
    }'''
new2 = '''    if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) {
        if (mute_changed) {
            ui_draw_all(&s_ui);   /* SQ 상태 갱신 */
        } else {
            ui_draw_adv_signal4b(&s_ui);
        }
    }'''
assert old2 in t
t = t.replace(old2, new2)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) settings_data.c
p = 'main/settings_data.c'
t = io.open(p, encoding='utf-8').read()
t = t.replace('{ ST_SIGDBG,     "SIG DBG",    1 },', '{ ST_SIGDBG,     "SIG spd",    1 },')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) menu.c
p = 'main/menu.c'
t = io.open(p, encoding='utf-8').read()
old3 = '''    snprintf(s_sbuf[ST_SIGDBG], sizeof(s_sbuf[ST_SIGDBG]), "%s",
             (const char *[]){"OFF", "LOG", "HOLD", "SLOW"}[s_dbg_sig > 3 ? 0
                                                                       : s_dbg_sig]);'''
new3 = '''    snprintf(s_sbuf[ST_SIGDBG], sizeof(s_sbuf[ST_SIGDBG]), "%s",
             (const char *[]){"LOW", "HIGH"}[s_dbg_sig > 1 ? 1
                                                           : s_dbg_sig]);'''
assert old3 in t
t = t.replace(old3, new3)

old4 = '''        case ST_SIGDBG:
            opt_label(n++, "OFF");
            opt_label(n++, "LOG (NORMAL)");
            opt_label(n++, "HOLD (NO REDRAW)");
            opt_label(n++, "SLOW 250ms");
            s_opt.title = "SIGNAL DBG";
            live = false;
            break;'''
new4 = '''        case ST_SIGDBG:
            opt_label(n++, "LOW (250ms)");
            opt_label(n++, "HIGH (30ms)");
            s_opt.title = "SIGNAL SPEED";
            live = false;
            break;'''
assert old4 in t
t = t.replace(old4, new4)

old5 = '''        case ST_SIGDBG:
            s_dbg_sig = (uint8_t)((idx < 0) ? 0 : (idx > 3 ? 3 : idx));
            toast(s_dbg_sig == 2 ? "SIG HOLD"
                  : (s_dbg_sig == 3 ? "SIG SLOW 250ms"
                  : (s_dbg_sig == 1 ? "SIG LOG" : "SIG OFF")));
            break;'''
new5 = '''        case ST_SIGDBG:
            s_dbg_sig = (uint8_t)((idx <= 0) ? 0 : 1);
            toast(s_dbg_sig ? "SIG spd HIGH" : "SIG spd LOW");
            break;'''
assert old5 in t
t = t.replace(old5, new5)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 4) app.h 주석 + 기본값
p = 'main/app.h'
t = io.open(p, encoding='utf-8').read()
import re
t = re.sub(r'extern uint8_t  s_dbg_sig;[^\n]*\n',
           'extern uint8_t  s_dbg_sig;   /* signal speed: 0=LOW(250ms), 1=HIGH(30ms) */\n',
           t)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

p = 'main/app_state.c'
t = io.open(p, encoding='utf-8').read()
t = re.sub(r'uint8_t  s_dbg_sig;[^\n]*\n',
           'uint8_t  s_dbg_sig = 1;   /* 0=LOW(250ms), 1=HIGH(30ms) */\n',
           t)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
