# producer-consumer
## Description
Solution for group assignment for Computer Architecture course in Innopolis University. 


## Features
Inside there are two different approaches for solving this problem. First is queue in shared memory and producer and consumer in two threads. Second is producer and consumer are in different processes and communication is made with sockets.


## How to compile
gcc prod_cons.c -o prod_cons -lpthread


## How to run
First you need to create 2 files: "items.txt", "results.txt".


"results.txt" - is empty. Results will be written into it.


"item.txt" - contains any amount of items for processing. Item format: "%d %d %d %d" - four integers divided by space, where first - itemId, second - produceTime, third - consumeTime, fourth - priority. 


####Shared memory approach and queue size 120


./prod_cons 120 shmem


####Different processes and sockets approach and queue size 120


./prod_cons 40 socket
