#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

char buff[4096];
#define MAXSIZE 4096
#define N_MSG 50

const char* const data[] = { "msg1", "msg2", "msg3" };

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
	//sleep(1);

	printf("opening device %s\n", device);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", device);
		return NULL;
	}
	printf("device %s successfully opened\n", device);
	
	ioctl(fd, 0, 10);

	for (int i = 0; i < N_MSG; i++) {
		write(fd, data[pos], strlen(data[pos]));
		printf("writed: %s\n", data[pos]);
		usleep(50);
	}
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
	
	//ioctl(fd, 0, 10);
	
	msg = malloc(MAXSIZE);
	for (int i = 0; i < N_MSG; i++) { 
		size = read(fd, msg, MAXSIZE);
		printf("readed: %s\n", msg);
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
    

    for (int i = 0; i < minors; i++) {
		sprintf(buff, "mknod %s%d c %d %i\n", path, i, major, i);
		system(buff);
		sprintf(buff, "%s%d", path, i);
		struct t_data * data = malloc(sizeof(struct t_data)); 
		data->device =  strdup(buff);
		data->pos = i;
		pthread_create(&tid, NULL, writing, data);
		pthread_create(&tid, NULL, reading, strdup(buff));
    }
    pause();
    return 0;
}
