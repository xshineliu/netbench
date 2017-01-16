// socket server example, handles multiple clients using threads

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>    //strlen
#include <stdlib.h>    //strlen
#include <sys/syscall.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <pthread.h> //for threading , link with lpthread
#include <sched.h>
#include <time.h>


#define CONF_MAX_THREADS 128
#define CONF_LARGE_PKG_SIZE 0x200000
#define SIZE_LONG_LONG_BYTES    8
// LEVEL1_DCACHE_LINESIZE

#ifndef CACHE_LINE_WIDTH_BYTES
#define CACHE_LINE_WIDTH_BYTES  64
#endif

#define LL_PER_CLINE    (CACHE_LINE_WIDTH_BYTES / SIZE_LONG_LONG_BYTES)


struct thread_parm{
    int sock;
    int thread_idx;
};

//the thread function
void *connection_handler(void *);
pthread_t tid[CONF_MAX_THREADS];
struct thread_parm *tparm[CONF_MAX_THREADS];

// read & write most, avoid cache line issue
unsigned long long counter[CONF_MAX_THREADS][LL_PER_CLINE];

static int conf_num_threads = 1;
static unsigned long long conf_step_size = (10UL * 1024UL * 1024UL * 1024UL); // 10GiB
static int conf_cpubind = 0;
static int conf_server_stat = 0;
static int conf_stat_interval = 5;

//static char conf_address[64] = {'\0',};
static in_addr_t conf_addr = INADDR_ANY;
static unsigned short conf_port = 53000;

static int running_workers = 0;

static inline long long unsigned time_ns(struct timespec* const ts) {
        if (clock_gettime(CLOCK_REALTIME, ts)) {
                exit(1);
        }
        return ((long long unsigned) ts->tv_sec) * 1000000000LLU
                + (long long unsigned) ts->tv_nsec;
}


void taskbind(int i) {
        if(!conf_cpubind) {
                return;
        }
        cpu_set_t my_set;        /* Define your cpu_set bit mask. */
        CPU_ZERO(&my_set);       /* Initialize it all to 0, i.e. no CPUs selected. */
        CPU_SET(i, &my_set);     /* set the bit that represents core 7. */
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set); /* Set affinity of tihs process to */
}


void *progress_worker(void *parm)
{
    int i;
    unsigned long long total = 0L;
    while(!running_workers) {
        sleep (1);
    }

    while(running_workers > 0) {
        sleep(conf_stat_interval);
        total = 0L;
        printf("Received:");
        for(i = 0; i < running_workers; i++) {
            printf("\t%lld", counter[i][0]);
            total += counter[i][0];
            counter[i][0] = 0L;
        }
        // don't need so much accuracy
        printf("\t%lld\t%f GB/s\n", total, (double)total / 1000000000.f / (double)conf_stat_interval );
    }
}


/*
  This will handle connection for each client
  */
void *connection_handler(void *parm)
{
    //Get the socket descriptor
    struct thread_parm *p = (struct thread_parm *)parm;
    int sock = p->sock;
    int id = p->thread_idx;
    int n;
    unsigned long long total = 0, step = 0;
    unsigned long long delta;
    unsigned long long start_ns;
    struct timespec ts;

    char send_buffer[128], client_message[CONF_LARGE_PKG_SIZE];


    start_ns = time_ns(&ts);
    while ((n = recv(sock, client_message, CONF_LARGE_PKG_SIZE, 0)) > 0)
    {
        //counter[id][0] += n;
        __sync_fetch_and_add((unsigned long long *)(counter + id), n);
        if (conf_server_stat) {
            total += n;
            step +=n;

            if(step > conf_step_size) {
                delta = time_ns(&ts) - start_ns;
                sprintf(send_buffer, "Thread %d\treceived\t%llu in\t%llus:\t%f GB/s", id, step, delta, (double)step / (double)delta);
                fprintf(stderr, "%s\n", send_buffer);
                    //send(sock, send_buffer, strlen(send_buffer), 0);
                step = 0L;
                start_ns = time_ns(&ts);
            }
        }
    }


    close(sock);

    if(n == 0)
    {
      fprintf(stderr, "%d\tClient Disconnected", id);
    }
    else
    {
      perror("recv failed");
    }

   __sync_fetch_and_sub(&running_workers, 1);

   return 0;
} 


int main(int argc, char *argv[])
{
    int i;
    int socket_desc, client_sock, c;
    int thread_id = 0, opt;
    struct sockaddr_in server, client;

    pthread_attr_t attr; /* set of thread attributes */
    pthread_t reporter_thread;

    while ((opt = getopt(argc, argv, "n:d:p:cb")) != -1) {
            switch (opt) {
                case 'n':
                    conf_num_threads = strtoul(optarg, NULL, 0);
                    break;
                case 'b':
                    conf_cpubind = 1;
                    break;
                case 'c':
                    conf_server_stat = 1;
                    break;
                case 'd':
                    conf_addr = inet_addr(optarg);
                    break;
                case 'p':
                    conf_port = strtoul(optarg, NULL, 0);
                    break;
                default:
                    exit(EXIT_FAILURE);
            }
    }


    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        fprintf(stderr, "Could not create socket\n");
    }
    fprintf(stderr, "Socket created.\n");

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    //server.sin_addr.s_addr = INADDR_ANY;
    server.sin_addr.s_addr = conf_addr;
    server.sin_port = htons(conf_port);

    //Bind
    if( bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    fprintf(stderr, "bind done.\n");

    //Listen
    listen(socket_desc , SOMAXCONN - 1);

    //Accept and incoming connection
    fprintf(stderr, "Waiting for incoming connections...\n");
 
    if( 0 > (pthread_create(&reporter_thread, NULL, progress_worker, NULL)))
    {
        perror("could not create reporter thread");
        return 1;
    }

    c = sizeof(struct sockaddr_in);

       for(; thread_id < conf_num_threads; thread_id++)
       {
   
            client_sock = accept(socket_desc, (struct sockaddr*)&client, (socklen_t*)&c);
            // error checking

            if (client_sock < 0)
            {
                perror("accept failed");
                return 1;
            }

            fprintf(stderr, "Connection accepted: %x\n", client);

            tparm[thread_id] = malloc(sizeof(struct thread_parm));
            // delete in ...
            struct thread_parm* p = tparm[thread_id];

            p->sock = client_sock;
            p->thread_idx = thread_id;

            if (0 > (pthread_create(tid + thread_id, NULL, connection_handler, (void*) p)))
            {
                perror("could not create thread");
                return 1;
            }

            fprintf(stderr, "Thread id %d created\n", p->thread_idx);
        }

    running_workers = conf_num_threads;


    for(i = 0; i < thread_id; i++) {
        pthread_join(tid[i], NULL);
    }

    pthread_join(reporter_thread, NULL);
    // thread_join

    return 0;
}
