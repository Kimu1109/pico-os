#include "hal/CardKB_Uart.hpp"

#include <cstring>

namespace {
    using KeyInputFunctions::Event;
    using KeyInputFunctions::Key;

    // キー番号 → 文字(通常 / Aa / Sym)。M5Unit-KEYBOARD(MIT、M5Stack)の unit_CardKB2UART.cpp の表から。
    // 0は文字にならないキー(欠番・Aa・Fn・Sym)。0x08=Delete(後退)、0x0A=Enter
    constexpr char kKeyMap[CardKbUart::kNumKeys][3] = {
        {'1','1','!'}, {'2','2','@'}, {'3','3','#'}, {'4','4','$'}, {'5','5','%'},
        {'6','6','^'}, {'7','7','&'}, {'8','8','*'}, {'9','9','('}, {'0','0',')'},
        {0,0,0},                                                            // 10: 欠番
        {'q','Q','~'}, {'w','W','`'}, {'e','E','?'}, {'r','R','\\'}, {'t','T','/'},
        {'y','Y','|'}, {'u','U','_'}, {'i','I','-'}, {'o','O','+'}, {'p','P','='},
        {0x08,0x08,0x08},                                                   // 21: Delete
        {0,0,0},                                                            // 22: Aa
        {'a','A','{'}, {'s','S','}'}, {'d','D','^'}, {'f','F','['}, {'g','G',']'},
        {'h','H','"'}, {'j','J','\''}, {'k','K',';'}, {'l','L',':'},
        {0x0A,0x0A,0x0A},                                                   // 32: Enter
        {0,0,0}, {0,0,0},                                                   // 33: Fn, 34: Sym
        {'z','Z','z'}, {'x','X','x'}, {'c','C','c'}, {'v','V','<'}, {'b','B','>'},
        {'n','N',','}, {'m','M','.'},
        {' ',' ',' '},                                                      // 42: Space
    };

    // 1回押し/2回押し/長押しの判定(本体の iot_button と同じ流れ。M5Unit-KEYBOARD の ButtonEventDetector を写したもの)
    enum class Click : uint8_t { None, Single, Double };
    struct ClickDetector {
        enum class State : uint8_t { Idle, Down, WaitRepeat, RepeatDown, LongHold };
        State state = State::Idle;
        uint8_t repeat = 0;
        unsigned long t0 = 0;

        // 押した/離した。短く押して離したとき(長押しでないとき)はその回数(1=1回目)を返す。それ以外は0
        uint8_t edge(bool pressed, unsigned long now){
            switch(state){
                case State::Idle:
                    if(pressed){ repeat = 1; t0 = now; state = State::Down; }
                    break;
                case State::Down:
                case State::RepeatDown:
                    if(!pressed){ t0 = now; state = State::WaitRepeat; return repeat; }
                    break;
                case State::WaitRepeat:
                    if(pressed){ repeat++; t0 = now; state = State::RepeatDown; }
                    break;
                case State::LongHold:
                    if(!pressed) state = State::Idle;
                    break;
            }
            return 0;
        }
        // 間に別のキーを打った: 押している/離したばかりの回を数えない(Aaを押しながら打ってから離しても1回押しにしない、
        // 「Aa・英字・Aa」をすばやく打っても2回押しにしない)
        void cancel(){
            if(state == State::WaitRepeat){ state = State::Idle; repeat = 0; }
            else if(state == State::Down || state == State::RepeatDown){ state = State::LongHold; }
        }
        Click poll(unsigned long now){
            if(state == State::Down && now - t0 >= CardKbUart::kLongPressMs){
                state = State::LongHold;
            }else if(state == State::WaitRepeat && now - t0 > CardKbUart::kClickGapMs){
                const uint8_t r = repeat;
                repeat = 0;
                state = State::Idle;
                if(r == 1) return Click::Single;
                if(r == 2) return Click::Double;
            }
            return Click::None;
        }
    };

    // ---- フレームの読みかけ ----
    uint8_t  frame[5];
    int      frame_len = 0;
    uint32_t frames_ok = 0;
    uint32_t bytes_dropped = 0;

    // ---- キーの状態 ----
    uint64_t held = 0;                              // 押しているキー(番号のビット)
    unsigned long press_at[CardKbUart::kNumKeys];   // 押した時刻
    unsigned long next_repeat[CardKbUart::kNumKeys];

    // ---- 修飾キー ----
    ClickDetector sym_click, caps_click;
    bool fn_held = false;
    bool sym_held = false, sym_mode = false;
    bool caps_hold = false, caps_one = false, caps_lock = false;
    bool suppressed = false;

    bool SymActive(){ return sym_mode || sym_held; }
    bool CapsActive(){ return caps_one || caps_lock || caps_hold; }

    bool IsModifier(int k){ return k == CardKbUart::kKeyAa || k == CardKbUart::kKeyFn || k == CardKbUart::kKeySym; }

    void Emit(int key){
        if(suppressed) return;
        Event ev;
        if(!CardKbUart::KeyToEvent(key, fn_held, SymActive(), CapsActive(), ev)) return;
        KeyInputFunctions::Push(ev);
        sym_click.cancel();
        caps_click.cancel();
        //1回押しの大文字は次の英字1文字で使い切る
        if(ev.key == Key::Char && !ev.ctrl() &&
           ((ev.cp >= 'A' && ev.cp <= 'Z') || (ev.cp >= 'a' && ev.cp <= 'z'))){
            if(caps_one && !caps_lock && !caps_hold) caps_one = false;
        }
    }

    void OnKey(int key, bool pressed, unsigned long now){
        const uint64_t bit = 1ull << key;
        if(key == CardKbUart::kKeyFn){
            fn_held = pressed;
        }else if(key == CardKbUart::kKeySym){
            if(pressed == ((held & bit) != 0)) return;  //同じ状態の重複は無視
            sym_held = pressed;
            //短く押して離すたびに切り替える(本体は離して200ms待ってから切り替えるが、待たずにすぐ打てるように)
            if(sym_click.edge(pressed, now) > 0) sym_mode = !sym_mode;
        }else if(key == CardKbUart::kKeyAa){
            if(pressed == ((held & bit) != 0)) return;
            const uint8_t clicks = caps_click.edge(pressed, now);
            //押している間だけ大文字(固定中は何もしない)
            if(!caps_lock) caps_hold = pressed;
            //1回目を離したらすぐ「次の1文字だけ大文字」にする(本体は200ms待ってから。待つと続けて打った文字が小文字になる)。
            //2回押し(固定)かどうかは Tick() が200ms後に決める
            if(clicks == 1 && !caps_lock) caps_one = true;
        }

        if(pressed){
            if(held & bit) return;      //押したままの重複
            held |= bit;
            press_at[key] = now;
            next_repeat[key] = now + CardKbUart::kRepeatDelayMs;
            if(!IsModifier(key)) Emit(key);
        }else{
            held &= ~bit;
        }
    }

    bool FrameValid(){
        if(frame[0] != CardKbUart::kHeader || frame[1] != CardKbUart::kLength) return false;
        if(frame[3] != CardKbUart::kStatePressed && frame[3] != CardKbUart::kStateReleased) return false;
        return frame[4] == (uint8_t)(frame[1] + frame[2] + frame[3]);
    }
}

bool CardKbUart::KeyToEvent(int key, bool fn, bool sym, bool caps, KeyInputFunctions::Event& out){
    out = Event{};
    if(key < 0 || key >= kNumKeys) return false;
    if(caps) out.mods |= KeyInputFunctions::Shift;

    if(fn){
        switch(key){
            case 0:          out.key = Key::Escape; return true;   // Fn+1
            case 25:         out.key = Key::Up;     return true;   // Fn+D
            case 35:         out.key = Key::Left;   return true;   // Fn+Z
            case 36:         out.key = Key::Down;   return true;   // Fn+X
            case 37:         out.key = Key::Right;  return true;   // Fn+C
            case kKeyDelete: out.key = Key::Delete; return true;
            case kKeySpace:  out.key = Key::Tab;    return true;
            case kKeyEnter:  out.key = Key::Enter;  return true;
            default: break;
        }
        const char c = kKeyMap[key][0];
        if(c >= 'a' && c <= 'z'){
            out.key  = Key::Char;
            out.cp   = (uint32_t)c;     //Ctrl+文字は小文字で送る(USBシリアルの "key 1 u+63" と同じ)
            out.mods = (uint8_t)(out.mods | KeyInputFunctions::Ctrl);
            return true;
        }
        return false;   //Fn+数字等は割り当てなし
    }

    const char c = kKeyMap[key][sym ? 2 : (caps ? 1 : 0)];
    if(c == 0) return false;
    if(c == 0x08){ out.key = Key::Backspace; return true; }
    if(c == 0x0A){ out.key = Key::Enter;     return true; }
    out.key = Key::Char;
    out.cp  = (uint32_t)(uint8_t)c;
    return true;
}

void CardKbUart::Reset(){
    frame_len = 0;
    frames_ok = 0;
    bytes_dropped = 0;
    held = 0;
    memset(press_at, 0, sizeof(press_at));
    memset(next_repeat, 0, sizeof(next_repeat));
    sym_click = ClickDetector{};
    caps_click = ClickDetector{};
    fn_held = sym_held = sym_mode = false;
    caps_hold = caps_one = caps_lock = false;
    suppressed = false;
}

void CardKbUart::Feed(uint8_t b, unsigned long now_ms){
    if(frame_len == 0 && b != kHeader){
        bytes_dropped++;
        return;
    }
    frame[frame_len++] = b;
    if(frame_len < 5) return;

    if(FrameValid()){
        frame_len = 0;
        frames_ok++;
        const int key = frame[2];
        if(key < kNumKeys) OnKey(key, frame[3] == kStatePressed, now_ms);
        return;
    }
    //合わない: 先頭を1バイト捨て、残りの中で次のAAから読み直す
    int start = 1;
    while(start < 5 && frame[start] != kHeader) start++;
    bytes_dropped += (uint32_t)start;
    frame_len = 5 - start;
    memmove(frame, frame + start, (size_t)frame_len);
}

void CardKbUart::Tick(unsigned long now_ms){
    sym_click.poll(now_ms);     //長押しの判定を進めるだけ(切り替えは離したとき)

    const Click c = caps_click.poll(now_ms);
    if(c == Click::Single){
        //固定中の1回押しは解除(固定していなければ、離したときに既に「次の1文字」にしてある)
        if(caps_lock){ caps_lock = false; caps_one = false; }
    }else if(c == Click::Double){
        caps_lock = !caps_lock;
        caps_one = false;
    }

    for(int k = 0; k < kNumKeys; k++){
        if(!(held & (1ull << k)) || IsModifier(k)) continue;
        if(now_ms - press_at[k] > kMaxRepeatMs) continue;
        if((long)(now_ms - next_repeat[k]) < 0) continue;
        next_repeat[k] = now_ms + kRepeatPeriodMs;
        Emit(k);
    }
}

uint64_t CardKbUart::HeldKeys(){ return held; }
void CardKbUart::SetSuppressed(bool on){ suppressed = on; }

uint32_t CardKbUart::FramesReceived(){ return frames_ok; }
uint32_t CardKbUart::BytesDropped(){ return bytes_dropped; }
