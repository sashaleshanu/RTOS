#include <iostream>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>

using namespace std;

int INPUT_SIZE;

struct CmdArgv{
    char* path_to_text;
    char* patch_to_cypher;
    int x;
    int a;
    int c;
    int m;
};

struct Worker {
    pthread_barrier_t* barrier;
    char* text;
    char* output_text;
    char* pseudorandom_seq;
    int down_index;
    int top_index;
};

void* LCG(void* cmd_argv_ptr)
{
    CmdArgv* cmd_argv = static_cast<CmdArgv*>(cmd_argv_ptr);
    int x = cmd_argv->x;
    int a = cmd_argv->a;
    int c = cmd_argv->c;
    int m = cmd_argv->m;

    int count_of_int = INPUT_SIZE / sizeof(int);
    int* buff = new int[count_of_int + 1];
    buff[0] = x;

    for(size_t i = 1; i < count_of_int + 1; i++)
    {
        buff[i]= (a * buff[i-1] + c) % m;
    }

    char* seq = reinterpret_cast<char *>(buff);
    return seq;
}

void* encrypt(void * worker_ptr)
{
    Worker* worker = static_cast<Worker*>(worker_ptr);
    int top_index = worker->top_index;
    int down_index = worker->down_index;

    while(down_index < top_index)
    {
        worker->output_text[down_index] = worker->pseudorandom_seq[down_index] ^ worker->text[down_index];
        down_index++;
    }

    int status = pthread_barrier_wait(worker->barrier);
    if (status != 0 && status != PTHREAD_BARRIER_SERIAL_THREAD) {
        exit(status);
    }

    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc != 13)
    {
        std::cout << "Arguments error" << std::endl;
        exit(1);
    }
    int c;
    CmdArgv cmd_argv;
    while ((c = getopt(argc, argv, "i:o:x:a:c:m:")) != -1)
    {
        switch (c)
        {
            case 'i':
                cmd_argv.path_to_text = optarg;
                break;
            case 'o':
                cmd_argv.patch_to_cypher = optarg;
                break;
            case 'x':
                cmd_argv.x = atoi(optarg);
                break;
            case 'a':
                cmd_argv.a = atoi(optarg);
                break;
            case 'c':
                cmd_argv.c = atoi(optarg);
                break;
            case 'm':
                cmd_argv.m = atoi(optarg);
                break;
            default:
                break;
        }
    }

    if (optind < argc) {
        std::cout << "Some line elements weren't recognised" << std::endl;
        exit(1);
    }

    int input_file = open(cmd_argv.path_to_text, O_RDONLY);
    if (input_file == -1)
    {
        std::cout << "Unable to open " << cmd_argv.path_to_text << " file" << std::endl;
        exit(1);
    }

    INPUT_SIZE = lseek(input_file, 0, SEEK_END);
    if (INPUT_SIZE > 10000)
    {
        std::cout << "The file you're trying to open is too large"<< std:: endl;
        exit(1);
    }
    lseek(input_file, 0, SEEK_SET);

    char* text = new char[INPUT_SIZE];
    if(read(input_file, text, INPUT_SIZE) == -1)
    {
        std::cout << "Can't map the input file to RAM" << std::endl;
        exit(1);
    }

    pthread_t keygen_thread;
    if (pthread_create(&keygen_thread, NULL, LCG, &cmd_argv) != 0)
    {
        std::cout << "Unable to create a new keygen thread" << std::endl;
        exit(1);
    }

    char* pseudorandom_seq = nullptr;
    if(pthread_join(keygen_thread, (void**)&pseudorandom_seq))
    {
        std::cout << "Unable to join a keygen thread thread" << std::endl;
        exit(1);
    }

    pthread_barrier_t barrier;

    // Number of CPUs available
    int number_of_processors = sysconf(_SC_NPROCESSORS_ONLN);

    pthread_barrier_init(&barrier, NULL, number_of_processors + 1);
    pthread_t crypt_threads[number_of_processors];
    std::vector <Worker*> workers;

    size_t part_len = INPUT_SIZE / number_of_processors;
    if (INPUT_SIZE % number_of_processors != 0) {
        part_len ++;
    }

    char* output_text = new char[INPUT_SIZE];
    for(int i = 0; i < number_of_processors; i++)
    {
        Worker* worker = new Worker;

        worker->barrier = &barrier;
        worker->text = text;
        worker->output_text = output_text;
        worker->pseudorandom_seq = pseudorandom_seq;
        worker->down_index = i * part_len;

        if (i == number_of_processors - 1)
            worker->top_index = INPUT_SIZE;
        else
            worker->top_index = worker->down_index + part_len;

        workers.push_back(worker);
        pthread_create(&crypt_threads[i], NULL, encrypt, worker);
    }

    int status = pthread_barrier_wait(&barrier);
    if (status != 0 && status != PTHREAD_BARRIER_SERIAL_THREAD)
    {
        std::cout << "Some problems with barrier" << std::endl;
        exit(status);
    }

    int output_file = open(cmd_argv.patch_to_cypher, O_WRONLY, O_TRUNC);
    if (output_file == -1)
    {
        std::cout << "Unable to open " << cmd_argv.patch_to_cypher << " file" << std::endl;
        exit(1);
    }

    write(output_file, output_text, INPUT_SIZE);

    close(output_file);

    pthread_barrier_destroy(&barrier);

    return 0;
}

