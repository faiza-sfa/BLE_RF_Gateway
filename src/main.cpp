#include "main.h"

void setup()
{
	strcpy(mqtt_server, DEFAULT_MQTT_BROKER);
	strcpy(pub_topic, DEFAULT_MQTT_PUB_TOPIC);

	Serial.begin(9600);
	delay(2000);
	pinMode(RESET_PIN, INPUT);
	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(RED_LED, OUTPUT);
	pinMode(GREEN_LED, OUTPUT);
	pinMode(BLUE_LED, OUTPUT);

	digitalWrite(RED_LED, LOW);
	digitalWrite(GREEN_LED, LOW);
	digitalWrite(BLUE_LED, LOW);

	initSPIFFS();
	
	sem_mqtt = xSemaphoreCreateBinary();
	sem_ble = xSemaphoreCreateBinary();
	sem_data = xSemaphoreCreateBinary();
	bleDataQueue = xQueueCreate(MAX_BLE_QUEUE_SIZE, sizeof(BLE_Data_t));
	ledBlinkQueue = xQueueCreate(MAX_LED_BLINK_QUEUE_SIZE, sizeof(LED_Blink_t));

	xTaskCreatePinnedToCore(BlinkLedTask, "blink", 2048, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	// xTaskCreatePinnedToCore(BLEScanTask, "BLE Scan Task", 4096*5, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	// xTaskCreatePinnedToCore(BleDataTask, "BLE Data Task", 4096, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	xTaskCreatePinnedToCore(MqttTask, "MQTT", 4096*2, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	xTaskCreatePinnedToCore(WiFitask, "Wifi", 4096, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	// xTaskCreatePinnedToCore(RFTask, "RF", 4096, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	if (s_esp_RF433_queue == NULL)
	{
		s_esp_RF433_queue = xQueueCreate(1, sizeof(uint8_t));
		if (s_esp_RF433_queue != NULL)
		{
			// Configure the data input
			gpio_config_t data_pin_config = {
				 .pin_bit_mask = GPIO_SEL_23, // GPIO_NUM_22 (SEL) DATA PIN!
				 .mode = GPIO_MODE_INPUT,
				 .pull_up_en = GPIO_PULLUP_DISABLE,
				 .pull_down_en = GPIO_PULLDOWN_DISABLE,
				 .intr_type = GPIO_INTR_ANYEDGE};

			gpio_config(&data_pin_config);

			// Attach the interrupt handler
			gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
			gpio_isr_handler_add(GPIO_NUM_22, data_interrupt_handler, NULL); // GPIO_NUM_22 DATA PIN!
			xTaskCreate(&receiver_rf433, "receiver_rf433", 2048, NULL, 3, NULL);
		}
	}
	// xTaskCreatePinnedToCore(Resettask, "reset", 2048, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
	
}

void loop()
{
}

void callback(char *topic, byte *payload, unsigned int length)
{
	// handle incoming message
}

void reconnect()
{
	// Loop until we're reconnected
	while (!client.connected())
	{
		digitalWrite(RED_LED, HIGH);
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		char *rand_str = randomString(30);
		if (client.connect(rand_str))
		{
			Serial.println(F("connected"));
			client.subscribe(DEFAULT_MQTT_SUB_TOPIC);
		}
		else
		{
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			vTaskDelay(pdMS_TO_TICKS(5000));
		}
		free(rand_str);
	}
	digitalWrite(RED_LED, LOW);
}
// Initialize SPIFFS
void initSPIFFS()
{
	if (!SPIFFS.begin(true))
	{
		Serial.println("An error has occurred while mounting SPIFFS");
	}
	Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path)
{
	Serial.printf("Reading file: %s\r\n", path);

	File file = fs.open(path);
	if (!file || file.isDirectory())
	{
		Serial.println("- failed to open file for reading");
		return String();
	}

	String fileContent;
	while (file.available())
	{
		fileContent = file.readStringUntil('\n');
		break;
	}
	Serial.println(fileContent);
	return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message)
{
	Serial.printf("Writing file: %s\r\n", path);

	File file = fs.open(path, FILE_WRITE);
	if (!file)
	{
		Serial.println("- failed to open file for writing");
		return;
	}
	if (file.print(message))
	{
		Serial.println("- file written");
	}
	else
	{
		Serial.println("- write failed");
	}
}

// Initialize WiFi
bool initWiFi()
{
	if (ssid == "" || ip == "")
	{
		Serial.println("Undefined SSID or IP address.");
		return false;
	}

	WiFi.mode(WIFI_STA);
	localIP.fromString(ip.c_str());
	localGateway.fromString(gateway.c_str());

	// if (!WiFi.config(localIP, localGateway, subnet))
	// {
	// 	Serial.println("STA Failed to configure");
	// 	return false;
	// }

	WiFi.begin(ssid.c_str(), pass.c_str());
	// server.end();
	Serial.printf("Connecting to SSID:%s\n", ssid.c_str());

	unsigned long currentMillis = millis();
	previousMillis = currentMillis;

	digitalWrite(RED_LED, HIGH);
	while (WiFi.status() != WL_CONNECTED);
	digitalWrite(RED_LED, LOW);

	Serial.println(WiFi.localIP());
	return true;
}

void serverhost()
{

	digitalWrite(LED_BUILTIN, HIGH);
	// Connect to Wi-Fi network with SSID and password
	Serial.println("Setting AP (Access Point)");
	// NULL sets an open Access Point
	WiFi.softAP("DMASS-233600", NULL);

	IPAddress IP = WiFi.softAPIP();
	Serial.print("AP IP address: ");
	Serial.println(IP);

	// Web Server Root URL
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->send(SPIFFS, "/wifimanager.html", "text/html"); });

	server.serveStatic("/", SPIFFS, "/");

	server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
			  {
		int params = request->params();
		for(int i=0;i<params;i++){
			AsyncWebParameter* p = request->getParam(i);
			if(p->isPost()){
			// HTTP POST ssid value
			if (p->name() == PARAM_INPUT_1) {
				ssid = p->value().c_str();
				Serial.print("SSID set to: ");
				Serial.println(ssid);
				// Write file to save value
				writeFile(SPIFFS, ssidPath, ssid.c_str());
			}
			// HTTP POST pass value
			if (p->name() == PARAM_INPUT_2) {
				pass = p->value().c_str();
				Serial.print("Password set to: ");
				Serial.println(pass);
				// Write file to save value
				writeFile(SPIFFS, passPath, pass.c_str());
			}
			// HTTP POST ip value
			if (p->name() == PARAM_INPUT_3) {
				ip = p->value().c_str();
				Serial.print("IP Address set to: ");
				Serial.println(ip);
				// Write file to save value
				writeFile(SPIFFS, ipPath, ip.c_str());
			}
			// HTTP POST gateway value
			if (p->name() == PARAM_INPUT_4) {
				gateway = p->value().c_str();
				Serial.print("Gateway set to: ");
				Serial.println(gateway);
				// Write file to save value
				writeFile(SPIFFS, gatewayPath, gateway.c_str());
			}
			//Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
			}
		}
			request->send(200, "text/plain", "Done!");
			LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
			led->blinks = 2;
			led->pin = GREEN_LED;
			led->high_delay = 150;
			led->low_delay = 150;
			if(xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
			{
				printf("BLE Data NOT sent!");
			}
			free(led);
			vTaskDelay(1000);
			ESP.restart(); });
	server.begin();
}
void readCredentialsFromSPIFFS()
{
	// Load values saved in SPIFFS
	ssid = readFile(SPIFFS, ssidPath);
	pass = readFile(SPIFFS, passPath);
	ip = readFile(SPIFFS, ipPath);
	gateway = readFile(SPIFFS, gatewayPath);
	Serial.println(ssid);
	Serial.println(pass);
	Serial.println(ip);
	Serial.println(gateway);
}
void WiFitask(void *parameter)
{
	readCredentialsFromSPIFFS();
	bool host_server = false;

	if (!initWiFi())
	{
		serverhost();
		host_server = true;
	}
	else
	{
		LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
		led->blinks = 3;
		led->pin = GREEN_LED;
		led->high_delay = 150;
		led->low_delay = 150;
		if(xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
		{
			printf("BLE Data NOT sent!");
		}
		free(led);

		if (xSemaphoreGive(sem_mqtt) != pdTRUE)
		{
			Serial.println(F("MQTT Semaphore not released"));
		}
		else
		{
			Serial.println(F("MQTT Semaphore released"));
		}

		if (xSemaphoreGive(sem_ble) != pdTRUE)
		{
			Serial.println(F("BLE Semaphore not released"));
		}
		else
		{
			Serial.println(F("BLE Semaphore released"));
		}
	}

	uint32_t last_millis = millis();
	while (1)
	{
		if (WiFi.status() != WL_CONNECTED)
		{
			if (!host_server)
				digitalWrite(RED_LED, HIGH);
			else
			{
				digitalWrite(GREEN_LED, HIGH);
				vTaskDelay(500);
				digitalWrite(GREEN_LED, LOW);
				vTaskDelay(500);
			}
				

			if (millis() - last_millis > WIFI_DISCONNECTED_THRESHOLD)
			{
				last_millis = millis();
				if (!host_server)
				{
					// restart
					ESP.restart();
				}
			}
		}
		else
		{
			last_millis = millis();
		}
	}
}
void Resettask(void *parameter)
{
	bool presentState;
	bool previousState = HIGH;
	int press_Time;
	// int release_Time;
	bool pressed = 0;

	while (1)
	{
		presentState = digitalRead(RESET_PIN);

		if (previousState == HIGH && presentState == LOW)
		{
			// Serial.println("Pressed");
			pressed = 1;
			previousState = LOW;
			press_Time = millis();
		}
		else if (presentState == HIGH)
		{
			if(pressed && (millis() - press_Time >= 1000))
			{
				ESP.restart();
			}
			// Serial.println("Unpressed");
			previousState = HIGH;
			pressed = 0;
		}
		

		if (pressed)
		{
			if (millis() - press_Time >= BUTTON_PRESSING_THRESHOLD)
			{
				// Serial.println("Crossed threshold");
				initSPIFFS();
				ssid = "";
				pass = "";
				writeFile(SPIFFS, ssidPath, ssid.c_str());
				writeFile(SPIFFS, passPath, pass.c_str());
				LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
				led->blinks = 5;
				led->pin = RED_LED;
				led->high_delay = 100;
				led->low_delay = 100;
				if (xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
				{
					printf("BLE Data NOT sent!");
				}
				free(led);
				vTaskDelay(1000);
				ESP.restart();
			}
		}
	}
}

void MqttTask(void *parameter)
{
	xSemaphoreTake(sem_mqtt, portMAX_DELAY);
	client.setServer(mqtt_server, 1883);
	client.setCallback(callback);
	

	uint32_t tic = millis();
	while (1)
	{
		if (!client.connected())
		{
			reconnect();
		}
		client.loop();
		
		if(millis() - tic > CONNECTION_STATUS_SEND_PERIOD)
		{
			tic = millis();
			char msg[50];
			sprintf(msg, "%s,connected", DEVICE_ID);
			if (client.publish(pub_topic, msg))
			{
				LED_Blink_t *led = (LED_Blink_t *)malloc(sizeof(LED_Blink_t));
				led->blinks = 1;
				led->pin = GREEN_LED;
				led->high_delay = 150;
				led->low_delay = 150;
				if (xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
				{
					printf("LED Data NOT sent!");
				}
				free(led);

				Serial.println(F("Published Connection Satatus"));
			}
			else
			{
				LED_Blink_t *led = (LED_Blink_t *)malloc(sizeof(LED_Blink_t));
				led->blinks = 1;
				led->pin = RED_LED;
				led->high_delay = 300;
				led->low_delay = 00;
				if (xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
				{
					printf("LED Data NOT sent!");
				}
				free(led);

				Serial.println(F("Published Connection S
				tatus"));
			}
		}
	}
}

void BLEScanTask(void *param)
{
	xSemaphoreTake(sem_ble, portMAX_DELAY);
	xSemaphoreGive(sem_data);
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan(); // create new scan
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true); // active scan uses more power, but get results faster
	pBLEScan->setInterval(100);
	pBLEScan->setWindow(99); // less or equal setInterval value

	while (1)
	{
		BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
		Serial.print("Devices found: ");
		Serial.println(foundDevices.getCount());
		Serial.println("Scan done!");
		pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
		// vTaskDelay(pdMS_TO_TICKS(1000));
	}
}


void RFTask(void *param)
{
	// xSemaphoreTake(, portMAX_DELAY);
	// xSemaphoreGive();
	// vw_set_rx_pin(CONFIG_RF_PIN); // Set the pin to be used for receiving data
	// vw_setup(2000);					// Initialize VirtualWire at 2000 bits per second
	// vw_rx_start();						// Start the receiver

	// while (1)
	// {
	// 	uint8_t buf[CONFIG_VW_MAX_MESSAGE_LEN]; // Create a buffer to store the received data
	// 	uint8_t buflen = CONFIG_VW_MAX_MESSAGE_LEN;
	// 	if (vw_get_message(buf, &buflen))
	// 	{ // Check if data is available
	// 		Serial.print("Received data: ");
	// 		for (int i = 0; i < buflen; i++)
	// 		{
	// 			Serial.print((char)buf[i]); // Print each byte of the received data
	// 		}
	// 		Serial.println();
	// 	}
	// }
}


void showBleData(BLE_Data_t *ble)
{
	if(ble != NULL)
	{
		Serial.print(F("Mac: "));
		Serial.println(ble->mac.toString().c_str());
		Serial.print(F("RSSI: "));
		Serial.println(ble->rssi);
		Serial.printf("Temperature: %.2f C\n", ble->temperature);
		Serial.printf("Battery: %d mV\n", ble->battery_voltage_mv);
	}
}

void BleDataTask(void* parameter)
{	
	xSemaphoreTake(sem_data, portMAX_DELAY);
	BLE_Data_t *ble = (BLE_Data_t *)malloc(sizeof(BLE_Data_t));
	while(1)
	{
		if(bleDataQueue != NULL)
		{
			if(xQueueReceive(bleDataQueue, ble, portMAX_DELAY) == pdPASS)
			{
				showBleData(ble);
				char mqtt_payload[MAX_PAYLOAD_SIZE];
				makeBleDataString(mqtt_payload, DEVICE_ID, ble);
				Serial.println(mqtt_payload);
				if(client.publish(pub_topic, mqtt_payload))
				{
					LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
					led->blinks = 2;
					led->pin = GREEN_LED;
					led->high_delay = 150;
					led->low_delay = 150;
					if (xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
					{
						printf("BLE Data NOT sent!");
					}
					free(led);

					Serial.println(F("Published BLE Data"));
				}
				else
				{
					LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
					led->blinks = 1;
					led->pin = RED_LED;
					led->high_delay = 300;
					led->low_delay = 0;
					if (xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
					{
						printf("BLE Data NOT sent!");
					}
					free(led);
					Serial.println(F("Error Publishing BLE Data"));
				}
			}
		}
	}
	free(ble);
}

void makeBleDataString(char* buff, char* device_id, BLE_Data_t* ble)
{
	strcpy(buff, device_id);
	strcat(buff, ",");
	strcat(buff, ble->mac.toString().c_str());
	strcat(buff, ",");
	char temp[10];
	sprintf(temp, "%.6f,", ble->temperature);
	strcat(buff, temp);
	char rssi[5];
	sprintf(rssi, "%d", ble->rssi);
	strcat(buff, rssi);
}

void BlinkLedTask(void* parameter)
{
	LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
	while(1)
	{
		if(ledBlinkQueue != NULL)
		{
			if(xQueueReceive(ledBlinkQueue, led, portMAX_DELAY) == pdPASS)
			{
				for(int i = 0; i < led->blinks; i++)
				{
					digitalWrite(led->pin, HIGH);
					vTaskDelay(pdMS_TO_TICKS(led->high_delay));
					digitalWrite(led->pin, LOW);
					vTaskDelay(pdMS_TO_TICKS(led->low_delay));
				}
			}
		}
	}
	free(led);
}

void data_interrupt_handler(void* arg)
{
	static unsigned int changeCount = 0;
	static unsigned long lastTime = 0;
	static unsigned int repeatCount = 0;

	const long time = esp_timer_get_time();
	const unsigned int duration = time - lastTime;


	if (duration > nSeparationLimit) {
	    // A long stretch without signal level change occurred. This could
	    // be the gap between two transmission.
	    if (diff(duration, timings[0]) < 200) {
	      // This long signal is close in length to the long signal which
	      // started the previously recorded timings; this suggests that
	      // it may indeed by a a gap between two transmissions (we assume
	      // here that a sender will send the signal multiple times,
	      // with roughly the same gap between them).
	      repeatCount++;
	      if (repeatCount == 2) {
	        for(uint8_t i = 1; i <= numProto; i++) {
	          if (receiveProtocol(i, changeCount)) {
	            // receive succeeded for protocol i
              uint8_t protocol_num = (uint8_t)i;
              xQueueSendFromISR(s_esp_RF433_queue, &protocol_num, NULL);
	            break;
	          }
	        }
	        repeatCount = 0;
	      }
	    }
	    changeCount = 0;
	  }
	  // detect overflow
	  if (changeCount >= RCSWITCH_MAX_CHANGES) {
	    changeCount = 0;
	    repeatCount = 0;
	  }

	  timings[changeCount++] = duration;
	  lastTime = time;
}



void receiver_rf433(void* pvParameter)
{
  uint8_t prot_num = 0;
  while(1)
  {
    if (xQueueReceive(s_esp_RF433_queue, &prot_num, portMAX_DELAY) == pdFALSE) {
      Serial.println(F("RF433 interrurpt fail"));
    }
    else {
  		Serial.printf("Received %lu / %dbit Protocol: %d.\n", getReceivedValue(), getReceivedBitlength(), prot_num);
			resetAvailable();
			
		}
  }
}