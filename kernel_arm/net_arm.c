// --- 共通化: IPテーブル管理API雛形 ---
#define IP_TABLE_SIZE 32
typedef struct {
    uint32_t ip_addr;
    int      valid;
} ip_entry_t;
static ip_entry_t ip_table[IP_TABLE_SIZE];

int ip_table_add(uint32_t ip_addr) {
    for (int i = 0; i < IP_TABLE_SIZE; ++i) {
        if (!ip_table[i].valid) {
            ip_table[i].ip_addr = ip_addr;
            ip_table[i].valid = 1;
            return 0;
        }
    }
    return -1;
}

int ip_table_lookup(uint32_t ip_addr) {
    for (int i = 0; i < IP_TABLE_SIZE; ++i) {
        if (ip_table[i].valid && ip_table[i].ip_addr == ip_addr) {
            return 0;
        }
    }
    return -1;
}

// --- 共通化: イーサネットフレーム生成・パース ---
int ethernet_build_frame(ethernet_frame_t *frame, const uint8_t dst_mac[6], const uint8_t src_mac[6], uint16_t ethertype, const uint8_t *payload, uint16_t len) {
    if (!frame || !dst_mac || !src_mac || !payload || len > ETH_DATA_LEN) return -1;
    for (int i = 0; i < ETH_ALEN; ++i) {
        frame->header.h_dest[i] = dst_mac[i];
        frame->header.h_source[i] = src_mac[i];
    }
    frame->header.h_proto = ethertype;
    for (int i = 0; i < len; ++i) frame->payload[i] = payload[i];
    return 0;
}

int ethernet_parse_frame(const ethernet_frame_t *frame, uint8_t dst_mac[6], uint8_t src_mac[6], uint16_t *ethertype, uint8_t *payload, uint16_t *len) {
    if (!frame || !dst_mac || !src_mac || !ethertype || !payload || !len) return -1;
    for (int i = 0; i < ETH_ALEN; ++i) {
        dst_mac[i] = frame->header.h_dest[i];
        src_mac[i] = frame->header.h_source[i];
    }
    *ethertype = frame->header.h_proto;
    *len = ETH_DATA_LEN; // 仮
    for (int i = 0; i < *len; ++i) payload[i] = frame->payload[i];
    return 0;
}
// --- 共通化: TCPパケット生成・パース ---
int tcp_build_packet(tcp_packet_t *pkt, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len) {
    if (!pkt || !data || len > ETH_DATA_LEN) return -1;
    pkt->header.src_port = src_port;
    pkt->header.dst_port = dst_port;
    // 他ヘッダ項目は必要に応じて設定
    for (int i = 0; i < len; ++i) pkt->payload[i] = data[i];
    return 0;
}

int tcp_parse_packet(const tcp_packet_t *pkt, uint16_t *src_port, uint16_t *dst_port, uint8_t *data, uint16_t *len) {
    if (!pkt || !src_port || !dst_port || !data || !len) return -1;
    *src_port = pkt->header.src_port;
    *dst_port = pkt->header.dst_port;
    // 他ヘッダ項目は必要に応じて取得
    *len = ETH_DATA_LEN; // 仮
    for (int i = 0; i < *len; ++i) data[i] = pkt->payload[i];
    return 0;
}

// --- 共通化: ARPテーブル管理API雛形 ---
int arp_table_add(uint32_t ip_addr, const uint8_t mac_addr[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (!arp_table[i].valid) {
            arp_table[i].ip_addr = ip_addr;
            for (int j = 0; j < 6; ++j) arp_table[i].mac_addr[j] = mac_addr[j];
            arp_table[i].valid = 1;
            return 0;
        }
    }
    return -1;
}

int arp_table_lookup(uint32_t ip_addr, uint8_t mac_addr[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (arp_table[i].valid && arp_table[i].ip_addr == ip_addr) {
            for (int j = 0; j < 6; ++j) mac_addr[j] = arp_table[i].mac_addr[j];
            return 0;
        }
    }
    return -1;
}
// --- 共通化: UDPパケット生成・パース・チェックサム計算 ---
// UDPパケット生成
int udp_build_packet(udp_packet_t *pkt, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len) {
    if (!pkt || !data || len > ETH_DATA_LEN) return -1;
    pkt->header.src_port = src_port;
    pkt->header.dst_port = dst_port;
    pkt->header.length = sizeof(struct udphdr_arm) + len;
    pkt->header.checksum = 0; // 仮: チェックサム未計算
    for (int i = 0; i < len; ++i) pkt->payload[i] = data[i];
    return 0;
}

// UDPパケットパース
int udp_parse_packet(const udp_packet_t *pkt, uint16_t *src_port, uint16_t *dst_port, uint8_t *data, uint16_t *len) {
    if (!pkt || !src_port || !dst_port || !data || !len) return -1;
    *src_port = pkt->header.src_port;
    *dst_port = pkt->header.dst_port;
    *len = pkt->header.length - sizeof(struct udphdr_arm);
    for (int i = 0; i < *len; ++i) data[i] = pkt->payload[i];
    return 0;
}

// 簡易チェックサム計算（16bit和の1の補数）
uint16_t calc_checksum(const uint8_t *data, uint16_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; i += 2) {
        uint16_t word = data[i];
        if (i + 1 < len) word = (word << 8) | data[i + 1];
        sum += word;
        if (sum & 0x10000) sum = (sum & 0xFFFF) + 1;
    }
    return ~((uint16_t)sum);
}
// --- UDP/TCPパケット構造体（btron-pc資産に準拠） ---
struct udphdr_arm {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};
typedef struct {
    struct udphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} udp_packet_t;

struct tcphdr_arm {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
};
typedef struct {
    struct tcphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} tcp_packet_t;
// --- ARP層雛形 ---

struct arphdr_arm {
    unsigned short ar_hrd;
    unsigned short ar_pro;
    unsigned char  ar_hln;
    unsigned char  ar_pln;
    unsigned short ar_op;
};

typedef struct {
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
    int      valid;
} arp_entry_t;

#define ARP_TABLE_SIZE 32
static arp_entry_t arp_table[ARP_TABLE_SIZE];

// ARP層初期化
void init_arp_arm(void) {
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) arp_table[i].valid = 0;
}

// ARP解決
int arp_resolve_arm(uint32_t ip_addr, uint8_t mac_addr[6]) {
    // 1. ARPテーブル検索
    if (arp_table_lookup(ip_addr, mac_addr) == 0) {
        return 0; // 発見
    }
    // 2. ARP要求送信（雛形: 実装はlink_send_arm等を利用）
    // ARPリクエストパケット生成（省略: 実装時はarphdr_arm等を利用）
    // uint8_t arp_req[...];
    // link_send_arm(ブロードキャストMAC, ETH_P_ARP, arp_req, sizeof(arp_req));
    // 3. 応答待ち・再検索（省略: 非同期/タイムアウト処理は今後実装）
    return -1; // 未解決
}

// --- リンク層雛形 ---

#define ETH_ALEN      6
#define ETH_HLEN      14
#define ETH_DATA_LEN  1500
#define ETH_FRAME_LEN 1514

// EtherType定数（btron-pc資産より）
#define ETH_P_IP      0x0800
#define ETH_P_ARP     0x0806
#define ETH_P_IPV6    0x86DD

struct ethhdr_arm {
    unsigned char  h_dest[ETH_ALEN];
    unsigned char  h_source[ETH_ALEN];
    unsigned short h_proto;
};

typedef struct {
    struct ethhdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} ethernet_frame_t;

// リンク層初期化
void init_link_arm(void) {
    // MACアドレス初期化・NIC初期化・バッファ初期化雛形
    // nic_init_arm();
    // mac_addr_init();
    // tx/rxバッファ初期化
}

// フレーム送信
int link_send_arm(const uint8_t dst_mac[6], uint16_t ethertype, const void *data, uint16_t len) {
    // 1. 送信元MACアドレス（仮: 00:11:22:33:44:55）
    uint8_t src_mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    ethernet_frame_t frame;
    if (ethernet_build_frame(&frame, dst_mac, src_mac, ethertype, data, len) < 0) return -1;
    // 2. 実機送信処理（雛形: 今後デバイスドライバ経由で送信）
    // ここでは送信バッファに格納するだけ
    // TODO: 実機送信APIに接続
    // 例: nic_send(frame) など
    return 0;
}

// フレーム受信
int link_recv_arm(uint8_t src_mac[6], uint16_t *ethertype, void *buf, uint16_t buflen) {
    // イーサネットフレーム受信・バッファ格納雛形
    // 1. NIC受信API呼び出し（nic_recv_arm等）
    // 2. 受信バッファからデータ取得
    // 3. MAC/ethertype/ペイロード抽出
    return -1;
}
// --- TCP層雛形 ---
// TCPコネクション管理テーブル
#define MAX_TCP_CONN 16
typedef struct {
    int used;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    int state; // 0: CLOSED, 1: SYN_SENT, 2: ESTABLISHED, ...
} tcp_conn_t;
static tcp_conn_t tcp_table[MAX_TCP_CONN];

// TCP層初期化
void init_tcp_arm(void) {
    for (int i = 0; i < MAX_TCP_CONN; ++i) tcp_table[i].used = 0;
    // TCPコネクションテーブル・バッファ初期化雛形
    // tcp_buffer_init();
}

// TCP接続（三者間ハンドシェイク雛形）
int tcp_connect_arm(uint32_t dst_ip, uint16_t dst_port) {
    // 空きコネクション検索
    int conn_id = -1;
    for (int i = 0; i < MAX_TCP_CONN; ++i) {
        if (!tcp_table[i].used) { conn_id = i; break; }
    }
    if (conn_id < 0) return -1;
    tcp_table[conn_id].used = 1;
    tcp_table[conn_id].remote_ip = dst_ip;
    tcp_table[conn_id].remote_port = dst_port;
    tcp_table[conn_id].seq_num = 0; // 仮
    tcp_table[conn_id].ack_num = 0;
    tcp_table[conn_id].state = 1; // SYN_SENT
    // TODO: SYNパケット送信（ip_send_arm経由）
    return conn_id;
}

// TCP送信
int tcp_send_arm(int conn_id, const void *data, uint16_t len) {
    if (conn_id < 0 || conn_id >= MAX_TCP_CONN || !tcp_table[conn_id].used) return -1;
    if (tcp_table[conn_id].state != 2) return -1; // ESTABLISHEDのみ
    // TCPパケット生成・ip_send_arm呼び出し雛形
    // 1. tcp_build_packetでパケット生成
    // 2. ip_send_armで送信
    // 3. 送信バッファ管理
    return 0;
}

// TCP受信
int tcp_recv_arm(int conn_id, void *buf, uint16_t buflen) {
    if (conn_id < 0 || conn_id >= MAX_TCP_CONN || !tcp_table[conn_id].used) return -1;
    if (tcp_table[conn_id].state != 2) return -1;
    // 受信バッファ管理・ip_recv_arm呼び出し雛形
    // 1. 受信バッファからデータ取得
    // 2. ip_recv_armで受信
    // 3. 必要に応じてACK送信
    return -1;
}

// TCP切断
int tcp_close_arm(int conn_id) {
    if (conn_id < 0 || conn_id >= MAX_TCP_CONN || !tcp_table[conn_id].used) return -1;
    tcp_table[conn_id].used = 0;
    tcp_table[conn_id].state = 0;
    // TODO: FINパケット送信
    return 0;
}
// --- IP層雛形 ---

#define IPVERSION 4

struct iphdr_arm {
    unsigned char ip_hl:4, ip_v:4;
    unsigned char ip_tos;
    unsigned short ip_len;
    unsigned short ip_id;
    unsigned short ip_off;
    unsigned char ip_ttl;
    unsigned char ip_p;
    unsigned short ip_csum;
    unsigned long ip_src, ip_dst;
};

typedef struct {
    struct iphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} ip_packet_t;

// IP層初期化
void init_ip_arm(void) {
    // TODO: ルーティングテーブル初期化等
}

// IP送信
int ip_send_arm(uint32_t dst_ip, uint8_t protocol, const void *data, uint16_t len) {
    // IPパケット生成
    ip_packet_t pkt;
    pkt.header.ip_v = 4;
    pkt.header.ip_hl = 5;
    pkt.header.ip_tos = 0;
    pkt.header.ip_len = sizeof(struct iphdr_arm) + len;
    pkt.header.ip_id = 0; // 仮
    pkt.header.ip_off = 0;
    pkt.header.ip_ttl = 64;
    pkt.header.ip_p = protocol;
    pkt.header.ip_src = 0xC0A80001; // 仮: 192.168.0.1
    pkt.header.ip_dst = dst_ip;
    pkt.header.ip_csum = 0; // 仮: チェックサム未計算
    for (int i = 0; i < len; ++i) pkt.payload[i] = ((uint8_t*)data)[i];
    // 下位層送信（未実装: link_send_arm）
    // return link_send_arm(NULL, ETH_P_IP, &pkt, pkt.header.ip_len);
    return 0; // 仮: 送信成功
}

// IP受信
int ip_recv_arm(uint32_t *src_ip, uint8_t *protocol, void *buf, uint16_t buflen) {
    // 下位層受信（未実装: link_recv_arm）
    // uint8_t pktbuf[1600];
    // int ret = link_recv_arm(NULL, NULL, pktbuf, sizeof(pktbuf));
    // if (ret < 0) return -1;
    // IPパケットパース
    // ip_packet_t *pkt = (ip_packet_t*)pktbuf;
    // if (src_ip) *src_ip = pkt->header.ip_src;
    // if (protocol) *protocol = pkt->header.ip_p;
    // for (int i = 0; i < buflen; ++i) ((uint8_t*)buf)[i] = pkt->payload[i];
    // return buflen; // 仮
    return -1; // 仮: 未実装
}
// --- UDP層雛形 ---
// UDP層初期化
void init_udp_arm(void) {
    // UDPソケットテーブル・バッファ初期化雛形
    // udp_buffer_init();
}

// UDP送信
int udp_send_arm(uint32_t dst_ip, uint16_t dst_port, const void *data, uint16_t len) {
    // UDPパケット生成
    udp_packet_t pkt;
    if (udp_build_packet(&pkt, 12345, dst_port, data, len) < 0) return -1; // src_portは仮
    // IP層送信（未実装: ip_send_arm）
    // return ip_send_arm(dst_ip, 17, &pkt, sizeof(struct udphdr_arm) + len);
    return 0; // 仮: 送信成功
}

// UDP受信
int udp_recv_arm(uint32_t *src_ip, uint16_t *src_port, void *buf, uint16_t buflen) {
    // IP層受信（未実装: ip_recv_arm）
    // uint8_t pktbuf[1600];
    // int ret = ip_recv_arm(src_ip, NULL, pktbuf, sizeof(pktbuf));
    // if (ret < 0) return -1;
    // UDPパケットパース
    // udp_packet_t *pkt = (udp_packet_t*)pktbuf;
    // uint16_t len;
    // if (udp_parse_packet(pkt, src_port, NULL, buf, &len) < 0) return -1;
    // return len;
    return -1; // 仮: 未実装
}
// kernel_arm/net_arm.c
// ARM ネットワーク管理雛形
// 2025/12/27 新規作成

#include "../include_arm/net_api_arm.h"
#include "../include_arm/types_arm.h"
#include <stdint.h>

#define MAX_SOCKETS 32

typedef struct {
    int sockfd;
    int used;
    int type; // 0:UDP, 1:TCP
    uint32_t remote_ip;
    uint16_t remote_port;
    // ...他に必要な情報
} socket_t;

static socket_t socket_table[MAX_SOCKETS];

// ネットワーク初期化
void net_init(void) {
    for (int i = 0; i < MAX_SOCKETS; ++i) socket_table[i].sockfd = -1;
    // サブシステム間の依存順序を明示
    // 1. タイマ（割り込み/タイムアウト管理）
    init_timer_arm();
    // 2. リンク層（NIC/物理層）
    init_link_arm();
    // 3. ARP（MAC-IP解決）
    init_arp_arm();
    // 4. IP層（ルーティング/パケット配送）
    init_ip_arm();
    // 5. UDP層
    init_udp_arm();
    // 6. TCP層
    init_tcp_arm();
    // 7. ソケットテーブル（本関数内）
    // 依存関係: タイマ→リンク→ARP→IP→UDP/TCP→ソケット
    // これにより、各層の初期化・連携が保証される
}

// 各初期化関数の雛形（btron-pc資産流用用）
void init_timer_arm(void) { /* TODO: タイマ初期化 */ }
void init_link_arm(void)  { /* TODO: リンク層初期化 */ }
void init_ip_arm(void)    { /* TODO: IP層初期化 */ }
void init_udp_arm(void)   { /* TODO: UDP層初期化 */ }
void init_tcp_arm(void)   { /* TODO: TCP層初期化 */ }
void init_arp_arm(void)   { /* TODO: ARP初期化 */ }

// ソケット作成
int net_socket(int type) {
    for (int i = 0; i < MAX_SOCKETS; ++i) {
        if (!socket_table[i].used) {
            socket_table[i].used = 1;
            socket_table[i].type = type;
            socket_table[i].sockfd = i;
            socket_table[i].remote_ip = 0;
            socket_table[i].remote_port = 0;
            // 必要に応じて他のフィールドも初期化
            return i;
        }
    }
    return -1;
}

// データ送信
int net_send(int sockfd, const void *buf, uint32_t size) {
    // TODO: データ送信処理
    return -1;
}

// データ受信
int net_recv(int sockfd, void *buf, uint32_t size) {
    // TODO: データ受信処理
    return -1;
}

// ソケットクローズ
int net_close(int sockfd) {
    // TODO: エントリ解放
    return -1;
}
