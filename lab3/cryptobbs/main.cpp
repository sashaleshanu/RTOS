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
#include <sys/neutrino.h>
#include <sys/resmgr.h>
#include <cstdint>
#include <mutex>
#include <map>


static resmgr_connect_funcs_t    connect_funcs;
static resmgr_io_funcs_t         io_funcs;
static iofunc_attr_t             attr;

// Отключаем предупреждения
#define THREAD_POOL_PARAM_T dispatch_context_t
#pragma GCC diagnostic ignored "-fpermissive"
std::mutex mut;
std::unique_lock<std::mutex> mut_lock(mut, std::defer_lock);

struct Client {
	bbs::BBSParams params;
	std::uint32_t current_element;
};

std::map<std::int32_t, Client> contexts;

bool isFirst = false;
std::uint32_t generate_element(Client& client) {
	std::uint32_t count_bits = 32;
	std::uint32_t result = 0;
	std::uint32_t M = client.params.p * client.params.q;
	for (int i = 0; i < count_bits; i++) {
		client.current_element = client.current_element * client.current_element % M;
		result = (result << 1) | (client.current_element & 1);
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
	bbs::BBSParams* data = nullptr;
	std::uint32_t client_id = ctp->info.scoid;
	// 2) Узнать, что за команда, и отработать ее
	switch (msg->i.dcmd) {
		case SET_GEN_PARAMS:
			mut_lock.lock();
			contexts[ctp->info.scoid].params = *reinterpret_cast<bbs::BBSParams*>(rx_data);
			contexts[ctp->info.scoid].current_element = contexts[ctp->info.scoid].params.seed;
			mut_lock.unlock();
			break;
		case GET_ELEMENT:
			mut_lock.lock();
			*reinterpret_cast<uint32_t*>(rx_data) = generate_element(contexts[ctp->info.scoid]);
			//std::cout  << " data : " << (*(std::uint32_t*)rx_data) << std::endl;
			nbytes = sizeof(std::uint32_t);
			mut_lock.unlock();
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

int io_open (resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *extra)
{
	mut_lock.lock();
	contexts.insert({ctp->info.scoid, Client()});
	std::cout << "Client " << ctp->info.scoid << " connected" << std::endl;
	mut_lock.unlock();
	return (iofunc_open_default (ctp, msg, handle, extra));
}

int io_close(resmgr_context_t *ctp, io_close_t *msg, iofunc_ocb_t *ocb)
{
	mut_lock.lock();
	if (contexts.count(ctp->info.scoid)) {
		contexts.erase(ctp->info.scoid);
	}
	std::cout << "Client " << ctp->info.scoid << " disconnected" << std::endl;
	mut_lock.unlock();
	return (iofunc_close_dup_default(ctp, msg, ocb));
}

int main(int argc, char **argv)
{
		std::cout << "Running server..." << std::endl;
		thread_pool_attr_t   pool_attr;
	    resmgr_attr_t        resmgr_attr;
	    dispatch_t           *dpp;
	    thread_pool_t        *tpp;
	    dispatch_context_t   *ctp;
	    int                  id;

	    /* инициализация интерфейса диспетчеризации */
	    if((dpp = dispatch_create()) == NULL) {
	        fprintf(stderr,
	                "%s: Unable to allocate dispatch handle.\n",
	                argv[0]);
	        return EXIT_FAILURE;
	    }

	    /* инициализация атрибутов АР - параметры IOV */
	    memset(&resmgr_attr, 0, sizeof resmgr_attr);
	    resmgr_attr.nparts_max = 1;
	    resmgr_attr.msg_max_size = 2048;

	    /* инициализация структуры функций-обработчиков сообщений */
	    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
	                     _RESMGR_IO_NFUNCS, &io_funcs);

	    /* инициализация атрибутов устройства*/
	    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

	    io_funcs.devctl = io_devctl;
	    connect_funcs.open 	= io_open;
	    io_funcs.close_dup = io_close;


	    /* прикрепление к точке монтирования в пространстве имён путей */
	    id = resmgr_attach(
	            dpp,            /* хэндл интерфейса диспетчеризации */
	            &resmgr_attr,   /* атрибуты АР */
	            "/dev/cryptobbs",  /* точка монтирования */
	            _FTYPE_ANY,     /* open type              */
	            0,              /* флаги                  */
	            &connect_funcs, /* функции установления соединения */
	            &io_funcs,      /* функции ввода-вывода   */
	            &attr);         /* хэндл атрибутов устройства */
	    if(id == -1) {
	        fprintf(stderr, "%s: Unable to attach name.\n", argv[0]);
	        return EXIT_FAILURE;
	    }

	    /* инициализация атрибутов пула потоков */
	    memset(&pool_attr, 0, sizeof pool_attr);
	    pool_attr.handle = dpp;
	    pool_attr.context_alloc = dispatch_context_alloc;
	    pool_attr.block_func = dispatch_block;
	    pool_attr.unblock_func = dispatch_unblock;
	    pool_attr.handler_func = dispatch_handler;
	    pool_attr.context_free = dispatch_context_free;
	    pool_attr.lo_water = 3;
	    pool_attr.hi_water = 4;
	    pool_attr.increment = 1;
	    pool_attr.maximum = 50;

	    /* инициализация пула потоков */
	    if((tpp = thread_pool_create(&pool_attr,
	                                 POOL_FLAG_EXIT_SELF)) == NULL) {
	        fprintf(stderr, "%s: Unable to initialize thread pool.\n",
	                argv[0]);
	        return EXIT_FAILURE;
	    }

	    /* запустить потоки, блокирующая функция */
	    thread_pool_start(tpp);
	    /* здесь вы не окажетесь, грустно */
}
