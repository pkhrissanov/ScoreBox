/*---------------------------------------------------------------------------- -
  ESP8266 + ARDUINO wireless fencing client
  ESP-01 VERSION (NO FastLED, uses SoftwareSerial on GPIO2/GPIO0)

  - Runs AsyncWebServer + AsyncWebSocket
  - Receives 4-byte messages from Arduino over SoftwareSerial
  - Broadcasts SoftAP SSID and serves web scoreboard at http://192.168.5.1

  IMPORTANT ESP-01 PINS:
    GPIO2 = SoftwareSerial RX  (connect Arduino TX -> voltage divider -> GPIO2)
    GPIO0 = SoftwareSerial TX  (connect GPIO0 -> Arduino RX)  [optional if one-way]
    EN/CH_PD must be HIGH (3.3V)
    GPIO0 & GPIO2 must be HIGH at boot for normal run (do NOT hold low)

  NOTE:
    SoftwareSerial at 115200 is often flaky on ESP8266.
    If you get dropped/garbage serial messages, change BOTH Arduino and ESP to 19200 or 9600.

-------------------------------------------------------------------------------*/

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Hash.h>
#include <SoftwareSerial.h>

String version = "version: patched with random priority + P flag + smaller log";

// WiFi AP
const char* ssid     = "FencingBox";
const char* password = "let_me_in";

IPAddress local_IP(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

// ======================= ESP-01 SERIAL PINS =======================
SoftwareSerial mySerial(2, 0); // RX, TX
// =================================================================

// State / timing
bool LEDON = false;
bool AUTOINCREMENT = false;
unsigned long LEDcounter = 0;

int redscore = 0;
int greenscore = 0;

char incode[5] = {0};

// Timer
int minutes = 3;
int seconds = 0;
unsigned long timerpreviousMillis = 0;
const long timerinterval = 1000;
bool timerrunning = false;

bool REDHIT = false;
bool GREENHIT = false;
char SWORD = 'F';
char PRIORITY = 'N'; // N = none, R = red, G = green

String redname = "R:name";
String greenname = "G:name";

unsigned long interval  = 2500;

// Server + websocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --------------------------- HELP PAGE ----------------------------
char helppage[] PROGMEM = R"=====(
<html>
<body style="font-family:Arial,Helvetica,sans-serif; padding:20px;">
<p>
Use <b>F11</b> to make your browser full screen, CTRL +/- to resize the display to fit your screen.
<h3>Controls</h3>
<ul>
<li>Space bar : start/stop timer
<br>also the Stop/Resume button
<li>Click - or + buttons left/right of timer to change settings
<br>Left/right arrow keys also change timer setting
<li>Reset button resets time to 3:00 and scores to zero
<li>+/- buttons under scores to change score
<li>0 button under scores to reset to zero
</ul>

<h3>Extra Controls</h3>
<ul>
<li><b>Set Time</b> buttons set the clock directly to 1:00 or 3:00
<li><b>Choose Priority</b> randomly picks Red or Green
<li><b>Clear Priority</b> removes priority
<li>The chosen side shows a <b>P</b> next to its score
<li><b>Rename Fencers</b> lets you set red and green fencer names
<li><b>Event Log</b> shows websocket updates and scoring messages
</ul>

<p>
<a href="#" onclick="history.go(-1)">Back</a>
</body>
</html>
)=====";

//DISPLAY VERSION 

char displaypage[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
  <title> ScoreBox - Display</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,"/>
  <style>
    :root {
      --bg1: #05070a;
      --bg2: #0c1118;
      --panel: rgba(18,25,34,0.96);
      --border: rgba(255,255,255,0.08);
      --text: #f5f7fb;
      --muted: #aab4c2;
      --yellow: #ffe96a;
      --red1: #8d2020;
      --red2: #4b1111;
      --green1: #16914a;
      --green2: #0d532a;
      --shadow: 0 18px 40px rgba(0,0,0,0.45);
      --radius: 28px;
    }

    * { box-sizing: border-box; }

    html, body {
      margin: 0;
      padding: 0;
      width: 100%%;
      height: 100%%;
      background: radial-gradient(circle at top, #182434 0%%, #0b1017 45%%, #05070a 100%%);
      color: var(--text);
      font-family: Arial, Helvetica, sans-serif;
      overflow: hidden;
    }

    body {
      padding: 18px;
    }

    .app {
      height: calc(100vh - 36px);
      display: grid;
      grid-template-rows: auto 1fr;
      gap: 18px;
    }

    .topbar {
      display: grid;
      grid-template-columns: 1fr auto 1fr;
      gap: 18px;
      align-items: stretch;
    }

    .name-box {
      border-radius: var(--radius);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      display: flex;
      align-items: center;
      justify-content: center;
      text-align: center;
      padding: 16px 24px;
      font-size: clamp(2rem, 3.2vw, 4rem);
      font-weight: 800;
      min-height: 110px;
      word-break: break-word;
    }

    #red {
      background: linear-gradient(180deg, var(--red1) 0%%, var(--red2) 100%%);
      color: #fff3f3;
    }

    #green {
      background: linear-gradient(180deg, var(--green1) 0%%, var(--green2) 100%%);
      color: #f3fff7;
    }

    .timer-box {
      min-width: 32vw;
      border-radius: var(--radius);
      border: 1px solid var(--border);
      background: var(--panel);
      box-shadow: var(--shadow);
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 12px 28px;
      font-size: clamp(4.5rem, 10vw, 9rem);
      font-weight: 900;
      letter-spacing: 0.08em;
    }

    .main {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 18px;
      min-height: 0;
    }

    .score-panel {
      border-radius: var(--radius);
      border: 1px solid var(--border);
      background: linear-gradient(180deg, rgba(18,25,34,0.98) 0%%, rgba(10,15,21,0.98) 100%%);
      box-shadow: var(--shadow);
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 28px;
      min-height: 0;
      position: relative;
      overflow: hidden;
    }

    .score-value {
      font-size: clamp(9rem, 24vw, 20rem);
      font-weight: 900;
      line-height: 1;
      letter-spacing: 0.03em;
    }

    .priority-flag {
      font-size: clamp(2rem, 5vw, 4rem);
      font-weight: 900;
      color: var(--yellow);
      opacity: 0;
      min-width: 1.2em;
      text-align: center;
    }

    .priority-flag.active {
      opacity: 1;
    }

    .footer-note {
      position: fixed;
      right: 18px;
      bottom: 10px;
      color: var(--muted);
      font-size: 0.95rem;
      opacity: 0.7;
      letter-spacing: 0.08em;
      text-transform: uppercase;
    }

    @media (max-width: 1100px) {
      .topbar {
        grid-template-columns: 1fr;
      }

      .timer-box {
        min-width: 0;
      }

      .main {
        grid-template-columns: 1fr;
      }

      .app {
        height: auto;
        min-height: calc(100vh - 36px);
      }
    }
  </style>
</head>
<body onload="javascript:init()">
  <script>
    var message;
    var action;
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;

    window.addEventListener('load', onLoad);

    function init() {}

    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onmessage = onMessage;
    }

    function onOpen(event) { console.log('Connection opened'); }

    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }

    function onMessage(event) {
      message = event.data;
      var fencer = message[0];

      if (fencer == "R") { redbox(); }
      else if (fencer == "G") { greenbox(); }
      else if (fencer == "r") { redscore(); }
      else if (fencer == "g") { greenscore(); }
      else if (fencer == "t") { timer(); }
      else if (fencer == "x") { redname(); }
      else if (fencer == "y") { greenname(); }
      else if (fencer == "p") { priorityupdate(); }
    }

    function onLoad(event) { initWebSocket(); }

    function redbox() {
      action = message[1];
      var box = document.getElementById("red");

      if (action == "H") {
        box.style.background = '#FF0000';
        box.style.color = '#fff2f2';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #8d2020 0%, #4b1111 100%)';
        box.style.color = '#fff3f3';
      }
    }

    function greenbox() {
      action = message[1];
      var box = document.getElementById("green");

      if (action == "H") {
        box.style.background = '#00FF00';
        box.style.color = '#111111';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #16914a 0%, #0d532a 100%)';
        box.style.color = '#f3fff7';
      }
    }

    function redscore() {
      var score = message.substr(1,2);
      document.getElementById("redtext").textContent = score.trim();
    }

    function greenscore() {
      var score = message.substr(1,2);
      document.getElementById("greentext").textContent = score.trim();
    }

    function redname() {
      var rname = message.substr(1);
      document.getElementById("red").textContent = rname;
    }

    function greenname() {
      var gname = message.substr(1);
      document.getElementById("green").textContent = gname;
    }

    function timer() {
      var countdown = message.substr(1,4);
      document.getElementById("timer").textContent = countdown.trim();
    }

    function priorityupdate() {
      var p = message[1];

      var redFlag = document.getElementById("redPriorityFlag");
      var greenFlag = document.getElementById("greenPriorityFlag");

      redFlag.textContent = "";
      greenFlag.textContent = "";
      redFlag.classList.remove("active");
      greenFlag.classList.remove("active");

      if (p == "R") {
        redFlag.textContent = "P";
        redFlag.classList.add("active");
      } else if (p == "G") {
        greenFlag.textContent = "P";
        greenFlag.classList.add("active");
      }
    }
  </script>

  <div class="app">
    <div class="topbar">
      <div id="red" class="name-box">R:name</div>
      <div id="timer" class="timer-box">3:00</div>
      <div id="green" class="name-box">G:name</div>
    </div>

    <div class="main">
      <div class="score-panel">
        <span id="redtext" class="score-value">0</span>
        <span id="redPriorityFlag" class="priority-flag"></span>
      </div>

      <div class="score-panel">
        <span id="greentext" class="score-value">0</span>
        <span id="greenPriorityFlag" class="priority-flag"></span>
      </div>
    </div>
  </div>

  <div class="footer-note">Display Mode</div>
</body>
</html>
)=====";



// --------------------------- ADMIN PAGE ---------------------------
char adminpage[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
  <title>ScoreBox - Admin</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,"/>
  <style>
    :root {
      --bg: #0b0f14;
      --panel: rgba(18, 25, 34, 0.94);
      --panel2: #18212c;
      --text: #eef2f7;
      --muted: #a9b4c2;
      --border: rgba(255,255,255,0.08);
      --red-dim: #4a1010;
      --green-dim: #0f4a22;
      --score-bg: #0d131a;
      --btn-bg: #1a2430;
      --btn-hover: #243243;
      --blue: #1b4fd6;
      --yellow: #ffe96a;
      --radius: 18px;
      --shadow: 0 12px 30px rgba(0,0,0,0.35);
    }

    * { box-sizing: border-box; }

    html, body {
      margin: 0;
      padding: 0;
      min-height: 100%%;
      background: radial-gradient(circle at top, #16212e 0%%, #0b0f14 45%%, #070b10 100%%);
      color: var(--text);
      font-family: Arial, Helvetica, sans-serif;
    }

    body { padding: 14px; }

    .app {
      max-width: 1480px;
      margin: 0 auto;
      display: grid;
      gap: 14px;
    }

    .panel {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      overflow: hidden;
    }

    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      padding: 14px 18px;
      border-bottom: 1px solid var(--border);
    }

    .title {
      font-size: 1.25rem;
      font-weight: 800;
    }

    .subtitle {
      font-size: 0.92rem;
      color: var(--muted);
      margin-top: 4px;
    }

    .badge-row {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }

    .badge {
      padding: 8px 12px;
      border-radius: 999px;
      border: 1px solid var(--border);
      background: #182230;
      color: var(--muted);
      font-size: 0.86rem;
    }

    .timer-wrap {
      display: grid;
      grid-template-columns: 88px 1fr 88px;
      align-items: stretch;
      min-height: 160px;
    }

    .timer-adjust {
      border: 0;
      background: transparent;
      color: var(--text);
      font-size: clamp(2rem, 4vw, 3.3rem);
      cursor: pointer;
      transition: background 0.15s ease, transform 0.12s ease;
    }

    .timer-adjust:hover,
    .timer-main:hover,
    .btn:hover,
    .mini-btn:hover,
    .sidebar-btn:hover,
    .help-btn:hover {
      background: rgba(255,255,255,0.05);
    }

    .timer-adjust:active,
    .btn:active,
    .mini-btn:active,
    .sidebar-btn:active,
    .help-btn:active,
    .timer-main:active {
      transform: scale(0.985);
    }

    .timer-main {
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: clamp(3.5rem, 10vw, 8rem);
      font-weight: 700;
      letter-spacing: 0.06em;
      user-select: none;
      cursor: pointer;
      text-align: center;
      padding: 10px;
    }

    .top-actions {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 14px;
    }

    .btn {
      width: 100%%;
      min-height: 68px;
      border: 1px solid var(--border);
      background: var(--btn-bg);
      color: var(--text);
      font-size: clamp(1rem, 2.2vw, 1.25rem);
      font-weight: 700;
      border-radius: var(--radius);
      cursor: pointer;
      transition: background 0.15s ease, transform 0.12s ease;
    }

    .layout {
      display: grid;
      grid-template-columns: 2fr 1fr;
      gap: 14px;
    }

    .fencers {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
    }

    .side {
      display: grid;
      gap: 14px;
    }

    .name-card {
      min-height: 120px;
      display: flex;
      align-items: center;
      justify-content: center;
      text-align: center;
      font-size: clamp(1.5rem, 3vw, 2.4rem);
      font-weight: 700;
      padding: 18px;
      border-radius: var(--radius);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      word-break: break-word;
    }

    #red {
      background: linear-gradient(180deg, #6b1515 0%%, var(--red-dim) 100%%);
      color: #fff2f2;
    }

    #green {
      background: linear-gradient(180deg, #12743a 0%%, var(--green-dim) 100%%);
      color: #f1fff5;
    }

    .score-card {
      background: linear-gradient(180deg, #101720 0%%, var(--score-bg) 100%%);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      min-height: 220px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 18px;
      font-size: clamp(4.5rem, 13vw, 9rem);
      font-weight: 800;
      letter-spacing: 0.04em;
    }

    .score-flag {
      font-size: clamp(1.2rem, 3vw, 2rem);
      font-weight: 900;
      color: var(--yellow);
      min-width: 1.2em;
      text-align: center;
      opacity: 0;
    }

    .score-flag.active {
      opacity: 1;
    }

    .score-controls {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 10px;
    }

    .mini-btn {
      min-height: 74px;
      border: 1px solid var(--border);
      background: var(--btn-bg);
      border-radius: 16px;
      font-size: clamp(1.2rem, 3vw, 2rem);
      font-weight: 700;
      cursor: pointer;
      color: var(--text);
      transition: background 0.15s ease, transform 0.12s ease;
    }

    .sidebar {
      display: grid;
      gap: 14px;
    }

    .card {
      padding: 14px;
      border-radius: var(--radius);
      border: 1px solid var(--border);
      background: rgba(255,255,255,0.02);
    }

    .label {
      color: var(--muted);
      font-size: 0.82rem;
      text-transform: uppercase;
      letter-spacing: 0.12em;
      margin-bottom: 8px;
    }

    .name-input,
    .console {
      width: 100%%;
      border-radius: 14px;
      border: 1px solid var(--border);
      background: #0e151d;
      color: var(--text);
      padding: 12px 14px;
      font-size: 1rem;
      outline: none;
    }

    .console {
      resize: vertical;
      min-height: 90px;
      max-height: 160px;
      font-size: 0.92rem;
    }

    .row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-top: 10px;
    }

    .sidebar-btn,
    .help-btn {
      min-height: 58px;
      border: 1px solid var(--border);
      border-radius: 14px;
      background: var(--btn-bg);
      color: var(--text);
      font-size: 1rem;
      font-weight: 700;
      cursor: pointer;
      transition: background 0.15s ease, transform 0.12s ease;
    }

    .help-btn {
      background: var(--blue);
      color: white;
    }

    .footer-actions {
      display: grid;
      grid-template-columns: 120px 1fr;
      gap: 12px;
      align-items: stretch;
    }

    @media (max-width: 1050px) {
      .layout { grid-template-columns: 1fr; }
    }

    @media (max-width: 900px) {
      body { padding: 10px; }
      .fencers,
      .top-actions,
      .footer-actions,
      .row {
        grid-template-columns: 1fr;
      }
      .timer-wrap {
        grid-template-columns: 68px 1fr 68px;
        min-height: 120px;
      }
      .score-card { min-height: 170px; }
      .name-card { min-height: 92px; }
      .mini-btn,
      .btn,
      .sidebar-btn,
      .help-btn,
      .name-input,
      .console {
        min-height: 56px;
      }
    }
  </style>
</head>
<body onload="javascript:init()">
  <script>
    var message;
    var action;
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;

    document.addEventListener('keydown', logKey);
    function logKey(e) {
      if (`${e.code}` == "ArrowRight") { timerup(); }
      if (`${e.code}` == "ArrowLeft") { timerdown(); }
      if (`${e.code}` == "Space") { timertoggle(); e.preventDefault(); }
    }

    window.addEventListener('load', onLoad);

    function init() {}

    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onmessage = onMessage;
    }

    function onOpen(event) { console.log('Connection opened'); }

    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }

    function onMessage(event) {
      document.getElementById("rxConsole").value += event.data + "\n";
      document.getElementById("rxConsole").scrollTop = document.getElementById("rxConsole").scrollHeight;
      message = event.data;
      var fencer = message[0];

      if (fencer == "R") { redbox(); }
      else if (fencer == "G") { greenbox(); }
      else if (fencer == "r") { redscore(); }
      else if (fencer == "g") { greenscore(); }
      else if (fencer == "t") { timer(); }
      else if (fencer == "x") { redname(); }
      else if (fencer == "y") { greenname(); }
      else if (fencer == "p") { priorityupdate(); }
    }

    function onLoad(event) { initWebSocket(); }

    function redbox() {
      action = message[1];
      var box = document.getElementById("red");
      if (action == "H") {
        box.style.background = '#FF0000';
        box.style.color = '#fff2f2';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #6b1515 0%, #4a1010 100%)';
        box.style.color = '#fff2f2';
      }
    }

    function greenbox() {
      action = message[1];
      var box = document.getElementById("green");
      if (action == "H") {
        box.style.background = '#00FF00';
        box.style.color = '#111111';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #12743a 0%, #0f4a22 100%)';
        box.style.color = '#f1fff5';
      }
    }

    function redscore() {
      var score = message.substr(1,2);
      document.getElementById("redtext").textContent = score.trim();
    }

    function greenscore() {
      var score = message.substr(1,2);
      document.getElementById("greentext").textContent = score.trim();
    }

    function redname() {
      var rname = message.substr(1);
      document.getElementById("red").textContent = rname;
    }

    function greenname() {
      var gname = message.substr(1);
      document.getElementById("green").textContent = gname;
    }

    function timer() {
      var countdown = message.substr(1,4);
      document.getElementById("timer").textContent = countdown.trim();
    }

    function priorityupdate() {
      var p = message[1];

      var redFlag = document.getElementById("redPriorityFlag");
      var greenFlag = document.getElementById("greenPriorityFlag");

      redFlag.textContent = "";
      greenFlag.textContent = "";
      redFlag.classList.remove("active");
      greenFlag.classList.remove("active");

      if (p == "R") {
        redFlag.textContent = "P";
        redFlag.classList.add("active");
      } else if (p == "G") {
        greenFlag.textContent = "P";
        greenFlag.classList.add("active");
      }
    }

    function redup() { websocket.send("R+"); }
    function reddown() { websocket.send("R-"); }
    function greenup() { websocket.send("G+"); }
    function greendown() { websocket.send("G-"); }
    function redzero() { websocket.send("R0"); }
    function greenzero() { websocket.send("G0"); }

    function timertoggle() { websocket.send("TT"); }
    function timerreset() { websocket.send("TR"); }
    function timerdown() { websocket.send("T-"); }
    function timerup() { websocket.send("T+"); }
    function settime1() { websocket.send("T1"); }
    function settime3() { websocket.send("T3"); }

    function choosepriority() { websocket.send("PX"); }
    function clearpriority() { websocket.send("P0"); }

    function setRedName() {
      var v = document.getElementById("redNameInput").value;
      websocket.send("R:" + v);
      document.getElementById("redNameInput").value = "";
    }

    function setGreenName() {
      var v = document.getElementById("greenNameInput").value;
      websocket.send("G:" + v);
      document.getElementById("greenNameInput").value = "";
    }
  </script>

  <div class="app">
    <div class="panel">
      <div class="header">
        <div>
          <div class="title">Admin Dashboard</div>
          <div class="subtitle">Desktop / laptop layout</div>
        </div>
        <div class="badge-row">
          <div class="badge">Auto mode</div>
          <div class="badge">WebSocket control</div>
        </div>
      </div>

      <div class="timer-wrap">
        <button id="timer-" type="button" onclick="timerdown();" value="T-" class="timer-adjust">-</button>
        <div id="timer" onclick="timertoggle()" value="TT" class="timer-main">3:00</div>
        <button id="timer+" type="button" onclick="timerup();" value="T+" class="timer-adjust">+</button>
      </div>
    </div>

    <div class="top-actions">
      <button id="timertoggle" type="button" onclick="timertoggle();" value="TT" class="btn">Stop / Resume</button>
      <button id="timerreset" type="button" onclick="timerreset();" value="TR" class="btn">Reset</button>
      <button type="button" onclick="window.location='/display';" class="btn">Display Mode</button>
    </div>

    <div class="layout">
      <div class="fencers">
        <div class="side">
          <div id="red" class="name-card">R:name</div>
          <div class="score-card">
            <span id="redtext">0</span>
            <span id="redPriorityFlag" class="score-flag"></span>
          </div>
          <div class="score-controls">
            <button id="red-" type="button" onclick="reddown();" value="R-" class="mini-btn">-</button>
            <button id="red0" type="button" onclick="redzero();" value="R0" class="mini-btn">0</button>
            <button id="red+" type="button" onclick="redup();" value="R+" class="mini-btn">+</button>
          </div>
        </div>

        <div class="side">
          <div id="green" class="name-card">G:name</div>
          <div class="score-card">
            <span id="greentext">0</span>
            <span id="greenPriorityFlag" class="score-flag"></span>
          </div>
          <div class="score-controls">
            <button id="green-" type="button" onclick="greendown();" value="G-" class="mini-btn">-</button>
            <button id="green0" type="button" onclick="greenzero();" value="G0" class="mini-btn">0</button>
            <button id="green+" type="button" onclick="greenup();" value="G+" class="mini-btn">+</button>
          </div>
        </div>
      </div>

      <div class="sidebar">
        <div class="card">
          <div class="label">Rename Fencers</div>
          <input id="redNameInput" class="name-input" type="text" placeholder="Red fencer name" onkeydown="if(event.keyCode == 13) setRedName();" />
          <div class="row">
            <button type="button" onclick="setRedName();" class="sidebar-btn">Set Red</button>
            <button type="button" onclick="document.getElementById('redNameInput').value='';" class="sidebar-btn">Clear</button>
          </div>

          <input id="greenNameInput" class="name-input" type="text" placeholder="Green fencer name" style="margin-top:10px;" onkeydown="if(event.keyCode == 13) setGreenName();" />
          <div class="row">
            <button type="button" onclick="setGreenName();" class="sidebar-btn">Set Green</button>
            <button type="button" onclick="document.getElementById('greenNameInput').value='';" class="sidebar-btn">Clear</button>
          </div>
        </div>

        <div class="card">
          <div class="label">Set Time</div>
          <div class="row">
            <button type="button" onclick="settime1();" class="sidebar-btn">1 Minute</button>
            <button type="button" onclick="settime3();" class="sidebar-btn">3 Minutes</button>
          </div>

          <div class="label" style="margin-top:14px;">Priority</div>
          <div class="row">
            <button type="button" onclick="choosepriority();" class="sidebar-btn">Choose Priority</button>
            <button type="button" onclick="clearpriority();" class="sidebar-btn">Clear Priority</button>
          </div>

          <div class="label" style="margin-top:14px;">Event Log</div>
          <div class="footer-actions">
            <button onclick="document.location='help'" class="help-btn">Help</button>
            <textarea id="rxConsole" class="console"></textarea>
          </div>
        </div>
      </div>
    </div>
  </div>
</body>
</html>
)=====";

// -------------------------- REFEREE PAGE --------------------------
char refereepage[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
  <title>ScoreBox - Referee</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,"/>
  <style>
    :root {
      --bg: #0b0f14;
      --panel: rgba(18,25,34,0.94);
      --text: #eef2f7;
      --muted: #a9b4c2;
      --border: rgba(255,255,255,0.08);
      --red1: #7a1c1c;
      --red2: #4d1212;
      --green1: #138a45;
      --green2: #0d5129;
      --btn: #1b2632;
      --btn-hover: #273648;
      --blue: #2166f3;
      --radius: 22px;
      --shadow: 0 14px 35px rgba(0,0,0,0.35);
      --yellow: #ffe96a;
    }

    * { box-sizing: border-box; }

    html, body {
      margin: 0;
      min-height: 100%%;
      font-family: Arial, Helvetica, sans-serif;
      color: var(--text);
      background: radial-gradient(circle at top, #172433 0%%, #0c1016 45%%, #07090d 100%%);
    }

    body { padding: 12px; }

    .app {
      max-width: 920px;
      margin: 0 auto;
      display: grid;
      gap: 14px;
    }

    .panel {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      overflow: hidden;
    }

    .timer {
      padding: 18px;
      text-align: center;
      user-select: none;
      cursor: pointer;
    }

    .timer-label {
      color: var(--muted);
      font-size: 0.9rem;
      letter-spacing: 0.12em;
      text-transform: uppercase;
      margin-bottom: 8px;
    }

    .timer-value {
      font-size: clamp(4rem, 16vw, 8rem);
      font-weight: 800;
      letter-spacing: 0.06em;
      line-height: 1;
    }

    .timer-sub {
      margin-top: 10px;
      color: var(--muted);
      font-size: 0.95rem;
    }

    .score-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
    }

    .side {
      display: grid;
      gap: 14px;
    }

    .name {
      min-height: 92px;
      display: flex;
      align-items: center;
      justify-content: center;
      border-radius: var(--radius);
      font-size: clamp(1.6rem, 4vw, 2.5rem);
      font-weight: 800;
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      text-align: center;
      padding: 14px;
      word-break: break-word;
    }

    #red {
      background: linear-gradient(180deg, var(--red1) 0%%, var(--red2) 100%%);
      color: #fff2f2;
    }

    #green {
      background: linear-gradient(180deg, var(--green1) 0%%, var(--green2) 100%%);
      color: #f1fff5;
    }

    .score {
      min-height: 220px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 18px;
      border-radius: var(--radius);
      font-size: clamp(5rem, 20vw, 9rem);
      font-weight: 900;
      background: linear-gradient(180deg, #111821 0%%, #0d131a 100%%);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      user-select: none;
    }

    .score-flag {
      font-size: clamp(1.2rem, 5vw, 2rem);
      font-weight: 900;
      color: var(--yellow);
      min-width: 1.2em;
      text-align: center;
      opacity: 0;
    }

    .score-flag.active {
      opacity: 1;
    }

    .controls {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 10px;
    }

    .touch-btn {
      min-height: 88px;
      border: none;
      border-radius: 18px;
      font-size: clamp(1.1rem, 4vw, 1.8rem);
      font-weight: 800;
      color: var(--text);
      background: var(--btn);
      border: 1px solid var(--border);
      cursor: pointer;
      transition: background 0.15s ease, transform 0.12s ease;
    }

    .touch-btn:hover,
    .big-btn:hover,
    .help-btn:hover,
    .console:hover {
      background: var(--btn-hover);
    }

    .touch-btn:active,
    .big-btn:active,
    .help-btn:active {
      transform: scale(0.98);
    }

    .actions {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }

    .big-btn, .help-btn {
      min-height: 82px;
      border: none;
      border-radius: 18px;
      font-size: clamp(1rem, 3.2vw, 1.35rem);
      font-weight: 800;
      cursor: pointer;
      transition: background 0.15s ease, transform 0.12s ease;
      border: 1px solid var(--border);
      color: var(--text);
      background: var(--btn);
    }

    .help-btn {
      background: var(--blue);
      color: white;
    }

    .console-panel {
      padding: 12px;
    }

    .command-row {
      display: grid;
      grid-template-columns: 110px 1fr;
      gap: 10px;
      margin-bottom: 10px;
    }

    .console {
      width: 100%%;
      border-radius: 16px;
      border: 1px solid var(--border);
      padding: 12px;
      font-size: 0.92rem;
      outline: none;
      background: #0e151d;
      color: var(--text);
      min-height: 80px;
      max-height: 130px;
      resize: vertical;
    }

    .status {
      color: var(--muted);
      font-size: 0.92rem;
      margin-top: 8px;
    }

    @media (max-width: 720px) {
      .score-grid, .actions {
        grid-template-columns: 1fr;
      }
      .score { min-height: 180px; }
      .touch-btn, .big-btn, .help-btn { min-height: 74px; }
    }
  </style>
</head>
<body onload="javascript:init()">
  <script>
    var message;
    var action;
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;

    document.addEventListener('keydown', logKey);
    function logKey(e) {
      if (`${e.code}` == "ArrowRight") { timerup(); }
      if (`${e.code}` == "ArrowLeft") { timerdown(); }
      if (`${e.code}` == "Space") { timertoggle(); e.preventDefault(); }
    }

    window.addEventListener('load', onLoad);

    function init() {}

    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onmessage = onMessage;
    }

    function onOpen(event) { console.log('Connection opened'); }

    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }

    function onMessage(event) {
      document.getElementById("rxConsole").value += event.data + "\n";
      document.getElementById("rxConsole").scrollTop = document.getElementById("rxConsole").scrollHeight;
      message = event.data;
      var fencer = message[0];

      if (fencer == "R") { redbox(); }
      else if (fencer == "G") { greenbox(); }
      else if (fencer == "r") { redscore(); }
      else if (fencer == "g") { greenscore(); }
      else if (fencer == "t") { timer(); }
      else if (fencer == "x") { redname(); }
      else if (fencer == "y") { greenname(); }
      else if (fencer == "p") { priorityupdate(); }
    }

    function onLoad(event) { initWebSocket(); }

    function redbox() {
      action = message[1];
      var box = document.getElementById("red");
      if (action == "H") {
        box.style.background = '#FF0000';
        box.style.color = '#fff2f2';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #7a1c1c 0%, #4d1212 100%)';
        box.style.color = '#fff2f2';
      }
    }

    function greenbox() {
      action = message[1];
      var box = document.getElementById("green");
      if (action == "H") {
        box.style.background = '#00FF00';
        box.style.color = '#111111';
      }
      else if (action == "M") {
        box.style.background = '#FFFFFF';
        box.style.color = '#111111';
      }
      else if (action == "O") {
        box.style.background = 'linear-gradient(180deg, #138a45 0%, #0d5129 100%)';
        box.style.color = '#f1fff5';
      }
    }

    function redscore() {
      var score = message.substr(1,2);
      document.getElementById("redtext").textContent = score.trim();
    }

    function greenscore() {
      var score = message.substr(1,2);
      document.getElementById("greentext").textContent = score.trim();
    }

    function redname() {
      var rname = message.substr(1);
      document.getElementById("red").textContent = rname;
    }

    function greenname() {
      var gname = message.substr(1);
      document.getElementById("green").textContent = gname;
    }

    function timer() {
      var countdown = message.substr(1,4);
      document.getElementById("timer").textContent = countdown.trim();
    }

    function priorityupdate() {
      var p = message[1];

      var redFlag = document.getElementById("redPriorityFlag");
      var greenFlag = document.getElementById("greenPriorityFlag");

      redFlag.textContent = "";
      greenFlag.textContent = "";
      redFlag.classList.remove("active");
      greenFlag.classList.remove("active");

      if (p == "R") {
        redFlag.textContent = "P";
        redFlag.classList.add("active");
      } else if (p == "G") {
        greenFlag.textContent = "P";
        greenFlag.classList.add("active");
      }
    }

    function redup() { websocket.send("R+"); }
    function reddown() { websocket.send("R-"); }
    function greenup() { websocket.send("G+"); }
    function greendown() { websocket.send("G-"); }
    function redzero() { websocket.send("R0"); }
    function greenzero() { websocket.send("G0"); }

    function timertoggle() { websocket.send("TT"); }
    function timerreset() { websocket.send("TR"); }
    function timerdown() { websocket.send("T-"); }
    function timerup() { websocket.send("T+"); }
    function settime1() { websocket.send("T1"); }
    function settime3() { websocket.send("T3"); }

    function choosepriority() { websocket.send("PX"); }
    function clearpriority() { websocket.send("P0"); }
  </script>

  <div class="app">
    <div class="panel timer" id="timerTap" onclick="timertoggle()" value="TT">
      <div class="timer-label">Bout Timer</div>
      <div class="timer-value" id="timer">3:00</div>
      <div class="timer-sub">Tap timer or Start / Stop</div>
    </div>

    <div class="score-grid">
      <div class="side">
        <div id="red" class="name">R:name</div>
        <div class="score">
          <span id="redtext">0</span>
          <span id="redPriorityFlag" class="score-flag"></span>
        </div>
        <div class="controls">
          <button id="red-" type="button" onclick="reddown();" value="R-" class="touch-btn">-1</button>
          <button id="red0" type="button" onclick="redzero();" value="R0" class="touch-btn">Reset</button>
          <button id="red+" type="button" onclick="redup();" value="R+" class="touch-btn">+1</button>
        </div>
      </div>

      <div class="side">
        <div id="green" class="name">G:name</div>
        <div class="score">
          <span id="greentext">0</span>
          <span id="greenPriorityFlag" class="score-flag"></span>
        </div>
        <div class="controls">
          <button id="green-" type="button" onclick="greendown();" value="G-" class="touch-btn">-1</button>
          <button id="green0" type="button" onclick="greenzero();" value="G0" class="touch-btn">Reset</button>
          <button id="green+" type="button" onclick="greenup();" value="G+" class="touch-btn">+1</button>
        </div>
      </div>
    </div>

    <div class="actions">
      <button id="timertoggle" type="button" onclick="timertoggle();" value="TT" class="big-btn">Start / Stop</button>
      <button id="timerreset" type="button" onclick="timerreset();" value="TR" class="big-btn">Reset Bout</button>
    </div>

    <div style="display:none;">
      <button id="timer-" type="button" onclick="timerdown();" value="T-">-</button>
      <button id="timer+" type="button" onclick="timerup();" value="T+">+</button>
    </div>

    <div class="panel console-panel">
      <div class="status" style="margin-bottom:10px;">Set Time</div>
      <div class="actions" style="margin-bottom:12px;">
        <button type="button" onclick="settime1();" class="big-btn">1 Minute</button>
        <button type="button" onclick="settime3();" class="big-btn">3 Minutes</button>
      </div>

      <div class="status" style="margin-bottom:10px;">Priority</div>
      <div class="actions" style="margin-bottom:12px;">
        <button type="button" onclick="choosepriority();" class="big-btn">Choose Priority</button>
        <button type="button" onclick="clearpriority();" class="big-btn">Clear Priority</button>
      </div>

      <div class="command-row" style="grid-template-columns: 110px 1fr;">
        <button onclick="document.location='help'" class="help-btn">Help</button>
        <textarea id="rxConsole" class="console"></textarea>
      </div>

      <div class="status">Phone / tablet layout. Arrow left/right still adjust the timer. Space still toggles the timer.</div>
    </div>
  </div>
</body>
</html>
)=====";

// ----------------------- FORWARD DECLS ---------------------------
void initWebSocket();
String processor(const String& var);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void getlocalarduino();
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);

bool isMobileUserAgent(AsyncWebServerRequest *request);

void redhit();
void greenhit();
void redmiss();
void greenmiss();
void redoff();
void greenoff();

void redup();
void reddown();
void greenup();
void greendown();
void redzero();
void greenzero();

void sendred();
void sendgreen();
void pushredname();
void pushgreenname();
void sendpriority();

void timer();
void resettimer();
void sendtime();
void timerup();
void timerdown();
void settimer1();
void settimer3();
void clearpriority();
void chooserandompriority();
void setpriorityred();
void setprioritygreen();

// ----------------------------- SETUP -----------------------------
void setup() {
  Serial.begin(74880);
  delay(200);
  Serial.println();
  Serial.println("BOOT OK - starting fencing AP sketch");
  Serial.println(version);

  mySerial.begin(115200);
  randomSeed(micros());

  Serial.println("About to configure SoftAP...");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "SoftAPConfig: Ready" : "SoftAPConfig: Failed!");

  Serial.print("Setting soft-AP ... ");
  Serial.println(WiFi.softAP(ssid, password) ? "SoftAP: Ready" : "SoftAP: Failed!");
  Serial.println("SoftAP call done.");

  Serial.print("Soft-AP IP address = ");
  Serial.println(WiFi.softAPIP());

  initWebSocket();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (isMobileUserAgent(request)) {
      request->send_P(200, "text/html", refereepage, processor);
    } else {
      request->send_P(200, "text/html", adminpage, processor);
    }
  });

  server.on("/display", HTTP_GET, [](AsyncWebServerRequest * request) {
  request->send_P(200, "text/html", displaypage, processor);
}); 

  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", adminpage, processor);
  });

  server.on("/referee", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", refereepage, processor);
  });

  server.on("/help", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", helppage, processor);
  });

  server.begin();
  Serial.println("HTTP server started.");

  resettimer();
}

// ------------------------------ LOOP -----------------------------
void loop() {
  if (mySerial.available()) {
    getlocalarduino();
  }
  ws.cleanupClients();
  timer();
}

// ====================== DEVICE DETECTION ======================
bool isMobileUserAgent(AsyncWebServerRequest *request) {
  if (!request->hasHeader("User-Agent")) return false;

  const AsyncWebHeader* h = request->getHeader("User-Agent");
  if (h == nullptr) return false;

  String ua = h->value();
  ua.toLowerCase();

  if (ua.indexOf("android") >= 0) return true;
  if (ua.indexOf("iphone") >= 0) return true;
  if (ua.indexOf("ipod") >= 0) return true;
  if (ua.indexOf("ipad") >= 0) return true;
  if (ua.indexOf("mobile") >= 0) return true;
  if (ua.indexOf("tablet") >= 0) return true;
  if (ua.indexOf("opera mini") >= 0) return true;
  if (ua.indexOf("iemobile") >= 0) return true;
  if (ua.indexOf("silk") >= 0) return true;

  return false;
}

// ====================== WEBSOCKET HANDLING ======================
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    return;
  }
  if (len == 0) return;

  char c0 = (char)data[0];
  char c1 = (len > 1) ? (char)data[1] : '\0';

  if (c0 == 'R') {
    if (c1 == 'H') {
      Serial.println("* Red hit (web)");
      redhit();
      LEDcounter = millis() + interval;
      LEDON = true;
    }
    else if (c1 == 'M') {
      Serial.println("* Red miss (web)");
      redmiss();
      LEDcounter = millis() + interval;
      LEDON = true;
    }
    else if (c1 == '-') {
      Serial.println("* Red down (web)");
      reddown();
    }
    else if (c1 == '+') {
      Serial.println("* Red up (web)");
      redup();
    }
    else if (c1 == '0') {
      Serial.println("* Red zero (web)");
      redzero();
    }
    else if (c1 == ':') {
      String payload_str;
      payload_str.reserve(len);
      for (size_t i = 0; i < len; i++) payload_str += (char)data[i];
      redname = payload_str.substring(2);
      Serial.print("* Red name: ");
      Serial.println(redname);
      pushredname();
    }
  }
  else if (c0 == 'G') {
    if (c1 == 'H') {
      Serial.println("* Green hit (web)");
      greenhit();
      LEDcounter = millis() + interval;
      LEDON = true;
    }
    else if (c1 == 'M') {
      Serial.println("* Green miss (web)");
      greenmiss();
      LEDcounter = millis() + interval;
      LEDON = true;
    }
    else if (c1 == '-') {
      Serial.println("* Green down (web)");
      greendown();
    }
    else if (c1 == '+') {
      Serial.println("* Green up (web)");
      greenup();
    }
    else if (c1 == '0') {
      Serial.println("* Green zero (web)");
      greenzero();
    }
    else if (c1 == ':') {
      String payload_str;
      payload_str.reserve(len);
      for (size_t i = 0; i < len; i++) payload_str += (char)data[i];
      greenname = payload_str.substring(2);
      Serial.print("* Green name: ");
      Serial.println(greenname);
      pushgreenname();
    }
  }
  else if (c0 == 'T') {
    if (c1 == 'T') {
      timerrunning = !timerrunning;
      Serial.print("Timer running: ");
      Serial.println(timerrunning);
    }
    else if (c1 == 'R') {
      resettimer();
    }
    else if (c1 == '-') {
      timerdown();
    }
    else if (c1 == '+') {
      timerup();
    }
    else if (c1 == '1') {
      settimer1();
    }
    else if (c1 == '3') {
      settimer3();
    }
  }
  else if (c0 == 'P') {
    if (c1 == 'X') {
      chooserandompriority();
    }
    else if (c1 == '0' || c1 == 'N') {
      clearpriority();
    }
    else if (c1 == 'R') {
      setpriorityred();
    }
    else if (c1 == 'G') {
      setprioritygreen();
    }
  }
  else if (c0 == 'M') {
    if (c1 == 'F') { Serial.println("* FOIL - autoincrement OFF"); SWORD='F'; AUTOINCREMENT=false; }
    else if (c1 == 'E') { Serial.println("* EPEE - autoincrement ON"); SWORD='E'; AUTOINCREMENT=true; }
    else if (c1 == 'S') { Serial.println("* SABRE - autoincrement OFF"); SWORD='S'; AUTOINCREMENT=false; }
  }
  else if (c0 == '#') {
    Serial.print("# ");
    for (size_t i = 0; i < len; i++) Serial.print((char)data[i]);
    Serial.println();
  }
  else {
    Serial.println("Unknown web command");
  }
}

// ====================== SERIAL FROM ARDUINO ======================
void getlocalarduino() {
  if (mySerial.peek() > 13) {
    incode[0] = mySerial.read();
    incode[1] = mySerial.read();
    incode[2] = mySerial.read();
    incode[3] = mySerial.read();
    incode[4] = '\0';

    if (incode[0] == 'R') {
      if (incode[1] == 'H') {
        Serial.println("* Red hit (arduino)");
        redhit();
        LEDcounter = millis() + interval;
        LEDON = true;
      } else if (incode[1] == 'M') {
        Serial.println("* Red miss (arduino)");
        redmiss();
        LEDcounter = millis() + interval;
        LEDON = true;
      }
    }
    else if (incode[0] == 'G') {
      if (incode[1] == 'H') {
        Serial.println("* Green hit (arduino)");
        greenhit();
        LEDcounter = millis() + interval;
        LEDON = true;
      } else if (incode[1] == 'M') {
        Serial.println("* Green miss (arduino)");
        greenmiss();
        LEDcounter = millis() + interval;
        LEDON = true;
      }
    }
    else if (incode[0] == 'L' && incode[1] == 'O') {
      Serial.println("* Lights out (arduino)");
      redoff();
      greenoff();
    }
    else if (incode[0] == 'M') {
      if (incode[1] == 'F') { AUTOINCREMENT=false; SWORD='F'; Serial.println("* FOIL - autoincrement OFF"); }
      else if (incode[1] == 'E') { AUTOINCREMENT=true; SWORD='E'; Serial.println("* EPEE - autoincrement ON"); }
      else if (incode[1] == 'S') { AUTOINCREMENT=false; SWORD='S'; Serial.println("* SABRE - autoincrement OFF"); }
    }
    else {
      Serial.print("Other msg from Arduino: ");
      Serial.println(incode);
    }
  } else {
    mySerial.read();
  }
}

// ====================== GAME EVENTS ======================
void redhit() {
  if (timerrunning) {
    if (AUTOINCREMENT) redup();
    REDHIT = true;
  }
  ws.textAll("RH");
  sendred();
}

void greenhit() {
  if (timerrunning) {
    if (AUTOINCREMENT) greenup();
    GREENHIT = true;
  }
  ws.textAll("GH");
  sendgreen();
}

void redmiss() {
  timerrunning = false;
  ws.textAll("RM");
  timer();
}

void greenmiss() {
  timerrunning = false;
  ws.textAll("GM");
  timer();
}

void redoff()   { ws.textAll("RO"); }
void greenoff() { ws.textAll("GO"); }

// ====================== SCORING ======================
void redup() {
  redscore++;
  if (redscore > 99) redscore = 0;
  sendred();
}

void reddown() {
  redscore--;
  if (redscore < 0) redscore = 99;
  sendred();
}

void greenup() {
  greenscore++;
  if (greenscore > 99) greenscore = 0;
  sendgreen();
}

void greendown() {
  greenscore--;
  if (greenscore < 0) greenscore = 99;
  sendgreen();
}

void redzero() {
  redscore = 0;
  sendred();
}

void greenzero() {
  greenscore = 0;
  sendgreen();
}

void sendred() {
  char rscore[6] = {0};
  itoa(redscore, rscore, 10);

  char msg[16] = {0};
  strcpy(msg, "r");
  strcat(msg, rscore);
  strcat(msg, " ");
  ws.textAll(msg);
}

void sendgreen() {
  char gscore[6] = {0};
  itoa(greenscore, gscore, 10);

  char msg[16] = {0};
  strcpy(msg, "g");
  strcat(msg, gscore);
  strcat(msg, " ");
  ws.textAll(msg);
}

void pushredname() {
  char msg[48] = {0};
  strcpy(msg, "x");

  char payload[44] = {0};
  redname.toCharArray(payload, sizeof(payload));
  strncat(msg, payload, sizeof(msg) - 2);

  ws.textAll(msg);
}

void pushgreenname() {
  char msg[48] = {0};
  strcpy(msg, "y");

  char payload[44] = {0};
  greenname.toCharArray(payload, sizeof(payload));
  strncat(msg, payload, sizeof(msg) - 2);

  ws.textAll(msg);
}

void sendpriority() {
  char msg[4] = {0};
  msg[0] = 'p';
  msg[1] = PRIORITY;
  msg[2] = '\0';
  ws.textAll(msg);
}

// ====================== TIMER ======================
void timer() {
  if ((LEDON == true) && (LEDcounter < millis())) {
    timerrunning = false;
    Serial.println("..Lights out (web)");
    redoff();
    greenoff();
    REDHIT = false;
    GREENHIT = false;
    LEDON = false;
  }

  unsigned long timercurrentMillis = millis();

  if ((timercurrentMillis - timerpreviousMillis >= timerinterval) &&
      timerrunning && !REDHIT && !GREENHIT) {

    timerpreviousMillis = timercurrentMillis;
    seconds--;

    if (seconds < 0) {
      seconds = 59;
      minutes--;
      if (minutes < 0) minutes = 0;
    }

    if (seconds == 0 && minutes == 0) {
      timerrunning = false;
      sendtime();
      for (int i = 0; i < 4; i++) {
        ws.textAll("RH");
        ws.textAll("GH");
        delay(800);
        redoff();
        greenoff();
        delay(800);
      }
    }

    sendtime();
  }
}

void resettimer() {
  timerrunning = false;
  minutes = 3;
  seconds = 0;
  PRIORITY = 'N';
  REDHIT = false;
  GREENHIT = false;
  LEDON = false;
  Serial.println("Timer reset");
  sendtime();
  sendpriority();
  redzero();
  greenzero();
  redoff();
  greenoff();
}

void sendtime() {
  char mins[4] = {0};
  itoa(minutes, mins, 10);

  char secs[4] = {0};
  itoa(seconds, secs, 10);

  char msg[16] = {0};
  strcpy(msg, "t");
  strcat(msg, mins);
  strcat(msg, ":");
  if (seconds < 10) strcat(msg, "0");
  strcat(msg, secs);
  strcat(msg, " ");
  ws.textAll(msg);
}

void timerup() {
  timerrunning = false;
  seconds++;
  if (seconds > 59) {
    seconds = 0;
    minutes++;
  }
  sendtime();
}

void timerdown() {
  timerrunning = false;
  seconds--;

  if (seconds < 0 && minutes > 0) {
    seconds = 59;
    minutes--;
    if (minutes < 0) minutes = 0;
  }
  if (seconds < 0 && minutes == 0) {
    seconds = 0;
  }
  sendtime();
}

void settimer1() {
  timerrunning = false;
  minutes = 1;
  seconds = 0;
  Serial.println("Timer set to 1:00");
  sendtime();
}

void settimer3() {
  timerrunning = false;
  minutes = 3;
  seconds = 0;
  Serial.println("Timer set to 3:00");
  sendtime();
}

void clearpriority() {
  PRIORITY = 'N';
  Serial.println("Priority cleared");
  sendpriority();
}

void setpriorityred() {
  PRIORITY = 'R';
  Serial.println("Priority set: RED");
  sendpriority();
}

void setprioritygreen() {
  PRIORITY = 'G';
  Serial.println("Priority set: GREEN");
  sendpriority();
}

void chooserandompriority() {
  if (random(0, 2) == 0) {
    PRIORITY = 'R';
    Serial.println("Priority randomly chosen: RED");
  } else {
    PRIORITY = 'G';
    Serial.println("Priority randomly chosen: GREEN");
  }
  sendpriority();
}

// ====================== WEBSOCKET SETUP ======================
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var) {
  return String();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      sendtime();
      sendred();
      sendgreen();
      pushredname();
      pushgreenname();
      sendpriority();
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;

    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;

    case WS_EVT_PONG:
    case WS_EVT_ERROR:
    default:
      break;
  }
}
