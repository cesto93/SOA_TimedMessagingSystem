#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "../timed_messaging_system.h"

char buff[4096];
int major;
int r_timeout = 0;

#define MAXSIZE 4096
#define R_MSG 1
#define THREAD_PER_NODE 1

void *reading(void * path){
	char * device;
	char * msg;
	int fd, size;

	device = (char*) path;
	usleep(100000);

	printf("opening device %s\n", device);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	
	if (ioctl(fd, SET_RECV_TIMEOUT_NR(major), r_timeout) == -1) {
		printf("error in ioctl on device %s\n", device);
	}
	
	msg = malloc(MAXSIZE);
	for (int i = 0; i < R_MSG; i++) { 
		size = read(fd, msg, MAXSIZE);
		 if (size != 0) {
			printf("readed on dev %s: %s\n", device, msg);
		 } else {
			puts("Nothing to read");
		}
		if (size == -1) {
			perror("error on read");
			return NULL;
		}		
	}	
	return NULL;
}

int main(int argc, char** argv){
	int ret;
    int minors;
    char *path;

	if (argc < 4) {
		printf("useg: prog pathname major minors [read_timeout]\n");
		return -1;
    }

	path = argv[1];
    major = strtol(argv[2], NULL, 10);
    minors = strtol(argv[3], NULL, 10);
    
	if (argc >= 5) {
		r_timeout = strtol(argv[4], NULL, 10);
	}
	
	pthread_t * tids = malloc(sizeof(pthread_t) * minors * THREAD_PER_NODE);
	
    for (int i = 0; i < minors; i++) {
		sprintf(buff, "%s%d", path, i);
		for (int j = 0; j < THREAD_PER_NODE; j++) {
			pthread_create(&tids[i+j], NULL, reading, strdup(buff));
		}
    }
    for (int i = 0; i < minors * THREAD_PER_NODE; i++) {
		pthread_join(tids[i], NULL);
	}
    return 0;
}
