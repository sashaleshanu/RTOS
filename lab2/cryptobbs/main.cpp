#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <memory.h>
#include <sys/types.h>
#include <devctl.h>
#include "bbs.h"


static resmgr_connect_funcs_t    connect_funcs;
static resmgr_io_funcs_t         io_funcs;
static iofunc_attr_t             attr;

bbs::BBSParams* params;
std::uint32_t current_element;

std::uint32_t generate_element() {
	std::uint32_t count_bits = 32;
	std::uint32_t result = 0;
	std::uint32_t M = params->p * params->q;

	for (int i = 0; i < count_bits; i++) {
		current_element = current_element * current_element % M;
		result = (result << 1) | (current_element & 1);
	}
	return result;
}

int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb) {
	int sts, nbytes;

	// 1) Проверить, не является ли это обычным
	// POSIX-совместимым devctl()
	if ((sts = iofunc_devctl_default(ctp, msg, ocb)) != _RESMGR_DEFAULT) {
	 return (sts);
	}
	sts = nbytes = 0;

	void* rx_data = _DEVCTL_DATA(msg->i);

	// 2) Узнать, что за команда, и отработать ее
	switch (msg->i.dcmd) {
		case SET_GEN_PARAMS:
			params = reinterpret_cast<bbs::BBSParams*>(rx_data);
			current_element = params->seed;
			break;
		case GET_ELEMENT:
			*reinterpret_cast<uint32_t*>(rx_data) = generate_element();
			std::cout  << " data : " << (*(std::uint32_t*)rx_data) << std::endl;
			nbytes = sizeof(std::uint32_t);
			break;
		// 3) Если мы не знаем такой команды, отвергнуть ее
		default:
			return (ENOSYS);
	}

	// 4) Сказать клиенту, что все отработано
	memset(&msg->o, 0, sizeof(msg->o));
	msg->o.ret_val = sts;
	msg->o.nbytes = nbytes;
	//SETIOV(ctp->iov, &msg->o, sizeof(msg->o) + msg->o.nbytes);
	//return (_RESMGR_NPARTS(1));
	return(_RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + nbytes));
}


int main(int argc, char **argv)
{
	std::cout << "Running server..." << std::endl;
    /* declare variables we'll be using */
    resmgr_attr_t        resmgr_attr;
    dispatch_t           *dpp;
    dispatch_context_t   *ctp;
    int                  id;

    /* initialize dispatch interface */
    if((dpp = dispatch_create()) == NULL) {
        fprintf(stderr,
                "%s: Unable to allocate dispatch handle.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    /* initialize resource manager attributes */
    memset(&resmgr_attr, 0, sizeof resmgr_attr);
    resmgr_attr.nparts_max = 1;
    resmgr_attr.msg_max_size = 2048;

    /* initialize functions for handling messages */
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, 
                     _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.devctl = io_devctl;

    /* initialize attribute structure used by the device */
    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

    /* attach our device name */
    id = resmgr_attach(
            dpp,            /* dispatch handle        */
            &resmgr_attr,   /* resource manager attrs */
            "/dev/cryptobbs",  /* device name            */
            _FTYPE_ANY,     /* open type              */
            0,              /* flags                  */
            &connect_funcs, /* connect routines       */
            &io_funcs,      /* I/O routines           */
            &attr);         /* handle                 */
    if(id == -1) {
        fprintf(stderr, "%s: Unable to attach name.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* allocate a context structure */
    ctp = dispatch_context_alloc(dpp);

    /* start the resource manager message loop */
    while(1) {
        if((ctp = dispatch_block(ctp)) == NULL) {
            fprintf(stderr, "block error\n");
            return EXIT_FAILURE;
        }
        dispatch_handler(ctp);
    }
    return EXIT_SUCCESS; // never go here
}
