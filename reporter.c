#include "interface.h"

#include <math.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/timerfd.h>
#include <netdb.h>

#include <termios.h>
#include <syslog.h>

#define BUFFSIZE 4096
#define FIELDBUFSIZE 512

#define handle_error(msg) \
        do { perror(msg); return -1; } while (0)

void Die(char *mess) { perror(mess); exit(1); }

struct ModbusRequest
{
        uint16_t transactionId;
        uint16_t protocolId;
        uint16_t length;
        uint8_t  unitId;
        uint8_t  functionCode;
        uint16_t referenceNumber;
        uint16_t wordCount;
};


enum DataComplete { INCOMPLETE, COMPLETE, COMPLETE_WITH_ERROR };

enum ModBusStatus { NO_CONNECTION, WAIT_FOR_CONNECTION, WAIT_FOR_SEND_REQ, WAIT_FOR_REPLY };

static void logTime(int line)
{
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) == -1) return;
        fprintf(stderr,"===== line %i %li.%9li\n",line,now.tv_sec,now.tv_nsec);
}

static void printHelp(const InitializationData* id, const char* p1data, char* buffer);

static void getField(const char* p1data, const char* field, char* buffer)
{
        const char* c=strstr(p1data,field);
        if (!c)
        {
                *buffer='\0';
                return;
        }
        const char* n=strchr(c,'\n');
        int size=n?n-c:strlen(c);
        if (size>=FIELDBUFSIZE) size=FIELDBUFSIZE-1;
        memcpy(buffer,c,size);
        buffer[size]='\0';
}

static void testCmd(const InitializationData* id, const char* p1data, char* buffer)
{
        sprintf(buffer,"Hoi, dit is een test %s %s %i",__FUNCTION__,__FILE__,__LINE__);
}

static double getP1Value(const char* p1data, const char* field)
{
        double f=NAN;
        const char* c=strstr(p1data,field);
        if (!c) return f;
        /* Find open brace before the newline */
        const char *newline=strchr(c,'\n'), *openbrace=strchr(c,'(');
        if (newline && openbrace && openbrace<newline)
        {
                f=atof(openbrace+1);
        }
        return f;
}

static float toFloat(const char* p_field)
{
        float f=0.;
        const char* num=strchr(p_field,'(');
        if (num && *++num)
        {
                f=atof(num);
        }
        return f;
}

static void compute10SAvg(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        float power=0;
        getField(p1data,"1-0:21.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:41.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:61.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        sprintf(buffer,"%f",power*1000.);
}

static void returnPowerCurUse(const InitializationData* id, const char* p1data, char* buffer)
{
        compute10SAvg(id,p1data,buffer);
}

static void returnPowerCurProd(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        float power=0;
        getField(p1data,"1-0:22.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:42.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:62.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        sprintf(buffer,"%f",power*1000.);
}

static void returnPowerCurNet(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        float power=0;
        getField(p1data,"1-0:21.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:41.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:61.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        getField(p1data,"1-0:22.7.0",tmpbuf);
        power-=toFloat(tmpbuf);
        getField(p1data,"1-0:42.7.0",tmpbuf);
        power-=toFloat(tmpbuf);
        getField(p1data,"1-0:62.7.0",tmpbuf);
        power-=toFloat(tmpbuf);
        sprintf(buffer,"%f",power*1000.);
}

static void returnNetSinceStart(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        getField(p1data,"1-0:1.8.1",tmpbuf);
        const float totalPowerUsed1     = toFloat(tmpbuf);
        getField(p1data,"1-0:1.8.2",tmpbuf);
        const float totalPowerUsed2     = toFloat(tmpbuf);
        getField(p1data,"1-0:2.8.1",tmpbuf);
        const float totalPowerProduced1 = toFloat(tmpbuf);
        getField(p1data,"1-0:2.8.2",tmpbuf);
        const float totalPowerProduced2 = toFloat(tmpbuf);
        sprintf(buffer,"%f",totalPowerUsed1+totalPowerUsed2-totalPowerProduced1-totalPowerProduced2);
}

static void returnTotalSinceStart(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        getField(p1data,"1-0:1.8.1",tmpbuf);
        const float totalPower1=toFloat(tmpbuf);
        getField(p1data,"1-0:1.8.2",tmpbuf);
        const float totalPower2=toFloat(tmpbuf);
        sprintf(buffer,"%f",totalPower1+totalPower2);
}

static void returnTotalProducedSinceStart(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        getField(p1data,"1-0:2.8.1",tmpbuf);
        const float totalPower1=toFloat(tmpbuf);
        getField(p1data,"1-0:2.8.2",tmpbuf);
        const float totalPower2=toFloat(tmpbuf);
        sprintf(buffer,"%f",totalPower1+totalPower2);
}

static void totalGas(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        const char *brace1=0, *brace2=0;
        float value=0;
        getField(p1data,"0-1:24.2.1",tmpbuf);
        /* Gas field is special: first field is timestamp of measurement, second is actual field */
        brace1=strchr(tmpbuf,')');
        if (brace1) brace2=strchr(brace1,'(');
        if (brace2 && *(brace2+1)) value=atof(brace2+1);
        sprintf(buffer,"%f",value);
}

static void voltage(const InitializationData* id, const char* p1data, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        getField(p1data,"1-0:32.7.0",tmpbuf);
        const float voltage=toFloat(tmpbuf);
        sprintf(buffer,"%f",voltage);
}

static float value(const char* p1data, const char* field, const float scale)
{
        char tmpbuf[FIELDBUFSIZE];
        getField(p1data,field,tmpbuf);
        return toFloat(tmpbuf) * scale;
}

static void showAll(const InitializationData* id, const char* p1data, char* buffer)
{
        memcpy(buffer,p1data,BUFFSIZE);       
}

static void netConsumption(const char* p1data, const uint16_t* modbusData, const struct timespec modbusUpdateTime, const struct timespec p1UpdateTime, char* buffer)
{
        char tmpbuf[FIELDBUFSIZE];
        double power=0;
        /* Actual consumption reported by P1 */
        getField(p1data,"1-0:1.7.0",tmpbuf);
        power+=toFloat(tmpbuf);
        /* Or if not, actual production reported by P1 */
        getField(p1data,"1-0:2.7.0",tmpbuf);
        power-=toFloat(tmpbuf);
        SunSpecValue* ssv=getParam(40084);
        const uint16_t* data=modbusData+ssv->valueFieldNr-modbusBase;
        const double sunSpecProd=ssv->calcFn(data,ssv->scaleFieldOffset);
        power*=1000;
        power+=sunSpecProd;
        sprintf(buffer,"%f",power);
}

static void jsonOutput(const char* p1data, const uint16_t* modbusData, const struct timespec modbusUpdateTime, const struct timespec p1UpdateTime, char* buffer)
{
        /* Get energy used/produced, unit Wh */
        int offset=0;
        const double p1ConsumedTariff1=1000.*getP1Value(p1data,"1-0:1.8.1");
        const double p1ConsumedTariff2=1000.*getP1Value(p1data,"1-0:1.8.2");
        const double p1ProducedTariff1=1000.*getP1Value(p1data,"1-0:2.8.1");
        const double p1ProducedTariff2=1000.*getP1Value(p1data,"1-0:2.8.2");
        const double sunSpecProduced  =getSunSpecValue(modbusData,40094);
        /* Get power, unit W */
        const double p1Consuming      =1000.*getP1Value(p1data,"1-0:1.7.0");
        const double p1Producing      =1000.*getP1Value(p1data,"1-0:2.7.0");
        const double sunSpecProducing =getSunSpecValue(modbusData,40084);
        offset+=sprintf(buffer+offset,"{ \"energy\": { \"unit\":\"Wh\",");
        offset+=sprintf(buffer+offset,"\"p1consumedtariff1\":%F,",p1ConsumedTariff1);
        offset+=sprintf(buffer+offset,"\"p1consumedtariff2\":%F,",p1ConsumedTariff2);
        offset+=sprintf(buffer+offset,"\"p1producedtariff1\":%F,",p1ProducedTariff1);
        offset+=sprintf(buffer+offset,"\"p1producedtariff2\":%F,",p1ProducedTariff2);
        offset+=sprintf(buffer+offset,"\"sunspecproduced\":%F",sunSpecProduced);
        offset+=sprintf(buffer+offset,"}, \"power\": { \"unit\":\"W\",");
        offset+=sprintf(buffer+offset,"\"p1consuming\":%F,",p1Consuming);
        offset+=sprintf(buffer+offset,"\"p1producing\":%F,",p1Producing);
        offset+=sprintf(buffer+offset,"\"sunspecproducing\":%F,",sunSpecProducing);
        offset+=sprintf(buffer+offset,"\"netconsuming\":%F",p1Consuming+sunSpecProducing-p1Producing);
        offset+=sprintf(buffer+offset,"}, \"p1timestamp\":%li.%09li,",p1UpdateTime.tv_sec,p1UpdateTime.tv_nsec);
        offset+=sprintf(buffer+offset,"\"modbustimestamp\":%li.%09li ",modbusUpdateTime.tv_sec,modbusUpdateTime.tv_nsec);
        offset+=sprintf(buffer+offset,"}");
}

static void computeLastDayAvg(const InitializationData* id, char* buffer)
{
        strcpy(buffer,"last day avg: not implemented");
}

typedef void(*ComputeFn)(const InitializationData* id, const char* p1data, char* buffer);
typedef void(*ComputeFnP1Modbus)(const char* p1data, const uint16_t* modbusData,  const struct timespec modbusUpdateTime, const struct timespec p1UpdateTime, char* buffer);

/* Requires specific functionality, with only P1 data */
typedef struct 
{
        const char* fnName;
        ComputeFn computeFunction;
        const char* description;
} Command;

/* Requires specific functionality, with both P1 and modbus data */
typedef struct
{
        const char* fnName;
        ComputeFnP1Modbus computeFunction;
        const char* description;
} CombinedCommand;

/* Extract direct value from P1 telegram */
typedef struct
{
        const char* fnName;
        const char* fieldName;
        const char* description;
        float       scale;
} CommandMap;

const Command cmd[] = {
        { "help",         printHelp                     , "show this help"},
        { "10s",          compute10SAvg                 , "current power usage" },
        { "test",         testCmd                       , "simple test command" },
        { "volt",         voltage                       , "voltage L1" },
        { "gas",          totalGas                      , "total gas used" },
        { "net",          returnNetSinceStart           , "net total used (curused - curproduced)" },
        { "cur",          returnTotalSinceStart         , "total used" },
        { "curused",      returnTotalSinceStart         , "total used" },
        { "curproduced",  returnTotalProducedSinceStart , "total produced" },
        { "pcuruse",      returnPowerCurUse             , "power currently used (W)" },
        { "pcurprod",     returnPowerCurProd            , "power currently produced (W)" },
        { "pcurnet",      returnPowerCurNet             , "power currently net used (used - produced) (W)" },
        { "all",          showAll                       , "complete telegram" },
        { 0, 0, 0 }
};

const CombinedCommand combinedCmd[] = {
        { "consumption", netConsumption, "Consumption: Net P1 usage - SunSpec production (W)" },
        { "json", jsonOutput, "json output of relevant fields" },
/*        { "production",  netProduction , "Production reported by SunSpec (W)" },
        { "consumption", 0, 0 },
          { "production", 0, 0 }, */
        { 0, 0, 0 }
};

const CommandMap cmdMap[] = {
        { "VL1",          "1-0:32.7.0", "Voltage L1"                              ,1    },
        { "VL2",          "1-0:52.7.0", "Voltage L2"                              ,1    },
        { "VL3",          "1-0:72.7.0", "Voltage L3"                              ,1    },
        { "PL1+",         "1-0:21.7.0", "Power L1 consumption"                    ,1000 },
        { "PL2+",         "1-0:41.7.0", "Power L2 consumption"                    ,1000 },
        { "PL3+",         "1-0:61.7.0", "Power L3 consumption"                    ,1000 },
        { "PL1-",         "1-0:22.7.0", "Power L1 production"                     ,1000 },
        { "PL2-",         "1-0:42.7.0", "Power L2 production"                     ,1000 },
        { "PL3-",         "1-0:62.7.0", "Power L3 production"                     ,1000 },
        { "gastime",      "0-1:24.2.1", "Time when gas measurement took place"    ,1    },
        { "usagetariff1", "1-0:1.8.1",  "Total Electricity usage tariff 1"        ,1    },
        { "usagetariff2", "1-0:1.8.2",  "Total Electricity usage tariff 2"        ,1    },
        { "timestamp",    "0-0:1.0.0",  "Timestamp when telegram was measured"    ,1    },
        { 0,0,0,0 }
};

static void printHelp(const InitializationData* id, const char* p1data, char* buffer)
{
        int offset=0;
        const Command*   cmd1;
        const CommandMap* cmd2;
        const SunSpecValue* ssv;
        char format[32];
        offset+=sprintf(buffer+offset,"\nP1 commands:\n");

        int width=0;
        for(cmd1=cmd;cmd1->fnName;cmd1++)
        {
                const int len=strlen(cmd1->fnName);
                if (len>width) width=len;
        }
        for(cmd2=cmdMap;cmd2->fnName;cmd2++)
        {
                const int len=strlen(cmd2->fnName);
                if (len>width) width=len;
        }

        sprintf(format,"%%-%is %%s\n",width);
        for(cmd1=cmd;cmd1->fnName;cmd1++)
        {
                offset+=sprintf(buffer+offset,format,cmd1->fnName,cmd1->description);
        }
        for(cmd2=cmdMap;cmd2->fnName;cmd2++)
        {
                offset+=sprintf(buffer+offset,format,cmd2->fnName,cmd2->description);
        }
        offset+=sprintf(buffer+offset,"SunSpec commands:\n");
        sprintf(format,"%%-%is %%-4s %%s\n",width);
        offset+=sprintf(buffer+offset,format,"Field","Unit","Description");
        sprintf(format,"%%-%ii %%-4s %%s\n",width);
        for (ssv=getParam(0); ssv->valueFieldNr;ssv++)
        {
                offset+=sprintf(buffer+offset,format,ssv->valueFieldNr,ssv->unit,ssv->description);
        }
}

void handleCommand(const InitializationData* id, const uint16_t* modbusData, const char* p1data, const struct timespec modbusUpdateTime, const struct timespec p1UpdateTime, char* buffer)
{
        const Command*   command;
        const CommandMap* cmdmap;
        const CombinedCommand* combCmd;
        char* buf2;
        for (buf2=buffer; *buf2 ; buf2++)
        {
                if (*buf2=='\n') *buf2='\0';
        }
        if (id->debug) fprintf(stderr,"Got command '%s'\n",buffer);
        const int cmdNumber=atoi(buffer);
        for (command=cmd; command->fnName; command++)
        {
                if (strcmp(command->fnName, buffer)==0)
                {
                        if (id->debug) fprintf(stderr,"Doing command '%s'\n", command->fnName);
                        return command->computeFunction(id,p1data,buffer);
                }
        }
        for (cmdmap=cmdMap; cmdmap->fnName; cmdmap++)
        {
                if (strcmp(cmdmap->fnName,buffer)==0)
                {
                        const float val=value(p1data,cmdmap->fieldName,cmdmap->scale);
                        if (id->debug) fprintf(stderr,"Doing command '%s'\n", cmdmap->fnName);
                        sprintf(buffer,"%f",val);
                        return;
                }
        }
        for (combCmd=combinedCmd;combCmd->fnName;combCmd++)
        {
                if (strcmp(combCmd->fnName,buffer)==0)
                {
                        return combCmd->computeFunction(p1data,modbusData,modbusUpdateTime,p1UpdateTime,buffer);
                }
        }
        SunSpecValue* ssv=getParam(cmdNumber);
        if (cmdNumber && ssv && modbusData)
        {
                const uint16_t* data=modbusData+ssv->valueFieldNr-modbusBase;
                const double val=ssv->calcFn(data,ssv->scaleFieldOffset);
                if (id->debug) fprintf(stderr,"Doing sunspec cmd %i %s %f %i %i %i\n", cmdNumber,ssv->description,val,ssv->valueFieldNr,modbusBase, ssv->scaleFieldOffset);
                sprintf(buffer,"%f", val);
                return;
        }
        strcpy(buffer,"not implemented, run 'help' for an overview");
}

static int setupTcpSocket(const int port)
{
        int sock;
        struct sockaddr_in6 echoserver;
        unsigned serverlen;

        /* Create the TCP socket */
        if ((sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
        {
                Die("Failed to create socket");
        }
        int on=1;
        setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );
        /* Construct the server sockaddr_in structure */
        memset(&echoserver, 0, sizeof(echoserver));       /* Clear struct */
        echoserver.sin6_family = AF_INET6;                  /* Internet/IP */
        echoserver.sin6_addr=in6addr_any;   /* Any IP address */
        echoserver.sin6_port = htons(port);       /* server port */

        /* Bind the socket */
        serverlen = sizeof(echoserver);
        if (bind(sock, (struct sockaddr *) &echoserver, serverlen) < 0)
        {
                Die("Failed to bind server socket");
        }

        const int backlog=5;
        if (listen(sock,backlog)==-1)
        {
                Die("Failed to set socket into listen mode");
        }
        return sock;
}



static int setupUdpSocket(const int port)
{
        int sock;
        struct sockaddr_in6 echoserver;
        unsigned serverlen;

        /* Create the UDP socket */
        if ((sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        {
                Die("Failed to create socket");
        }
        int on=1;
        setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );
        /* Construct the server sockaddr_in structure */
        memset(&echoserver, 0, sizeof(echoserver));       /* Clear struct */
        echoserver.sin6_family = AF_INET6;                  /* Internet/IP */
        echoserver.sin6_addr=in6addr_any;   /* Any IP address */
        echoserver.sin6_port = htons(port);       /* server port */

        /* Bind the socket */
        serverlen = sizeof(echoserver);
        if (bind(sock, (struct sockaddr *) &echoserver, serverlen) < 0)
        {
                Die("Failed to bind server socket");
        }
        return sock;
}



int openP1Device(const char* p_dev)
{
        int fd=-1;
        const char* bashtcp="/dev/tcp/";
        if (strncmp(bashtcp,p_dev,strlen(bashtcp))==0)
        {
                char* newHost=strdup(p_dev+strlen(bashtcp));
                char* port;
                char* slash=strchr(newHost,'/');
                if (slash) {
                        port=slash+1;
                        *slash='\0';
                } else {
                        goto out2;
                }
                struct addrinfo* ai;
                struct addrinfo hints;
                bzero(&hints,sizeof(hints));
                hints.ai_socktype=SOCK_STREAM;
                hints.ai_family=AF_INET6;
                hints.ai_flags=AI_V4MAPPED;
                int res=getaddrinfo(newHost,port,&hints,&ai);
                if (res)
                {
                        fprintf(stderr,"Error getting address for %s %s %s: %s\n",p_dev,newHost,port,gai_strerror(res));
                        goto out2;
                }

                fd=socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
                if (fd<0)
                {
                        perror("creating socket");
                        fd=-1;
                        goto out1;
                }
                if (connect(fd,ai->ai_addr,ai->ai_addrlen))
                {
                        perror("Connect");
                        fd=-1;
                        goto out1;
                }

out1:

                freeaddrinfo(ai);
out2:
                free(newHost);


        } else {
                fd=open(p_dev,O_RDONLY|O_NOCTTY);
                if (fd==-1) return fd;

                struct termios p;
                memset(&p,0,sizeof(p));
                if (tcgetattr(fd,&p)==-1) { close(fd); return -1; }

                if (cfsetspeed(&p,B115200)==-1) return -1;
                cfmakeraw(&p);

                if (tcsetattr(fd,TCSANOW,&p)==-1) { close(fd); return -1; }
        }
        return fd;
}



static int handleUserQuery(const InitializationData* id, const uint16_t* modbusData, const char* p1data, const struct timespec modbusUpdateTime, const struct timespec p1UpdateTime, const int sock)
{
        char buffer[BUFFSIZE];
        unsigned int echolen;
        struct sockaddr_in6 echoclient;
        int received = 0;
        int len;
        /* Receive a message from the client */
        unsigned clientlen = sizeof(struct sockaddr_in6);
        if ((received = recvfrom(sock, buffer, BUFFSIZE, 0,
                                        (struct sockaddr *) &echoclient,
                                        &clientlen)) < 0)
        {
                Die("Failed to receive message");
                return 1;
        }
        char ipaddr[400];
        const char* resIpAddr=inet_ntop(AF_INET6, &echoclient.sin6_addr, ipaddr, 399 );
        if (resIpAddr)
        {
                if (id->debug) fprintf(stderr, "Client connected: %s\n", resIpAddr);
        } else {
                perror("getting ip address");
        }
        /* Send the message back to client */
        handleCommand(id, modbusData, p1data, modbusUpdateTime, p1UpdateTime, buffer);
        len=strlen(buffer);
        if (sendto(sock, buffer, len, 0,
                                (struct sockaddr *) &echoclient,
                                sizeof(echoclient)) != len)
        {
                Die("Mismatch in number of echo'd bytes");
                return 1;
        }
        return 0;
}



static int acceptConnection(int p_fd, int* p_tcpconnections, const int p_maxconns)
{
        /* Find free connection */
        int i;
        int newsock=accept(p_fd,0,0);
        if (newsock==-1) return -1;

        for (i=0;i<p_maxconns;i++)
        {
                if (p_tcpconnections[i]==-1)
                {
                        p_tcpconnections[i]=newsock;
                        break;
                }
        }
        if (i==p_maxconns)
        {
                /* Too many open sockets, reject */
                close(newsock);
                newsock=-1;
        }
        return newsock;
}



static void closeConnection(int p_fd, int* p_tcpconnections, const int p_maxconns)
{
        int i;
        close(p_fd);
        for (i=0;i<p_maxconns;i++)
        {
                if (p_tcpconnections[i]==p_fd)
                {
                        p_tcpconnections[i]=-1;
                        return;
                }
        }
}



static enum DataComplete dataComplete(const char* p_p1data, const int len)
{
        /* First find the exclamation mark, start of the signature */
        const char* excl=strchr(p_p1data,'!');
        if (excl==0) return INCOMPLETE;
        /* After the '!', there are 4 digits, CR, LF, so len should be 7 ahead of excl */
        const char* lf=strchr(excl,'\n');
        if (!lf || (lf-excl)!=6) return INCOMPLETE;
        /* Full amount of data read, check the checksum , still to be
         * implemented ... */
        
        return COMPLETE;
}



static int startModbusConnection(InitializationData* id)
{
        /* Return value: -1 in case of failure, fd with non-blocking socket in
         * connecting mode to modbus device */
        if (id->modbusAddr==0) return -1;
        int sock=socket(id->ai_family, id->ai_socktype | SOCK_NONBLOCK, id->ai_protocol);
        if (sock<0) return -1;
        
        int conn=connect(sock,id->modbusAddr, id->modbusAddrlen);
        if (conn==0 || errno==EINPROGRESS) return sock;

        close(sock);
        return -1;
}



static int readSerialData(InitializationData* id, const int fd, const int p1size, int* p1TmpCount, char* p1tmpdata)
{
        int bytesToRead=p1size-1-*p1TmpCount;
        if (bytesToRead==0)
        {
                // Buffer fully read but still nothing, start again
                *p1TmpCount=0;
                bytesToRead=p1size-1;
                syslog(LOG_INFO,"corrupted data read from p1 port");
        }
        int bytesRead=read(fd,p1tmpdata+*p1TmpCount,bytesToRead);
        if (id->debug) fprintf(stderr,"Got %i bytes from serial port\n",bytesRead);
        if (bytesRead>0)
        {
                *p1TmpCount+=bytesRead;
                p1tmpdata[*p1TmpCount]='\0';
                if (dataComplete(p1tmpdata,*p1TmpCount)!=INCOMPLETE)
                {
                        return 0;
                } else {
                        if (id->debug) fprintf(stderr,"Data not yet complete\n");
                }
        } else {
                /* Something went wrong. Close the serial port and open it again. */
                int errornr=errno;
                close(id->serialDeviceFd);
                const char* msg="P1 serial device closed, reopening";
                const char* errmsg=strerror(errornr);
                syslog(LOG_INFO,"%s %s", msg, errmsg);

                id->serialDeviceFd=openP1Device(id->serialDeviceName);
        }
        /* Not complete */
        return 1;
}



static enum ModBusStatus readModbusData(const int debug, const int modbusfd, const short revents, const enum ModBusStatus modbusStatus, uint16_t* modbusData, uint16_t** activeModBusData, struct timespec* modbusUpdateTime)
{
        if (debug)
        {
                fprintf(stderr,"modbusstatus 3: %i\n", modbusStatus);
        }
        /* Only modify the state somewhere inside this function, so we start
         * with the original state here */
        enum ModBusStatus nextStatus=modbusStatus;
        switch (modbusStatus)
        {
                case WAIT_FOR_CONNECTION:
                        if (revents & POLLOUT)
                        {
                                /* connect() finished, check
                                 * if it succeeeded */
                                if(debug) fprintf(stderr,"modbus connect returned\n");
                                const int level=SOL_SOCKET, optname=SO_ERROR;
                                int optval; socklen_t optlen=sizeof(int);
                                const int sockoptret=getsockopt(modbusfd,level,optname,&optval,&optlen);
                                if(debug)
                                {
                                        fprintf(stderr,"getsockopt: %i optval %i\n",sockoptret,optval);
                                        errno=optval;
                                        perror("connect to solaredge");
                                }
                                if (sockoptret==0 && optval==0)
                                {
                                        if(debug) fprintf(stderr,"modbus got connected\n");
                                        /* Success connecting,
                                         * proceed */
                                        nextStatus=WAIT_FOR_SEND_REQ;
                                } else {
                                        /* Error, close socket
                                         * */
                                        close(modbusfd);
                                        if (debug) fprintf(stderr,"closed modbusfd: fd=%i\n",modbusfd);
                                        nextStatus=NO_CONNECTION;
                                }
                        }
                        break;
                case WAIT_FOR_SEND_REQ:
                        if (revents & POLLOUT)
                        {
                                const struct ModbusRequest mbr = { 
                                        .transactionId=htons(1),
                                        .protocolId=htons(0),
                                        .length=htons(6),
                                        .unitId=1,
                                        .functionCode=3,
                                        .referenceNumber=htons(40000),
                                        .wordCount=htons(modbusRegCount)
                                };
                                if (write(modbusfd,&mbr,sizeof(mbr))==sizeof(mbr))
                                {
                                        nextStatus=WAIT_FOR_REPLY;
                                } else {
                                        close(modbusfd);
                                        if (debug) fprintf(stderr,"closed modbusfd: fd=%i\n",modbusfd);
                                        nextStatus=NO_CONNECTION;
                                }
                        }
                        break;
                case WAIT_FOR_REPLY:
                        if (revents & POLLIN)
                        {
                                uint16_t* buf=(*activeModBusData==modbusData)?modbusData+modbusRegCount:modbusData;
                                const int modbusHeaderSize=9; /* Modbus reply header size, content not relevant for us now */
                                char tmpbuf[modbusHeaderSize];
                                if (read(modbusfd,tmpbuf,modbusHeaderSize)!=modbusHeaderSize)
                                {
                                        close(modbusfd);
                                        nextStatus=NO_CONNECTION;
                                }
                                if (read(modbusfd,buf,sizeof(uint16_t)*modbusRegCount)==sizeof(uint16_t)*modbusRegCount)
                                {
                                        clock_gettime(CLOCK_REALTIME,modbusUpdateTime);
                                        *activeModBusData=buf;
                                }
                                close(modbusfd);
                                if (debug) fprintf(stderr,"closed modbusfd after read: fd=%i\n",modbusfd);
                                nextStatus=NO_CONNECTION;
                        }
                        break;
                default:
                        break;
        }
        return nextStatus;
}



static int setupModbusTimer(const int timeout)
{
        int fd=-1;
        struct itimerspec new_value;
        struct timespec now;
        uint64_t exp, tot_exp;
        ssize_t s;


        if (clock_gettime(CLOCK_REALTIME, &now) == -1)
                handle_error("clock_gettime");

        /* Create a CLOCK_REALTIME absolute timer with initial expiration 1s
         * from now, and interval as specified in argument */

        new_value.it_value.tv_sec = now.tv_sec+1;
        new_value.it_value.tv_nsec = now.tv_nsec;
        new_value.it_interval.tv_sec = timeout;
        new_value.it_interval.tv_nsec = 0;

        fd = timerfd_create(CLOCK_REALTIME, 0);
        if (fd == -1)
                handle_error("timerfd_create");

        if (timerfd_settime(fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)
                handle_error("timerfd_settime");

        return fd;
}



int reporter(InitializationData* id) {
        const int udpsock=setupUdpSocket(id->port);
        const int tcpsock=setupTcpSocket(id->port);
        const int maxConns=50;
        const int p1size=BUFFSIZE;
        int i;
        int gotNewP1=0;
        int p1TmpCount=0;
        int timerfd=-1;

        struct timespec p1UpdateTime, modbusUpdateTime;
        bzero(&p1UpdateTime,sizeof(struct timespec));
        bzero(&modbusUpdateTime,sizeof(struct timespec));

        enum ModBusStatus modbusStatus=NO_CONNECTION;

        const int syslogopt=0;
        const int syslogfacility=0;



        /* Setup arrays for modbus data. We have two arrays (allocated as one
         * big of 2 * modbusRegCount), one is active, the other is to load
         * data into with read(2). In case the read succeeds, we switch the
         * active pointer to the new buffer, alternatively, the active pointer
         * becomes 0. Then we always have either valid data or no data */

        uint16_t* modbusData = malloc(2*sizeof(uint16_t)*modbusRegCount);
        if (!modbusData)
        {
                perror("malloc, exiting");
                exit(1);
        }

        uint16_t *activeModBusData=0; 


        if (id->modbusAddr)
        {
                /* In case we have modbus active, setup the timer, to
                 * specified # of seconds */ 
                timerfd=setupModbusTimer(1);
        }

        openlog("powermonitor",syslogopt,syslogfacility);

        int* tcpconnections=malloc(maxConns*sizeof(int));
        for (i=0;i<maxConns;i++) tcpconnections[i]=-1;

        char* p1data=malloc(p1size);
        char* p1tmpdata=malloc(p1size);
        sprintf(p1data,"Uninitialized\n");
        sprintf(p1tmpdata,"Uninitialized\n");
        struct pollfd* pfd=malloc((maxConns+3)*sizeof(struct pollfd));
        struct pollfd pollData; /* Object used for preparing contents of pollfd array */

        if (id->debug) fprintf(stderr,"tcpsocket=%i udpsock=%i serialfd=%i\n",tcpsock,udpsock,id->serialDeviceFd);
        int modbusfd=-1;

        while (1)
        {
                int wroteDataToTcp=0;
                int p=0;
                int modbuspollpos=-1;
                int timerfdpollpos=-1;
                int p1DevicePollPos=-1;
                int tcpConnectionOffset=0;
                /* Prepare array pfd */
                if (id->debug) logTime(__LINE__);
                pollData.fd=udpsock,pollData.events=POLLIN, pollData.revents=0; /* UDP socket for command handling */
                pfd[p++]=pollData;
                pollData.fd=tcpsock,pollData.events=POLLIN, pollData.revents=0; /* TCP socket to accept connections from */
                pfd[p++]=pollData;
                if (id->serialDeviceFd>=0)
                {
                        pollData.fd=id->serialDeviceFd,pollData.events=POLLIN, pollData.revents=0; /* Serial fd */
                        p1DevicePollPos=p;   
                        pfd[p++]=pollData;
                }
                if (timerfd>=0)
                {
                        if (id->debug) fprintf(stderr,"adding timerfd %i\n", timerfd);
                        timerfdpollpos=p;
                        pollData.fd=timerfd, pollData.events=POLLIN, pollData.revents=0; /* modbus timer fd */
                        pfd[p++]=pollData;
                }
                switch (modbusStatus)
                {
                        case WAIT_FOR_REPLY:
                                if (id->debug) fprintf(stderr,"modbus waitforreply\n");
                                modbuspollpos=p;
                                pollData.fd=modbusfd; pollData.events=POLLIN, pollData.revents=0;
                                pfd[p++]=pollData;
                                break;
                        case WAIT_FOR_SEND_REQ:
                                if (id->debug) fprintf(stderr,"modbus waitforsend\n");
                                modbuspollpos=p;
                                pollData.fd=modbusfd; pollData.events=POLLOUT, pollData.revents=0;
                                pfd[p++]=pollData;
                                break;
                        case WAIT_FOR_CONNECTION:
                                if (id->debug) fprintf(stderr,"modbus waitforconnection\n");
                                modbuspollpos=p;
                                pollData.fd=modbusfd; pollData.events=POLLOUT, pollData.revents=0;
                                pfd[p++]=pollData;
                                break;
                        default:
                                break;
                }
                if (id->debug)
                {
                        fprintf(stderr,"modbusstatus a: %i, p=%i\n", modbusStatus,p);
                }
                for (i=0;i<maxConns && gotNewP1;i++)
                {
                        /* TCP sockets to write data on */
                        if (tcpconnections[i]!=-1)
                        {
                                pollData.fd=tcpconnections[i],pollData.events=(gotNewP1?POLLOUT:0)|POLLIN, pollData.revents=0;
                                pfd[p++]=pollData;
                        }
                }

                /* Poll invocation */
                for (int j=0;id->debug && j<p; ++j)
                {
                        fprintf(stderr,"poll %i fd %i events %i\n", j, pfd[j].fd, pfd[j].events);
                }
                const int pollResult=poll(pfd,p,-1);
                for (int j=0;id->debug && j<p; ++j)
                {
                        fprintf(stderr,"poll %i fd %i revents %i\n", j, pfd[j].fd, pfd[j].revents);
                }
                if (pollResult==0) continue;
                if (pollResult==-1)
                {
                        perror("poll");
                        return -1;
                }
                /* Handle events */
                tcpConnectionOffset++;
                /* Read UDP command */
                if (pfd[0].revents & POLLIN)
                {
                        handleUserQuery(id,activeModBusData,p1data,modbusUpdateTime,p1UpdateTime,pfd[0].fd);
                }
                tcpConnectionOffset++;
                /* Accept new tcp connection */
                if (pfd[1].revents & POLLIN)
                {
                        const int newsock=acceptConnection(pfd[1].fd,tcpconnections,maxConns);
                        if (id->debug) fprintf(stderr,"accepted new tcp connection, fd=%i\n",newsock);
                }
                tcpConnectionOffset++;
                /* Read P1 data from Serial port */
                if (p1DevicePollPos>=0)
                {
                        tcpConnectionOffset++;
                        if (pfd[2].revents & POLLIN)
                        {
                                if (readSerialData(id, pfd[2].fd, p1size, &p1TmpCount, p1tmpdata)==0)
                                {
                                        /* Succesful read, so swap the two buffers */
                                        gotNewP1=p1TmpCount;
                                        char* tmp=p1tmpdata;
                                        p1tmpdata=p1data;
                                        p1data=tmp;
                                        p1TmpCount=0;
                                        clock_gettime(CLOCK_REALTIME,&p1UpdateTime);
                                        if (id->debug) fprintf(stderr,"Data complete, swapping\n");
                                        if (id->debug) fprintf(stderr,"Data:\n%s\n",p1data);
                                }
                        }
                }
                /* Read timer event, if enabled */
                if (timerfdpollpos>=0)
                {
                        tcpConnectionOffset++;
                        if (pfd[timerfdpollpos].revents & POLLIN)
                        {
                                uint64_t val;
                                int r=read(pfd[timerfdpollpos].fd,&val,sizeof(uint64_t));
                                if (modbusStatus==NO_CONNECTION)
                                {
                                        modbusfd=startModbusConnection(id);
                                        if (modbusfd>=0) modbusStatus=WAIT_FOR_CONNECTION;
                                        if (id->debug) fprintf(stderr,"Initiated new modbus connection: %i\n",modbusfd);
                                }
                        }
                }
                /* Handle modbus data */
                if (modbuspollpos>=0)
                {
                        modbusStatus=
                                readModbusData(id->debug, pfd[modbuspollpos].fd,
                                                pfd[modbuspollpos].revents,
                                                modbusStatus,modbusData,&activeModBusData,
                                                &modbusUpdateTime);
                        tcpConnectionOffset++;
                }
                /* Handle tcp connections */
                for (i=tcpConnectionOffset;i<p;i++)
                {
                        if (pfd[i].revents & POLLHUP)
                        {
                                closeConnection(pfd[i].fd,tcpconnections,maxConns);
                        }
                        if (pfd[i].revents & POLLIN)
                        {
                                char buffer[1024];
                                if (read(pfd[i].fd,buffer,1024)==0)
                                        closeConnection(pfd[i].fd,tcpconnections,maxConns);
                        }
                        if (pfd[i].revents & POLLOUT)
                        {
                                wroteDataToTcp=1;
                                int written=write(pfd[i].fd,p1data,gotNewP1);
                                if (written<gotNewP1) closeConnection(pfd[i].fd,tcpconnections,maxConns);
                        }
                }
                if (wroteDataToTcp) gotNewP1=0;
        }
        closelog();
        free(pfd);
        free(p1tmpdata);
        free(p1data);
        free(tcpconnections);
        return 0;
}
