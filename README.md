# 動きの波形（スクーミー）v1.1

オレンジボード＋加速度センサー（S-CN-A10）でSDカードに記録したデータを、iPadのSafariで波形にして見るページ。

公開URL：https://shiojima-test.github.io/schoomy-motion-viewer/

最初に使う場：2026-11-08 昭和町立押原中学校 ソフトボール部の体験レク

## 決まり

- データはiPadの中だけで扱う。サーバーには送らない（ファイルはブラウザで選ぶだけ。保存先はiPadのブラウザ内 IndexedDB）
- 一度開けば、ネットがなくても動く（sw.js でページ・Excelの部品・ロゴを保存）
- 画面はスクーミーのロゴと色。ひらがなを多用しない

## 中身

| ファイル | 内容 |
|---|---|
| index.html | ページ本体（1 名前を入れる → 2 ファイルを入れる → 3 波形を見る → 4 まとめ・Excel） |
| sw.js | ネットなしで動かすための保存 |
| lib/xlsx.full.min.js | Excel書き出し（SheetJS 0.18.5） |
| firmware/softball_logger_v1.0/ | ボードに書き込む記録プログラム |

## SDカードの形（softball_logger v1.0）

- ファイル名：A0012.CSV（セットの記号＋起動回数。電源を入れるたびに新しいファイル）
- 1行目：`# softball_logger v1.0 rate=100Hz accel=16G(2048/G) gyro=2000dps(16.4/dps) ... board=A boot=12 ...`
- 2行目：`t_ms,ax,ay,az,gx,gy,gz,vcc_mV`
- 10msごと（1秒に100回）に1行。加速度は2048で割るとG、角速度は16.4で割ると度/秒

ページは1行目の `accel=…(N/G)` `gyro=…(N/dps)` を読んで換算するので、rocket_logger（v3.3〜v5.11）のCSVもそのまま読める。電源断で壊れた行は飛ばす。

## 確かめたこと（2026-11-04）

- schoomy-bottle-rocket の実機CSV（24分・14.4万行など4本）で読み込み・波形・Excel書き出しを確認。iPad（1180×820）の画面サイズで表示を確認
- softball_logger v1.0 は rocket_logger v5.11 から音センサーを外しただけ。文法の確認のみで、ボードへの書き込みはまだ
