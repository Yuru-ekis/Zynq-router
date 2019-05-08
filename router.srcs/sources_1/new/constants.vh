// 7 preamble + 1 sfd + 12 mac + 2 ethertype + 1500 payload + 4 fcs
`define MAX_ETHERNET_FRAME_BYTES 1526

// controls whether the control logic is handled in hardware or software
`define HARDWARE_CONTROL_PLANE 1

`define MAX_FIFO_SIZE 8192

`define BYTE_WIDTH 8
`define LENGTH_WIDTH 14

`define BUCKET_INDEX_COUNT 8
`define BUCKET_INDEX_WIDTH 3
`define BUCKET_DEPTH_COUNT 4
`define BUCKET_DEPTH_WIDTH 2

`define IPV4_WIDTH 32
`define MAC_WIDTH 48
`define ETHERTYPE_WIDTH 16
`define PORT_COUNT 4
`define PORT_WIDTH 2

// Ethernet
`define DST_MAC_BEGIN 0
`define DST_MAC_END 6
`define SRC_MAC_BEGIN 6
`define SRC_MAC_END 12
`define ETHERTYPE_BEGIN 12
`define ETHERTYPE_END 14

`define IPV4_ETHERTYPE `ETHERTYPE_WIDTH'h0800
`define ARP_ETHERTYPE `ETHERTYPE_WIDTH'h0806

// ARP
`define ARP_RESPONSE_COUNT 42
`define ARP_OPCODE_COUNT 2
`define ARP_OPCODE_BEGIN 20
`define ARP_OPCODE_END 22
`define ARP_OPCODE_REQUEST 16'h1
`define ARP_OPCODE_REPLY 16'h2
`define ARP_SRC_IPV4_BEGIN 28
`define ARP_SRC_IPV4_END 32
`define ARP_DST_IPV4_BEGIN 38
`define ARP_DST_IPV4_END 42

// IPv4
`define IPV4_BEGIN 14
`define IPV4_TTL_BEGIN 22
`define IPV4_TTL_END 23
`define IPV4_CHECKSUM_BEGIN 24
`define IPV4_CHECKSUM_END 26
`define IPV4_CHECKSUM_COUNT 2
`define IPV4_SRC_IP_BEGIN 26
`define IPV4_SRC_IP_END 30
`define IPV4_DST_IP_BEGIN 30
`define IPV4_DST_IP_END 34