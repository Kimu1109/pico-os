#pragma once
#include <cmath>
#include <cstdint>

// 音名/MIDIノート番号 → 周波数。Luaの pico.note_freq() と、曲データを読む側の共通の下請け。
//
// - MIDIノート番号: 69 = A4 = 440Hz、60 = C4(真ん中のド)。平均律
// - 音名: "C4" "C#4" "Db4" "a3"(大小は問わない)。#/b は1つだけ。オクターブは -1〜9
namespace NoteName {

    // MIDIノート番号 → Hz(0〜127の外は範囲外として0を返す)
    inline float MidiToFreq(int note){
        if(note < 0 || note > 127) return 0.0f;
        return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
    }

    // 音名 → MIDIノート番号。読めなければ-1
    inline int Parse(const char* s){
        if(!s) return -1;
        static const int kBase[7] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G
        char c = s[0];
        if(c >= 'a' && c <= 'g') c = (char)(c - 'a' + 'A');
        if(c < 'A' || c > 'G') return -1;
        int semitone = kBase[c - 'A'];
        int i = 1;
        if(s[i] == '#'){ semitone++; i++; }
        else if(s[i] == 'b'){ semitone--; i++; }

        bool neg = false;
        if(s[i] == '-'){ neg = true; i++; }
        if(s[i] < '0' || s[i] > '9') return -1;
        int octave = s[i] - '0';
        i++;
        if(s[i] != '\0') return -1;
        if(neg) octave = -octave;
        if(octave < -1 || octave > 9) return -1;

        const int note = (octave + 1) * 12 + semitone;
        return (note < 0 || note > 127) ? -1 : note;
    }
}
