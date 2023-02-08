/*
 * Copyright (c) 2020 GARDENA GmbH
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *   Lukas Woodtli, GARDENA GmbH - Please refer to git log
 */


#include "liblwm2m.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <inttypes.h>

#include "commandline.h"
#include "connection.h"

#define MAX_PACKET_SIZE 2048

static bool g_quit = false;

static const char * prv_dump_version(lwm2m_version_t version)
{
    switch(version)
    {
    case VERSION_MISSING:
        return "Missing";
    case VERSION_UNRECOGNIZED:
        return "Unrecognized";
    case VERSION_1_0:
        return "1.0";
    case VERSION_1_1:
        return "1.1";
    default:
        return "";
    }
}

static void prv_dump_binding(lwm2m_binding_t binding)
{
    if(BINDING_UNKNOWN == binding)
    {
        fprintf(stdout, "\tbinding: \"Not specified\"\r\n");
    }
    else
    {
        const struct bindingTable
        {
            lwm2m_binding_t binding;
            const char *text;
        } bindingTable[] =
        {
            { BINDING_U, "UDP" },
            { BINDING_T, "TCP" },
            { BINDING_S, "SMS" },
            { BINDING_N, "Non-IP" },
            { BINDING_Q, "queue mode" },
        };
        size_t i;
        bool oneSeen = false;
        fprintf(stdout, "\tbinding: \"");
        for (i = 0; i < sizeof(bindingTable) / sizeof(bindingTable[0]); i++)
        {
            if ((binding & bindingTable[i].binding) != 0)
            {
                if (oneSeen)
                {
                    fprintf(stdout, ", %s", bindingTable[i].text);
                }
                else
                {
                    fprintf(stdout, "%s", bindingTable[i].text);
                    oneSeen = true;
                }
            }
        }
        fprintf(stdout, "\"\r\n");
    }
}

static void prv_dump_client(lwm2m_client_t * targetP)
{
    lwm2m_client_object_t * objectP;

    fprintf(stdout, "Client #%d:\r\n", targetP->internalID);
    fprintf(stdout, "\tname: \"%s\"\r\n", targetP->name);
    fprintf(stdout, "\tversion: \"%s\"\r\n", prv_dump_version(targetP->version));
    prv_dump_binding(targetP->binding);
    if (targetP->msisdn) fprintf(stdout, "\tmsisdn: \"%s\"\r\n", targetP->msisdn);
    if (targetP->altPath) fprintf(stdout, "\talternative path: \"%s\"\r\n", targetP->altPath);
    fprintf(stdout, "\tlifetime: %d sec\r\n", targetP->lifetime);
    fprintf(stdout, "\tobjects: ");
    for (objectP = targetP->objectList; objectP != NULL ; objectP = objectP->next)
    {
        if (objectP->instanceList == NULL)
        {
            if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
            {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            }
            else
            {
                fprintf(stdout, "/%d, ", objectP->id);
            }
        }
        else
        {
            lwm2m_list_t * instanceP;

            if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
            {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            }

            for (instanceP = objectP->instanceList; instanceP != NULL ; instanceP = instanceP->next)
            {
                fprintf(stdout, "/%d/%d, ", objectP->id, instanceP->id);
            }
        }
    }
    fprintf(stdout, "\r\n");
}


static void prv_monitor_callback(lwm2m_context_t *lwm2mH, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                 block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                 void *userData) {
    lwm2m_client_t * targetP;

    /* unused parameters */
    (void)uriP;
    (void)block_info;
    (void)format;
    (void)data;
    (void)dataLength;
    (void)userData;

    switch (status)
    {
    case COAP_201_CREATED:
        fprintf(stdout, "\r\nNew client #%d registered.\r\n", clientID);

        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);

        prv_dump_client(targetP);
        break;

    case COAP_202_DELETED:
        fprintf(stdout, "\r\nClient #%d unregistered.\r\n", clientID);
        break;

    case COAP_204_CHANGED:
        fprintf(stdout, "\r\nClient #%d updated.\r\n", clientID);

        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);

        prv_dump_client(targetP);
        break;

    default:
        fprintf(stdout, "\r\nMonitor callback called with an unknown status: %d.\r\n", status);
        break;
    }

    fprintf(stdout, "\r\n ");
    fflush(stdout);
}

void handle_sigint(int signum)
{
    (void)signum;
    g_quit = true;
}


int main()
{
    int sock;
    struct timeval tv;
    int result;
    lwm2m_context_t * lwm2mH = NULL;
    connection_t * connList = NULL;
    int addressFamily = AF_INET6;
    const char * localPort = LWM2M_STANDARD_PORT_STR;
    lwm2m_set_coap_block_size(512);
    int num_calls = 0;

    sock = create_socket(localPort, addressFamily);
    if (sock < 0)
    {
        fprintf(stderr, "Error opening socket: %d\r\n", errno);
        return -1;
    }

    lwm2mH = lwm2m_init(NULL);
    if (NULL == lwm2mH)
    {
        fprintf(stderr, "lwm2m_init() failed\r\n");
        return -1;
    }

    signal(SIGINT, handle_sigint);

    fprintf(stdout, "lwm2m test server started");
    lwm2m_set_monitoring_callback(lwm2mH, prv_monitor_callback, NULL);

    const time_t timeout = time(NULL) + 10;
    while (!g_quit)
    {

        tv.tv_sec = 60;
        tv.tv_usec = 0;

        result = lwm2m_step(lwm2mH, &(tv.tv_sec));
        if (result != 0)
        {
            fprintf(stderr, "lwm2m_step() failed: 0x%X\r\n", result);
            return -1;
        }

        uint8_t buffer[MAX_PACKET_SIZE];
        ssize_t numBytes;

        struct sockaddr_storage addr;
        socklen_t addrLen;

        addrLen = sizeof(addr);
        numBytes = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&addr, &addrLen);
        ++num_calls;

        if (numBytes == -1)
        {
            fprintf(stderr, "Error in recvfrom(): %d\r\n", errno);
        }
        else if (numBytes >= MAX_PACKET_SIZE)
        {
            fprintf(stderr, "Received packet >= MAX_PACKET_SIZE\r\n");
        }
        else
        {
            char s[INET6_ADDRSTRLEN];
            in_port_t port;
            connection_t * connP;
            s[0] = 0;

            struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)&addr;
            inet_ntop(saddr->sin6_family, &saddr->sin6_addr, s, INET6_ADDRSTRLEN);
            port = saddr->sin6_port;

            fprintf(stderr, "%zd bytes received from [%s]:%hu\r\n", numBytes, s, ntohs(port));
            output_buffer(stderr, buffer, (size_t)numBytes, 0);

            connP = connection_find(connList, &addr, addrLen);
            if (connP == NULL)
            {
                connP = connection_new_incoming(connList, sock, (struct sockaddr *)&addr, addrLen);
                if (connP != NULL)
                {
                    connList = connP;
                }
            }
            if (connP != NULL)
            {
                lwm2m_handle_packet(lwm2mH, buffer, (size_t)numBytes, connP);
            }
        }
        if (num_calls == 2) {
            g_quit = true;
        }
    }

    lwm2m_close(lwm2mH);
    close(sock);
    connection_free(connList);

    return 0;
}
