#ifndef __SHWAPNO_BLE_TEMP_CONFIG_H_
#define __SHWAPNO_BLE_TEMP_CONFIG_H_

#define CONFIG_DEVICE_ID                                "2022002303060000"

#define CONFIG_RED_LED_PIN                              5
#define CONFIG_BLUE_LED_PIN                             18
#define CONFIG_GREEN_LED_PIN                            19
#define CONFIG_RESET_PIN                                26
#define CONFIG_BUTTON_PRESSING_THRESHOLD                5000
#define CONFIG_RF_PIN                                23


#define CONFIG_MAX_SSID_SIZE							50
#define CONFIG_MAX_PASS_SIZE							50
#define CONFIG_WIFI_DISCONNECTED_THRESHOLD              30000
#define CONFIG_MAX_PAYLOAD_SIZE                         50
#define CONFIG_VW_MAX_MESSAGE_LEN                     50


#define CONFIG_DEFAULT_MQTT_BROKER						"broker.hivemq.com"
#define CONFIG_DEFAULT_MQTT_PORT						1883
#define CONFIG_DEFAULT_MQTT_PUB_TOPIC					"DMA/BLE_Temperature_filtered"
#define CONFIG_DEFAULT_MQTT_SUB_TOPIC					"DMA/BLE/Temperature/2022002303060000"
#define CONFIG_MAX_MQTT_BROKER_SIZE						50	
#define CONFIG_MAX_MQTT_TOPIC_SIZE 						50	
#define CONFIG_CONNECTION_STATUS_SEND_PERIOD		    30000

#define CONFIG_MAX_BLE_QUEUE_SIZE                       100
#define CONFIG_MAX_LED_BLINK_QUEUE_SIZE		            100

#endif // !__SHWAPNO_BLE_TEMP_CONFIG_H_