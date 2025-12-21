#include <assert.h>
#include <stdlib.h>
#include <chrono>
#include "ts_queue.hpp"
#include "item.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include "producer.hpp"
#include "consumer_controller.hpp"

#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 100

int main(int argc, char** argv) {

	// start the clock
	// using Clock = std::chrono::steady_clock;
    // auto start = Clock::now();


	assert(argc == 4);

	int n = atoi(argv[1]);
	std::string input_file_name(argv[2]);
	std::string output_file_name(argv[3]);

	// TODO: implements main function

	// 1. initialize tsqueues input_queue, worker_queue, output_queue 各一個
	TSQueue<Item*>* input_queue = new TSQueue<Item*>(READER_QUEUE_SIZE);
	TSQueue<Item*>* worker_queue = new TSQueue<Item*>(WORKER_QUEUE_SIZE);
	TSQueue<Item*>* output_queue = new TSQueue<Item*>(WRITER_QUEUE_SIZE);

	// 2. initialize transformer
	Transformer* transformer = new Transformer();

	// 3. initialize each thread: 1 reader, 1 writer, 4 producers 
	Reader* reader = new Reader(n, input_file_name, input_queue);
	Writer* writer = new Writer(n, output_file_name, output_queue);
	std::vector<Producer*> producers;
	for (int i = 0; i < 4; i++) {
		producers.push_back(new Producer(input_queue, worker_queue, transformer));
	}

	// 4.initialize 1 consumer_controller thread
	int low_threshold = WORKER_QUEUE_SIZE * CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE / 100;
	int high_threshold = WORKER_QUEUE_SIZE * CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE / 100;

	ConsumerController* consumer_controller = new ConsumerController(
		worker_queue,
		output_queue,
		transformer,
		CONSUMER_CONTROLLER_CHECK_PERIOD,
		low_threshold,
		high_threshold
	);

	// 5. start all threads
	reader->start();
	writer->start();
	for (auto& p : producers) {
		p->start();
	}
	consumer_controller->start();

	// main 必須要等待 reader 讀完整個 input file 後才能結束
	reader->join();
	for (int i = 0; i < 4; i++) {
		input_queue->enqueue(nullptr);
	}
	for (auto& p : producers) {
		p->join();
	}
	// main 等待 writer 寫完所有內容到 ouput file 後才能結束
	writer->join();

	consumer_controller->cancel();
	consumer_controller->join();
	// 6. 釋放 main 中配置的記憶體
	delete reader;
	delete writer;
	for (auto& producer : producers) {
		delete producer;
	}
	delete consumer_controller;
	delete transformer;
	delete input_queue;
	delete worker_queue;
	delete output_queue;


	// auto end = Clock::now();
    // auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // std::cout << "Total execution time: "
    //           << elapsed.count() << " ms" << std::endl;

	return 0;
}
