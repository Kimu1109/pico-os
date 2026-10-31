#pragma once

// 実機での確認(docs/HARDWARE_CHECKLIST.md)のためのシリアルのコマンド。
// /sys/debug.cfg の hw-test = true のときだけ受け付ける(既定は切れている。わざと落とすコマンドがあるため)。
//
//   hw help        コマンドの一覧
//   hw info        起動からの時間・電池・Wi-Fi・音の周波数・ウォッチドッグ
//   hw boot        起動の理由とクラッシュダンプの元(RAMの記録・スクラッチレジスタ)。起動直後のログの代わり
//   hw mem         ヒープの状態(mallinfo・連続で取れる最大・ヒープの末尾の残り)
//   hw clock [MHz] 今のCPUクロックとスリープ中に下げる先。MHzを付けると sleep-cpu-mhz を今だけ変える(0で下げない)
//   hw i2c         I2C0(GP0/GP1)のスキャン。SDA/SCLの電圧・応答する機器のアドレス。0x52が見えたらIDと状態の6バイトも読む
//                  (ID末尾 01 01 = Wiiクラシック、00 00 = ヌンチャク)。つないだコントローラーが生きているかの確認用
//   hw sleep       今すぐスリープへ(電源メニューのスリープと同じ。消費電流をスリープの前後で比べる)
//   hw trap        未定義命令でHardFaultを起こす(クラッシュダンプの確認。PCビルドはSIGILL)
//   hw buswrite    存在しない番地へ書いてHardFault(バスフォールト)を起こす(PCビルドはSIGSEGV)
//   hw hang        loop()を戻さない(ウォッチドッグの確認。watchdog = true が要る)
//
// 結果はログ([HW] で始まる行)へ出る。行はPadFunctionsのシリアルの振り分けから届く(main.cppが差す)。
namespace HwTestFunctions {
    // "hw" で始まる行なら扱って true。debug.cfg で切れていれば理由をログへ出して true(他へは回さない)
    bool FeedSerialLine(const char* line);
}
