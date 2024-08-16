#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

String kode;

#define RELAY_PIN1 13
#define RELAY_PIN2 14
#define WIFI_SSID "Lolipop"
#define WIFI_PASSWORD "luplup02"
#define API_KEY "AIzaSyCC-IAiDDfX42OZJ8QE1Ky3n2hz_A9LzF8"
#define FIREBASE_PROJECT_ID "smartlocker-df2cb"
#define USER_EMAIL "koplak90@gmail.com"
#define USER_PASSWORD "Koplak90"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
LiquidCrystal_I2C lcd(0x27, 16, 2);

String generateUniqueId() {
  String id = "";
  char characters[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) id += characters[random(0, 16)];
  id += "-";
  for (int i = 0; i < 4; i++) id += characters[random(0, 16)];
  id += "-";
  for (int i = 0; i < 4; i++) id += characters[random(0, 16)];
  id += "-";
  for (int i = 0; i < 4; i++) id += characters[random(0, 16)];
  id += "-";
  for (int i = 0; i < 12; i++) id += characters[random(0, 16)];
  return id;
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  Serial1.setTimeout(100);

  pinMode(RELAY_PIN1, OUTPUT);
  pinMode(RELAY_PIN2, OUTPUT);
  digitalWrite(RELAY_PIN1, HIGH);
  digitalWrite(RELAY_PIN2, HIGH);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Connecting to ");
  lcd.setCursor(0, 1);
  lcd.print("WiFi ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    spinner();
  }
  lcd.clear();
  Serial.println("Connected to WiFi");

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!Firebase.ready()) {
    Serial.println("Firebase Initialization Failed!");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Firebase Initialized");
  randomSeed(analogRead(0));
}

void loop() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("---[SCAN  QR]---");
  lcd.setCursor(0, 1);
  lcd.print("---[TO  OPEN]---");

  if (Serial1.available() > 0) {
    kode = Serial1.readString();
    Serial.println(kode);
    kode.trim();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PROCESSING");
    lcd.setCursor(0, 1);
    lcd.print("THE ORDER");
    spinner();

    if (retrieveAndProcessOrder(kode)) {
      Serial.println("Order processed successfully.");
    } else {
      Serial.println("Order processing failed.");
    }
  }
  delay(5000);
}

bool retrieveAndProcessOrder(const String& kode) {
  String path = "users";
  Serial.println("Attempting to retrieve document...");

  if (!Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", path.c_str(), "")) {
    Serial.println("Document retrieval failed");
    Serial.println(fbdo.errorReason());
    return false;
  }

  Serial.println("Document retrieval successful");

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, fbdo.payload());

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  JsonArray documents = doc["documents"].as<JsonArray>();
  for (JsonObject document : documents) {
    JsonArray orders = document["fields"]["order"]["arrayValue"]["values"].as<JsonArray>();
    bool orderFound = false;

    for (size_t i = 0; i < orders.size(); i++) {
      JsonObject order = orders[i]["mapValue"].as<JsonObject>();
      String orderQR = order["fields"]["orderQR"]["stringValue"].as<String>();
      String lokerName = order["fields"]["lokerName"]["stringValue"].as<String>();
      lokerName.trim();

      if (orderQR == kode) {
        processOrder(order, lokerName, orders, i);
        orderFound = true;
        break;
      }
    }

    if (orderFound) {
      if (updateFirestoreDocument(document, orders)) {
        Serial.println("Order updated and/or deleted successfully.");
      } else {
        Serial.println("Failed to update Firestore");
      }
      return true;
    }
  }
  return false;
}

void processOrder(JsonObject order, const String& lokerName, JsonArray& orders, size_t index) {
  String orderType = order["fields"]["orderType"]["stringValue"].as<String>();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ORDER FOUND");
  delay(2000);
  Serial.println("Match found with orderType '" + orderType + "'!");

  LokersName(lokerName);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("OPENING DOOR ON:");
  lcd.setCursor(0, 1);
  lcd.print(lokerName);
  spinner();

  if (orderType == "masuk") {
    order["fields"]["orderQR"]["stringValue"] = generateUniqueId();
    order["fields"]["orderType"]["stringValue"] = "keluar";
  } else if (orderType == "keluar") {
    orders.remove(index);
  } else {
    Serial.println("The order not found");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ORDER NOT FOUND");
    lcd.setCursor(0, 1);
    lcd.print("PLEASE TRY AGAIN");
    delay(2000);
  }
}

bool updateFirestoreDocument(const JsonObject& document, const JsonArray& orders) {
  DynamicJsonDocument updateDoc(1024);
  JsonObject fields = updateDoc.createNestedObject("fields");
  JsonObject orderArray = fields.createNestedObject("order");
  JsonObject arrayValue = orderArray.createNestedObject("arrayValue");
  JsonArray values = arrayValue.createNestedArray("values");

  for (JsonObject order : orders) {
    values.add(order);
  }

  fields["id"]["stringValue"] = document["fields"]["id"]["stringValue"];
  fields["username"]["stringValue"] = document["fields"]["username"]["stringValue"];
  fields["password"]["stringValue"] = document["fields"]["password"]["stringValue"];
  fields["email"]["stringValue"] = document["fields"]["email"]["stringValue"];

  String documentPath = document["name"].as<String>();
  String correctPath = documentPath.substring(documentPath.indexOf("documents/") + 10);
  Serial.println("Updating document at path: " + correctPath);

  String updatedDocument;
  serializeJson(updateDoc, updatedDocument);
  Serial.println("Updated Document JSON:");
  Serial.println(updatedDocument);

  return Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", correctPath.c_str(), updatedDocument.c_str(), "");
}

void LokersName(const String& lokername) {
  if (lokername == "Loker1") {
    Serial.println(lokername);
    digitalWrite(RELAY_PIN1, LOW);
    Serial.println("Loker1 Terbuka");
    delay(5000);
    digitalWrite(RELAY_PIN1, HIGH);
    Serial.println("Loker1 Tertutup");
  } else if (lokername == "Loker2") {
    Serial.println(lokername);
    digitalWrite(RELAY_PIN2, LOW);
    Serial.println("Loker2 Terbuka");
    delay(5000);
    digitalWrite(RELAY_PIN2, HIGH);
    Serial.println("Loker2 Tertutup");
  }
}

void spinner() {
  static int8_t counter = 0;
  const char* glyphs = "\xa1\xa5\xdb";
  lcd.setCursor(15, 1);
  lcd.print(glyphs[counter++]);
  if (counter == strlen(glyphs)) {
    counter = 0;
  }
}
