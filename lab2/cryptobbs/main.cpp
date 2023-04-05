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
#define count_bits sizeof(std::uint32_t) // == 32

std::uint32_t generate_element() {
	std::uint32_t middle_element = 0;
	std::uint32_t result = 0;
	bool bit = false;
	std::uint32_t place = 1;

	for (int i = 0; i < count_bits; i++) {
		middle_element = current_element * current_element % (params->p * params->q);
		bit = middle_element % 2;
		result += place * bit;
		current_element = middle_element;
		place = place * 2;
	}
	return result;
}

int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, iofunc_ocb_t *ocb) {
	int sts;
	// 1) Проверить, не является ли это обычным
	// POSIX-совместимым devctl()
	if ((sts = iofunc_devctl_default(ctp, msg, ocb)) != _RESMGR_DEFAULT) {
	 return (sts);
	}

	std::uint32_t res = 0;
	// 2) Узнать, что за команда, и отработать ее
	void* data = _DEVCTL_DATA(msg->i); //  указатель на местоположение данных
	switch (msg->i.dcmd) {
		case SET_GEN_PARAMS:
			params = reinterpret_cast<bbs::BBSParams*>(data);
			current_element = params->seed;
			break;
		case GET_ELEMENT:
			res = generate_element();
			memcpy(data, &res, sizeof(std::uint32_t));
			break;
		// 3) Если мы не знаем такой команды, отвергнуть ее
		default:
			return (ENOSYS);
	}

	// 4) Сказать клиенту, что все отработано
	memset(&(msg->o), 0, sizeof(msg->o));
	SETIOV(ctp->iov, &msg->o, sizeof(msg->o));
	return (_RESMGR_NPARTS(1));
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
