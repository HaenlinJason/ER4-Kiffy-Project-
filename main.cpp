/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Sam Danbury - initial implementation
 *    Ian Craggs - refactoring to remove STL and other changes
 *    Sam Grove  - added check for Ethernet cable.
 *    Chris Styles - Added additional menu screen for software revision
 *    James Sutton - Mac fix and extra debug
 *    Ian Craggs - add not authorized messages
 *
 * To do :
 *    Add magnetometer sensor output to IoT data stream
 *
 ******************************************************************************/

#include "MQTTClient.h"
#include "MQTTEthernet.h"
#include "MFRC522.h"
#include "rtos.h"
#include "GPS.h"
#include "th02.h"

//Select the right device
//The first device enable to use a database on my bluemix account
//The second device enable the user to use a other database managing by 
//the professor

//#define DEVICE1
#define DEVICE2

// Update this to the next number *before* a commit
#define __APP_SW_REVISION__ "18"

// Configuration values needed to connect to IBM IoT Cloud
#ifdef DEVICE1
#define ORG "yltc20"             // For a registered connection, replace with your org
#define ID  "LampadaireNice01"              // For a registered connection, replace with your id
#define AUTH_TOKEN "*me82k!7+5ZVWx8K(G"                // For a registered connection, replace with your auth-token
#define TYPE "Lampadaire"      // For a registered connection, replace with your type
#define DEVICE_NAME "Mbed"     // Replace with your own name
#endif //DEVICE1

// Configuration values needed to connect to IBM IoT Cloud
#ifdef DEVICE2
#define ORG "j0l8vo"             // For a registered connection, replace with your org
#define ID  "03kiffy04"              // For a registered connection, replace with your id
#define AUTH_TOKEN "03kiffy04"   // For a registered connection, replace with your auth-token
#define TYPE "kiffy"      // For a registered connection, replace with your type
#define DEVICE_NAME "Mbed"     // Replace with your own name
#endif //DEVICE2

//LPC1768 Pins for MFRC522 SPI interface
#define SPI_MOSI    p5
#define SPI_MISO    p6
#define SPI_SCLK    p7
#define SPI_CS      p21
//
#define MF_RESET    p8
#define ECH 10

//temp & hum
#define RX p28
#define TX p27
#define ADDRESS 0x80

//Air Quality
#define PIN p16

#define MQTT_PORT 1883
#define MQTT_TLS_PORT 8883
#define IBM_IOT_PORT MQTT_PORT

#define MQTT_MAX_PACKET_SIZE 250

#if defined(TARGET_UBLOX_C027)
#warning "Compiling for mbed C027"
#include "C027.h"
#elif defined(TARGET_LPC1768)
#warning "Compiling for mbed LPC1768"
#include "LPC1768.h"
#elif defined(TARGET_K64F)
#warning "Compiling for mbed K64F"
#include "K64F.h"
#endif

MFRC522    RfChip   (SPI_MOSI, SPI_MISO, SPI_SCLK, SPI_CS, MF_RESET);
TH02 MyTH02 (RX,TX,ADDRESS);// connect hsensor on RX2 TX2
AnalogIn sensorUV(p15);
AnalogIn sensorPol(p16);
GPS gps(p9, p10);

Ticker tRFID;
Ticker tUV;
Ticker tGas;

//AirQuality airqualitysensor;
int pollution = -1;
int gicptuv=0,gicptpol=0;
int gitabuv[10],gitabpol[10];

char rfidUid[10]= {0};

float gUv;
bool quickstartMode = false;
char org[11] = ORG;
char type[30] = TYPE;
char id[30] = ID;                 // mac without colons
char auth_token[30] = AUTH_TOKEN; // Auth_token is only used in non-quickstart mode

bool connected = false;
bool mqttConnecting = false;
bool netConnected = false;
bool netConnecting = false;
bool ethernetInitialising = true;
int connack_rc = 0; // MQTT connack return code
int retryAttempt = 0;
int menuItem = 0;

char* joystickPos = "CENTRE";
int blink_interval = 0;

char* ip_addr = "";
char* gateway_addr = "";
char* host_addr = "";
int connectTimeout = 1000;
int iTemp,iTime,iTempbrute,iRH,iRHbrute;

// If we wanted to manually set the MAC address,
// this is how to do it. In this example, we take
// the original Mbed Set MAC address and combine it
// with a prefix of our choosing.
/*
extern "C" void $Super$$mbed_mac_address(char *s);
extern "C" void $Sub$$mbed_mac_address(char *s)
{
   char originalMAC[6] = "";
   $Super$$mbed_mac_address(originalMAC);

   char mac[6];
   mac[0] = 0x00;
   mac[1] = 0x08;
   mac[2] = 0xdc;
   mac[3] = originalMAC[3];
   mac[4] = originalMAC[4];
   mac[5] = originalMAC[5];
   memcpy(s, mac, 6);
}*/

int connect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    const char* iot_ibm = ".messaging.internetofthings.ibmcloud.com";

    char hostname[strlen(org) + strlen(iot_ibm) + 1];
    sprintf(hostname, "%s%s", org, iot_ibm);
    EthernetInterface& eth = ipstack->getEth();
    ip_addr = eth.getIPAddress();
    gateway_addr = eth.getGateway();

    // Construct clientId - d:org:type:id
    char clientId[strlen(org) + strlen(type) + strlen(id) + 5];
    sprintf(clientId, "d:%s:%s:%s", org, type, id);

    // Network debug statements
    LOG("=====================================\n\r");
    LOG("Connecting Ethernet.\n\r");
    LOG("IP ADDRESS: %s\n\r", eth.getIPAddress());
    LOG("MAC ADDRESS: %s\n\r", eth.getMACAddress());
    LOG("Gateway: %s\n\r", eth.getGateway());
    LOG("Network Mask: %s\n\r", eth.getNetworkMask());
    LOG("Server Hostname: %s\n\r", hostname);
    LOG("Client ID: %s\n\r", clientId);
    LOG("=====================================\n\r");

    netConnecting = true;
    int rc = ipstack->connect(hostname, IBM_IOT_PORT, connectTimeout);
    if (rc != 0) {
        WARN("IP Stack connect returned: %d\n\r", rc);
        return rc;
    }
    netConnected = true;
    netConnecting = false;

    // MQTT Connect
    mqttConnecting = true;
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = clientId;

    if (!quickstartMode) {
        data.username.cstring = "use-token-auth";
        data.password.cstring = auth_token;
    }

    if ((rc = client->connect(data)) == 0) {
        connected = true;
    } else
        WARN("MQTT connect returned %d\n\r", rc);
    if (rc >= 0)
        connack_rc = rc;
    mqttConnecting = false;
    return rc;
}


int getConnTimeout(int attemptNumber)
{
    // First 10 attempts try within 3 seconds, next 10 attempts retry after every 1 minute
    // after 20 attempts, retry every 10 minutes
    return (attemptNumber < 10) ? 3 : (attemptNumber < 20) ? 60 : 600;
}


void attemptConnect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    connected = false;

    // make sure a cable is connected before starting to connect
    while (!linkStatus()) {
        wait(1.0f);
        WARN("Ethernet link not present. Check cable connection\n\r");
    }

    while (connect(client, ipstack) != MQTT_CONNECTION_ACCEPTED) {
        if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD)
            return; // don't reattempt to connect if credentials are wrong


        int timeout = getConnTimeout(++retryAttempt);
        WARN("Retry attempt number %d waiting %d\n\r", retryAttempt, timeout);

        // if ipstack and client were on the heap we could deconstruct and goto a label where they are constructed
        //  or maybe just add the proper members to do this disconnect and call attemptConnect(...)

        // this works - reset the system when the retry count gets to a threshold
        if (retryAttempt == 5)
            NVIC_SystemReset();
        else
            wait(timeout);
    }
}


int publish(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message;
    char* pubTopic = "iot-2/evt/status/fmt/json";

    char buf[250];
    sprintf(buf,
            "{\"d\":{\"kiffy_id\":\"03kiffy04\",\"status\":\"libre\",\"badge_id\":\"%s\",\"battchg\":0.50,\"loc\":{\"long\":%f,\"lat\":%f,\"alt\":25.1},\"temp\":%2.1f,\"hum\":%2.1f,\"UV\":%2.1f,\"gaz\":123.4,\"pol\":%d,\"lum\":0.80}}"
            ,rfidUid,gps.longitude,gps.latitude,iTemp/100.0f,iRH/100.0f,gUv,pollution);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf);

    LOG("Publishing %s\n", buf);
    return client->publish(pubTopic, message);
}


char* getMac(EthernetInterface& eth, char* buf, int buflen)    // Obtain MAC address
{
    strncpy(buf, eth.getMACAddress(), buflen);

    char* pos;                                                 // Remove colons from mac address
    while ((pos = strchr(buf, ':')) != NULL)
        memmove(pos, pos + 1, strlen(pos) + 1);
    return buf;
}


void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    char topic[md.topicName.lenstring.len + 1];

    sprintf(topic, "%.*s", md.topicName.lenstring.len, md.topicName.lenstring.data);

    LOG("Message arrived on topic %s: %.*s\n\r",  topic, message.payloadlen, message.payload);

    // Command topic: iot-2/cmd/blink/fmt/json - cmd is the string between cmd/ and /fmt/
    char* start = strstr(topic, "/cmd/") + 5;
    int len = strstr(topic, "/fmt/") - start;

    if (memcmp(start, "blink", len) == 0) {
        char payload[message.payloadlen + 1];
        sprintf(payload, "%.*s", message.payloadlen, (char*)message.payload);

        char* pos = strchr(payload, '}');
        if (pos != NULL) {
            *pos = '\0';
            if ((pos = strchr(payload, ':')) != NULL) {
                int blink_rate = atoi(pos + 1);
                blink_interval = (blink_rate <= 0) ? 0 : (blink_rate > 50 ? 1 : 50/blink_rate);
            }
        }
    } else
        WARN("Unsupported command: %.*s\n\r", len, start);
}
void GetRFID (void)
{
    // Look for new cards
    if (RfChip.PICC_IsNewCardPresent()) {
        // Select one of the cards
        if (RfChip.PICC_ReadCardSerial()) {
            for (uint8_t i = 0; i < RfChip.uid.size; i++) {
                sprintf((rfidUid+(i*2)),"%02X", RfChip.uid.uidByte[i]);
            }

        }
    }
}
void GetGPS(void const *args)
{
    while(true) {
        gps.sample();
        Thread::wait(100);
    }
}
void GetUV (void)
{
    gitabuv[gicptuv]=(((sensorUV.read()*5*1000)/4.3)-83)/21;
    if(gicptuv>=ECH) {
        gicptuv=0;
        int tmp;
        for(int i=0; i<ECH; i++ ) {
            tmp=tmp+gitabuv[i];
        }
        gUv = tmp/ECH;
        if(gUv<0) gUv = 0;
    }
    gicptuv++;
}
void GetTemp(void const *args)
{
    while(true) {
        MyTH02.startTempConv(true,true);
        iTime= MyTH02.waitEndConversion();// wait until conversion  is done
        iTempbrute= MyTH02.getConversionValue();
        iTemp=MyTH02.getLastRawTemp();
        Thread::wait(100);
    }
}
void GetRh (void const *args)
{
    while(true) {
        MyTH02.startRHConv(true,true);
        iTime= MyTH02.waitEndConversion();// wait until conversion  is done
        iRHbrute= MyTH02.getConversionValue();
        iRH=MyTH02.getLastRawRH();
        Thread::wait(100);
    }
}
void GetPol (void)
{
    gitabpol[gicptpol]=sensorPol.read()*1000*3.3;
    if(gicptpol>=ECH) {
        gicptpol=0;
        int tmp;
        for(int i=0; i<ECH; i++ ) {
            tmp=tmp+gitabpol[i];
        }
        pollution = tmp/ECH;
    }
    gicptpol++;
}
int main()
{
    quickstartMode = (strcmp(org, "quickstart") == 0);

    //lcd.set_font((unsigned char*) Arial12x12);  // Set a nice font for the LCD screen

    led2 = LED2_OFF; // K64F: turn off the main board LED


    LOG("***** IBM IoT Client Ethernet Example *****\n\r");
    MQTTEthernet ipstack;
    ethernetInitialising = false;
    MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE> client(ipstack);
    LOG("Ethernet Initialized\n\r");

    if (quickstartMode)
        getMac(ipstack.getEth(), id, sizeof(id));

    attemptConnect(&client, &ipstack);

    if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD) {
        while (true)
            wait(1.0); // Permanent failures - don't retry
    }

    if (!quickstartMode) {
        int rc = 0;
        if ((rc = client.subscribe("iot-2/cmd/+/fmt/json", MQTT::QOS1, messageArrived)) != 0)
            WARN("rc from MQTT subscribe is %d\n\r", rc);
    }

    blink_interval = 10;
    int count = 5;
    RfChip.PCD_Init();
    tRFID.attach(&GetRFID,1);
    tUV.attach(&GetUV,0.1);
    tGas.attach(&GetPol,0.1);
    Thread thread(GetGPS);
    Thread thread2(GetTemp);
    Thread thread3(GetRh);
    while (true) {
        if (++count == 1000) { // Here is the count to change the number of publish/second
            // Publish a message every second
            if (publish(&client, &ipstack) != 0)
                attemptConnect(&client, &ipstack);   // if we have lost the connection
            count = 0;
        }

        if (blink_interval == 0)
            led2 = LED2_OFF;
        else if (count % blink_interval == 0)
            led2 = 1;
        client.yield(10);  // allow the MQTT client to receive messages
    }
}
