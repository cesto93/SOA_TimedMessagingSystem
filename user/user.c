#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

int i;
char buff[4096];
#define DATA "ciao a tutti\n"
#define SIZE strlen(DATA)
#define MAXSIZE 4096

void *writing(void * path){
	char* device;
	int fd;

	device = (char*) path;
	sleep(1);

	printf("opening device %s\n", device);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	ioctl(fd,1);
	for (i = 0; i < 100; i++) 
		write(fd, DATA, SIZE);
	return NULL;
}

void *reading(void * path){
	char * device;
	char * msg;
	int fd, size;

	device = (char*) path;
	sleep(1);

	printf("opening device %s\n", device);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	ioctl(fd,1);
	msg = malloc(SIZE);
	for (i = 0; i < 100; i++) { 
		size = read(fd, msg, MAXSIZE);
		puts(msg);
		if (size == -1) {
			perror("error on read");
			return NULL;
		}		
	}
	return NULL;
}

int main(int argc, char** argv){
	int ret;
    int major;
    int minors;
    char *path;
    pthread_t tid;

	if (argc < 4) {
		printf("useg: prog pathname major minors");
		return -1;
    }

	path = argv[1];
    major = strtol(argv[2],NULL,10);
    minors = strtol(argv[3],NULL,10);
    printf("creating %d minors for device %s with major %d\n", minors, path, major);

    for (i = 0; i < minors; i++) {
		sprintf(buff, "mknod %s%d c %d %i\n", path, i, major, i);
		system(buff);
		sprintf(buff, "%s%d", path, i);
		pthread_create(&tid, NULL, writing, strdup(buff));
		pthread_create(&tid, NULL, reading, strdup(buff));
    }
    pause();
    return 0;
}
