#ifndef __SHWAPNO_BLE_TEMP_MAIN_H_
#define __SHWAPNO_BLE_TEMP_MAIN_H_

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>
#include "random_string.h"
#include "config.h"
#include "esp32_rf_receiver.h"


#define DEVICE_ID                                       CONFIG_DEVICE_ID

#define RED_LED                                         CONFIG_RED_LED_PIN
#define BLUE_LED                                        CONFIG_BLUE_LED_PIN
#define GREEN_LED                                       CONFIG_GREEN_LED_PIN
#define RESET_PIN                                       CONFIG_RESET_PIN
#define BUTTON_PRESSING_THRESHOLD                       CONFIG_BUTTON_PRESSING_THRESHOLD

#define MAX_SSID_SIZE									CONFIG_MAX_SSID_SIZE
#define MAX_PASS_SIZE									CONFIG_MAX_PASS_SIZE
#define WIFI_DISCONNECTED_THRESHOLD                     CONFIG_WIFI_DISCONNECTED_THRESHOLD
#define MAX_PAYLOAD_SIZE                                CONFIG_MAX_PAYLOAD_SIZE

#define DEFAULT_MQTT_BROKER								CONFIG_DEFAULT_MQTT_BROKER
#define DEFAULT_MQTT_PORT								CONFIG_DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PUB_TOPIC							CONFIG_DEFAULT_MQTT_PUB_TOPIC
#define DEFAULT_MQTT_SUB_TOPIC							CONFIG_DEFAULT_MQTT_SUB_TOPIC
#define MAX_MQTT_BROKER_SIZE							CONFIG_MAX_MQTT_BROKER_SIZE
#define MAX_MQTT_TOPIC_SIZE 							CONFIG_MAX_MQTT_TOPIC_SIZE
#define CONNECTION_STATUS_SEND_PERIOD		            CONFIG_CONNECTION_STATUS_SEND_PERIOD

#define MAX_BLE_QUEUE_SIZE                              CONFIG_MAX_BLE_QUEUE_SIZE
#define MAX_LED_BLINK_QUEUE_SIZE		                CONFIG_MAX_LED_BLINK_QUEUE_SIZE


#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))

static const Protocol proto[] = {
  { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1
  { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
  { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
  { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
  { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
  { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 6 (HT6P20B)
};

enum {
   numProto = sizeof(proto) / sizeof(proto[0])
};



volatile unsigned long nReceivedValue = 0;
volatile unsigned int nReceivedBitlength = 0;
volatile unsigned int nReceivedDelay = 0;
volatile unsigned int nReceivedProtocol = 0;
int nReceiveTolerance = 60;
const unsigned nSeparationLimit = 4300;
// separationLimit: minimum microseconds between received codes, closer codes are ignored.
// according to discussion on issue #14 it might be more suitable to set the separation
// limit to the same time as the 'low' part of the sync signal for the current protocol.

unsigned int timings[RCSWITCH_MAX_CHANGES];

/* helper function for the receiveProtocol method */
static inline unsigned int diff(int A, int B) {
  return abs(A - B);
}

bool receiveProtocol(const int p, unsigned int changeCount) {
    const Protocol pro = proto[p-1];

    unsigned long code = 0;
    //Assuming the longer pulse length is the pulse captured in timings[0]
    const unsigned int syncLengthInPulses =  ((pro.syncFactor.low) > (pro.syncFactor.high)) ? (pro.syncFactor.low) : (pro.syncFactor.high);
    const unsigned int delay = timings[0] / syncLengthInPulses;
    const unsigned int delayTolerance = delay * nReceiveTolerance / 100;

    /* For protocols that start low, the sync period looks like
     *               _________
     * _____________|         |XXXXXXXXXXXX|
     *
     * |--1st dur--|-2nd dur-|-Start data-|
     *
     * The 3rd saved duration starts the data.
     *
     * For protocols that start high, the sync period looks like
     *
     *  ______________
     * |              |____________|XXXXXXXXXXXXX|
     *
     * |-filtered out-|--1st dur--|--Start data--|
     *
     * The 2nd saved duration starts the data
     */
    const unsigned int firstDataTiming = (pro.invertedSignal) ? (2) : (1);

    for (unsigned int i = firstDataTiming; i < changeCount - 1; i += 2) {
        code <<= 1;
        if (diff(timings[i], delay * pro.zero.high) < delayTolerance &&
            diff(timings[i + 1], delay * pro.zero.low) < delayTolerance) {
            // zero
        } else if (diff(timings[i], delay * pro.one.high) < delayTolerance &&
                   diff(timings[i + 1], delay * pro.one.low) < delayTolerance) {
            // one
            code |= 1;
        } else {
            // Failed
            return false;
        }
    }

    if (changeCount > 7) {    // ignore very short transmissions: no device sends them, so this must be noise
        nReceivedValue = code;
        nReceivedBitlength = (changeCount - 1) / 2;
        nReceivedDelay = delay;
        nReceivedProtocol = p;
        return true;
    }

    return false;
}

// -- Wrappers over variables which should be modified only internally

bool available() {
	return nReceivedValue != 0;
}

void resetAvailable() {
	nReceivedValue = 0;
}

unsigned long getReceivedValue() {
	return nReceivedValue;
}

unsigned int getReceivedBitlength() {
	return nReceivedBitlength;
}

unsigned int getReceivedDelay() {
	return nReceivedDelay;
}

unsigned int getReceivedProtocol() {
	return nReceivedProtocol;
}

unsigned int* getReceivedRawdata() {
  return timings;
}

void data_interrupt_handler(void* arg);
void receiver_rf433(void* pvParameter);


typedef struct LED_Blink_t
{
	int pin;
	int blinks;
	int high_delay;
	int low_delay;
}LED_Blink_t;


typedef struct BLE_Data_t
{
	BLEAddress mac;
	int rssi;
	float temperature;	
	uint16_t battery_voltage_mv;
}BLE_Data_t;


WiFiClient espClient;
PubSubClient client(espClient);


AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";
const char *PARAM_INPUT_3 = "ip";
const char *PARAM_INPUT_4 = "gateway";


// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char *ssidPath = "/ssid.txt";
const char *passPath = "/pass.txt";
const char *ipPath = "/ip.txt";
const char *gatewayPath = "/gateway.txt";

IPAddress localIP;
// IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
// IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 255, 255);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)

int scanTime = 5; // In seconds
BLEScan *pBLEScan;

char mqtt_server[MAX_MQTT_BROKER_SIZE];
char pub_topic[MAX_MQTT_TOPIC_SIZE];

xTaskHandle wifi_task_handle;						/*!< */
xTaskHandle reset_task_handle;
xTaskHandle ble_task_handle;
xTaskHandle ble_data_task_handle;
xTaskHandle led_blink_task_handle;
xTaskHandle rf_task_handle;

QueueHandle_t bleDataQueue;
QueueHandle_t ledBlinkQueue;
QueueHandle_t s_esp_RF433_queue;
SemaphoreHandle_t sem_mqtt;
SemaphoreHandle_t sem_ble;
SemaphoreHandle_t sem_data;


/**
 * @brief 
 * 
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		if(!advertisedDevice.haveServiceUUID())
		{
			return;
		}

		uint8_t *payLoad = advertisedDevice.getPayload();
		// uint16_t payload_length = advertisedDevice.getPayloadLength();

		BLEUUID checkUrlUUID = (uint16_t)0xfeaa;
		if (advertisedDevice.getServiceUUID().equals(checkUrlUUID) && payLoad[11] == 0x20 && payLoad[15] != 0x80)
		{
			Serial.println(F("Found an EddystoneTLM beacon!"));
			BLEEddystoneTLM foundEddyURL = BLEEddystoneTLM();
			std::string eddyContent((char *)&payLoad[11]); // incomplete EddystoneURL struct!

			eddyContent = "01234567890123";

			for (int idx = 0; idx < 14; idx++)
			{
				eddyContent[idx] = payLoad[idx + 11];
			}

			BLE_Data_t *ble = (BLE_Data_t *)malloc(sizeof(BLE_Data_t));
			ble->mac = advertisedDevice.getAddress();
			ble->rssi = advertisedDevice.getRSSI();
			ble->battery_voltage_mv = foundEddyURL.getVolt();
			// BIG ENDIAN payload in signed 8.8 fixed-point notation. Unit is Celsius.
			int16_t temp_payload = payLoad[16] + (payLoad[15] << 8);
			ble->temperature = temp_payload / 256.0f;
			// Serial.printf("payload[16]:%d, payload[15]:%d, payload[15]<<8:%d, temp:%d, calc:%f\n", payLoad[16], payLoad[15], payLoad[15] << 8, temp_payload, ble->temperature);
			if(xQueueSend(bleDataQueue, ble, 0) != pdPASS)
			{
				printf("BLE Data NOT sent!");
			}
			// showBleData(ble);

			free(ble);

			LED_Blink_t* led = (LED_Blink_t*)malloc(sizeof(LED_Blink_t));
			led->blinks = 2;
			led->pin = BLUE_LED;
			led->high_delay = 150;
			led->low_delay = 150;
			if(xQueueSend(ledBlinkQueue, led, 0) != pdPASS)
			{
				printf("BLE Data NOT sent!");
			}
			free(led);
		}
		
	}
};


/**
 * @brief Serial print the BLE Data
 * 
 * @param ble : BLE_Data_t object pointer
 */
void showBleData(BLE_Data_t *ble);

/**
 * @brief make a string with the received data from the beacons, 
 * the string written at the buff variable consists of the mac
 *  address, temperature and rssi value of the beacon
 * 
 * @param buff : character pointer where the ble data string will be written at
 * @param device_id :  unique address of the gateway
 * @param ble : BLE_Data_t object pointer (a structure with mac, rssi, temperature, battery_voltage_mv variables)
 * 
 * @note 
 */
void makeBleDataString(char* buff, char* device_id, BLE_Data_t* ble);

/**
 * @brief WiFitask the task that handles all the wifi related processes. 
 * at first, it tries to read the saved wifi credentials data from the file
 * if the wifi fails to initialize with the credentials, esp will host a webserver
 * else it send the ble data to a queue.
 * 
 * @param parameter 
 */
void WiFitask(void *parameter);

/**
 * @brief Resettask if the button pressing is longer than the threshold then 
 * the written credentials will be erased and esp will restart.
 * 
 * @param parameter 
 */
void Resettask(void *parameter);

/**
 * @brief MqttTask connects with mqtt and publishes message every 30s.
 * After every successfull msg delivery the green LED blinks
 * 
 * @param parameter 
 */
void MqttTask(void *parameter);

/**
 * @brief BLEScanTask scans for BLE devices in an interval of 0.1s. 
 * 
 * @param param 
 */

void BLEScanTask(void *param);

/**
 * @brief BleDataTask  enqueues the data to publish and makes the esp blink
 *  in accordance with the successful delivery of the data to mqtt 
 * also a semaphore task, gets released when sem_data is released 
 * @param parameter 
 */
void BleDataTask(void* parameter);

/**
 * @brief BlinkLedTask the task to make the led blink 
 * 
 * @param parameter 
 */
void BlinkLedTask(void* parameter);



/**
 * @brief RFTask will process the RF 433 data receive task
 * 
 * @param parameter 
 */

void RFTask(void* parameter);

/**
 * @brief callback handles incoming message from mqtt
 * 
 * @param topic mqtt topic name
 * @param payload the received message
 * @param length messages length
 */

void callback(char *topic, byte *payload, unsigned int length);\

/**
 * @brief reconnect tries to reconnect with mqtt if the connection fails
 * 
 */
void reconnect();

/**
 * @brief initSPIFFS mounts the SPI flash file system in
 * 
 */
void initSPIFFS();

/**
 * @brief readCredentialsFromSPIFFS read the ssid, pass, ip, gateway
 * from the SPI flash file system
 * 
 * 
 */
void readCredentialsFromSPIFFS();

/**
 * @brief readFile reads the data from the desired file
 * 
 * @param fs pointer to the file
 * @param path path to the file
 * @return String 
 */
String readFile(fs::FS &fs, const char *path);


/**
 * @brief writeFile writes message to the file
 * 
 * @param fs pointer to the file
 * @param path path to the file
 * @param message the message to write in the file
 */
void writeFile(fs::FS &fs, const char *path, const char *message);


/**
 * @brief initWiFi initialize WiFi with the available credentials in the file system
 * 
 * @return true 
 * @return false 
 */
bool initWiFi();

/**
 * @brief serverhost hosts a webserver with Access point to collect to wifi credentials
 * 
 */
void serverhost();



#endif // !__SHWAPNO_BLE_TEMP_MAIN_H_