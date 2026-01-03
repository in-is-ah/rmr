#include "SSD1306Wire.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char *ssid = "RobotESP32-Network";
const char *password = "rmr123456";
WiFiServer server(80);

// MQTT setup for Pi communication
const char* mqttBroker = "192.168.4.10";  // Pi's IP when connected to ESP32's network
const int mqttPort = 1883;
const char* mqttClientId = "robot_esp32";
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// MQTT topics
const char* topicRobotIn = "robot/robot-in";           // ESP32 subscribes (Pi publishes)
const char* topicFloorRequest = "robot/floor-request"; // ESP32 publishes (Pi subscribes)
const char* topicStatus = "robot/status";              // ESP32 publishes (optional)

// LoRa setup
#define LORA_VEXT 3
SX1262 radio = new Module(8, 14, 12, 13);
#define LORA_FREQ 920.0

// OLED Set up
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define OLED_VEXT 36

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

// Binary message structure
struct Message {
  uint8_t ack;
  uint16_t seqNum;
  uint8_t currentFloor;
  uint8_t targetFloor;
  uint32_t timestamp;
} __attribute__((packed));

// Robot state
int currentFloor = 0;
int requestedFloor = 0;
uint16_t seqNum = 0;
int maxRetries = 5;

// MQTT connection state
unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 5000;  // Retry every 5 seconds
unsigned long lastMQTTMessageTime = 0;
const unsigned long MQTT_MESSAGE_DISPLAY_DURATION = 3000;  // Show message for 3 seconds

// Pi connection
const int inputPin = 47; // GPIO connected to Pi's output
int lastState = LOW;

enum RobotStatus {
  IDLE,
  FLOOR_REQUEST_SUCCESS,
  CALLING_ELEVATOR,
  ELEVATOR_CONFIRMED,
  ROBOT_IN,
  ROBOT_WAIT,
  ROBOT_OUT,
  COMMUNICATION_ERROR
};

RobotStatus robotStatus = IDLE;

void handleWebRequests();
void handleCurrentFloorUpdate(WiFiClient client, String request);
void handleFloorRequest(WiFiClient client, String request);
void handleStatusRequest(WiFiClient client);
void sendWebPage(WiFiClient client);
int extractFloorNumber(String request, String routeType);
void sendElevatorRequest();
String statusToString(RobotStatus status);
bool listenForAck(unsigned long timeoutMs);
bool robotIn(int inputPin);
void updateDisplay(String line1, String line2);

// MQTT functions
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool connectMQTT();  // Returns true if connected, false if not (non-blocking)
void sendFloorRequestToPi(int currentFloor, int targetFloor);
void getPositionFromPi(String line1, String line2);

void updateDisplay(String line1, String line2) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, line1);
  display.drawString(0, 12, line2);
  display.display();
}

// MQTT callback - handles messages from Pi
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n========== MQTT CALLBACK TRIGGERED ==========");
  Serial.print("Topic length: ");
  Serial.println(strlen(topic));
  Serial.print("Topic: '");
  Serial.print(topic);
  Serial.println("'");
  Serial.print("Expected topic: '");
  Serial.print(topicRobotIn);
  Serial.println("'");
  Serial.print("Payload length: ");
  Serial.println(length);
  
  // Convert MQTT payload to a C-string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("Message: '");
  Serial.print(message);
  Serial.println("'");
  
  lastMQTTMessageTime = millis();  // Track when we received this message
  
  // Use strcmp for reliable topic comparison
  int topicMatch = strcmp(topic, topicRobotIn);
  Serial.print("Topic comparison result: ");
  Serial.println(topicMatch);
  
  if (topicMatch == 0) {
    Serial.println("✓ Topic matches! Processing message...");
    updateDisplay("Received: " + String(message), "");
    
    if (strcmp(message, "entered1") == 0) {
      robotStatus = ROBOT_WAIT;
      updateDisplay("Robot at RWZ", "(from Pi via MQTT)");
      Serial.println("→ Robot at RWZ (from Pi via MQTT)"); 
    } else if (strcmp(message, "entered2") == 0) {
      robotStatus = ROBOT_IN;
      updateDisplay("Robot in elevator", "(from Pi via MQTT)");
      Serial.println("→ Robot entered elevator (from Pi via MQTT)"); 
    } else if (strcmp(message, "exited") == 0) {
      robotStatus = ROBOT_OUT;
      updateDisplay("Robot exited elevator", "(from Pi via MQTT)");
      Serial.println("→ Robot exited elevator (from Pi via MQTT)");
    } else if (strcmp(message, "positioning") == 0) { 
      updateDisplay("Robot positioning", "(from Pi via MQTT)");
      Serial.println("→ Robot positioning (from Pi via MQTT)");
    } else {
      Serial.print("→ Unknown message value: '");
      Serial.print(message);
      Serial.println("'");
    }
  } else {
    Serial.print("✗ Topic mismatch! Received on: '");
    Serial.print(topic);
    Serial.print("', expected: '");
    Serial.print(topicRobotIn);
    Serial.println("'");
  }
  Serial.println("==========================================\n");
}

// Connect to MQTT broker (non-blocking - attempts once per call)
bool connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }
  
  Serial.print("Connecting to MQTT broker at ");
  Serial.print(mqttBroker);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.println("...");
  
  if (mqttClient.connect(mqttClientId)) {
    Serial.println("MQTT connected!");
    Serial.print("Client ID: ");
    Serial.println(mqttClientId);
    Serial.print("Broker: ");
    Serial.print(mqttBroker);
    Serial.print(":");
    Serial.println(mqttPort);
    updateDisplay("MQTT connected", "");
    
    // Subscribe to robot-in topic with QoS 1 (explicit)
    Serial.print("Attempting to subscribe to: ");
    Serial.println(topicRobotIn);
    if (mqttClient.subscribe(topicRobotIn, 1)) {
      Serial.println("✓ Subscribed to: " + String(topicRobotIn) + " (QoS 1)");
      updateDisplay("Subscribed to", String(topicRobotIn));
      delay(100); // Small delay to ensure subscription is processed
      
      // Test: Publish a message to verify connection works
      String testMsg = "ESP32 connected and subscribed";
      if (mqttClient.publish("robot/status", testMsg.c_str())) {
        Serial.println("Test publish successful - connection verified");
      } else {
        Serial.println("Test publish failed - connection may be unstable");
      }
    } else {
      Serial.println("✗ Failed to subscribe to: " + String(topicRobotIn));
      Serial.print("MQTT state: ");
      Serial.println(mqttClient.state());
      updateDisplay("Failed to subscribe", String(topicRobotIn));
    }
    return true;
  } else {
    Serial.print("MQTT connection failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" (will retry next loop iteration)");
    return false;
  }
}

// Publish floor request to Pi via MQTT
void sendFloorRequestToPi(int currentFloor, int targetFloor) {
  if (!mqttClient.connected()) {
    connectMQTT();  // Non-blocking attempt
    if (!mqttClient.connected()) {
      Serial.println("Cannot publish - MQTT not connected");
      return;
    }
  }
  
  // Create JSON payload
  String payload = String(currentFloor) + "," + String(targetFloor) + "," + String(millis()/1000);
  
  // Publish payload to MQTT Topic (Floor Request)
  if (mqttClient.publish(topicFloorRequest, payload.c_str())) {
    Serial.println("Published floor request to Pi via MQTT: " + payload);
    updateDisplay("Sent to Pi via MQTT", "Floor: " + String(currentFloor) + "->" + String(targetFloor));
  } else {
    Serial.println("Failed to publish floor request to Pi");
  }
}

// Obtain Position Status from Pi via MQTT
// void getPositionFromPi(String line1, String line2) {
// }

String statusToString(RobotStatus status) {
  switch (status) {
  case IDLE:
    return "Ready for floor request";
  case FLOOR_REQUEST_SUCCESS:
    return "Request sent";
  case CALLING_ELEVATOR:
    return "Calling elevator";
  case ELEVATOR_CONFIRMED:
    return "Elevator called";
  case COMMUNICATION_ERROR:
    return "Elevator call failed";
  case ROBOT_IN:
    return "Robot is in the elevator";
  case ROBOT_WAIT:
    return "Robot waiting for elevator";
  case ROBOT_OUT:
    return "Robot exited elevator";
  default:
    return "Unknown";
  }
}

void handleWebRequests() {
  // Check if client connects
  WiFiClient client = server.available();
  if (!client)
    return;
  Serial.println("Client connected");
  String request = "";
  // Read the HTTP request
  unsigned long timeout = millis() + 3000;
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      request += line;
      if (line == "\r")
        break; // End of HTTP headers
    }
    delay(1);
  }

  // read request
  if (request.indexOf("GET /status") >= 0) {
    handleStatusRequest(client);
  } else if (request.indexOf("GET /") == 0 &&
             request.indexOf("GET /floor/") < 0 &&
             request.indexOf("GET /currentfloor/") < 0) {
    sendWebPage(client); // Allow webpage access
  } else if (robotStatus == IDLE) {
    if (request.indexOf("GET /currentfloor/") >= 0) {
      handleCurrentFloorUpdate(client, request);
    } else if (request.indexOf("GET /floor/") >= 0) {
      handleFloorRequest(client, request);
    }
  } else {
    client.println("HTTP/1.1 409 Conflict");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": false, \"error\": \"Request already in "
                   "progress\", \"status\": \"" +
                   statusToString(robotStatus) + "\"}");

    Serial.println("Floor request rejected - already processing: " +
                   statusToString(robotStatus));
  }
  client.stop();
}
int extractFloorNumber(String request, String routeType) {
  String route = "/" + routeType + "/"; // Makes "/currentfloor/" or "/floor/"
  int startPos = request.indexOf(route);

  if (startPos >= 0) {
    startPos += route.length();                  // Move past "/currentfloor/"
    int endPos = request.indexOf(" ", startPos); // Find space after number
    if (endPos > startPos) {
      return request.substring(startPos, endPos).toInt();
    }
  }
  return 0; // Error case
}

void handleCurrentFloorUpdate(WiFiClient client, String request) {
  // Extract floor number from request
  int floor = extractFloorNumber(request, "currentfloor");
  if (floor > 0) {
    currentFloor = floor;
    updateDisplay("Current floor " + String(currentFloor), "");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": true, \"currentFloor\": " + String(floor) +
                   "}");
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": false, \"error\": \"Invalid floor\"}");
  }
  Serial.println("Current Floor Selected:" + String(floor));
}

void handleFloorRequest(WiFiClient client, String request) {
  // Extract floor number from request
  int floor = extractFloorNumber(request, "floor");
  if (floor > 0) {
    requestedFloor = floor;
    updateDisplay("Target floor " + String(requestedFloor), "");
    robotStatus = FLOOR_REQUEST_SUCCESS;
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": true, \"floor\": " + String(floor) +
                   ", \"status\": \"" + statusToString(robotStatus) + "\"}");
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": false, \"error\": \"Invalid floor\"}");
  }
  Serial.println("Target Floor Selected:" + String(floor));
}

void handleStatusRequest(WiFiClient client) {
  // Send current robot status as JSON
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"status\": \"" + statusToString(robotStatus) +
                 "\", \"currentFloor\": " + String(currentFloor) +
                 ", \"requestedFloor\": " + String(requestedFloor) + "}");
}

void sendWebPage(WiFiClient client) {

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("    <title>Floor Selection</title>");
  client.println("    <meta name='viewport' content='width=device-width, "
                 "initial-scale=1'>");
  client.println("    <style>");
  client.println("        body{");
  client.println("            font-family: sans-serif;");
  client.println("            text-align: center;");
  client.println("            background: #f5f5f5;");
  client.println("            max-width: 500px;");
  client.println("            margin: 0 auto;");
  client.println("        }");
  client.println("        .stack{");
  client.println("            display: flex;");
  client.println("            flex-direction: column;");
  client.println("        }");
  client.println("select {");
  client.println("  width: 200px;");
  client.println("  margin: 0 auto;");
  client.println("}");
  client.println("    </style>");
  client.println("</head>");
  client.println("<body>");
  client.println("    <h1>Robot RMR</h1>");
  client.println("    <p>Status: <span id='robotStatus'>Ready</span></p>");
  client.println(
      "    <p>Current Floor: <span id='currentDisplay'>--</span></p>");
  client.println(
      "    <p>Requested Floor: <span id='requestedDisplay'>--</span></p>");
  client.println("    ");
  client.println("<div class='stack'>");

  client.println("<label for='currentFloor'>Current :</label>");
  client.println("<select id='currentFloor' name='currentFloor' "
                 "onchange='selectCurrentFloor(this.value)'>");
  client.println("<option value=''>-- Select Current Floor --</option>");
  for (int i = 1; i <= 7; i++) {
    client.println("<option value='" + String(i) + "'>" + String(i) +
                   "</option>");
  }
  client.println("</select>");

  client.println("<label for='targetFloor'>Request:</label>");
  client.println("<select id='targetFloor' name='targetFloor' "
                 "onchange='selectTargetFloor(this.value)'>");
  client.println("<option value=''>-- Select Target Floor --</option>");
  for (int i = 1; i <= 7; i++) {
    client.println("<option value='" + String(i) + "'>" + String(i) +
                   "</option>");
  }
  client.println("</select>");

  client.println("</div>");
  client.println("    ");
  client.println("    <script>");
  client.println("        function selectCurrentFloor(currFloor){");
  client.println("            fetch('/currentfloor/' + currFloor)");
  client.println("            .then(response => response.json())");
  client.println("            .then(data => {");
  client.println("                if (data.success) {");
  client.println(
      "                    "
      "document.getElementById('currentDisplay').textContent = currFloor;");
  client.println("                }");
  client.println("            })");
  client.println("            .catch(error => {");
  client.println("                alert('Error selecting current floor');");
  client.println("            });");
  client.println("        }");
  client.println("        ");
  client.println("        function selectTargetFloor(targetFloor){");
  client.println("            if (!targetFloor) return;");
  client.println("            const currentFloorSelect = "
                 "document.getElementById('currentFloor');");
  client.println(
      "            const selectedCurrentFloor = currentFloorSelect.value;");
  client.println("            ");
  client.println("            if (selectedCurrentFloor <= 0) {");
  client.println(
      "                alert('Please select your current floor first!');");
  client.println(
      "                document.getElementById('targetFloor').value = '';");
  client.println("                return;");
  client.println("            }");
  client.println("            ");
  client.println("            if (selectedCurrentFloor == targetFloor) {");
  client.println("                alert('You are already on floor ' + "
                 "targetFloor + '!');");
  client.println(
      "                document.getElementById('targetFloor').value = '';");
  client.println("                return;");
  client.println("            }");
  client.println("            ");
  client.println("            fetch('/floor/' + targetFloor)");
  client.println("            .then(response => response.json())");
  client.println("            .then(data => {");
  client.println("                if (data.success) {");
  client.println("                    "
                 "document.getElementById('requestedDisplay').textContent = "
                 "targetFloor;");
  client.println(
      "                    "
      "document.getElementById('robotStatus').textContent = data.status;");
  client.println("                }");
  client.println("            })");
  client.println("            .catch(error => {");
  client.println("                alert('Error selecting floor');");
  client.println("            });");
  client.println("        }");
  client.println("        ");
  client.println("        function updateStatus() {");
  client.println("            fetch('/status')");
  client.println("            .then(response => response.json())");
  client.println("            .then(data => {");
  client.println(
      "                document.getElementById('robotStatus').textContent = "
      "data.status;");
  client.println(
      "                document.getElementById('currentDisplay').textContent "
      "= data.currentFloor;");
  client.println("                "
                 "document.getElementById('requestedDisplay').textContent = "
                 "data.requestedFloor || '--';");
  client.println("            })");
  client.println(
      "            .catch(error => console.log('Status update failed'));");
  client.println("        }");
  client.println("        ");
  client.println("        setInterval(updateStatus, 3000);");
  client.println("    </script>");
  client.println("</body>");
  client.println("</html>");
}

bool listenForAck(unsigned long timeoutMs) {
  Serial.println("------Listening for ACK----");
  updateDisplay("Waiting for ACK from panel", "");
  unsigned long startTime = millis();
  while (millis() - startTime < timeoutMs) {
    uint8_t messageBuffer[sizeof(Message)];
    int state = radio.receive(messageBuffer, sizeof(Message));
    if (state == RADIOLIB_ERR_NONE) {
      // cast back to message struct
      Message *receivedMsg = (Message *)messageBuffer;
      // If msg is ACK,
      if (receivedMsg->ack == 1 && receivedMsg->seqNum == seqNum) {
        Serial.println("ACK received from panel!");
        updateDisplay("Received ACK from panel", "");
        return true;
      } else if (receivedMsg->ack == 0) {
        Serial.println("Ignoring request message from myself");
      } else {
        // Keep listening, if there's no ACK
        return false;
      }
    }
  }
  Serial.println("Listen timeout - no valid ACK received");
  return false;
}

void sendElevatorRequest() {
  Serial.println("Sending LoRa message...");
  // Create binary message
  Message msg;
  msg.ack = 0; // 0 = request, 1 = acknowledgment
  msg.seqNum = seqNum;
  msg.currentFloor = currentFloor;
  msg.targetFloor = requestedFloor;
  msg.timestamp = millis() / 1000;
  Serial.println("Message details");
  Serial.println("   Sequence: " + String(msg.seqNum));
  Serial.println("   Current Floor: " + String(msg.currentFloor));
  Serial.println("   Target Floor: " + String(msg.targetFloor));
  Serial.println("Bytes sent: " + String(sizeof(msg)));
  // Send
  int state = radio.transmit((uint8_t *)&msg, sizeof(msg));
  if (state == RADIOLIB_ERR_NONE) {
    // Print message details for debugging
    Serial.println("Request succuesfully sent!");
    updateDisplay("Sent current floor: " + String(msg.currentFloor),
                  "target floor: " + String(msg.targetFloor));
  } else {
    Serial.println("Binary transmission failed, code: " + String(state));
    robotStatus = COMMUNICATION_ERROR;
    return; // Exit early on failure
  }
}

bool robotIn(int inputPin) {
  int currentState = digitalRead(inputPin);
  if (currentState != lastState) {
    lastState = currentState;
    if (currentState == HIGH) {
      Serial.println("Elevator Entered");
      return true;
    } else {
      Serial.println(" Elevator Exited");
      return false;
    }
  }
  delay(50);
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(inputPin, INPUT_PULLDOWN);
  delay(300);
  // WiFi hotspot setup
  WiFi.softAP(ssid, password);
  server.begin();
  Serial.println("WiFi hotspot started");
  Serial.println("Connect to: " + String(ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  // LoRa initialization
  pinMode(LORA_VEXT, OUTPUT);
  digitalWrite(LORA_VEXT, LOW);
  delay(500);
  Serial.print("Initializing LoRa... ");
  if (radio.begin(LORA_FREQ) == RADIOLIB_ERR_NONE) {
    Serial.println("SUCCESS!");
    radio.setSpreadingFactor(12);
    radio.setBandwidth(125.0);
    radio.setCodingRate(5);
    radio.setOutputPower(14);
  } else {
    Serial.println("Failed");
  }
  // OLED initialization
  pinMode(OLED_VEXT, OUTPUT);
  digitalWrite(OLED_VEXT, LOW);
  delay(100);
  //  OLED reset
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(50);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  // MQTT initialization
  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);  // 60 second keepalive
  mqttClient.setSocketTimeout(5);  // 5 second socket timeout
  Serial.println("MQTT client initialized");
  Serial.println("Note: MQTT will connect when Pi connects to this network");
}

void loop() {
  // Handle incoming web requests (MUST be called regularly - non-blocking)
  handleWebRequests();
  
  // Maintain MQTT connection (non-blocking attempt, rate-limited)
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMQTTAttempt >= MQTT_RETRY_INTERVAL) {
      connectMQTT();  // Attempts once, returns immediately
      lastMQTTAttempt = now;
    }
  }
  
  // Process MQTT messages (non-blocking) - MUST be called frequently
  if (mqttClient.connected()) {
    mqttClient.loop();  // Must call regularly to process incoming messages
    
    // Only show status if we haven't received a message recently
    if (millis() - lastMQTTMessageTime > MQTT_MESSAGE_DISPLAY_DURATION) {
      updateDisplay("MQTT connected", "Waiting for messages...");
    }
    
    // Debug: Print connection status periodically
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 10000) {  // Every 10 seconds
      Serial.println("=== MQTT Status ===");
      Serial.print("Connected: ");
      Serial.println(mqttClient.connected() ? "YES" : "NO");
      Serial.print("Subscribed to: ");
      Serial.println(topicRobotIn);
      Serial.print("Last message time: ");
      Serial.println(lastMQTTMessageTime);
      Serial.print("Time since last message: ");
      Serial.print((millis() - lastMQTTMessageTime) / 1000);
      Serial.println(" seconds");
      Serial.println("==================");
      lastStatusPrint = millis();
    }
  } else {
    updateDisplay("MQTT not connected", "");
    static unsigned long lastDisconnectPrint = 0;
    if (millis() - lastDisconnectPrint > 5000) {
      Serial.print("MQTT disconnected. State: ");
      Serial.println(mqttClient.state());
      lastDisconnectPrint = millis();
    }
  }
  
  // Ensure subscription is active (resubscribe if needed)
  static unsigned long lastSubscriptionCheck = 0;
  if (mqttClient.connected() && (millis() - lastSubscriptionCheck > 30000)) {
    // Resubscribe every 30 seconds to ensure subscription is active
    Serial.println("Re-subscribing to ensure subscription is active...");
    if (mqttClient.subscribe(topicRobotIn, 1)) {
      Serial.println("Re-subscription successful");
    } else {
      Serial.println("Re-subscription failed!");
    }
    lastSubscriptionCheck = millis();
  }
  
  // Check if robot should send LoRa message
  if (robotStatus == FLOOR_REQUEST_SUCCESS && requestedFloor > 0) {
    robotStatus = CALLING_ELEVATOR;
    
    // Send floor request to Pi via MQTT (optional - for logging/monitoring)
    sendFloorRequestToPi(currentFloor, requestedFloor);
    // getPositionFromPi("Getting position", "");

    mqttClient.loop();
    
    // Increment sequence number
    seqNum++;
    bool received_ack = false;
    // Variable to keep track of retries
    int numRetries = 0;
    while (!received_ack && (numRetries <= maxRetries)) {
      sendElevatorRequest();
      received_ack = listenForAck(1000);
      if (received_ack) {
        robotStatus = ELEVATOR_CONFIRMED;
        Serial.println("Elevator request confirmed!");
        // reset back to accept new request for now, TODO: add more state
        // management
        // check if the robot is in the elevator
        // while (!robotIn(inputPin)) {
        //   delay(300);
        // }
        // robotStatus = ROBOT_IN;
        // Serial.println("Robot is in the elevator");
        // delay(500);
        robotStatus = IDLE;
        currentFloor = 0;
        requestedFloor = 0;
        break;
      } else {
        numRetries++;
        updateDisplay("Didn't receive ACK back", "Send request again");
        Serial.println(
            "Didn't receive ACK back. Send request again. Retry attempt " +
            String(numRetries) + " Seq: " + String(seqNum));
        delay(2000);
      }
    }
    if (!received_ack) {
      Serial.println("Failed to receive ack after" + String(maxRetries) +
                     ".Please request floor again");
      robotStatus = COMMUNICATION_ERROR;
      // Reset for testing
      robotStatus = IDLE;
      currentFloor = 0;
      requestedFloor = 0;
    }
  }
  delay(1000);
}