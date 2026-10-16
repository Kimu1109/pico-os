#pragma once

#include <cstdint>

// ファームの更新ファイル(.pfw、FIRMWARE_UPDATE.md)の署名を確かめる公開鍵(Ed25519、32バイト)。
//
// **ファームに焼き込んだものだけを信頼する**(アプリストアと違い、SDの設定ファイルの鍵は読まない。
// SDへは sd_outside_app_dir の権限を持つLuaアプリも書けるので、SDの鍵を信頼すると
// アプリがファームを差し込めてしまうため)。
//
// 鍵を作るのは script/firmware_tool.py keygen(秘密鍵は手元のPCにだけ置く。リポジトリにも
// GitHubのSecretsにも置かないこと)。表示された1行をここへ足し、USBで1回書き込めば、
// 以後はその鍵で署名した更新を受け付ける。
//
// 全部0の鍵は「まだ登録していない」目印で、信頼しない。
namespace FwKeys {
    struct Key { uint8_t pub[32]; };

    // 例: { { 0x12, 0x34, ... } },
    inline constexpr Key kBuiltin[] = {
        { { 0xef, 0x9e, 0x53, 0x7b, 0x4e, 0xc1, 0xfe, 0xa9, 0x02, 0x25, 0xd9, 0x9d, 0x7e, 0x79, 0x28, 0x46, 0xfd, 0x7e, 0x30, 0x2c, 0xfc, 0xb7, 0x73, 0xcc, 0x3d, 0x24, 0xf1, 0x95, 0x93, 0xca, 0x47, 0x80 } },
    };
    inline constexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);
}
