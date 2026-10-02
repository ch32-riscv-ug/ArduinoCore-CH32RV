# VBUS 電圧計リグ（XY-FZ25）

USB PD などで 20 V 級になる電源線を、PC から読むための臨時の機材です。電子負荷モジュール XY-FZ25 を、**負荷を切ったまま**
電圧計として使います。常設ではありません。使う試験の間だけつなぎ、`TEST_VBUS_METER` でシリアル port を渡したときだけ
読みます（渡さなければ、使う試験は理由付きで skip）。

**値は目安です。** 「VBUS が契約どおりに動いた」を見る程度に使い、電源の精度を測る用途には使いません（下の実測を参照）。

## 機材

| 項目 | 内容 |
|---|---|
| 本体 | XY-FZ25（4 A / 25 W の電子負荷。上位機種の XY-FZ35 も同じプロトコル） |
| 負荷側の電圧範囲 | DC 1.5〜25.0 V、逆接保護あり。**25 V を超える線（PD の EPR 28 V 以上）にはつながない** |
| 本体の電源 | 負荷側とは別に DC 5〜30 V（または USB-TTL 側の 5 V） |
| 負荷を切ったときの入力 | 10 V で約 0.1 mA（約 10 kΩ 相当、[lygte-info の実測](https://lygte-info.dk/project/Electronic%20Load%20XY-FZ25%20UK.html)）。VBUS をほとんど引かない |
| 電圧の分解能 | 10 mV（表示 `xx.xxV`） |
| PC との接続 | USB-TTL（いまは CH340 `1a86:7523`、`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`） |

**絶縁はありません。** シリアルの GND、本体の電源の GND、負荷の − は内部でつながっています。負荷の − を測る線の GND に
つなぐと、PC・probe・DUT の GND とも共通になります。負荷を切って使うかぎり、電流計測の shunt を GND の回り込みが迂回して
困ることはありません（負荷を入れる使い方をするなら、GND の経路を見直してから）。

## 結線

```
被測定の線（例: USB-C の VBUS）── FZ25 負荷 +
被測定の線の GND              ── FZ25 負荷 −
FZ25 TX ── USB-TTL RX
FZ25 RX ── USB-TTL TX
FZ25 GND ── USB-TTL GND
```

- PD の試験では、USB-C の VBUS と GND に分岐して入れる。**CC 線には何もつながない**（長い線で CC に触ると PD のメッセージが
  decode できなくなる。WeAct の板で確認）。
- USB 電流計（インラインの USB テスター）と同時に入れても問題なかった。
- TTL のレベルは資料によって 5 V / 3.3 V と割れている。いまの CH340 のアダプタでは問題なく通っている。別のアダプタや、
  probe の UART につなぎ替えるときは、先に FZ25 の TX の待機電圧をテスターで測る（P4 / ESP32 は 5 V 非対応）。

## プロトコル（v1）

9600 bps、8N1。コマンドに改行は付けず、返答は CRLF で終わります。

| 送る | 返り | 意味 |
|---|---|---|
| `off` | `sucess`（綴りはこのまま） | 負荷を切る。電圧計として使う前に必ず送る |
| `start` | `sucess`、以後 1 秒ごとに `04.93V,0.00A,0.001Ah,00:00` | 測定値の連続送信を始める |
| `stop` | `sucess` | 連続送信を止める |
| `on` / `x.xxA` / `read` | | 負荷を入れる / 電流の設定 / 設定の読み出し。**このリグでは使わない** |

- **負荷を切ったままでも電圧は読めます**（資料には「OFF のときは全部 0」とあるが、この個体は電圧を返す。2026-10-02 確認）。
- 1 秒に 1 行しか来ないので、電圧を変えた直後は 2 秒以上待ってから、受信バッファを捨てて次の 1 行を読む（`millivolts()` がそうしている）。
- amazon.co.jp で売られている個体には別のプロトコル（v2、資料なし）のものがあるという報告がある
  （[yellobyte/ElectronicLoad-Control-XY-FZ35](https://github.com/yellobyte/ElectronicLoad-Control-XY-FZ35)）。この個体は v1。
  以前 [nawotech/fz35-load](https://github.com/nawotech/fz35-load) でつないだ実績もある（ライセンスの表記が無いので、コードは持ち込まずプロトコルだけ合わせている）。

## 使い方

```sh
# リグが答えるか（5 行読む）
uv run tests/manual/vbus_meter/vbus_meter.py /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0

# 同じ確認を pytest で
cd tests && TEST_VBUS_METER=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  uv run pytest manual/vbus_meter/vbus_meter.py -s
```

試験から使うとき:

```python
from loader import load
vm = load("tests/manual/vbus_meter/vbus_meter.py", "vbus_meter")
meter = vm.open_meter()            # TEST_VBUS_METER が無ければ None → skip する
mv = meter.millivolts()            # 2.5 秒待ってから 1 行
meter.close()                      # stop を送って閉じる
```

いまの利用者: [`bench/basic/pd_sink/pd_vbus.py`](../../bench/basic/pd_sink/pd_vbus.py)（PD の契約ごとに VBUS が動いたかを見る。
ファイル名を指定したときだけ走る）。

## 実測（2026-10-02、WeAct CH32X035F8U6 の USB-C）

| 契約 | 読み | 電源 |
|---|---|---|
| 固定 5 / 9 / 12 / 15 / 20 V | 5.03 / 9.13 / 12.20 / 15.29 / 20.43 V（+0.6〜+2.2 %） | 5〜20 V 固定の充電器 |
| 固定 5 / 9 / 12 V | 4.93 / 8.98 / 12.01 V | 5/9/12 V + PPS 3.3〜11 V の充電器 |
| PPS 7.14 / 11.0 V | 7.10 / 11.00 V | 同上 |
| PPS 4.0 V | 4.02 V | 同上 |
| PPS 3.3 V | 3.98 V | 同上。**充電器自身が 4 V 付近で止まる**（インラインの USB テスターも約 4 V） |

ずれは電源ごとに違い、メーターの誤差と電源の出力の差は切り分けていません。だから試験の判定は緩くしています
（`pd_vbus.py`: 4.5 V 以上は ±10 %、それ未満は「5 V から下がった」だけ）。
