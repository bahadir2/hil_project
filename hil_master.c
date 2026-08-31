cat > /home/debian/hil_project/hil_master.c << 'EOF'
/*
 * HIL Master - BeagleBone Black
 * CAN Bus HIL Test Sistemi - Master Node
 *
 * Baglanti:
 *   P9_19 -> CAN0 RX -> SN65HVD230 R
 *   P9_20 -> CAN0 TX -> SN65HVD230 D
 *   P9_3  -> 3.3V    -> SN65HVD230 VCC
 *   P9_1  -> GND     -> SN65HVD230 GND
 *
 * Derleme:
 *   gcc -o hil_master hil_master.c
 *
 * Calistirma:
 *   sudo ./hil_master
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdint.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* CAN ID Tanimlari */
#define BBB_MASTER_ID       0x100   /* BBB -> ESP32 komut frame ID */
#define ESP32_SLAVE_ID      0x200   /* ESP32 -> BBB cevap frame ID */

/* Test Komutlari */
#define CMD_ENGINE_RPM      0x01    /* Motor RPM testi */
#define CMD_FUEL_INJECT     0x02    /* Yakit enjeksiyonu testi */
#define CMD_TEMP_SENSOR     0x03    /* Sicaklik sensoru testi */
#define CMD_HEARTBEAT       0x0F    /* Baglanti kontrol */

/* Beklenen Cevaplar */
#define RESP_OK             0xFF    /* Test basarili */
#define RESP_ERR            0xEE    /* Test basarisiz */

/* Log dosyasi */
#define LOG_FILE            "hil_log.txt"

/* Test sayaci */
static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

/* Log fonksiyonu */
void log_result(FILE *log, const char *msg) {
    time_t now = time(NULL);
    char timestr[20];
    strftime(timestr, sizeof(timestr), "%H:%M:%S", localtime(&now));
    printf("[%s] %s\n", timestr, msg);
    if (log) fprintf(log, "[%s] %s\n", timestr, msg);
}

/* CAN frame gonder */
int send_frame(int s, canid_t id, uint8_t cmd, uint16_t value) {
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.can_id  = id;
    frame.can_dlc = 8;
    frame.data[0] = cmd;
    frame.data[1] = (value >> 8) & 0xFF;   /* High byte */
    frame.data[2] = value & 0xFF;           /* Low byte */
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x00;

    int nbytes = write(s, &frame, sizeof(frame));
    return (nbytes == sizeof(frame)) ? 0 : -1;
}

/* CAN frame al (timeout ile) */
int recv_frame(int s, struct can_frame *frame, int timeout_ms) {
    fd_set rdfs;
    struct timeval tv;

    FD_ZERO(&rdfs);
    FD_SET(s, &rdfs);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(s + 1, &rdfs, NULL, NULL, &tv);
    if (ret <= 0) return -1;    /* Timeout veya hata */

    int nbytes = read(s, frame, sizeof(*frame));
    return (nbytes == sizeof(*frame)) ? 0 : -1;
}

/* Tek test senaryosu calistir */
int run_test(int s, FILE *log, uint8_t cmd, uint16_t value,
             const char *test_name) {
    char msg[128];
    struct can_frame rx_frame;

    test_count++;

    /* Komutu gonder */
    if (send_frame(s, BBB_MASTER_ID, cmd, value) < 0) {
        snprintf(msg, sizeof(msg),
                 "HATA: [%s] Frame gonderilemedi", test_name);
        log_result(log, msg);
        fail_count++;
        return -1;
    }

    snprintf(msg, sizeof(msg),
             "GONDERILDI: [%s] CMD=0x%02X VALUE=%d",
             test_name, cmd, value);
    log_result(log, msg);

    /* Cevap bekle (1000ms timeout) */
    if (recv_frame(s, &rx_frame, 1000) < 0) {
        snprintf(msg, sizeof(msg),
                 "TIMEOUT: [%s] ESP32 cevap vermedi", test_name);
        log_result(log, msg);
        fail_count++;
        return -1;
    }

    /* Cevabi kontrol et */
    if (rx_frame.can_id == ESP32_SLAVE_ID &&
        rx_frame.data[0] == RESP_OK) {
        snprintf(msg, sizeof(msg),
                 "BASARILI: [%s] ESP32 cevabi OK", test_name);
        log_result(log, msg);
        pass_count++;
        return 0;
    } else {
        snprintf(msg, sizeof(msg),
                 "BASARISIZ: [%s] Beklenen=0x%02X Gelen=0x%02X",
                 test_name, RESP_OK, rx_frame.data[0]);
        log_result(log, msg);
        fail_count++;
        return -1;
    }
}

int main(void) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    char msg[128];

    /* Log dosyasini ac */
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) printf("Uyari: Log dosyasi acilamadi\n");

    printf("===========================================\n");
    printf("  HIL Master - BeagleBone Black\n");
    printf("  CAN Bus HIL Test Sistemi v1.0\n");
    printf("===========================================\n\n");

    /* CAN soketi ac */
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("Socket hatasi");
        return EXIT_FAILURE;
    }

    /* CAN arayuzunu bayla */
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl hatasi - can0 aktif mi?");
        printf("\nCAN arayuzunu aktif etmek icin:\n");
        printf("  config-pin P9_19 can\n");
        printf("  config-pin P9_20 can\n");
        printf("  sudo ip link set can0 type can bitrate 500000\n");
        printf("  sudo ip link set up can0\n");
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind hatasi");
        return EXIT_FAILURE;
    }

    log_result(log, "HIL Master baslatildi - can0 @ 500kbps");

    /* Ana test dongusu */
    while (1) {
        printf("\n--- Test Turu %d ---\n", test_count + 1);

        /* Heartbeat testi */
        run_test(s, log, CMD_HEARTBEAT, 0x0000, "Heartbeat");
        sleep(1);

        /* Motor RPM testi - 3000 RPM */
        run_test(s, log, CMD_ENGINE_RPM, 3000, "Motor RPM 3000");
        sleep(1);

        /* Yakit enjeksiyonu testi */
        run_test(s, log, CMD_FUEL_INJECT, 0x001F, "Yakit Enjeksiyonu");
        sleep(1);

        /* Sicaklik sensoru testi - 85 derece */
        run_test(s, log, CMD_TEMP_SENSOR, 85, "Sicaklik 85C");
        sleep(1);

        /* Sonuc ozeti */
        snprintf(msg, sizeof(msg),
                 "OZET: Toplam=%d Basarili=%d Basarisiz=%d",
                 test_count, pass_count, fail_count);
        log_result(log, msg);

        sleep(2);   /* Tur arasi bekleme */
    }

    if (log) fclose(log);
    close(s);
    return EXIT_SUCCESS;
}
EOF
