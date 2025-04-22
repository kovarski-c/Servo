#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>

// --- Nastavení pinů ---
const int DIR       = 33;
const int PUL       = 32;
const int EN        = 25;
const int endstopF  = 14;
const int endstopB  = 12;
const int MS1       = 26;
const int MS2       = 27;

// --- Parametry motoru ---
int microStep = 8;
volatile int Pause_const, Revs_const, Cycles_const;
volatile int cycleCount = 0;
volatile int revsCount = 0;
volatile bool motorRunning = false;
volatile bool motorPaused  = false;

// --- WiFi a web server ---
AsyncWebServer server(80);
const char *ssid = "ESP32";
const char *password = "12345678";

// HTML kód zůstává nezměněn
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML lang="cs">
<html>
<head>
  <title>ESP Motor Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" charset="UTF-8">
  <style>
      button {
          border: none;
          color: white;
          padding: 15px 32px;
          text-align: center;
          font-size: 16px;
          margin: 4px 2px;
          cursor: pointer;
      }
      .stop { background-color: red; }
      .start { background-color: #04AA6D; }
      button:disabled {
          background-color: grey !important;
          cursor: not-allowed;
          opacity: 0.6;
      }
      input[type='number'] {
          padding: 10px;
          font-size: 16px;
          width: 140px;
          margin-bottom: 10px;
      }
  </style>
  
  <script>
    window.addEventListener("DOMContentLoaded", function() {
        const distanceInput = document.getElementById("distance");
        const speedInput = document.getElementById("speed");
        
        distanceInput.addEventListener("input", function() {
            let value = parseFloat(this.value);
            const max = 13.3;
            if (value > max) {
                this.value = max;
                alert("Maximální vzdálenost je 13,3 cm. Hodnota byla nastavena na 13,3.");
            }
        });
        
        speedInput.addEventListener("input", function() {
            let value = parseFloat(this.value);
            const max = 10;
            if (value > max) {
                this.value = max;
                alert("Maximální rychlost je 10 mm/s. Hodnota byla nastavena na 10. Maximální doporučená rychlost je 8 mm/s.");
            }
        });
    });

    let motorPaused = false; 
    let intervalId = null;  

    function handleStartInterrupt() {
      let btn = document.getElementById("start");
      if (btn.innerText === "Start") {
        sendCommand("started");
        btn.innerText = "Přerušit";
      } else {
        sendCommand("interrupt");
        btn.innerText = "Start"; 
      }
    }

    function sendCommand(state) {
        if (state === "started") {
            const distance = document.getElementById("distance").value;
            const speed = document.getElementById("speed").value;
            const cycles = document.getElementById("cycles").value;

            fetch(`/get?state=${state}&distance=${distance}&speed=${speed}&cycles=${cycles}`)
                .then(response => response.text())
                .then(() => {
                    document.querySelectorAll("input[type='number']").forEach(input => input.disabled = true);
                    document.getElementById("pause").disabled = false;
                    document.getElementById("start").disabled = false;

                    intervalId = setInterval(updateCycles, 1000);
                });
        } 
        else if (state === "paused") {
            fetch(`/get?state=${state}`)
                .then(response => response.text())
                .then(() => {
                    document.getElementById("pause").innerText = "Pokračovat";
                    document.getElementById("pause").style.backgroundColor = "#04AA6D";
                    motorPaused = true;

                    clearInterval(intervalId);
                })
                .catch(err => console.error(err));
        } 
        else if (state === "continued") {
            fetch(`/get?state=${state}`)
                .then(response => response.text())
                .then(() => {
                    document.getElementById("pause").innerText = "Pause";
                    document.getElementById("pause").style.backgroundColor = "red";
                    motorPaused = false;

                    intervalId = setInterval(updateCycles, 1000);
                })
                .catch(err => console.error(err));
        }

        else if (state === "interrupt") {
        fetch(`/get?state=${state}`)
          .then(response => response.text())
          .then(() => {
              // Reset UI: aktivujeme inputy, tlačítko Pause deaktivujeme
              document.querySelectorAll("input[type='number']").forEach(input => input.disabled = false);
              document.getElementById("pause").disabled = true;
              document.getElementById("start").disabled = false;
              document.getElementById("start").innerText = "Start";
              clearInterval(intervalId);
          })
          .catch(err => console.error(err));
      }
    }

    function updateCycles() {
        fetch('/cycles')
            .then(response => response.text())
            .then(data => {
                document.getElementById('cycleCount').innerText = data;
                let cyclesLeft = parseInt(data);
                if (isNaN(cyclesLeft)) cyclesLeft = 0; 

                if (cyclesLeft <= 0) {
                    document.querySelectorAll("input[type='number']").forEach(input => input.disabled = false);
                    document.getElementById("pause").disabled = true;
                    document.getElementById("start").disabled = false;
                    document.getElementById("start").innerText = "Start";
                    clearInterval(intervalId);
                }
            })
            .catch(error => {
              console.error("Chyba při získávání dat:", error);
              setTimeout(updateCycles, 1000);
            });
          }
  </script>
</head>
<body>
  <h2>ESP Motor Control</h2>
  
  <form onsubmit="return false;">
    <label>Vzdálenost (cm):</label>
    <input type="number" pattern="\d*" inputmode="numeric" step="0.1" id="distance" value="5"><br>
    <label>Rychlost (mm/s):</label>
    <input type="number" pattern="\d*" inputmode="numeric" step="0.1" id="speed" value="10"><br>
    <label>Počet cyklů:</label>
    <input type="number" pattern="\d*" inputmode="numeric" id="cycles" value="1"><br>

    <button class="start" id="start" onclick="handleStartInterrupt()">Start</button>
    <button class="stop" id="pause" onclick="sendCommand(motorPaused ? 'continued' : 'paused')" disabled>Pauza</button>
  </form>
  <hr>
  <p>Zbývající cykly: <span id="cycleCount">0</span></p>
    <hr>
    <br>
  <p>Maximální dosažitelná vzdálenost je <strong>13,3 cm</strong>.</p>
  <p>Maximální doporučená rychlost je <strong>8 mm/s</strong>.</p>
</body>
</html>
)rawliteral";

// Nastavení WiFi v režimu AP
void wifiSetup() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.print("ESP IP adresa: ");
  Serial.println(WiFi.softAPIP());
}

// --- Přesun firemní smyčky na výchozí polohu ---
void goToDefault(int pause = 10000 / (8 * 2 * microStep)) {
  digitalWrite(DIR, LOW);
  // Smyčka běží, dokud nenarazíme na přední spínač
  while (digitalRead(endstopF) == HIGH && motorRunning) {
    if (motorPaused) {
      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      digitalWrite(PUL, HIGH);
      delayMicroseconds(pause);
      digitalWrite(PUL, LOW);
      delayMicroseconds(pause);
    }
    // Uvolnit RTOS a nakrmit watchdog
    taskYIELD();
    esp_task_wdt_reset();
  }
}

// --- Jeden běh motoru ---
void servoRun(int pause, int revs) {
  digitalWrite(DIR, HIGH);
  revsCount = revs;
  while (revsCount > 0 && motorRunning) {
    if (motorPaused) {
      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      if (digitalRead(endstopB) == LOW) {
        Serial.println("Max distance reached! Stopping.");
        break;
      }
      digitalWrite(PUL, HIGH);
      delayMicroseconds(pause);
      digitalWrite(PUL, LOW);
      delayMicroseconds(pause);
      revsCount--;
    }
    taskYIELD();
    esp_task_wdt_reset();
  }
}

// --- FreeRTOS task pro ovládání motoru ---
void servoRunTask(void *parameter) {
  // Přidáme tento task do WDT
  esp_task_wdt_add(NULL);
  digitalWrite(EN, LOW);

  while (cycleCount > 0 && motorRunning) {
    servoRun(Pause_const, Revs_const);
    goToDefault(Pause_const);
    if (!motorPaused && motorRunning) {
      cycleCount--;
      revsCount = Revs_const;
    }
    // Krátce počkat pro stabilitu a feed WDT
    vTaskDelay(1);
    esp_task_wdt_reset();
  }

  // Dokončeno: vypnout driver a odebrat z WDT
  digitalWrite(EN, HIGH);
  motorRunning = false;
  esp_task_wdt_delete(NULL);
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  // Inicializace WDT: timeout 10s, bez panic
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms     = 10000,
    .idle_core_mask = 0, // sleduj obě jádra (0b11)
    .trigger_panic  = false                          // bez paniku‑handleru
  };

  pinMode(PUL, OUTPUT);
  pinMode(DIR, OUTPUT);
  pinMode(EN, OUTPUT);
  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  pinMode(endstopF, INPUT_PULLUP);
  pinMode(endstopB, INPUT_PULLUP);

  digitalWrite(EN, HIGH);
  digitalWrite(MS1, LOW);
  digitalWrite(MS2, LOW);

  wifiSetup();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", htmlPage);
  });
  
  server.on("/cycles", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(cycleCount));
  });

  // HTTP endpoint pro spuštění, pauzu a pokračování
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("state")) {
        String state = request->getParam("state")->value();

        if (state == "started" && !motorRunning) {
            motorRunning = true;
            motorPaused = false;
            Revs_const   = request->getParam("distance")->value().toFloat() * 1000 * microStep;
            Pause_const  = 10000 / (request->getParam("speed")->value().toFloat() * 2 * microStep);
            Cycles_const = request->getParam("cycles")->value().toFloat();

            cycleCount = Cycles_const;
            revsCount  = Revs_const;
            digitalWrite(EN, LOW);
            goToDefault();
            xTaskCreatePinnedToCore(servoRunTask, "MotorTask", 4096, NULL, 3, NULL, 1);
        } 
        else if (state == "paused") {
            motorPaused = true;
            digitalWrite(EN, HIGH);
        } 
        else if (state == "continued") {
            motorPaused = false;
            digitalWrite(EN, LOW);
        }
        else if (state == "interrupt") {
          motorRunning = false;
          motorPaused = false;
          digitalWrite(EN, HIGH);
          goToDefault(Pause_const);
      }
    }
    request->send(200, "text/plain", "OK");
  });
  server.begin();
}

void loop() {
  // Vše probíhá asynchronně, loop zůstává prázdný.
}