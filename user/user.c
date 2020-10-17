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
int r_timeout = 0;

#define MAXSIZE 4096
#define W_MSG 10
#define R_MSG 20

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
			printf("readed: %s\n", msg);
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
		printf("useg: prog pathname major minors [write_timeout] [read_timeout]\n");
		return -1;
    }

	path = argv[1];
    major = strtol(argv[2], NULL, 10);
    minors = strtol(argv[3], NULL, 10);
    
    if (argc >= 5) {
		w_timeout = strtol(argv[4], NULL, 10);
	}
	if (argc >= 6) {
		r_timeout = strtol(argv[5], NULL, 10);
	}
	
	pthread_t * tids = malloc(sizeof(pthread_t) * minors);
    printf("creating %d minors for device %s with major %d\n", minors, path, major);
    

    for (int i = 0; i < minors; i++) {
		sprintf(buff, "mknod %s%d c %d %i\n", path, i, major, i);
		system(buff);
		sprintf(buff, "%s%d", path, i);
		struct t_data * data = malloc(sizeof(struct t_data)); 
		data->device =  strdup(buff);
		data->pos = i;
		pthread_create(&tids[i], NULL, writing, data);
		pthread_create(&tids[i], NULL, reading, strdup(buff));
    }
    for (int i = 0; i < minors; i++) {
		pthread_join(tids[i], NULL);
	}
    return 0;
}
