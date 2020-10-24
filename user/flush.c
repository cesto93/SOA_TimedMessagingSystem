#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "../timed_messaging_system.h"

void *flushing(void * tdata){
	char* device = tdata;
	int fd;
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	
	//fsync(fd);
	return NULL;
}

int main(int argc, char** argv){
	int ret;
    int minors;
    char *path, *buff;    

	if (argc < 3) {
		printf("useg: prog pathname minors \n");
		return -1;
    }

	path = argv[1];
    minors = strtol(argv[2], NULL, 10);
	
	pthread_t * tids = malloc(sizeof(pthread_t) * minors);
	buff = malloc(strlen(path) + 10);
	
    for (int i = 0; i < minors; i++) {
		sprintf(buff, "%s%d", path, i);
		pthread_create(&tids[i], NULL, flushing, strdup(buff));
    }
    for (int i = 0; i < minors; i++) {
		pthread_join(tids[i], NULL);
	}
    return 0;
}
