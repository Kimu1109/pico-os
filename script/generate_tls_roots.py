#!/usr/bin/env python3
"""src/net/Tls_Roots_Data.hpp (HTTPSで信頼するルート証明書) を生成する。

pico-os は OS の証明書ストアを持たないので、繋ぎたい相手のルート証明書を
ファームウェアへ焼き込む。ここに無いルートで署名されたサーバへは繋がらない
(SDの /sys/tls/ca.pem に足せば繋がる。net/Tls_Roots.hpp 参照)。

証明書は母艦(Debian/Ubuntu)の /etc/ssl/certs から取る。
ルートを足す/差し替えるときは ROOTS を直して、このスクリプトを流し直すこと:

    python3 script/generate_tls_roots.py

1枚あたり、接続中だけRAMを約1.5KB使う(BearSSLが信頼の起点へ展開するため)。
むやみに増やさないこと。
"""
import datetime
import os
import subprocess
import sys

CERT_DIR = "/etc/ssl/certs"

# (ファイル名, 何のために入れているか)
ROOTS = [
    ("GTS_Root_R1.pem", "Google(RSAの証明書。calendar.google.com 等)"),
    ("GTS_Root_R4.pem", "Google(ECDSAの証明書)"),
    ("ISRG_Root_X1.pem", "Let's Encrypt(RSA)。自前のサーバの多くはこれ"),
    ("ISRG_Root_X2.pem", "Let's Encrypt(ECDSA)"),
    ("DigiCert_Global_Root_G2.pem", "DigiCert(Microsoft/Outlookの公開カレンダー等)"),
    ("DigiCert_Global_Root_CA.pem", "DigiCert(旧ルート。まだ多くのサイトが使う)"),
    ("USERTrust_RSA_Certification_Authority.pem", "Sectigo"),
]


def subject_and_expiry(path):
    out = subprocess.run(
        ["openssl", "x509", "-in", path, "-noout", "-subject", "-enddate"],
        check=True, capture_output=True, text=True).stdout
    subject = ""
    expiry = ""
    for line in out.splitlines():
        if line.startswith("subject="):
            subject = line[len("subject="):].strip()
        elif line.startswith("notAfter="):
            expiry = line[len("notAfter="):].strip()
    return subject, expiry


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(repo, "src", "net", "Tls_Roots_Data.hpp")

    lines = [
        "#pragma once",
        "",
        "// script/generate_tls_roots.py が生成する。手で書き換えないこと。",
        f"// 生成日: {datetime.date.today().isoformat()}",
        "//",
        "// HTTPSで信頼するルート証明書(PEMを連結したもの)。一覧:",
    ]
    pems = []
    for name, why in ROOTS:
        path = os.path.join(CERT_DIR, name)
        if not os.path.exists(path):
            print(f"見つかりません: {path}", file=sys.stderr)
            return 1
        subject, expiry = subject_and_expiry(path)
        lines.append(f"//   - {subject}")
        lines.append(f"//       {why} / 期限 {expiry}")
        with open(path, encoding="ascii") as f:
            pems.append(f.read().strip())

    lines.append("")
    lines.append("namespace TlsRootsData {")
    lines.append("    // RP2350では const のデータはフラッシュに置かれる(RAMを食わない)")
    lines.append("    static const char kPem[] =")
    for pem in pems:
        for pem_line in pem.splitlines():
            lines.append(f'        "{pem_line}\\n"')
    lines[-1] = lines[-1] + ";"
    lines.append(f"    static constexpr int kCount = {len(pems)};")
    lines.append("}")
    lines.append("")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"{out_path} へ {len(pems)} 枚を書きました")
    return 0


if __name__ == "__main__":
    sys.exit(main())
