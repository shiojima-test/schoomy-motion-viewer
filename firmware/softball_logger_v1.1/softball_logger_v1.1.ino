/*
 * ソフトボール部 動きの記録プログラム  softball_logger v1.1（2026-11-08）
 * 株式会社スクーミー / オレンジボード + 加速度センサー（S-CN-A10）+ フルカラーLED + SDカード（単四電池1本）
 *
 * ── 土台 ─────────────────────────────────────
 *   ペットボトルロケットの記録プログラム rocket_logger v5.11（schoomy-bottle-rocket）。
 *   10/15・10/17 に同じ形のボードで 100回/秒の記録を実機で確かめている版。
 *   センサーの読み方・SDカードの初期化（5回まで）・ファイル名（記号＋起動回数・前の記録を消さない）・
 *   100回/秒の固定・0.1秒ごとの書き込み・電源電圧の列・LEDの色は v5.11 のまま。
 *
 * ── v5.11 から変えたところ ─────────────────────────
 *   ① 音センサーを外した（ソフトボール部では加速度センサーだけを使う）。
 *      CSV から snd_min・snd_max の列を除いた。
 *   ② 記号 BOARD をセットの番号として使う（A〜H）。ファイル名の頭がセットの記号になる。
 *
 * ── v1.0 から変えたところ（2026-11-08）─────────────────
 *   ③ 3Gを超えたら青にする表示を外した（ソフトボールでは不要）。
 *   ④ 記録は30秒（REC_MS）で止める。止まったらファイルを閉じてLEDを白にする。1回の電源＝1人1回分のファイル。
 *
 * ── SDカードに残る形 ───────────────────────────
 *   ファイル名：  A0012.CSV（記号＋起動回数。電源を入れるたびに新しいファイル）
 *   1行目：      # softball_logger v1.1 rate=100Hz accel=16G(2048/G) gyro=2000dps(16.4/dps) dlpf=44Hz rec=30s board=A boot=12 power=AAA vcc_start=4850
 *   2行目：      t_ms,ax,ay,az,gx,gy,gz,vcc_mV
 *   3行目から：  10ms（1秒に100回）ごとに1行
 *     t_ms        電源を入れてからの時間（ミリ秒）
 *     ax,ay,az    加速度（そのままの数。2048 で割ると G。±16G まで）
 *     gx,gy,gz    角速度（そのままの数。16.4 で割ると 度/秒。±2000度/秒まで）
 *     vcc_mV      電池の電圧（ミリボルト）
 *   30秒で約0.12MB。
 *   見るサイト：https://shiojima-test.github.io/schoomy-motion-viewer/
 *
 * ── つなぐ場所 ─────────────────────────────────
 *   加速度センサー（S-CN-A10）  左上（IN_A）    SDA=A4 / SCL=A5   アドレス 0x68
 *   フルカラーLED（S-CN-A14）   左下（OUT_A）   R=6 / G=5 / B=3（つないでいなくても記録はできる）
 *   SDカード                   ボード内蔵（CARD） CS=4
 *
 * ── 光り方（フルカラーLEDをつないだとき）────────────────
 *   赤（約2秒）        起動中。待つ
 *   黄（4秒）          加速度センサーが応答していない。配線を確かめる
 *   赤の速い点滅       SDカードがNG。挿し直して電源を入れ直す
 *   緑                記録中（30秒）。この間に動く
 *   白                30秒たって記録が終わった。電源を切ってよい
 *
 * ── 使い方 ─────────────────────────────────────
 *   貼ってから電源を入れる → 緑になったら動く（30秒）→ 白になったら電源を切る → 次の人
 *   電源を入れ直すと新しいファイルになる。全員終わったらSDカードを抜いて iPad で見る。
 */
#include <SchooMyUtilities.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>

SchooMyUtilities scmUtils = SchooMyUtilities();

File _sbeFile_0;

#define MPU6050_ADDR         0x68
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1a
#define MPU6050_GYRO_CONFIG  0x1b
#define MPU6050_ACCEL_CONFIG 0x1c
#define MPU6050_PWR_MGMT_1   0x6b

const char     BOARD    = 'A';    // セットの記号。8セットなら A〜H にして、ボードごとに変えて書き込む
const int      SD_CS    = 4;      // SDカード（ボード内蔵 CARD）
const int      PIN_R    = 6;      // ㉕ フルカラーLED（左下＝OUT_A）赤
const int      PIN_G    = 5;      //    緑
const int      PIN_B    = 3;      //    青
const uint32_t PERIOD   = 10;     // 10ms = 100Hz
const uint32_t FLUSH_MS = 100;    // ㉙ 0.1秒に1回、確実に書き込む（v5.9 は 500）
const int      EE_BOOT  = 0;      // 起動回数を保存する EEPROM のアドレス

const uint32_t VCC_MS   = 1000;   // ㉓ 電源電圧を測り直す間隔
const uint32_t REC_MS   = 30000;  // ④ 記録する長さ（30秒）。緑になってからここまで記録して止める


int16_t  raw_acc_x = 0, raw_acc_y = 0, raw_acc_z = 0;
int16_t  raw_gyro_x = 0, raw_gyro_y = 0, raw_gyro_z = 0;

bool     ready    = false;
bool     done     = false;        // ④ 30秒の記録が終わったか
uint32_t recStart = 0;            // ④ 記録を始めた時刻
uint16_t vcc      = 0;            // ㉓ 直近の電源電圧(mV)
uint32_t nextTick = 0;

/* ブロックエディタの出力と同じ関数。14バイト揃わないときだけ直前の値を残す */
void accelerometerWireRead() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);
  if (Wire.available() >= 14) {
    /* 上位バイト → 下位バイトの順で届く。
       a << 8 | b と1行で書くと、どちらの read() が先に呼ばれるかが
       コンパイラ任せになるため、必ず順番どおりに取り出す */
    uint8_t h, l;
    h = Wire.read(); l = Wire.read(); raw_acc_x  = (int16_t)((h << 8) | l);
    h = Wire.read(); l = Wire.read(); raw_acc_y  = (int16_t)((h << 8) | l);
    h = Wire.read(); l = Wire.read(); raw_acc_z  = (int16_t)((h << 8) | l);
    Wire.read(); Wire.read();               // 温度は読み捨て
    h = Wire.read(); l = Wire.read(); raw_gyro_x = (int16_t)((h << 8) | l);
    h = Wire.read(); l = Wire.read(); raw_gyro_y = (int16_t)((h << 8) | l);
    h = Wire.read(); l = Wire.read(); raw_gyro_z = (int16_t)((h << 8) | l);
  }
}

void writeMPU6050(byte reg, byte data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

/* ㉕ フルカラーLED（アノードコモン。255-値で点く。10/6 のスケッチと同じ） */
void setColor(int r, int g, int b) {
  analogWrite(PIN_R, 255 - r);
  analogWrite(PIN_G, 255 - g);
  analogWrite(PIN_B, 255 - b);
}
void blinkRed(uint16_t ms) { if ((millis() / ms) % 2) setColor(255, 0, 0); else setColor(0, 0, 0); }

/* ㉓ 自分の電源電圧(mV)。内部の1.1V基準で測る */
uint16_t readVcc() {
  ADMUX = _BV(REFS0) | 0x0E;
  delay(3);
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC));
  uint16_t v = (uint16_t)(1125300UL / ADC);
  analogRead(A0);        // ADCの設定を通常に戻す
  return v;
}

void setup() {
  pinMode(PIN_R, OUTPUT); pinMode(PIN_G, OUTPUT); pinMode(PIN_B, OUTPUT);
  setColor(255, 0, 0);                   // ㉕ 起動中は赤

  Serial.begin(115200);
  delay(2000);
  Wire.begin();
  Wire.setClock(400000);

  /* ---------- センサーの設定（②測れる範囲を広げた） ---------- */
  writeMPU6050(MPU6050_PWR_MGMT_1,   0x01);   // スリープ解除＋クロック源をジャイロX軸PLLに
  writeMPU6050(MPU6050_CONFIG,       0x03);   // ㉚ デジタルフィルタ 加速度約44Hz・角速度約42Hz（v5.10 までは 0x00＝なし）
  writeMPU6050(MPU6050_GYRO_CONFIG,  0x18);   // ±2000 deg/s（出力そのままは 0x08 = ±500）
  writeMPU6050(MPU6050_ACCEL_CONFIG, 0x18);   // ±16G      （出力そのままは 0x00 = ±2G）
  writeMPU6050(MPU6050_SMPLRT_DIV,   0x00);   // 分周なし

  uint16_t boot;
  EEPROM.get(EE_BOOT, boot);
  if (boot > 60000) boot = 0;
  boot++;
  EEPROM.put(EE_BOOT, boot);

  /* ---------- ⑥ 飛ばす前の確認 ---------- */
  Serial.println();
  Serial.println(F("=== softball_logger v1.1 ==="));
  Serial.print(F("boot  : ")); Serial.print(boot);
  Serial.println(F(" 回目"));

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x75);                                  // WHO_AM_I
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 1, true);
  uint8_t who = Wire.available() ? Wire.read() : 0xFF;
  Serial.print(F("accel : "));
  if (who == 0xFF) Serial.println(F("NG  0x68 で応答なし。配線を確認（記録は続けます）"));
  else { Serial.print(F("OK  チップの型番=0x")); Serial.print(who, HEX);
         Serial.println(F("  ※型番は機種によって違います。応答があれば正常")); }

  /* ---------- ④ SDカードの初期化を5回まで試す ---------- */
  bool sdOk = false;
  for (int i = 0; i < 5; i++) {
    if (SD.begin(SD_CS)) { sdOk = true; break; }
    Serial.print(F("SD    : 初期化やり直し ")); Serial.println(i + 1);
    delay(200);
  }
  if (!sdOk) {
    Serial.println(F("SD    : NG  カードを挿し直して電源を入れ直してください"));
    return;
  }

  /* ---------- ①⑦ 前の記録を消さない／カードを替えても重複しない ---------- */
  char name[13];
  sprintf(name, "%c%04u.CSV", BOARD, boot);   // 例 A0012.CSV
  /* ㉖ 同じ名前がすでにあれば、無い番号まで進める（書き足しで記録が混ざるのを防ぐ） */
  while (SD.exists(name) && boot < 60000) {
    boot++;
    sprintf(name, "%c%04u.CSV", BOARD, boot);
  }
  EEPROM.put(EE_BOOT, boot);
  _sbeFile_0 = SD.open(name, FILE_WRITE);
  if (!_sbeFile_0) { Serial.println(F("SD    : NG  ファイルを開けません")); return; }

  vcc = readVcc();
  _sbeFile_0.print(F("# softball_logger v1.1 rate=100Hz accel=16G(2048/G) gyro=2000dps(16.4/dps) dlpf=44Hz rec=30s board="));
  _sbeFile_0.print(BOARD);
  _sbeFile_0.print(F(" boot="));
  _sbeFile_0.print(boot);
  _sbeFile_0.print(F(" power=AAA vcc_start="));
  _sbeFile_0.println(vcc);
  _sbeFile_0.println(F("t_ms,ax,ay,az,gx,gy,gz,vcc_mV"));
  _sbeFile_0.flush();

  ready = true;
  nextTick = millis();     // 0 のままだと起動時刻ぶん全速で回ってしまう
  Serial.print(F("SD    : OK  ")); Serial.println(name);
  /* ㉕㉘ センサーが応答しなければ黄を4秒。そのあと記録に入る */
  bool accNg = (who == 0xFF);
  if (accNg) {
    Serial.println(F("警告  : 加速度センサーが応答していません。記録は始めますが値は取れません"));
    setColor(255, 160, 0);               // ㉕ 黄を4秒
    delay(4000);
  }
  setColor(0, 255, 0);                   // ㉕ ここから記録。緑
  nextTick = millis();
  recStart = nextTick;                   // ④ 30秒はここから数える

  Serial.println(F("=== 記録開始。30秒たつとLEDが白になります ==="));
}

void loop() {
  /* ④ 30秒の記録が終わったら、白のまま何もしない */
  if (done) return;

  /* ㉕ 準備できていないとき（SDがNG）は赤の速い点滅が続く */
  if (!ready) {
    blinkRed(100);
    return;
  }

  static uint32_t lastFlush = 0;
  uint32_t now = millis();

  if ((int32_t)(now - nextTick) < 0) return;

  /* ⑤ 100Hz固定。遅れたときは取り戻そうとせず今に合わせ直す */
  nextTick += PERIOD;
  if ((int32_t)(now - nextTick) > (int32_t)PERIOD * 3) nextTick = now + PERIOD;

  /* ④ 30秒たったら書き込んでファイルを閉じ、白にする */
  if (now - recStart >= REC_MS) {
    _sbeFile_0.flush();
    _sbeFile_0.close();
    done = true;
    setColor(255, 255, 255);             // ④ 白＝記録が終わった
    Serial.println(F("=== 30秒の記録が終わりました。電源を切ってください ==="));
    return;
  }

  accelerometerWireRead();

  _sbeFile_0.print(now);            _sbeFile_0.print(',');
  _sbeFile_0.print(raw_acc_x);      _sbeFile_0.print(',');
  _sbeFile_0.print(raw_acc_y);      _sbeFile_0.print(',');
  _sbeFile_0.print(raw_acc_z);      _sbeFile_0.print(',');
  _sbeFile_0.print(raw_gyro_x);     _sbeFile_0.print(',');
  _sbeFile_0.print(raw_gyro_y);     _sbeFile_0.print(',');
  _sbeFile_0.print(raw_gyro_z);     _sbeFile_0.print(',');
  _sbeFile_0.println(vcc);

  /* ㉓ 1秒ごとに電源電圧を測り直す */
  static uint32_t lastVcc = 0;
  if (now - lastVcc >= VCC_MS) { vcc = readVcc(); lastVcc = now; }

  if (now - lastFlush >= FLUSH_MS) { _sbeFile_0.flush(); lastFlush = now; }
}
