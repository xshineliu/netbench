
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define CONF_PKG_BUFF_SIZE  65536UL
#define CONF_LOOP   3
#define CONF_STEP_SIZE  (10UL * 1024UL * 1024UL * 1024UL) // 10GiB
#define CONF_STEPS      (CONF_STEP_SIZE / CONF_PKG_BUFF_SIZE)


static inline long long unsigned time_ns(struct timespec* const ts) {
        if (clock_gettime(CLOCK_REALTIME, ts)) {
                exit(1);
        }
        return ((long long unsigned) ts->tv_sec) * 1000000000LLU
                + (long long unsigned) ts->tv_nsec;
}


int main(int argc, char *argv[])
{
    int i, sock_desc, n;
    struct sockaddr_in serv_addr;
    char sbuff[CONF_PKG_BUFF_SIZE], rbuff[CONF_PKG_BUFF_SIZE];
    unsigned long long j = 0UL;
    unsigned long long step;

    unsigned long long delta;
    unsigned long long start_ns;
    struct timespec ts;


    if((sock_desc = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        printf("Failed creating socket\n");

    bzero((char *) &serv_addr, sizeof (serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(strtoul(argv[2], NULL, 0));

    if (connect(sock_desc, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
        printf("Failed to connect to server\n");
        return -1;
    }

    printf("Connected successfully - Test starting ...\n");
    start_ns = time_ns(&ts);
    step = 0;

    //while(fgets(sbuff, CONF_PKG_BUFF_SIZE , stdin)!=NULL)
    for(i = 0; i < CONF_LOOP; i++)
    {
        for(j = 0; j < CONF_STEPS * 30UL; j++) {
            n = send(sock_desc, sbuff, CONF_PKG_BUFF_SIZE,0);
            // error checking
            step += n;
            if (step > CONF_STEP_SIZE) {
                delta = time_ns(&ts) - start_ns;
                sprintf(rbuff, "[%d - %llx] Send\t%llu in\t%llus:\t%f GB/s", i, j, step, delta, (double)step / (double)delta);
                fprintf(stderr, "%s\n", rbuff);
                step = 0L;
                start_ns = time_ns(&ts);
            } 
        }

    }

    close(sock_desc);
    return 0;

}

