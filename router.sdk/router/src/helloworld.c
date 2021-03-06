/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "xil_printf.h"
#include "xllfifo.h"
#include "xgpio.h"
#include "xscugic.h"
#include "xscutimer.h"
#include "xstatus.h"
#include "xbram.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "structs.h"
#include "video/video.h"

XLlFifo fifoInstance;
XGpio gpio1Instance;
XGpio gpio2Instance;
XGpio gpio3Instance;
XGpio gpio4Instance;
XGpio gpio5Instance;
XGpio gpio6Instance;
XGpio gpio7Instance;
XGpio gpio8Instance;
XScuGic gicInstance;
XScuTimer timerInstance;
XBram bramInstance;

XLlFifo_Config *fifoConfig;
XGpio_Config *gpio1Config;
XGpio_Config *gpio2Config;
XGpio_Config *gpio3Config;
XGpio_Config *gpio4Config;
XGpio_Config *gpio5Config;
XGpio_Config *gpio6Config;
XGpio_Config *gpio7Config;
XGpio_Config *gpio8Config;
XScuGic_Config *gicConfig;
XScuTimer_Config *timerConfig;
XBram_Config *bramConfig;

u32 time = 0;
u32 tick = 0;

const u8 portMAC[6] = {2, 2, 3, 3, 0, 0};
const u8 ripMAC[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x09};
const int ENABLE_PORT = 3;
const int INVALID_TIME = 30; // 180
const int FLUSH_TIME = 60; // 240

u16 bswap16(u16 i) {
    return (i >> 8) | ((i & 0xFF) << 8);
}

u32 bswap32(u32 i) {
    return (i >> 24) | ((i & 0xFF0000) >> 8) | ((i & 0xFF00) << 8) | ((i & 0xFF) << 24);
}

void sendToFifo(u8 port, u8 *data, u32 length) {
    //printf("Sending data to port %d of length %ld\n", port, length);
    while (!XLlFifo_iTxVacancy(&fifoInstance));
    XLlFifo_TxPutWord(&fifoInstance, (u32)port);
    for (u32 i = 0;i < length;i++) {
        //printf("%02x", data[i]);
        while (!XLlFifo_iTxVacancy(&fifoInstance));
        XLlFifo_TxPutWord(&fifoInstance, (u32)data[i]);
    }
    //printf("\n");
    XLlFifo_iTxSetLen(&fifoInstance, (length + 1) * 4);
    while(!XLlFifo_IsTxDone(&fifoInstance));
}

struct Route routingTable[1024];

int routingTableSize = 0;

u16 checksumAdd(u16 orig, u16 add) {
    u32 ans = orig;
    ans += add;
    ans = (ans >> 16) + (ans & 0xFFFF);
    ans = (ans >> 16) + (ans & 0xFFFF);
    return (u16)ans;
}

void fillIpChecksum(struct Ip *ip) {
    // skip ethernet
    u16 *data = ((u16 *)ip) + 7;
    ip->headerChecksum = 0;
    u16 checksum = 0;
    for (int i = 0;i < 10;i++) {
        checksum = checksumAdd(checksum, data[i]);
    }
    ip->headerChecksum = ~checksum;
}

void fillIcmpChecksum(struct Icmp *icmp, u32 length) {
    icmp->checksum = 0;
    u16 *data = ((u16 *)icmp);
    u16 checksum = 0;
    for (int i = 0; i < length / 2; i++) {
        checksum = checksumAdd(checksum, data[i]);
    }
    icmp->checksum = ~checksum;
}

void printIP(u32 ip) {
    int p1 = ip >> 24, p2 = (ip >> 16) & 0xFF, p3 = (ip >> 8) & 0xFF, p4 = ip & 0xFF;
    printf("%d.%d.%d.%d", p1, p2, p3, p4);
}

void handleEthernetFrame(u8 port, u8 *data) {
    u8 portIP[4] = {10, 0, port, 1};

    struct EthernetFrame *ether = (struct EthernetFrame *)data;
    struct ArpResponse *arp = (struct ArpResponse *)data;
    u32 buffer[512];
    u16 etherType = bswap16(ether->etherType);
    if (etherType == 0x0806) {
        // ARP
        u16 opcode = bswap16(arp->opcode);
        if (opcode == 0x0001) {
            //printf("Got ARP request\n");
            struct ArpResponse *arpResp = (struct ArpResponse *)buffer;
            memcpy(arpResp->dstMAC, arp->srcMAC, 6);
            memcpy(arpResp->srcMAC, arp->dstMAC, 6);
            arpResp->etherType = bswap16(0x0806);
            arpResp->hardwareType = bswap16(1);
            arpResp->protocolType = bswap16(0x0800);
            arpResp->hardwareSize = 6;
            arpResp->protocolSize = 4;
            arpResp->opcode = bswap16(2);
            memcpy(arpResp->targetMAC, arp->senderMAC, 6);
            memcpy(arpResp->targetIP, arp->senderIP, 4);
            // Filled by router in fact
            memcpy(arpResp->senderMAC, arp->dstMAC, 6);
            memcpy(arpResp->senderIP, arp->targetIP, 4);

            sendToFifo(port, buffer, sizeof(struct ArpResponse));
        }
    } else if (etherType == 0x0800) {
        // IP
        struct Ip *ip = (struct Ip *)data;
        struct Ip *ipResp = (struct Ip *)buffer;
        u32 destIP;
        memcpy(&destIP, ip->destIP, sizeof(u32));
        destIP = bswap32(destIP);
        if (ip->protocol == 1 && destIP == portIP) {
            // ICMP
            if (ip->payload.icmp.type == 8) {
                //printf("Got ICMP echo request\n");
                memcpy(ipResp->ethernet.dstMAC, ip->ethernet.srcMAC, 6);
                memcpy(ipResp->ethernet.srcMAC, portMAC, 6);
                ipResp->ethernet.etherType = bswap16(0x0800);
                ipResp->versionIHL = 0x45;
                ipResp->dsf = 0;
                u16 totalLength = bswap16(ip->totalLength);
                ipResp->totalLength = ip->totalLength;
                ipResp->identification = 0;
                ipResp->flags = 0;
                ipResp->ttl = 64;
                ipResp->protocol = 1;
                ipResp->headerChecksum = 0;
                memcpy(ipResp->sourceIP, portIP, 4);
                memcpy(ipResp->destIP, ip->sourceIP, 4);
                ipResp->payload.icmp.type = 0;
                ipResp->payload.icmp.code = 0;
                // type: 8 -> 0
                ipResp->payload.icmp.checksum = checksumAdd(ip->payload.icmp.checksum, 8);
                // assuming IHL=5
                memcpy(ipResp->payload.icmp.data, ip->payload.icmp.data, totalLength - 20 - 4);

                fillIpChecksum(ipResp);
                sendToFifo(port, buffer, totalLength + 14);
            }
        } else if (ip->protocol == 17) {
            // UDP
            if (ip->payload.udp.srcPort == bswap16(520) && ip->payload.udp.dstPort == bswap16(520)) {
                // RIP
                printf("Got RIP response from port %d:\n", port);
                u16 totalLength = bswap16(ip->totalLength);
                u32 sourceIP;
                memcpy(&sourceIP, ip->sourceIP, sizeof(u32));
                sourceIP = bswap32(sourceIP);
                int totalRoutes = (totalLength - 20 - 8 - 4) / 20;
                for (int routes = 0;routes < totalRoutes;routes++) {
                    u32 ip_net = bswap32(ip->payload.udp.payload.rip.routes[routes].ip);
                    u32 netmask = bswap32(ip->payload.udp.payload.rip.routes[routes].netmask);
                    u32 nexthop = bswap32(ip->payload.udp.payload.rip.routes[routes].nexthop);
                    if (nexthop == 0) {
                        memcpy(&nexthop, ip->sourceIP, 4);
                        nexthop = bswap32(nexthop);
                    }
                    u32 metric = bswap32(ip->payload.udp.payload.rip.routes[routes].metric);

                    printf("\t%d: ", routes);
                    printIP(ip_net);
                    printf(" netmask ");
                    printIP(netmask);
                    printf(" nexthop ");
                    printIP(nexthop);
                    printf(" metric %ld\n", metric);

                    metric += 1;
                    if (metric > 16) {
                        metric = 16;
                    }

                    int flag = 0;
                    for (int i = 0;i < routingTableSize;i++) {
                        if (routingTable[i].ip == ip_net && routingTable[i].netmask == netmask) {
                            if (routingTable[i].origin == sourceIP) {
                                routingTable[i].updateTime = time;
                            }
                            if (metric < routingTable[i].metric || memcmp(ip->sourceIP, &routingTable[i].origin, 4) == 0) {
                                // update this entry
                                if (metric >= 16) {
                                    // remove this entry
                                    memmove(&routingTable[i], &routingTable[i+1], sizeof(struct Route) * (routingTableSize - i - 1));
                                    routingTableSize --;
                                    i --;
                                } else {
                                    routingTable[i].metric = metric;
                                    routingTable[i].nexthop = nexthop;
                                    routingTable[i].port = port;
                                    routingTable[i].updateTime = time;
                                }
                            }
                            flag = 1;
                            break;
                        }
                    }
                    
                    if (!flag && metric < 16) {
                        // add this entry
                        routingTable[routingTableSize].ip = ip_net;
                        routingTable[routingTableSize].netmask = netmask;
                        routingTable[routingTableSize].nexthop = nexthop;
                        routingTable[routingTableSize].port = port;
                        routingTable[routingTableSize].metric = metric;
                        routingTable[routingTableSize].updateTime = time;
                        routingTable[routingTableSize].origin = sourceIP;
                        routingTableSize ++;
                    }
                }

            }
        } else if (ip->ttl == 1) {
            // send ICMP Time Exceeded
            memcpy(ipResp->ethernet.dstMAC, ip->ethernet.srcMAC, 6);
            memcpy(ipResp->ethernet.srcMAC, portMAC, 6);
            ipResp->ethernet.etherType = bswap16(0x0800);
            ipResp->versionIHL = 0x45;
            ipResp->dsf = 0;
            u16 totalLength = 20 + 4 + 4 + 20 + 8;
            ipResp->totalLength = bswap16(totalLength);
            ipResp->identification = 0;
            ipResp->flags = 0;
            ipResp->ttl = 64;
            ipResp->protocol = 1;
            ipResp->headerChecksum = 0;
            memcpy(ipResp->sourceIP, portIP, 4);
            memcpy(ipResp->destIP, ip->sourceIP, 4);
            ipResp->payload.icmp.type = 11; // Time exceeded
            ipResp->payload.icmp.code = 0;
            ipResp->payload.icmp.checksum = 0;
            // unused
            memset(ipResp->payload.icmp.data, 0, 4);
            // assuming IHL=5
            memcpy(ipResp->payload.icmp.data + 4, &ip->versionIHL, 20 + 8);

            fillIcmpChecksum(&ipResp->payload.icmp, 4 + 4 + 20 + 8);
            fillIpChecksum(ipResp);
            sendToFifo(port, buffer, totalLength + 14);
        }
    }
}

void fifoInterruptHandler(void *data) {
    printf("Got fifo interrupt\n");
}

void sendRIPReponse() {
    // send RIP response
    u32 buffer[512];
    for (u8 port = 0;port < ENABLE_PORT;port++) {
        u8 portIP[] = {10, 0, port, 1};
        u8 ripIP[4] = {224, 0, 0, 9};
        struct Ip *ip = (struct Ip *) buffer;
        memcpy(ip->ethernet.dstMAC, ripMAC, 6);
        memcpy(ip->ethernet.srcMAC, portMAC, 6);
        ip->ethernet.etherType = bswap16(0x0800);
        ip->versionIHL = 0x45;
        ip->dsf = 0;

        int routes = 0;
        for (int r = 0;r < routingTableSize;r++) {
            ip->payload.udp.payload.rip.routes[routes].family = bswap16(2);
            ip->payload.udp.payload.rip.routes[routes].routeTag = 0;
            ip->payload.udp.payload.rip.routes[routes].ip = bswap32(routingTable[r].ip);
            ip->payload.udp.payload.rip.routes[routes].netmask = bswap32(routingTable[r].netmask);
            ip->payload.udp.payload.rip.routes[routes].nexthop = bswap32(0);
            if (routingTable[r].port != port) {
                // not this port, split horizon
                ip->payload.udp.payload.rip.routes[routes].metric = bswap32(routingTable[r].metric);
            } else {
                // from this port, reverse poisoning
                ip->payload.udp.payload.rip.routes[routes].metric = bswap32(16);
            }
            routes++;
        }

        if (routes == 0) {
            routes = 1;
            ip->payload.udp.payload.rip.routes[0].family = 0;
            ip->payload.udp.payload.rip.routes[0].routeTag = 0;
            ip->payload.udp.payload.rip.routes[0].ip = 0;
            ip->payload.udp.payload.rip.routes[0].netmask = 0;
            ip->payload.udp.payload.rip.routes[0].nexthop = 0;
            ip->payload.udp.payload.rip.routes[0].metric = 0;
        }

        u16 totalLength = 20 + 8 + 4 + 20 * routes;

        ip->totalLength = bswap16(totalLength);
        ip->identification = 0;
        ip->flags = 0;
        ip->ttl = 1;
        ip->protocol = 17; // UDP
        memcpy(ip->sourceIP, portIP, 4);
        memcpy(ip->destIP, ripIP, 4);
        ip->payload.udp.srcPort = bswap16(520);
        ip->payload.udp.dstPort = bswap16(520);
        ip->payload.udp.length = bswap16(totalLength - 20);
        ip->payload.udp.checksum = 0;
        ip->payload.udp.payload.rip.command = 2;
        ip->payload.udp.payload.rip.version = 2;
        ip->payload.udp.payload.rip.zero = 0;

        fillIpChecksum(ip);
        sendToFifo(port, buffer, totalLength + 14);
    }
}


int routingTableCmp(const void *a, const void *b) {
    struct Route *aa = (struct Route *)a;
    struct Route *bb = (struct Route *)b;
    // unreachable last
    if (aa->metric < 16 && bb->metric >= 16) {
        return -1;
    } else if (aa->metric >= 16 && bb->metric < 16) {
        return 1;
    }
    // largest netmask first
    if (aa->netmask > bb->netmask)
        return -1;
    else if (aa->netmask < bb->netmask)
        return 1;

    // smallest ip first
    if (aa->ip < bb->ip)
        return -1;
    else if (aa->ip > bb->ip)
        return 1;
    return 0;
}

void applyCurrentRoutingTable() {
    qsort(routingTable, routingTableSize, sizeof(struct Route), routingTableCmp);
    // add all-zero route as the end
    for (int i = 0;i < 4;i ++) {
        XBram_Out32(bramConfig->MemBaseAddress + routingTableSize * 16 + i * 4, 0);
    }
    for (int i = routingTableSize - 1;i >= 0;i--) {
        if (routingTable[i].metric >= 16) {
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 0 * 4, 0);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 1 * 4, 0);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 2 * 4, 0);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 3 * 4, 0);
        } else {
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 0 * 4, routingTable[i].nexthop);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 1 * 4, routingTable[i].netmask);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 2 * 4, routingTable[i].ip);
            XBram_Out32(bramConfig->MemBaseAddress + i * 16 + 3 * 4, routingTable[i].port);
        }
    }
}

u32 all_routes[1024][4];
void printCurrentRoutingTable() {
    u32 offset = 0;
    int j = 0;
    for (int flag = 1;flag && j < 1024;j++) {
        u32 route[4];
        flag = 0;
        for (u32 i = 0;i < 4;i++) {
            route[i] = XBram_In32(bramConfig->MemBaseAddress + offset + i * 4);
            if (route[i]) {
                flag = 1;
            }
        }
        offset += 16;
        memcpy(all_routes[j], route, sizeof(route));
    }
    j--;
    printf("Hardware table:\n");
    for (int i = 0;i < j;i++) {
        printf("\t%d: ", i);
        printIP(all_routes[i][2]);
        printf(" netmask ");
        printIP(all_routes[i][1]);
        printf(" via ");
        printIP(all_routes[i][0]);
        printf(" dev port%ld\n", all_routes[i][3]);
    }
    printf("Software table:\n");
    for (int i = 0;i < routingTableSize;i++) {
        if (routingTable[i].nexthop != 0) {
            // indirect
            printf("\t%d: ", i);
            printIP(routingTable[i].ip);
            printf(" netmask ");
            printIP(routingTable[i].netmask);
            printf(" via ");
            printIP(routingTable[i].nexthop);
            printf(" dev port%ld metric %ld timer %ld learned from ", routingTable[i].port, routingTable[i].metric, time - routingTable[i].updateTime);
            printIP(routingTable[i].origin);
            printf("\n");
        } else {
            printf("\t%d: ", i);
            printIP(routingTable[i].ip);
            printf(" netmask ");
            printIP(routingTable[i].netmask);
            printf(" dev port%ld\n", routingTable[i].port);
        }
    }
    applyCurrentRoutingTable();
}

char statsBuffer[512];
void timerInterruptHandler(void *data) {
    // packets
    static u32 lastChan11 = 0, lastChan12 = 0, lastChan21 = 0, lastChan22 = 0, lastChan31 = 0, lastChan32 = 0, lastChan41 = 0, lastChan42 = 0;
    static u32 curChan11 = 0, curChan12 = 0, curChan21 = 0, curChan22 = 0, curChan31 = 0, curChan32 = 0, curChan41 = 0, curChan42 = 0;
    static u32 lastTickChan11 = 0, lastTickChan12 = 0, lastTickChan21 = 0, lastTickChan22 = 0, lastTickChan31 = 0, lastTickChan32 = 0, lastTickChan41 = 0, lastTickChan42 = 0;

    // bytes
    static u32 lastChan51 = 0, lastChan52 = 0, lastChan61 = 0, lastChan62 = 0, lastChan71 = 0, lastChan72 = 0, lastChan81 = 0, lastChan82 = 0;
    static u32 curChan51 = 0, curChan52 = 0, curChan61 = 0, curChan62 = 0, curChan71 = 0, curChan72 = 0, curChan81 = 0, curChan82 = 0;

    u32 chan11 = XGpio_DiscreteRead(&gpio1Instance, 1);
    u32 chan12 = XGpio_DiscreteRead(&gpio1Instance, 2);
    u32 chan21 = XGpio_DiscreteRead(&gpio2Instance, 1);
    u32 chan22 = XGpio_DiscreteRead(&gpio2Instance, 2);
    u32 chan31 = XGpio_DiscreteRead(&gpio3Instance, 1);
    u32 chan32 = XGpio_DiscreteRead(&gpio3Instance, 2);
    u32 chan41 = XGpio_DiscreteRead(&gpio4Instance, 1);
    u32 chan42 = XGpio_DiscreteRead(&gpio4Instance, 2);
    u32 chan51 = XGpio_DiscreteRead(&gpio5Instance, 1);
    u32 chan52 = XGpio_DiscreteRead(&gpio5Instance, 2);
    u32 chan61 = XGpio_DiscreteRead(&gpio6Instance, 1);
    u32 chan62 = XGpio_DiscreteRead(&gpio6Instance, 2);
    u32 chan71 = XGpio_DiscreteRead(&gpio7Instance, 1);
    u32 chan72 = XGpio_DiscreteRead(&gpio7Instance, 2);
    u32 chan81 = XGpio_DiscreteRead(&gpio8Instance, 1);
    u32 chan82 = XGpio_DiscreteRead(&gpio8Instance, 2);

    snprintf(statsBuffer, sizeof(statsBuffer), "%lu: Rx %lu, %lu, %lu, %lu packets/s, Tx %lu, %lu, %lu, %lu packets/s",
             time, curChan11 - lastChan11, curChan12 - lastChan12, curChan21 - lastChan21, curChan22 - lastChan22,
             curChan31 - lastChan31, curChan32 - lastChan32, curChan41 - lastChan41, curChan42 - lastChan42);

    tick ++;
    if ((tick % 10) == 0) {
        // 10FPS
        time ++;

        lastChan11 = curChan11;
        lastChan12 = curChan12;
        lastChan21 = curChan21;
        lastChan22 = curChan22;
        lastChan31 = curChan31;
        lastChan32 = curChan32;
        lastChan41 = curChan41;
        lastChan42 = curChan42;
        lastChan51 = curChan51;
        lastChan52 = curChan52;
        lastChan61 = curChan61;
        lastChan62 = curChan62;
        lastChan71 = curChan71;
        lastChan72 = curChan72;
        lastChan81 = curChan81;
        lastChan82 = curChan82;

        curChan11 = chan11;
        curChan12 = chan12;
        curChan21 = chan21;
        curChan22 = chan22;
        curChan31 = chan31;
        curChan32 = chan32;
        curChan41 = chan41;
        curChan42 = chan42;
        curChan51 = chan51;
        curChan52 = chan52;
        curChan61 = chan61;
        curChan62 = chan62;
        curChan71 = chan71;
        curChan72 = chan72;
        curChan81 = chan81;
        curChan82 = chan82;

        for (int i = 0;i < routingTableSize;i++) {
            if (routingTable[i].nexthop == 0) {
                // direct routes, skip
                continue;
            }
            if ((time - routingTable[i].updateTime) > INVALID_TIME) {
                routingTable[i].metric = 16;
            }
            if (routingTable[i].metric == 16 && (time - routingTable[i].updateTime) > FLUSH_TIME) {
                memmove(&routingTable[i], &routingTable[i+1], sizeof(struct Route) * (routingTableSize - i - 1));
                routingTableSize --;
                i --;
            }
        }

        if ((time % 10) == 0) {
            sendRIPReponse();
        }
        printf("time %ld:\n", time);
        printCurrentRoutingTable();
    }

    int packets[] = {
        chan11 - lastTickChan11, chan12 - lastTickChan12, chan21 - lastTickChan21, chan22 - lastTickChan22,
        chan31 - lastTickChan31, chan32 - lastTickChan32, chan41 - lastTickChan41, chan42 - lastTickChan42
    };

    int bytes[] = {
        curChan51 - lastChan51, curChan52 - lastChan52, curChan61 - lastChan61, curChan62 - lastChan62,
        curChan71 - lastChan71, curChan72 - lastChan72, curChan81 - lastChan81, curChan62 - lastChan82
    };

    renderData(routingTable, routingTableSize, statsBuffer, time, packets, bytes);

    lastTickChan11 = chan11;
    lastTickChan12 = chan12;
    lastTickChan21 = chan21;
    lastTickChan22 = chan22;
    lastTickChan31 = chan31;
    lastTickChan32 = chan32;
    lastTickChan41 = chan41;
    lastTickChan42 = chan42;
}

int main()
{
    u32 receiveLength;
    u32 i;
    u8 buffer[2048];

    init_platform();

    initVideo();

    printf("Lookup config\n");
    fifoConfig = XLlFfio_LookupConfig(XPAR_AXI_FIFO_0_DEVICE_ID);
    if (!fifoConfig) {
        printf("No config found\n");
        goto fail;
    }

    XLlFifo_CfgInitialize(&fifoInstance, fifoConfig, fifoConfig->BaseAddress);

	XLlFifo_IntClear(&fifoInstance,0xffffffff);

    gpio1Config = XGpio_LookupConfig(XPAR_AXI_GPIO_0_DEVICE_ID);
    if (!gpio1Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio1Instance, gpio1Config, gpio1Config->BaseAddress);

    gpio2Config = XGpio_LookupConfig(XPAR_AXI_GPIO_1_DEVICE_ID);
    if (!gpio2Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio2Instance, gpio2Config, gpio2Config->BaseAddress);

    gpio3Config = XGpio_LookupConfig(XPAR_AXI_GPIO_3_DEVICE_ID);
    if (!gpio3Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio3Instance, gpio3Config, gpio3Config->BaseAddress);

    gpio4Config = XGpio_LookupConfig(XPAR_AXI_GPIO_4_DEVICE_ID);
    if (!gpio4Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio4Instance, gpio4Config, gpio4Config->BaseAddress);

    gpio5Config = XGpio_LookupConfig(XPAR_AXI_GPIO_5_DEVICE_ID);
    if (!gpio5Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio5Instance, gpio5Config, gpio5Config->BaseAddress);

    gpio6Config = XGpio_LookupConfig(XPAR_AXI_GPIO_6_DEVICE_ID);
    if (!gpio6Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio6Instance, gpio6Config, gpio6Config->BaseAddress);

    gpio7Config = XGpio_LookupConfig(XPAR_AXI_GPIO_7_DEVICE_ID);
    if (!gpio7Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio7Instance, gpio7Config, gpio7Config->BaseAddress);

    gpio8Config = XGpio_LookupConfig(XPAR_AXI_GPIO_8_DEVICE_ID);
    if (!gpio8Config) {
        printf("No config found\n");
        goto fail;
    }
    XGpio_CfgInitialize(&gpio8Instance, gpio8Config, gpio8Config->BaseAddress);

    bramConfig = XBram_LookupConfig(XPAR_BRAM_0_DEVICE_ID);
    if (!bramConfig) {
        printf("No config found\n");
        goto fail;
    }

    XBram_CfgInitialize(&bramInstance, bramConfig, bramConfig->CtrlBaseAddress);

    timerConfig = XScuTimer_LookupConfig(XPAR_PS7_SCUTIMER_0_DEVICE_ID);
    if (!timerConfig) {
        printf("No timer config found\n");
        goto fail;
    }

    XScuTimer_CfgInitialize(&timerInstance, timerConfig, timerConfig->BaseAddr);
    XScuTimer_SelfTest(&timerInstance);
    XScuTimer_EnableAutoReload(&timerInstance);
    XScuTimer_LoadTimer(&timerInstance, 0x13D92D3F / 10); // 10FPS
    XScuTimer_Start(&timerInstance);

    gicConfig = XScuGic_LookupConfig(XPAR_PS7_SCUGIC_0_DEVICE_ID);
    if (!gicConfig) {
        printf("No config found\n");
        goto fail;
    }

    XScuGic_CfgInitialize(&gicInstance, gicConfig, gicConfig->CpuBaseAddress);

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler) XScuGic_InterruptHandler, &gicInstance);

    XScuGic_Connect(&gicInstance, XPAR_FABRIC_AXI_FIFO_MM_S_0_INTERRUPT_INTR, (Xil_ExceptionHandler)fifoInterruptHandler, NULL);
    XScuGic_Enable(&gicInstance, XPAR_FABRIC_AXI_FIFO_MM_S_0_INTERRUPT_INTR);
    XScuGic_Connect(&gicInstance, XPAR_PS7_SCUTIMER_0_INTR, (Xil_ExceptionHandler)timerInterruptHandler, NULL);
    XScuGic_Enable(&gicInstance, XPAR_PS7_SCUTIMER_0_INTR);
    XScuTimer_EnableInterrupt(&timerInstance);

    routingTableSize = 0;
    for (int i = 0; i < 4; i++) {
        routingTable[i].ip = 0x0a000000 + (i << 8);
        routingTable[i].netmask = 0xffffff00;
        routingTable[i].metric = 1;
        routingTable[i].nexthop = 0; // direct route
        routingTable[i].port = i;
        routingTable[i].updateTime = 0;
        routingTable[i].origin = 0; // myself
        routingTableSize++;
    }

    Xil_ExceptionEnable();


    printf("Waiting for data\n");
    for (int time = 0;;time++) {
        if (XLlFifo_iRxOccupancy(&fifoInstance)) {
            // Avoid racing
            XScuTimer_DisableInterrupt(&timerInstance);
            receiveLength = XLlFifo_iRxGetLen(&fifoInstance) / 4;
            //printf("%ld: Got length %ld\nData: ", ++count, receiveLength);
            for (i = 0;i < receiveLength;i++) {
                u32 word = XLlFifo_RxGetWord(&fifoInstance);
                buffer[i] = word;
                //printf("%02lx", word);
            }
            //printf("\n");
            handleEthernetFrame(buffer[0], &buffer[1]);

            // avoid racing
            XScuTimer_EnableInterrupt(&timerInstance);
        }
    }

fail:
    printf("Leaving\n");
    cleanup_platform();
    return 0;
}
