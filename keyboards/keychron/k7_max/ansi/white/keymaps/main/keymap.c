/* Copyright 2024 @ Keychron (https://www.keychron.com)
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H
#include "keychron_common.h"
#include "quantum/mousekey.h"

// シート移動
#define CT_PGUP  LCTL(KC_PGUP)
#define CT_PGDN  LCTL(KC_PGDN)
// コピペ
#define CM_Z     LCMD(KC_Z) // mac
#define CM_X     LCMD(KC_X)
#define CM_C     LCMD(KC_C)
#define CM_V     LCMD(KC_V)
#define CT_Z     LCTL(KC_Z) // win
#define CT_X     LCTL(KC_X)
#define CT_C     LCTL(KC_C)
#define CT_V     LCTL(KC_V)
#define CT_Y     LCTL(KC_Y)
// リロード
#define CM_R     LCMD(KC_R) // mac
#define CT_R     LCTL(KC_R) // win
// 新規タブ
#define CM_T     LCMD(KC_T) // mac
#define CT_T     LCTL(KC_T) // win
// 文字選択
#define SF_LEFT  RSFT(KC_LEFT)
#define SF_DOWN  RSFT(KC_DOWN)
#define SF_UP    RSFT(KC_UP)
#define SF_RGHT  RSFT(KC_RGHT)
#define SF_HOME  RSFT(KC_HOME)
#define SF_END   RSFT(KC_END)
// はてなとびっくり
#define SF_SLSH  RSFT(KC_SLSH)
#define SF_1     RSFT(KC_1)
// IME切替
#define CT_SPACE LCTL(KC_SPACE) // mac
#define SF_CAPS  LSFT(KC_CAPS)  // win
// タブ切替
#define CT_TAB   LCTL(KC_TAB)
// スリープ
#define M_SLEEP_DBL_TAP_MS 300
// アプリ終了
#define M_QUI_Q_DBL_TAP_MS 300
// IME強制切替
#define IME_SET_TAP_MS     300
// zプレフィックス
#define ZP_TIMEOUT_MS      500
// #define ZP_USE_ON_WIN true


// IME
static struct {
    bool down;  // キーを押しっぱなし中か
    bool need_force; // 押下時に通常トグルを送らなかったか
    uint16_t timer; // 押した時刻
} ime_key[4];

enum {IDX_M_EN, IDX_M_JP, IDX_W_EN, IDX_W_JP};

// zプレフィックス
typedef struct {
    bool waiting; // zを押してタイムアウト待ち中か
    uint16_t timer; // zを押した時刻
} zp_ctx_t;

static zp_ctx_t zp = {false, 0};

//─── レイヤー定義 ───────────────────────────────────────────────────────────
enum layers{    // ここに書く順番で優先度が決まる（末尾に行くほど優先度高）
    MAC_BASE,
    WIN_BASE,
    MAC_CAPS,
    WIN_CAPS,
    MAC_RSFT,
    WIN_RSFT,
    MAC_NUM,
    WIN_NUM,
    FN2,
};

//─── カスタムキーコード ────────────────────────────────────────────────────────
enum custom_keycodes {
    DFU = SAFE_RANGE,
    M_EN_SET, // mac 英語
    M_JP_SET, // mac 日本語
    W_EN_SET, // win 英語
    W_JP_SET, // win 日本語
    M_SLEEP,  // mac スリープ
};

//───状態フラグ────────────────────────────────────────────────────────────────
static bool m_is_english = true;
static bool w_is_english = true;
static bool m_sleep_waiting   = false;
static bool m_gui_q_waiting   = false;

//───タイマー─────────────────────────────────────────────────────────────────
static uint16_t m_sleep_tap_timer = 0;
static uint16_t m_gui_q_timer = 0;

//───ヘルパ───────────────────────────────────────────────────────────────────

/* MAC：IME切替を送信 */
static void send_ctrl_space_mac(void) {
    register_code(KC_LCTL);   // ctrl押下
    wait_ms(50);              // 待機
    tap_code(KC_SPACE);       // Space
    wait_ms(30);              // 待機
    unregister_code(KC_LCTL); // ctrl離上
}
/* WIN：IME切替を送信 */
static void send_shift_caps_win(void) {
    register_code(KC_LSFT);
    wait_ms(50);
    tap_code(KC_CAPS);
    wait_ms(30);
    unregister_code(KC_LSFT);
}
/* フラグが false のときだけ Enter を送信する */
static void enter_jp_only(void) {
    if (!m_is_english && layer_state_is(MAC_BASE)) {
        tap_code(KC_ENT);
    } else if (!w_is_english && layer_state_is(WIN_BASE)) {
        tap_code(KC_ENT);
    }
}

/* shiftを押しながらかっこを入力 */
static void brackets_mods_shift(uint16_t lbrac, uint16_t rbrac) {
    // 現在の修飾キー状態を保持
    uint8_t orig_mods = get_mods();
    // Shift を一時解除して送出
    unregister_mods(MOD_MASK_SHIFT);
    send_keyboard_report();

    // かっこをを挿入
    tap_code16(lbrac);  // '('
    tap_code16(rbrac);  // ')'
    wait_ms(10);
    enter_jp_only();
    wait_ms(10);
    tap_code16(KC_LEFT);

    // 修飾キーを元に戻す
    set_mods(orig_mods);
    send_keyboard_report();
}

/* macのスリープ */
static void mac_display_sleep(void) {
    register_code(KC_LCTL);
    register_code(KC_LSFT);
    wait_ms(20);                 // ← modifiers を 1 レポート分待つ
    tap_code(KC_EJCT);           // ⏏ の方が確実（Mac では Ctrl+Shift+Eject）
    unregister_code(KC_LSFT);
    unregister_code(KC_LCTL);
}



/* ------matrix_scan_user------------------------- */
void matrix_scan_user(void) {
    /* IME切替 */
    // Mac 英数／かな
    if (ime_key[IDX_M_EN].down && ime_key[IDX_M_EN].need_force
        && timer_elapsed(ime_key[IDX_M_EN].timer) >= IME_SET_TAP_MS) {
        send_ctrl_space_mac();                // 強制トグル
        ime_key[IDX_M_EN].need_force = false; // 1回だけ
    }
    if (ime_key[IDX_M_JP].down && ime_key[IDX_M_JP].need_force
        && timer_elapsed(ime_key[IDX_M_JP].timer) >= IME_SET_TAP_MS) {
        send_ctrl_space_mac();
        ime_key[IDX_M_JP].need_force = false;
    }
    // Windows 英数／かな
    if (ime_key[IDX_W_EN].down && ime_key[IDX_W_EN].need_force
        && timer_elapsed(ime_key[IDX_W_EN].timer) >= IME_SET_TAP_MS) {
        send_shift_caps_win();
        ime_key[IDX_W_EN].need_force = false;
    }
    if (ime_key[IDX_W_JP].down && ime_key[IDX_W_JP].need_force
        && timer_elapsed(ime_key[IDX_W_JP].timer) >= IME_SET_TAP_MS) {
        send_shift_caps_win();
        ime_key[IDX_W_JP].need_force = false;
    }

    /* zプレフィックス タイムアウト判定 */
    if (zp.waiting && timer_elapsed(zp.timer) > ZP_TIMEOUT_MS) {
        zp.waiting = false; // タイムアウト時間を超過したら無効化
    }
}


/***************************************************************************
 * キーマップ                                                                *
 ***************************************************************************/
// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

/* BASE */
[MAC_BASE] = LAYOUT_ansi_68(
    KC_ESC,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_SCLN,  KC_EQL,   KC_BSPC,           KC_DEL,
    KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,           KC_HOME,
MO(MAC_CAPS), KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_MINS,  KC_QUOT,            KC_ENT,            KC_PGUP,
    KC_LSFT,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,                   MO(MAC_RSFT),KC_UP,   KC_PGDN,
    KC_LCTL,  KC_LALT,  KC_LGUI,                                KC_SPC,                                 KC_RGUI,MO(MAC_CAPS),MO(FN2), KC_LEFT,  KC_DOWN, KC_RGHT),

[WIN_BASE] = LAYOUT_ansi_68(
    KC_ESC,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_SCLN,  KC_EQL,   KC_BSPC,           KC_DEL,
    KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,           KC_HOME,
MO(WIN_CAPS), KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_MINS,  KC_QUOT,            KC_ENT,            KC_PGUP,
    KC_LSFT,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,                   MO(WIN_RSFT),KC_UP,   KC_PGDN,
    KC_LCTL,  KC_LGUI,  KC_LALT,                                KC_SPC,                                 KC_RALT,MO(WIN_CAPS),MO(FN2), KC_LEFT,  KC_DOWN, KC_RGHT),

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* CAPS */
[MAC_CAPS] = LAYOUT_ansi_68(
    KC_GRV,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   _______,            _______,
    CT_TAB,   KC_DEL,   KC_END,   KC_UP,    KC_PGUP,  KC_PGDN,  _______,  _______,  _______,  _______,  KC_WH_U,  KC_PGUP,  KC_PGDN,  _______,            KC_END,
    KC_ACL0,  KC_HOME,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_BSPC,  KC_BSPC,  M_EN_SET, KC_MS_U,  M_JP_SET, KC_WH_D, CT_SPACE,            _______,            _______,
    _______,  CM_Z,     CM_X,     CM_C,     CM_V,     KC_BTN1,  KC_BTN1,  KC_MS_L,  KC_MS_D,  KC_MS_R,  KC_ACL0,                      _______,  _______,  _______,
    _______,MO(MAC_NUM),_______,                                KC_ENT,                              LSFT(KC_F10),KC_ACL0,  KC_BTN2,  _______,  _______,  _______),

[WIN_CAPS] = LAYOUT_ansi_68(
    KC_GRV,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   _______,            _______,
    CT_TAB,   KC_DEL,   KC_END,   KC_UP,    CT_PGUP,  CT_PGDN,  CT_Y,     _______,  _______,  _______,  KC_WH_U,  KC_PGUP,  KC_PGDN,  _______,            KC_END,
    KC_ACL0,  KC_HOME,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_BSPC,  KC_BSPC,  W_EN_SET, KC_MS_U,  W_JP_SET, KC_WH_D,  SF_CAPS,            _______,            _______,
    _______,  CT_Z,     CT_X,     CT_C,     CT_V,     KC_BTN1,  KC_BTN1,  KC_MS_L,  KC_MS_D,  KC_MS_R,  KC_ACL0,                      _______,  _______,  _______,
    _______,MO(WIN_NUM),_______,                                KC_ENT,                              LSFT(KC_F10),KC_ACL0,  KC_BTN2,  _______,  _______,  _______),

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* RSFT */
[MAC_RSFT] = LAYOUT_ansi_68(
    KC_TILD,  SF_1,     KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   _______,            _______,
    _______,  KC_DEL,   SF_END,   KC_UP,    CM_R,     CM_T,     _______,  _______,  _______,  _______,  KC_WH_U,  KC_PGUP,  KC_PGDN,  _______,            _______,
    _______,  SF_HOME,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_BSPC,  KC_BSPC,  _______,  SF_UP,    KC_WH_L,  KC_WH_D,  KC_WH_R,            _______,            _______,
    _______,  CM_Z,     CM_X,     CM_C,     CM_V,     _______,  _______,  SF_LEFT,  SF_DOWN,  SF_RGHT,  SF_SLSH,                      _______,  _______,  _______,
    _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______),


[WIN_RSFT] = LAYOUT_ansi_68(
    KC_TILD,  SF_1,     KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   _______,            _______,
    _______,  KC_DEL,   SF_END,   KC_UP,    CT_R,     CT_T,     _______,  _______,  _______,  _______,  KC_WH_U,  KC_PGUP,  KC_PGDN,  _______,            _______,
    _______,  SF_HOME,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_BSPC,  KC_BSPC,  _______,  SF_UP,    KC_WH_L,  KC_WH_D,  KC_WH_R,            _______,            _______,
    _______,  CT_Z,     CT_X,     CT_C,     CT_V,     _______,  _______,  SF_LEFT,  SF_DOWN,  SF_RGHT,  SF_SLSH,                      _______,  _______,  _______,
    _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______),

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* NUM */
[MAC_NUM] = LAYOUT_ansi_68(
    _______,  KC_KP_1,  KC_KP_2,  KC_KP_3,  KC_KP_4,  KC_KP_6,  KC_KP_6,  KC_KP_7,  KC_KP_8,  KC_KP_9,  KC_KP_0,KC_KP_MINUS,KC_KP_EQUAL,_______,          _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  KC_KP_4,  KC_KP_5,  KC_KP_6,  KC_PLUS,  _______,  _______,  _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  KC_KP_1,  KC_KP_2,  KC_KP_3,  KC_MINUS, KC_KP_ENTER,        _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  KC_KP_0,  KC_KP_0,KC_KP_COMMA,KC_KP_DOT,KC_KP_SLASH,               MO(MAC_RSFT),_______,  _______,
    _______,  _______,  _______,                                KC_SPC,                                KC_NO,     _______,  _______,  _______,  _______,  _______),

[WIN_NUM] = LAYOUT_ansi_68(
    _______,  KC_1,     KC_2,     KC_3,     KC_4,     KC_6,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINUS, KC_EQUAL, _______,          _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  KC_4,     KC_5,     KC_6,     KC_PLUS,  _______,  _______,  _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  KC_1,     KC_2,     KC_3,     KC_MINUS, KC_ENTER,        _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  KC_0,     KC_0,     KC_COMMA, KC_DOT,   KC_SLASH,               MO(WIN_RSFT),_______,  _______,
    _______,  _______,  _______,                                KC_SPC,                                KC_NO,     _______,  _______,  _______,  _______,  _______),

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* FN */
[FN2] = LAYOUT_ansi_68(
    DFU,      TO(1),    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    TO(0),    KC_F11,   KC_F12,   _______,            BL_OFF,
    _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  M_SLEEP,  _______,  _______,  _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,            _______,
    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,                      _______,  _______,  _______,
    _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______),

};


/***************************************************************************
 * ユーザー設定                                                              *
 ***************************************************************************/
bool process_record_user(uint16_t keycode, keyrecord_t *record) {

    /*-離上時の処理---------------------------------------------------------*/
    if (!record->event.pressed) {
        switch (keycode) {
            case M_EN_SET: ime_key[IDX_M_EN].down = false; return false;
            case M_JP_SET: ime_key[IDX_M_JP].down = false; return false;
            case W_EN_SET: ime_key[IDX_W_EN].down = false; return false;
            case W_JP_SET: ime_key[IDX_W_JP].down = false; return false;
        }

        /* mac スリープ */
        if (keycode != M_SLEEP) { // ここまでで一度も引っ掛からなかったらtrueで戻る
            return true;
        }
    }


    /*-zプレフィックス z直後のキーを先に捕まえる------------------------------------*/
    if (zp.waiting) {
        zp.waiting = false; // 解除
        // ここからマクロを定義
        switch (keycode) {
            // 【】
            case KC_LBRC:
                tap_code16(KC_MINUS); // 代替
                wait_ms(20);
                tap_code16(KC_SPC);
                wait_ms(20);
                tap_code16(KC_ENT);
                wait_ms(20);
                tap_code16(KC_LEFT);
                return false; // 処理済み
            // そのた
            case KC_C: // ◯
            case KC_X: // ※
            case KC_H: // ←
            case KC_J: // ↓
            case KC_K: // ↑
            case KC_L: // →
                tap_code16(keycode);
                wait_ms(20);
                tap_code16(KC_SPC);
                wait_ms(20);
                tap_code16(KC_ENT);
                return false; // 処理済み
            case KC_DOT: // …
                tap_code16(KC_D);   // 代替
                wait_ms(20);
                tap_code16(KC_SPC);
                wait_ms(20);
                tap_code16(KC_ENT);
                return false; // 処理済み
        }
    }

    /*-押下時の処理-----------------------------------------------------*/
    /*-キーコードを判定-------------------------------------*/
    switch (keycode) {
        /* かっこ系 */
        // ()
        case KC_9:
            if (get_mods() & MOD_MASK_SHIFT) {
                brackets_mods_shift(LSFT(KC_9), LSFT(KC_0));
                return false;
            }
            return true; // 普通に送る
        // 「」と『』と[]と{}
        case KC_LBRC:
            // shiftなし
            if (!(get_mods() & MOD_MASK_SHIFT)) {
                tap_code16(KC_LBRC);    // '「'
                tap_code16(KC_RBRC);    // '」'
                wait_ms(20);
                enter_jp_only();
                wait_ms(20);
                tap_code16(KC_LEFT);
            // shiftあり（win）
            } else if (layer_state & (1UL<<WIN_BASE)) {
                tap_code16(KC_Z);
                wait_ms(20);
                tap_code16(KC_LBRC);
                wait_ms(20);
                tap_code16(KC_SPC);
                wait_ms(20);
                enter_jp_only();
            // shiftあり（mac）
            } else {
                brackets_mods_shift(LSFT(KC_LBRC), LSFT(KC_RBRC));
            }
            return false;
        // ''と""
        case KC_QUOT:
            if (!(get_mods() & MOD_MASK_SHIFT)) {
                tap_code16(KC_QUOT);    // '
                tap_code16(KC_QUOT);    // '
                wait_ms(20);
                enter_jp_only();
                wait_ms(20);
                tap_code16(KC_LEFT);
            } else {
                brackets_mods_shift(LSFT(KC_QUOT), LSFT(KC_QUOT));
            }
            return false;
        
        /* IME切替 */
        // mac 英数
        case M_EN_SET:
        ime_key[IDX_M_EN].down  = true;
        ime_key[IDX_M_EN].timer = timer_read();

        if (!m_is_english) {        // フラグがズレていた
            send_ctrl_space_mac();  // 通常トグル
            m_is_english = true;
            ime_key[IDX_M_EN].need_force = false; // もう不要
        } else {                    // 合っていた
            ime_key[IDX_M_EN].need_force = true;  // 後で強制
        }
        return false;

        // mac かな
        case M_JP_SET:
            ime_key[IDX_M_JP].down  = true;
            ime_key[IDX_M_JP].timer = timer_read();

            if (m_is_english) {
                send_ctrl_space_mac();
                m_is_english = false;
                ime_key[IDX_M_JP].need_force = false;
            } else {
                ime_key[IDX_M_JP].need_force = true;
            }
            return false;

        // win 英数
        case W_EN_SET:
            ime_key[IDX_W_EN].down  = true;
            ime_key[IDX_W_EN].timer = timer_read();

            if (!w_is_english) {
                send_shift_caps_win();
                w_is_english = true;
                ime_key[IDX_W_EN].need_force = false;
            } else {
                ime_key[IDX_W_EN].need_force = true;
            }
            return false;

        // win かな
        case W_JP_SET:
            ime_key[IDX_W_JP].down  = true;
            ime_key[IDX_W_JP].timer = timer_read();

            if (w_is_english) {
                send_shift_caps_win();
                w_is_english = false;
                ime_key[IDX_W_JP].need_force = false;
            } else {
                ime_key[IDX_W_JP].need_force = true;
            }
            return false;


        /* DFU起動 */
        case DFU:
            reset_keyboard();
            return false;

        /* mac アプリ終了 */
        case KC_Q:
            if (layer_state_is(MAC_BASE) && get_mods() & MOD_MASK_GUI) {    // cmdと一緒だったら
                if (m_gui_q_waiting && timer_elapsed(m_gui_q_timer) < M_QUI_Q_DBL_TAP_MS) {
                    // 2回目：発火
                    tap_code16(LGUI(KC_Q)); // アプリ終了
                } else {
                    // 1回目：フラグとタイマーをセット
                    m_gui_q_waiting = true;
                    m_gui_q_timer   = timer_read();
                }
                return false;
            }
            return true; // そのまま送る
        
        /* mac スリープ */
        case M_SLEEP:
        if (record->event.pressed) {           // 押した
            if (m_sleep_waiting &&
                timer_elapsed(m_sleep_tap_timer) < M_SLEEP_DBL_TAP_MS) {
                m_sleep_waiting = false;       // 2回目認定
                mac_display_sleep();           // ★ 実行
            }
            return false;                      // M_SLEEP は送らない
        }
        // 離した瞬間に1回目をセット
        if (!m_sleep_waiting) {
            m_sleep_waiting   = true;
        }
        m_sleep_tap_timer = timer_read();        // 毎回更新
        return false;

        /* zプレフィックス */
        case KC_Z: {
            // 今日本語入力中か判定
            bool jp_mac = layer_state_is(MAC_BASE) && !m_is_english;
            bool jp_win = layer_state_is(WIN_BASE) && !w_is_english;

            if (jp_mac || jp_win) {
                zp.waiting = true; // 次のキーを待つ状態
                zp.timer = timer_read();
            }
            return true; // zをそのまま送る
        }
    }


    /*-特殊な条件-----------------------------------------*/
    /* win アプリ起動 */
    if (layer_state & (1UL<<WIN_BASE)
        && record->event.pressed
        && get_mods() & (MOD_MASK_GUI)
        && !(get_mods() & (MOD_MASK_SHIFT))
        && !(get_mods() & (MOD_MASK_CTRL))
        && !(layer_state & (1UL<<FN2))  // FN2を押すと通常のwinキー復活
    ) {
        switch (keycode) {
            case KC_C: // Chrome
                tap_code16(LGUI(KC_1));
                // win_launch_app(KC_1);
                break;
            case KC_S: // Slack
                tap_code16(LGUI(KC_2));
                // win_launch_app(KC_2);
                break;
            case KC_F: // Explorer
                tap_code16(LGUI(KC_3));
                // win_launch_app(KC_3);
                break;
            case KC_E: // memo
                tap_code16(LGUI(KC_4));
                // win_launch_app(KC_4);
                break;
            case KC_D: // PowerPoint
                tap_code16(LGUI(KC_5));
                // win_launch_app(KC_0);
                break;
            case KC_X: // Excel
                tap_code16(LGUI(KC_8));
                // win_launch_app(KC_8);
                break;
            case KC_T: // Teams
                tap_code16(LGUI(KC_9));
                // win_launch_app(KC_9);
                break;
            default:
                // その他（そのまま）
                return true;
        }
        return false;
    }

    return true;
}