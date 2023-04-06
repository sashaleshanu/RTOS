#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <devctl.h>
#include "bbs.h"

int signal_status = 0;

void signal_handler(int signal)
{
	signal_status = signal;
	std::cout << "Signal " << signal << " received" << std::endl;
}


int main( int argc, char **argv ) 
{
	std::cout << "Running client..." << std::endl;
	std::signal(SIGINT, signal_handler);
    // open a connection to the server (fd == coid)
    int fd = open("/dev/cryptobbs", O_RDONLY);
    if(fd < 0)
    {
        std::cerr << "E: unable to open server connection: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    bbs::BBSParams params;
    params.seed = 866;
    params.p = 3;
    params.q = 263;

    int error = devctl(fd, SET_GEN_PARAMS, &params, sizeof(params), NULL);

    if (error != EOK) {
    	std::cerr << "E: SET_GEN_PARAMS error: " << strerror(error);
    	exit(EXIT_FAILURE);
    }

    std::uint32_t* values_from_server = new std::uint32_t[1024];
    std::uint32_t element = 0;
    int i = 0;

    while (!signal_status) {
    	error = devctl(fd, GET_ELEMENT, &element, sizeof(std::uint32_t), NULL);
    	if (error != EOK) {
    		std::cerr << "E: GET_ELEMENT error: " << strerror(error);
    		exit(EXIT_FAILURE);
    	}
    	i++;
    	if (i == 1024) {
    		i = 0;
    	}
    	values_from_server[i] = element;
    }

    for (int i = 0; i < 1024; i++) {
    	std::cout << values_from_server[i] << std::endl;
    }

    close(fd);

    return EXIT_SUCCESS;
}
