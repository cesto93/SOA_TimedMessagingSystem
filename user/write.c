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
int w_timeout = 0;

#define MAXSIZE 4096
#define W_MSG 10

struct t_data {
	char *device;
	int pos;
};

void *writing(void * tdata){
	char* device;
	int fd;
	
	struct t_data * my_data = (struct t_data *) tdata; 
	device = my_data->device;
	int pos = my_data->pos;
	
	char * msg = alloca(50);
	
	printf("opening device %s\n", device);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	
	if (ioctl(fd, SET_SEND_TIMEOUT_NR(major), w_timeout) == -1) {
		printf("error in ioctl on device %s\n", device);
	}

	for (int i = 0; i < W_MSG; i++) {
		int len = sprintf(msg, "msg%d_%d", pos, i);
		write(fd, msg, len);
		printf("writed: %s\n", msg);
		usleep(5000);
	}
	return NULL;
}

int main(int argc, char** argv){
	int ret;
    int minors;
    char *path;    

	if (argc < 4) {
		printf("useg: prog pathname major minors [write_timeout]\n");
		return -1;
    }

	path = argv[1];
    major = strtol(argv[2], NULL, 10);
    minors = strtol(argv[3], NULL, 10);
    
    if (argc >= 5) {
		w_timeout = strtol(argv[4], NULL, 10);
	}
	
	pthread_t * tids = malloc(sizeof(pthread_t) * minors);
    printf("creating %d minors for device %s with major %d\n", minors, path, major);
	
    for (int i = 0; i < minors; i++) {
		sprintf(buff, "%s%d", path, i);
		struct t_data * data = malloc(sizeof(struct t_data)); 
		data->device =  strdup(buff);
		data->pos = i;
		pthread_create(&tids[i], NULL, writing, data);
    }
    for (int i = 0; i < minors; i++) {
		pthread_join(tids[i], NULL);
	}
	pause();
    return 0;
}