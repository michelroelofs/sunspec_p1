#include "interface.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>



void usage(const char* toolname)
{
        printf("Usage: %s -s <serial device> [-d] [-p port] [-H <sunspechost> -P <sunspecport>]\n",toolname);
        printf("   Monitor P1 port of power meter. Provide copy via TCP for Domoticz");
        printf("   and simple queries on UDP, default on port 9012.\n");
        printf("   SIGHUP makes the program restart.\n");
        printf("   -d shows debug output.\n");
        printf("   -H <sunspechost> -P <sunspecport> read out data from sunspec modbus device.\n");
        printf("\n");
        printf("   Example: %s -s /dev/ttyAMA0 -p 12345\n",toolname);
}

static char** cmdlineArgs;

static void restartTool(int p_signal)
{
        execvp(cmdlineArgs[0],cmdlineArgs);
}

static void closeConnections()
{
        int i;
        for (i=3;i<1024;++i) close(i);
}

static void closeTty()
{
        int nullfd=open("/dev/null",O_RDWR);
        if (nullfd>=0)
        {
                dup2(nullfd,0);
                dup2(nullfd,1);
                dup2(nullfd,2);
        }
        if (nullfd>2) close(nullfd);
}

static int getModbusAddr(const char* hostname, const char* port, InitializationData* id)
{
        int error=0;
        struct addrinfo hints;
        bzero(&hints,sizeof(struct addrinfo));

        hints.ai_family=AF_INET6;
        hints.ai_socktype=SOCK_STREAM;
        hints.ai_flags=AI_V4MAPPED;

        struct addrinfo* addresses;

        if (getaddrinfo(hostname, port, &hints, &addresses))
        {
                perror("getaddrinfo for modbus server");
                return 1;
        }

        if (!(id->modbusAddr=malloc(addresses->ai_addrlen)))
        {
                error=1;
        } else {
                id->modbusAddrlen=addresses->ai_addrlen;
                memcpy(id->modbusAddr,addresses->ai_addr,addresses->ai_addrlen);
                id->ai_family=addresses->ai_family;
                id->ai_socktype=addresses->ai_socktype;
                id->ai_protocol=addresses->ai_protocol;
        }

        freeaddrinfo(addresses);

        return error;
}

int main(int argc, char** argv)
{
        InitializationData id;
        char* sunspecHost=0;
        char* sunspecPort=0;
        id.debug=0;
        id.port=9012;
        id.modbusAddr=0;
        id.modbusAddrlen=0;

        const char* serialDevice;
        extern char* optarg;
        int opt;

        cmdlineArgs=argv;

        signal(SIGHUP,restartTool);

        /* Cleanup old connections, which may be required in case of restart */
        closelog();
        closeConnections();

        while ((opt=getopt(argc,argv,"s:dp:H:P:"))!=-1)
        {
                switch(opt)
                {
                        case 's':
                                serialDevice=optarg;
                                break;
                        case 'd':
                                id.debug=1;
                                break;
                        case 'p':
                                id.port=atoi(optarg);
                                break;
                        case 'H':
                                sunspecHost=optarg;
                                break;
                        case 'P':
                                sunspecPort=optarg;
                                break;
                        default:
                                usage(argv[0]);
                                return 0;
                                break;

                }
        }
        id.serialDeviceName=serialDevice;
        id.serialDeviceFd=openP1Device(serialDevice);

        getModbusAddr(sunspecHost,sunspecPort,&id);       
        
        return reporter(&id);
}
