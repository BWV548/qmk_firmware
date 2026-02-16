#pragma once

#define UNICODE_SELECTED_MODES UNICODE_MODE_MACOS,UNICODE_MODE_WINCOMPOSE


#define TAPPING_TERM   300   // 0.3 秒以内を「素早いタップ」と判定
#define TAPPING_TOGGLE 2     // 2 回タップでトグル
#define DISABLE_KEYMAP_INTROSPECTION


/* マウスキー設定 */
// キーを押してからカーソルが動き始めるまでの遅延（デフォルトは10ms）
#undef MOUSEKEY_DELAY
#define MOUSEKEY_DELAY 10
// 最大速度に達するまでの時間（フレーム、デフォルトは20）
#undef MOUSEKEY_TIME_TO_MAX
#define MOUSEKEY_TIME_TO_MAX 6
// 最大速度（デフォルトは10）
#undef MOUSEKEY_MAX_SPEED
#define MOUSEKEY_MAX_SPEED 3

// 加速＋低速の混合モードを有効化
#define MK_COMBINED

/* チャタリング防止 */
#define DEBOUNCE 12