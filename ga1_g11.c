#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

typedef struct {
        int itemId;
        int produceTime;
        int consumeTime;
        int priority;
} item;
typedef struct {
        int len;
        item * nodes;
        int max; // max queue size
        pthread_mutex_t lock;
        pthread_cond_t cond_not_empty;
        pthread_cond_t cond_full;
} queue_t;

int sv[2]; /* the pair of socket descriptors */
int svv[2];
int itemsNumber;
int queueSize;
queue_t q; /* queue_t it is used as in sharedmemory in first case.
              Also in socket, in second case,  it can't be accessed from
              another process, so it is not sharedmemory*/
void log(char * str){
        printf("Log. in:%s\n", str);
}

void init_queue(queue_t * q){
        q->nodes = malloc(queueSize * sizeof(item));
        q->len = 0;
        q->max = queueSize;
        log("Init queue");
        pthread_mutex_init(&q->lock, 0);
        pthread_cond_init(&q->cond_not_empty, 0);
        pthread_cond_init(&q->cond_full, 0);

}

void close_queue(queue_t * q) {
        //free(q->nodes);
        pthread_cond_broadcast(&q->cond_not_empty);
        pthread_cond_broadcast(&q->cond_full);
        pthread_mutex_unlock(&q->lock);
        log("Close queue");
}

void enqueue(queue_t * aq, item * it) {
        volatile queue_t *q = aq;
        int priority = it->priority;
        int place = 0;
        pthread_mutex_lock(&aq->lock);

        if(q->max <= q->len) {
                asm volatile ("" : : : "memory");
                while(q->max <= q->len)
                        pthread_cond_wait(&aq->cond_full, &aq->lock);
                asm volatile ("" : : : "memory");
        }

        for (size_t i = 0; i < q->len; i++) {
                if (priority < q->nodes[i].priority) {
                        break;
                }
                place++;
        }
        q->len++;
        for (size_t i = q->len; i > place; i--) {
                q->nodes[i] = q->nodes[i-1];
        }
        q->nodes[place].itemId = it->itemId;
        q->nodes[place].produceTime = it->produceTime;
        q->nodes[place].consumeTime = it->consumeTime;
        q->nodes[place].priority = it->priority;
        pthread_cond_broadcast(&q->cond_not_empty);

        asm volatile ("" : : : "memory");
        printf("QUEUE now:\n");
        for (size_t i = 0; i < q->len; i++) {
                printf("%d %d %d %d\n",
                       q->nodes[i].itemId,
                       q->nodes[i].produceTime,
                       q->nodes[i].consumeTime,
                       q->nodes[i].priority
                       );
        }

        pthread_mutex_unlock(&aq->lock);
}

item * dequeue(queue_t * aq){
        volatile queue_t *q = aq;
        item * result = malloc(sizeof(item));
        pthread_mutex_lock(&aq->lock);

        if (q->max > q->len)
                pthread_cond_broadcast(&aq->cond_full);

        while (q->len < 1) {
                pthread_cond_wait(&aq->cond_not_empty, &aq->lock);
        }
        result->itemId = q->nodes[0].itemId;
        result->produceTime = q->nodes[0].produceTime;
        result->consumeTime = q->nodes[0].consumeTime;
        result->priority = q->nodes[0].priority;
        q->len--;
        for (size_t i = 0; i < q->len; i++) {
                q->nodes[i] = q->nodes[i+1];
        }
        asm volatile ("" : : : "memory");
        pthread_cond_broadcast(&aq->cond_full);
        pthread_mutex_unlock(&aq->lock);
        return result;
}

// Parse ints from string into  array
void * parseInts(int * arr, char * line){
        char *str = line;
        char *ptr = str;
        for (size_t i = 0; i < 4; i++) {
                arr[i] = strtol(ptr, &ptr, 10);
        }
}
// Read items from array, results will appear in array from arguments
item* readItemsFromFile (FILE* fileName, item* items, int * size){
        char * line = NULL;
        size_t len = 0;
        ssize_t read;
        int count = 0;

        items = malloc (count * sizeof(item));

        while ((read = getline(&line, &len, fileName)) != -1) {
                items = realloc (items, (count + 1)*sizeof(item));
                int array[4];
                parseInts(array, line);
                items[count].itemId = array[0];
                items[count].produceTime = array[1];
                items[count].consumeTime = array[2];
                items[count].priority = array[3];
                count++;
        }
        *size = count;
        return items;
}
// Thread - socket producerReciever for queue
void * listenProducer(void * placeholder){
        int inputSize;
        // getting input size
        recv(sv[1], &inputSize, sizeof(item), 0);
        //getting items from producer
        for (size_t i = 0; i < inputSize; i++) {
                item buf;
                recv(sv[1], &buf, sizeof(item), 0);
                printf("producerListener GET item with values: %d, %d, %d, %d\n",
                       buf.itemId,
                       buf.produceTime,
                       buf.consumeTime,
                       buf.priority);
                enqueue(&q, &buf);
                send(sv[1], "0", sizeof(char), 0);
        }
        return NULL;

}
// Thread - socket consumerReciever for queue
void * listenConsumer(void * placeholder) {
        for (size_t i = 0; i < itemsNumber; i++) {
                item * it = dequeue(&q);
                printf("consumerListener SEND item with values: %d, %d, %d, %d\n",
                       it->itemId,
                       it->produceTime,
                       it->consumeTime,
                       it->priority);
                send(svv[0], it, sizeof(item), 0);
                char ret;
                recv(svv[0], &ret, 1, 0);
        }
        return NULL;
}

// Thread produser for sharedmemory
void * prodThread(void * args) {
        item * items = (item *) args;
        for (size_t i = 0; i < itemsNumber; i++) {
                usleep(items[i].produceTime * 1000); // producing time
                enqueue(&q, &items[i]);
        }
}

// Thread consumer for sharedmemory
void consThread(void * args) {
        FILE * outputFile = (FILE *) args;
        for (size_t i = 0; i < itemsNumber; i++) {
                item * it = dequeue(&q);
                usleep(it->consumeTime * 1000);
                fprintf(outputFile, "%d %d %d %d\n",
                        it->itemId,
                        it->produceTime,
                        it->consumeTime,
                        it->priority);
        }
}

int main(int argc, char const *argv[]) {
        int mode;   // 0 for sharedmemory
                    // 1 for socket
        FILE* inputFile;
        FILE* outputFile;
        item* items;


        // Parsing and checking arguments:
        if (argc != 3) {
                perror("Wrong number of arguments\nExecution aborted\n");
                exit(1);
        } else {
                queueSize = atoi(argv[1]);
                if (queueSize < 1 || queueSize > 4096) {
                        printf("Wrong queue size\n");
                        exit(0);
                }
                if (strcmp(argv[2], "shmem") == 0) {
                        mode = 0;
                } else if (strcmp(argv[2], "socket") == 0) {
                        mode = 1;
                } else {
                        printf("Wrong mode name\nExecution aborted\n");
                        exit (1);
                }
        }

        // Reading items from file
        inputFile = fopen("items.txt", "r");
        if (inputFile == NULL) {
                printf("Can\'t open the file (file items.txt is needed)\n");
                exit(1);
        }

        // get item from file
        items = readItemsFromFile(inputFile, items, &itemsNumber);
        fclose(inputFile);
        // Execution, based on chosen mode:
        if (mode == 0) {
                // Shared memory based procedure
                pthread_t producerThread, consumerThread;
                init_queue(&q);
                outputFile = fopen("results.txt", "w");
                if (outputFile == NULL) {
                        printf("Can\'t open the file (file results.txt is needed)\n");
                        exit(1);
                }
                if(pthread_create(&producerThread, NULL, prodThread, (void *)items)) {
                        printf("Can not create thread producerThread\nExecution aborted\n");
                        exit(1);
                }
                printf("Producer crated\n");
                if(pthread_create(&consumerThread, NULL, consThread, (void *)outputFile)) {
                        printf("Can not create thread consumerThread\nExecution aborted\n");
                        exit(1);
                }
                pthread_join(producerThread, NULL);
                pthread_join(consumerThread, NULL);
                close_queue(&q);
        } else if (mode == 1) {
                // Sockets based procedure
                pid_t queue;
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
                        perror("socketpair");
                        exit(1);
                }
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, svv) == -1) {
                        perror("socketpair");
                        exit(1);
                }
                queue = fork();
                if (queue) {
                        pid_t consumer;
                        consumer = fork();
                        if (consumer) {
                                // Consumer code
                                outputFile = fopen("results.txt", "w");
                                if (outputFile == NULL) {
                                        printf("Can\'t open the file (file results.txt is needed)\n");
                                        exit(1);
                                }
                                for (size_t i = 0; i < itemsNumber; i++) {
                                        item buf;
                                        recv(svv[1], &buf, sizeof(item), 0);
                                        printf("consumer Get item with values: %d, %d, %d, %d\n",
                                               buf.itemId,
                                               buf.produceTime,
                                               buf.consumeTime,
                                               buf.priority);
                                        usleep(buf.consumeTime * 1000);
                                        fprintf(outputFile, "%d %d %d %d\n",
                                                buf.itemId,
                                                buf.produceTime,
                                                buf.consumeTime,
                                                buf.priority);
                                        printf("Done with last item\n");
                                        send(svv[1], "0", sizeof(char), 0);
                                }
                                //free (items);
                                fclose(outputFile);
                                return NULL;
                        } else {
                                // Queue code
                                pthread_t prodListener, consListener;
                                init_queue(&q);

                                if(pthread_create(&prodListener, NULL, listenProducer, NULL)) {
                                        printf("Can not create thread producerListener\nExecution aborted\n");
                                        exit(1);
                                }
                                printf("Producer crated\n");
                                if(pthread_create(&consListener, NULL, listenConsumer, NULL)) {
                                        printf("Can not create thread consumerListener\nExecution aborted\n");
                                        exit(1);
                                }
                                pthread_join(prodListener, NULL);
                                pthread_join(consListener, NULL);
                                printf("Queue: done\n");
                                close_queue(&q);
                        }
                } else {
                        // Producer code
                        send(sv[0], &itemsNumber, sizeof(int), 0);
                        for (size_t i = 0; i < itemsNumber; i++) {

                                usleep(items[i].produceTime * 1000); // producing time
                                send(sv[0], &items[i], sizeof(item), 0);
                                // get responce when enshure that queue not full
                                char ret;
                                recv(sv[0], &ret, 1, 0);
                        }
                }

        } else {
                printf("Something goes wrong\nExecution aborted\n");
                exit(1);
        }
        // Finish successfully
        return 0;
}
