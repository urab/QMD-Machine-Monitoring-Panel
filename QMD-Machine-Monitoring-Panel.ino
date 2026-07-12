/*
  MACHINE PANEL v1.5
  - Mute-кнопка теперь не тумблер.
    Одно короткое нажатие = ACK: глушим звук по всем текущим красным машинам.
    Новая авария на любой машине снова включает звук.
  - Латч красного (redLatched) по тайм-ауту redClearMs (настраивается в /settings).
  - Красный прямоугольник держится до истечения redClearMs после последнего импульса.
  - Логотип QMD — «живой» круг с мягким сиянием.

  Hardware:
    Arduino Mega 2560 + Ethernet Shield W5100
    12 machines × 3 inputs (Blue/Green/Red) = 36 inputs
    DS18B20 on D6 (water temperature)
    DS3231 RTC (SDA=20, SCL=21)
    Alarm relay on D2 (active LOW)
    Mute button on D7 (to GND, INPUT_PULLUP)
    Thermostat relay on D9 (active HIGH)

  Network:
    IP:      192.168.10.250
    GW/DNS:  192.168.10.1
    Subnet:  255.255.255.0
*/

#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>

// ----------------- NETWORK -----------------
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x25, 0x60};
IPAddress ip(192, 168, 10, 250);
IPAddress dns(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

EthernetServer server(80);

// ----------------- RTC DS3231 -----------------
RTC_DS3231 rtc;

// ----------------- GRID -----------------
#define GRID_COLS 4
#define GRID_ROWS 3
#define NUM_MACHINES (GRID_COLS * GRID_ROWS)
#define STATES_PER_MACHINE 3   // 0=Blue,1=Green,2=Red

const char* machineLabels[NUM_MACHINES] = {
  "45","50","51","60",
  "120","110","121","61",
  "200","380","90","122"
};

// ----------------- INPUTS -----------------
/*
  Каждой машине соответствуют 3 входа:
  index 0 -> Blue
  index 1 -> Green
  index 2 -> Red

  Входы сделаны как INPUT_PULLUP.
  Активный уровень от машины = прижать вход к GND (LOW).
*/

const uint8_t pinMap[NUM_MACHINES][STATES_PER_MACHINE] = {
  {22, 23, 24}, {25, 26, 27}, {28, 29, 30}, {31, 32, 33},
  {34, 35, 36}, {37, 38, 39}, {40, 41, 42}, {43, 44, 45},
  {46, 47, 48}, {49, 54, 55}, {56, 57, 58}, {59, 60, 61}
};

#define DEBOUNCE_MS 30

struct Debounce {
  bool stable;
  bool lastRead;
  unsigned long lastT;
};

Debounce deb[NUM_MACHINES][STATES_PER_MACHINE];

// ----------------- ALARM / MUTE -----------------
#define BUZZER_PIN 2   // relay/siren (active LOW)
#define MUTE_PIN   7   // Mute button to GND, INPUT_PULLUP

// Латч аварии по машинам (красный прямоугольник)
bool redLatched[NUM_MACHINES];           // машина в аварии (для экрана)
unsigned long lastRedSeen[NUM_MACHINES]; // время последнего импульса Red

// ACK по машине: звук по этой машине заглушен до конца текущей аварии
bool alarmAck[NUM_MACHINES];

// *** Настраиваемый тайм-аут удержания аварии (мс) ***
unsigned long redClearMs = 5000;  // по умолчанию 5 секунд (меняется в /settings)

// состояние для дебаунса кнопки Mute
bool muteLastReading = false;
bool muteStable      = false;
unsigned long muteLastChange = 0;
const unsigned long MUTE_DEBOUNCE_MS = 50;

// Флаг для верхней надписи ALARM / OK
bool alarmVisual = false;

// ----------------- TEMP (DS18B20) -----------------
#define ONEWIRE_PIN 6   // DS18B20 data pin
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature dallas(&oneWire);

float waterC = NAN;
unsigned long lastTemp = 0;

// ----------------- THERMOSTAT -----------------
#define THERM_PIN 9     // thermostat relay output (active HIGH)

float thermoSet = 30.0;          // setpoint, °C
float thermoHyst = 1.0;          // hysteresis, °C
bool  thermoEnabled = false;     // thermostat enabled
bool  thermoHeatMode = true;     // true=HEAT (ON when cold), false=COOL
bool  thermoOutput = false;      // current output state

// ----------------- HTML MAIN PAGE -----------------
// PROGMEM HTML с новым логотипом QMD (круг+надпись)
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Machine Monitor</title>
<style>
:root{--bg:#0b0e12;--card:#10151b;--line:#1f2933;
--blue:#2f7fff;--green:#18c37e;--red:#ff3b30;--unk:#6c7a88;
--text:#e8eef6;--weak:#9fb0c2;}
*{box-sizing:border-box;font-family:Inter,system-ui,Segoe UI,Arial,sans-serif;}
body{
  margin:0;background:var(--bg);color:var(--text);
  height:100vh;display:flex;flex-direction:column;
  padding:1.5% 2.4% 1.8% 2.4%;
}
header{
  display:grid;grid-template-columns:0.9fr auto 1fr;
  align-items:start;margin-bottom:12px;
}
.timebox{display:flex;flex-direction:column;margin-top:4px;margin-left:0.6%;}
#clock{font-size:clamp(40px,6vw,90px);font-weight:900;line-height:1;}
#date{font-size:clamp(18px,2.5vw,32px);color:var(--weak);margin-top:4px;}
.centerbar{display:flex;align-items:center;justify-content:center;gap:18px;margin-left:3%;}

/* QMD logo */
.qmd-wrap{display:flex;align-items:center;gap:10px;}
.q-circle{
  width:40px;height:40px;border-radius:50%;
  background:radial-gradient(circle at 30% 30%,#ffffff,#87a9ff 35%,#003366 75%,#001022);
  position:relative;
  box-shadow:0 0 8px rgba(120,180,255,.5);
  animation:qglow 3s ease-in-out infinite;
}
.q-tail{
  position:absolute;bottom:6px;right:-8px;
  width:24px;height:10px;border-radius:20px;
  background:linear-gradient(90deg,#cfd8ff,#6a7aa5);
}
.qmd-text{
  font-size:clamp(22px,3.5vw,34px);
  font-weight:800;letter-spacing:0.18em;
  color:#dfe9ff;
}
@keyframes qglow{
  0%{box-shadow:0 0 4px rgba(120,180,255,.4);}
  50%{box-shadow:0 0 16px rgba(120,180,255,.95);}
  100%{box-shadow:0 0 4px rgba(120,180,255,.4);}
}

#alarm{font-weight:900;font-size:clamp(30px,5vw,70px);}
.mutebtn{
  width:clamp(38px,4vw,52px);
  height:clamp(38px,4vw,52px);
  border-radius:999px;
  border:none;
  display:flex;
  align-items:center;
  justify-content:center;
  font-size:clamp(20px,3vw,30px);
  cursor:pointer;
  box-shadow:0 0 0 2px rgba(0,0,0,0.15);
}
.mutebtn.off{background:#16a34a;color:#e5fdf2;}   /* sound ON */
.mutebtn.on{background:#991b1b;color:#fee2e2;}    /* muted */

.temp{text-align:right;font-weight:900;
font-size:clamp(30px,5vw,70px);margin-top:6px;}
#temp{white-space:nowrap;}
.settings-link{display:block;text-align:right;margin-top:6px;
font-size:clamp(12px,1.6vw,20px);color:var(--weak);text-decoration:none;}
.settings-link:hover{color:var(--text);}

.grid{
  flex:1;display:grid;
  grid-template-columns:repeat(4,1fr);
  grid-auto-rows:1fr;
  gap:14px;
}
.card{
  display:flex;align-items:center;justify-content:center;
  background:var(--card);border:2px solid var(--line);
  border-radius:16px;
}
.num{font-weight:800;font-size:clamp(42px,9vw,120px);}
.blue{color:var(--blue);} .green{color:var(--green);}
.red{color:var(--red);} .unk{color:var(--unk);}
</style></head><body>
<header>
<div class="timebox">
  <div id="clock">--:--:--</div>
  <div id="date">--.--.----</div>
</div>
<div class="centerbar">
  <div class="qmd-wrap">
    <div class="q-circle"><div class="q-tail"></div></div>
    <div class="qmd-text">QMD</div>
  </div>
  <span id="alarm">OK</span>
  <button id="mutebtn" class="mutebtn off" onclick="muteToggle()">🔊</button>
</div>
<div>
  <div id="temp" class="temp">🌡 — °C</div>
  <a href="/settings" class="settings-link">⚙ Settings</a>
</div>
</header>
<div class="grid" id="grid"></div>
<script>
const N=12;
const labels=["45","50","51","60","120","110","121","61","200","380","90","122"];

const alarmEl=document.getElementById('alarm');
const muteBtn=document.getElementById('mutebtn');
const tempEl=document.getElementById('temp');
const gridEl=document.getElementById('grid');
const clockEl=document.getElementById('clock');
const dateEl=document.getElementById('date');

function tile(l,s){
  let c=s==='Red'?'red':s==='Green'?'green':s==='Blue'?'blue':'unk';
  return `<div class="card"><div class="num ${c}">${l}</div></div>`;
}
function render(j){
  let h=''; for(let i=0;i<N;i++) h+=tile(labels[i], j.s[i]);
  gridEl.innerHTML=h;

  alarmEl.textContent = j.a ? '🚨 ALARM!' : '✅ OK';
  alarmEl.style.color = j.a ? 'var(--red)' : 'var(--green)';

  const muted = (j.m || j.hwm);
  muteBtn.className = 'mutebtn ' + (muted ? 'on' : 'off');
  muteBtn.textContent = muted ? '🔇' : '🔊';

  tempEl.textContent = '🌡 ' + (j.t==null ? '—' : Number(j.t).toFixed(1)) + ' °C';

  clockEl.textContent = j.time || '--:--:--';
  dateEl.textContent  = j.date || '--.--.----';
}
async function poll(){
  try{
    const r = await fetch('/api/state',{cache:'no-store'});
    render(await r.json());
  }catch(e){
    alarmEl.textContent='❌ OFFLINE';
    alarmEl.style.color='var(--unk)';
  }
}
function muteToggle(){ fetch('/api/mute').catch(()=>{}); }
poll(); setInterval(poll, 500);
</script></body></html>
)HTML";

// ----------------- HELPERS -----------------
bool readRaw(uint8_t p) {
  return digitalRead(p) == LOW; // активный LOW
}

void setupDebounce() {
  unsigned long t = millis();
  for (int m = 0; m < NUM_MACHINES; m++) {
    for (int s = 0; s < STATES_PER_MACHINE; s++) {
      bool r = readRaw(pinMap[m][s]);
      deb[m][s].stable   = r;
      deb[m][s].lastRead = r;
      deb[m][s].lastT    = t;
    }
  }
}

void updateDebounce() {
  unsigned long n = millis();
  for (int m = 0; m < NUM_MACHINES; m++) {
    for (int s = 0; s < STATES_PER_MACHINE; s++) {
      bool r = readRaw(pinMap[m][s]);
      Debounce &d = deb[m][s];
      if (r != d.lastRead) {
        d.lastRead = r;
        d.lastT = n;
      } else if (n - d.lastT >= DEBOUNCE_MS) {
        d.stable = d.lastRead;
      }
    }
  }
}

String stateStr(int m) {
  bool b  = deb[m][0].stable;
  bool g  = deb[m][1].stable;
  bool rL = redLatched[m];

  if (rL) return "Red";
  if (g)  return "Green";
  if (b)  return "Blue";
  return "Unknown";
}

bool relayLevel(bool on) {
  // alarm relay is active LOW
  return on ? LOW : HIGH;
}

void driveAlarmOutputs(bool alarmActive) {
  // alarmActive = есть хотя бы одна НЕ acknowledg-нутая авария
  digitalWrite(BUZZER_PIN, relayLevel(alarmActive));
}

// ACK со стороны кнопки / веб-интерфейса
void acknowledgeAll() {
  for (int m = 0; m < NUM_MACHINES; m++) {
    if (redLatched[m]) {
      alarmAck[m] = true;
    }
  }
}

// ----------------- THERMOSTAT LOGIC -----------------
void updateThermostat() {
  bool out = false;

  if (thermoEnabled && !isnan(waterC)) {
    if (thermoHeatMode) {
      // HEAT: ON when cold
      if (!thermoOutput && waterC < (thermoSet - thermoHyst/2)) out = true;
      else if (thermoOutput && waterC > (thermoSet + thermoHyst/2)) out = false;
      else out = thermoOutput;
    } else {
      // COOL: ON when hot
      if (!thermoOutput && waterC > (thermoSet + thermoHyst/2)) out = true;
      else if (thermoOutput && waterC < (thermoSet - thermoHyst/2)) out = false;
      else out = thermoOutput;
    }
  } else {
    out = false;
  }

  thermoOutput = out;
  digitalWrite(THERM_PIN, thermoOutput ? HIGH : LOW); // active HIGH
}

// ----------------- JSON / HTML -----------------
void sendIndex(EthernetClient &c) {
  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: text/html"));
  c.println(F("Connection: close"));
  c.println();

  const char* p = INDEX_HTML;
  while (true) {
    char ch = pgm_read_byte(p++);
    if (!ch) break;
    c.write(ch);
  }
}

void sendJson(EthernetClient &c) {
  DateTime now = rtc.now();

  // вычисляем суммарные статусы
  bool anyRed = false;
  bool anyUnacked = false;
  for (int i = 0; i < NUM_MACHINES; i++) {
    if (redLatched[i]) {
      anyRed = true;
      if (!alarmAck[i]) anyUnacked = true;
    }
  }
  bool muted = anyRed && !anyUnacked;

  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: application/json"));
  c.println(F("Connection: close"));
  c.println();

  c.print(F("{\"s\":["));
  for (int i = 0; i < NUM_MACHINES; i++) {
    if (i) c.print(',');
    c.print('"');
    c.print(stateStr(i));
    c.print('"');
  }

  c.print(F("],\"a\":"));
  c.print(anyRed ? 1 : 0);    // есть ли красные на экране
  c.print(F(",\"m\":"));
  c.print(muted ? 1 : 0);     // все ли текущие красные ACK-нуты
  c.print(F(",\"hwm\":0"));

  c.print(F(",\"t\":"));
  if (isnan(waterC)) c.print(F("null"));
  else c.print(String(waterC, 1));

  // time "HH:MM:SS"
  c.print(F(",\"time\":\""));
  if (now.hour() < 10) c.print('0');
  c.print(now.hour());
  c.print(':');
  if (now.minute() < 10) c.print('0');
  c.print(now.minute());
  c.print(':');
  if (now.second() < 10) c.print('0');
  c.print(now.second());
  c.print('\"');

  // date "DD.MM.YYYY"
  c.print(F(",\"date\":\""));
  if (now.day() < 10) c.print('0');
  c.print(now.day());
  c.print('.');
  if (now.month() < 10) c.print('0');
  c.print(now.month());
  c.print('.');
  c.print(now.year());
  c.print('\"');

  // thermostat info
  c.print(F(",\"th_en\":"));
  c.print(thermoEnabled ? 1 : 0);
  c.print(F(",\"th_mode\":\""));
  c.print(thermoHeatMode ? "HEAT" : "COOL");
  c.print(F("\",\"th_set\":"));
  c.print(String(thermoSet,1));
  c.print(F(",\"th_out\":"));
  c.print(thermoOutput ? 1 : 0);

  c.print('}');
}

// ----------------- SETTINGS PAGE -----------------
int getParamInt(const String &q, const String &name, int defVal) {
  int pos = q.indexOf(name + "=");
  if (pos < 0) return defVal;
  int start = pos + name.length() + 1;
  int end = q.indexOf('&', start);
  if (end < 0) end = q.length();
  String v = q.substring(start, end);
  v.trim();
  if (!v.length()) return defVal;
  return v.toInt();
}

float getParamFloat(const String &q, const String &name, float defVal) {
  int pos = q.indexOf(name + "=");
  if (pos < 0) return defVal;
  int start = pos + name.length() + 1;
  int end = q.indexOf('&', start);
  if (end < 0) end = q.length();
  String v = q.substring(start, end);
  v.trim();
  if (!v.length()) return defVal;
  return v.toFloat();
}

void sendSettingsPage(EthernetClient &c, bool updated) {
  DateTime now = rtc.now();

  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: text/html; charset=utf-8"));
  c.println(F("Connection: close"));
  c.println();
  c.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>Settings</title>"
              "<style>body{font-family:Arial,sans-serif;background:#111827;color:#e5e7eb;"
              "margin:0;padding:16px;}h1{margin-top:0;}fieldset{border:1px solid #374151;"
              "border-radius:8px;padding:12px;margin-bottom:16px;}legend{padding:0 6px;}"
              "label{display:block;margin:4px 0;}input,select{padding:4px 6px;margin-top:2px;"
              "border-radius:4px;border:1px solid #4b5563;background:#111827;color:#e5e7eb;}"
              "button{margin-top:8px;padding:6px 16px;border:none;border-radius:6px;"
              "background:#10b981;color:#111827;font-weight:bold;cursor:pointer;}"
              "a{color:#93c5fd;text-decoration:none;}</style></head><body>"));

  c.println(F("<h1>Settings</h1>"));
  if (updated) {
    c.println(F("<p style='color:#34d399;'>Saved ✔</p>"));
  }

  c.println(F("<form method='GET' action='/settings'>"));

  // TIME
  c.println(F("<fieldset><legend>Time &amp; Date (RTC DS3231)</legend>"));
  c.print(F("<label>Year: <input type='number' name='y' value='"));
  c.print(now.year());
  c.println(F("'></label>"));

  c.print(F("<label>Month: <input type='number' name='m' value='"));
  c.print(now.month());
  c.println(F("'></label>"));

  c.print(F("<label>Day: <input type='number' name='d' value='"));
  c.print(now.day());
  c.println(F("'></label>"));

  c.print(F("<label>Hour: <input type='number' name='hh' value='"));
  c.print(now.hour());
  c.println(F("'></label>"));

  c.print(F("<label>Minute: <input type='number' name='mm' value='"));
  c.print(now.minute());
  c.println(F("'></label>"));

  c.print(F("<label>Second: <input type='number' name='ss' value='"));
  c.print(now.second());
  c.println(F("'></label>"));

  c.println(F("</fieldset>"));

  // ALARM LATCH TIME
  int redSec = (int)(redClearMs / 1000UL);
  c.println(F("<fieldset><legend>Alarm latch</legend>"));
  c.print(F("<label>Hold time after last pulse, seconds: "
            "<input type='number' name='redsec' min='1' max='600' value='"));
  c.print(redSec);
  c.println(F("'></label>"));
  c.println(F("<p style='color:#9ca3af;font-size:0.9em;'>"
              "Machine stays red this many seconds after the last alarm pulse.</p>"));
  c.println(F("</fieldset>"));

  // THERMOSTAT
  c.println(F("<fieldset><legend>Thermostat (water)</legend>"));

  c.println(F("<label>Enabled: "
              "<select name='th_en'>"));
  c.print(F("<option value='0'"));
  if (!thermoEnabled) c.print(F(" selected"));
  c.println(F(">Disabled</option>"));
  c.print(F("<option value='1'"));
  if (thermoEnabled) c.print(F(" selected"));
  c.println(F(">Enabled</option></select></label>"));

  c.println(F("<label>Mode: <select name='th_mode'>"));
  c.print(F("<option value='0'"));
  if (thermoHeatMode) c.print(F(" selected"));
  c.println(F(">HEAT (ON when cold)</option>"));
  c.print(F("<option value='1'"));
  if (!thermoHeatMode) c.print(F(" selected"));
  c.println(F(">COOL (ON when hot)</option></select></label>"));

  c.print(F("<label>Setpoint, &deg;C: <input type='number' step='0.1' name='th_set' value='"));
  c.print(thermoSet, 1);
  c.println(F("'></label>"));

  c.print(F("<p>Current water temperature: "));
  if (isnan(waterC)) c.print(F("&mdash;"));
  else c.print(String(waterC,1));
  c.println(F(" &deg;C</p>"));

  c.println(F("</fieldset>"));

  c.println(F("<button type='submit'>Save</button>"));
  c.println(F("<p><a href='/'>⟵ Back to panel</a></p>"));
  c.println(F("</form></body></html>"));
}

void handleSettings(EthernetClient &c, const String &reqLine) {
  String path = reqLine.substring(4); // after "GET "
  int sp = path.indexOf(' ');
  if (sp > 0) path = path.substring(0, sp);
  int qm = path.indexOf('?');

  bool updated = false;

  if (qm >= 0) {
    String qs = path.substring(qm + 1);

    // ---- TIME ----
    DateTime cur = rtc.now();
    int y  = getParamInt(qs, "y",  cur.year());
    int mo = getParamInt(qs, "m",  cur.month());
    int d  = getParamInt(qs, "d",  cur.day());
    int hh = getParamInt(qs, "hh", cur.hour());
    int mi = getParamInt(qs, "mm", cur.minute());
    int ss = getParamInt(qs, "ss", cur.second());

    if (y != cur.year() || mo != cur.month() || d != cur.day() ||
        hh != cur.hour() || mi != cur.minute() || ss != cur.second()) {
      rtc.adjust(DateTime(y, mo, d, hh, mi, ss));
      updated = true;
    }

    // ---- ALARM LATCH TIME ----
    int redSec = getParamInt(qs, "redsec", (int)(redClearMs / 1000UL));
    if (redSec < 1) redSec = 1;
    if (redSec > 600) redSec = 600;
    unsigned long newMs = (unsigned long)redSec * 1000UL;
    if (newMs != redClearMs) {
      redClearMs = newMs;
      updated = true;
    }

    // ---- THERMOSTAT ----
    int en   = getParamInt(qs, "th_en", thermoEnabled ? 1 : 0);
    int mode = getParamInt(qs, "th_mode", thermoHeatMode ? 0 : 1);
    float set = getParamFloat(qs, "th_set", thermoSet);

    bool newEnabled = (en != 0);
    bool newHeat    = (mode == 0);

    if (newEnabled != thermoEnabled || newHeat != thermoHeatMode || set != thermoSet) {
      thermoEnabled = newEnabled;
      thermoHeatMode = newHeat;
      thermoSet = set;
      updated = true;
    }
  }

  sendSettingsPage(c, updated);
}

// ----------------- NETWORK MONITOR -----------------
unsigned long lastNetCheck = 0;

bool linkUp() {
#if defined(ETHERNET_LINK_STATUS)
  return Ethernet.linkStatus() == LinkON;
#else
  return true;
#endif
}

void ensureEthernet() {
  if (millis() - lastNetCheck < 5000) return;
  lastNetCheck = millis();
  if (!linkUp()) {
    Ethernet.begin(mac, ip, dns, gateway, subnet);
  }
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);

  // machine inputs
  for (int m = 0; m < NUM_MACHINES; m++) {
    for (int s = 0; s < STATES_PER_MACHINE; s++) {
      pinMode(pinMap[m][s], INPUT_PULLUP);
    }
    redLatched[m]  = false;
    alarmAck[m]    = false;
    lastRedSeen[m] = 0;
  }

  // Mute button and alarm relay
  pinMode(MUTE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, relayLevel(false)); // alarm off

  // Thermostat output
  pinMode(THERM_PIN, OUTPUT);
  digitalWrite(THERM_PIN, LOW);

  // disable SD on W5100 (CS = 4)
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  setupDebounce();
  dallas.begin();

  // RTC
  if (!rtc.begin()) {
    Serial.println(F("RTC ERROR (DS3231 not found)!"));
  } else {
    if (rtc.lostPower()) {
      Serial.println(F("RTC lost power, set from compile time"));
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  Ethernet.begin(mac, ip, dns, gateway, subnet);
  server.begin();
  Serial.print(F("Server at: http://"));
  Serial.println(Ethernet.localIP());
}

// ----------------- LOOP -----------------
void loop() {
  updateDebounce();

  unsigned long nowMs = millis();

  // ---- Machines: latch Red per machine ----
  bool anyRed      = false; // есть ли вообще красные на экране
  bool anyUnacked  = false; // есть ли красные без ACK (для звука)

  for (int m = 0; m < NUM_MACHINES; m++) {
    bool redInput = deb[m][2].stable;  // физический вход Red (пульсирующий)

    if (redInput) {
      lastRedSeen[m] = nowMs;
      if (!redLatched[m]) {
        redLatched[m] = true;          // новая авария этой машины
        // alarmAck[m] здесь не трогаем,
        // по умолчанию false => будет звук
      }
    } else {
      if (redLatched[m] && (nowMs - lastRedSeen[m] > redClearMs)) {
        // Давно нет импульсов -> авария реально закончилась
        redLatched[m] = false;
        alarmAck[m]   = false;         // сбрасываем ACK, чтобы след. авария позвонила
      }
    }

    if (redLatched[m]) {
      anyRed = true;
      if (!alarmAck[m]) {
        anyUnacked = true;
      }
    }
  }

  // флаг для верхней надписи
  alarmVisual = anyRed;

  // ---- Mute button: короткое нажатие = ACK ALL ----
  bool reading = (digitalRead(MUTE_PIN) == LOW);  // pressed = LOW
  if (reading != muteLastReading) {
    muteLastChange = nowMs;
    muteLastReading = reading;
  }
  if ((nowMs - muteLastChange) > MUTE_DEBOUNCE_MS) {
    if (reading != muteStable) {
      muteStable = reading;
      // фронт нажатия (НЕ нажато -> нажато)
      if (muteStable) {
        acknowledgeAll();
      }
    }
  }

  // ---- Alarm relay ----
  driveAlarmOutputs(anyUnacked);

  // ---- Water temperature ----
  if (nowMs - lastTemp > 1000) {
    lastTemp = nowMs;
    dallas.requestTemperatures();
    float tC = dallas.getTempCByIndex(0);

    Serial.print(F("Water temp raw: "));
    Serial.println(tC);

    if (tC <= -100.0 || tC > 125.0) {
      waterC = NAN;       // ошибка / нет датчика
    } else {
      waterC = tC;
    }
  }

  // ---- Thermostat ----
  updateThermostat();

  // ---- HTTP server ----
  EthernetClient client = server.available();
  if (client) {
    String reqLine = client.readStringUntil('\n');
    reqLine.trim();

    // read headers until empty line
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r" || line.length() == 0) break;
    }

    if (reqLine.startsWith("GET /api/state")) {
      sendJson(client);
    } else if (reqLine.startsWith("GET /api/mute")) {
      // веб-кнопка делает тот же ACK, что и физическая
      acknowledgeAll();
      client.println(F("HTTP/1.1 200 OK"));
      client.println(F("Content-Type: text/plain"));
      client.println(F("Connection: close"));
      client.println();
      client.println(F("OK"));
    } else if (reqLine.startsWith("GET /settings")) {
      handleSettings(client, reqLine);
    } else {
      sendIndex(client);
    }

    delay(1);
    client.stop();
  }

  ensureEthernet();
}