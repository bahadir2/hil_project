/*
 * HIL Slave - ESP32
 * CAN Bus HIL Test Sistemi - Slave Node (ECU Simulasyonu)
 *
 * Baglanti:
 *   GPIO4  -> TX -> SN65HVD230 D (data in)
 *   GPIO5  -> RX -> SN65HVD230 R (receive out)
 *   3.3V   ->      SN65HVD230 VCC
 *   GND    ->      SN65HVD230 GND
 *
 * CAN-H / CAN-L -> BBB SN65HVD230 ile ayni hatta
 * 120 ohm terminasyon her iki uca
 *
 * Derleme: PlatformIO ile Upload
 */

#include <Arduino.h>
#include "driver/twai.h"

/* CAN Pin Tanimlari */
#define TX_PIN  GPIO_NUM_4
#define RX_PIN  GPIO_NUM_5

/* CAN ID Tanimlari */
#define BBB_MASTER_ID   0x100   /* BBB'den gelen komut ID */
#define ESP32_SLAVE_ID  0x200   /* ESP32'den giden cevap ID */

/* Test Komutlari - BBB ile ayni olmali */
#define CMD_ENGINE_RPM      0x01
#define CMD_FUEL_INJECT     0x02
#define CMD_TEMP_SENSOR     0x03
#define CMD_HEARTBEAT       0x0F

/* Cevap Kodlari */
#define RESP_OK             0xFF
#define RESP_ERR            0xEE

/* LED (dahili) */
#define LED_PIN             2

/* Sayaclar */
static uint32_t rx_count = 0;
static uint32_t tx_count = 0;
static uint32_t err_count = 0;

/* CAN cevap gonder */
esp_err_t send_response(uint8_t status, uint8_t cmd, uint16_t value) {
    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(tx_msg));

    tx_msg.identifier       = ESP32_SLAVE_ID;
    tx_msg.data_length_code = 8;
    tx_msg.data[0]          = status;   /* RESP_OK veya RESP_ERR */
    tx_msg.data[1]          = cmd;      /* Hangi komuta cevap */
    tx_msg.data[2]          = (value >> 8) & 0xFF;
    tx_msg.data[3]          = value & 0xFF;
    tx_msg.data[4]          = 0x00;
    tx_msg.data[5]          = 0x00;
    tx_msg.data[6]          = 0x00;
    tx_msg.data[7]          = 0x00;

    esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        tx_count++;
        Serial.printf("[TX] Cevap gonderildi: STATUS=0x%02X CMD=0x%02X\n",
                      status, cmd);
    } else {
        err_count++;
        Serial.printf("[ERR] Gonderme hatasi: %s\n", esp_err_to_name(ret));
    }
    return ret;
}

/* Komut isle */
void process_command(uint8_t cmd, uint16_t value) {
    switch (cmd) {

        case CMD_HEARTBEAT:
            Serial.println("[CMD] Heartbeat alindi");
            send_response(RESP_OK, cmd, 0x0000);
            break;

        case CMD_ENGINE_RPM:
            Serial.printf("[CMD] Motor RPM komutu: %d RPM\n", value);
            /* Gercek sistemde: motor kontrolcusunu simule et */
            /* Simdilik her degeri kabul et */
            if (value > 0 && value <= 8000) {
                send_response(RESP_OK, cmd, value);
            } else {
                Serial.println("[WARN] Gecersiz RPM degeri");
                send_response(RESP_ERR, cmd, value);
            }
            break;

        case CMD_FUEL_INJECT:
            Serial.printf("[CMD] Yakit enjeksiyonu komutu: 0x%04X\n", value);
            send_response(RESP_OK, cmd, value);
            break;

        case CMD_TEMP_SENSOR:
            Serial.printf("[CMD] Sicaklik komutu: %d C\n", value);
            /* -40 ile 150 derece arasi gecerli */
            if (value <= 150) {
                send_response(RESP_OK, cmd, value);
            } else {
                Serial.println("[WARN] Gecersiz sicaklik degeri");
                send_response(RESP_ERR, cmd, value);
            }
            break;

        default:
            Serial.printf("[WARN] Bilinmeyen komut: 0x%02X\n", cmd);
            send_response(RESP_ERR, cmd, 0x0000);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("===========================================");
    Serial.println("  HIL Slave - ESP32");
    Serial.println("  CAN Bus HIL Test Sistemi v1.0");
    Serial.println("===========================================");

    /* TWAI konfigurasyonu */
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);

    twai_timing_config_t t_config =
        TWAI_TIMING_CONFIG_500KBITS();  /* BBB ile ayni! */

    twai_filter_config_t f_config =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();

    /* TWAI yukle */
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[ERR] TWAI kurulamadi!");
        return;
    }

    /* TWAI baslat */
    if (twai_start() != ESP_OK) {
        Serial.println("[ERR] TWAI baslatılamadi!");
        return;
    }

    Serial.println("[OK] TWAI baslatildi - 500kbps");
    Serial.println("[OK] BBB'den komut bekleniyor...\n");
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    twai_message_t rx_msg;

    /* BBB'den mesaj bekle (100ms timeout) */
    if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {

        /* Sadece BBB master ID kabul et */
        if (rx_msg.identifier == BBB_MASTER_ID) {
            rx_count++;

            uint8_t  cmd   = rx_msg.data[0];
            uint16_t value = (rx_msg.data[1] << 8) | rx_msg.data[2];

            Serial.printf("\n[RX] Frame alindi: ID=0x%03X CMD=0x%02X VALUE=%d\n",
                          rx_msg.identifier, cmd, value);

            /* LED yak - mesaj alindi */
            digitalWrite(LED_PIN, LOW);
            delay(50);
            digitalWrite(LED_PIN, HIGH);

            /* Komutu isle ve cevap ver */
            process_command(cmd, value);

        } else {
            Serial.printf("[INFO] Baska ID: 0x%03X (ignored)\n",
                          rx_msg.identifier);
        }
    }

    /* Her 10 saniyede istatistik yazdir */
    static uint32_t last_stat = 0;
    if (millis() - last_stat > 10000) {
        Serial.printf("\n[STAT] RX=%d TX=%d ERR=%d\n",
                      rx_count, tx_count, err_count);
        last_stat = millis();
    }
}