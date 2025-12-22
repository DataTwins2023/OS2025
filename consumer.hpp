#include <pthread.h>
#include <stdio.h>
#include "thread.hpp"
#include "ts_queue.hpp"
#include "item.hpp"
#include "transformer.hpp"

#ifndef CONSUMER_HPP
#define CONSUMER_HPP

class Consumer : public Thread {
public:
	// constructor
	Consumer(TSQueue<Item*>* worker_queue, TSQueue<Item*>* output_queue, Transformer* transformer);

	// destructor
	~Consumer();

	virtual void start() override;

	virtual int cancel() override;
private:
	TSQueue<Item*>* worker_queue;
	TSQueue<Item*>* output_queue;

	Transformer* transformer;

	bool is_cancel;

	// the method for pthread to create a consumer thread
	static void* process(void* arg);
};

Consumer::Consumer(TSQueue<Item*>* worker_queue, TSQueue<Item*>* output_queue, Transformer* transformer)
	: worker_queue(worker_queue), output_queue(output_queue), transformer(transformer) {
	is_cancel = false;
}

Consumer::~Consumer() {}

void Consumer::start() {
	// TODO: starts a Consumer thread
	pthread_create(&t, 0, Consumer::process, (void*)this);
}

int Consumer::cancel() {
	// TODO: cancels the consumer thread
	is_cancel = true; // 宣告一個 cancel flag 作為取消標記
	return pthread_cancel(t);
}

void* Consumer::process(void* arg) {
	Consumer* consumer = (Consumer*)arg;

	// 設置 thread 的 cancel type 為 deferred cancelation:
	// ->不馬上取消 thread, 而是在 thread 執行到可以被取消的點時才取消
	// pthread_canceltype 決定「何時」真正結束
 	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);

	while (!consumer->is_cancel) {
		// pthread_setcancelstate 決定「要不要」理會取消的要求
		// enable 的話，thread 會在可以被取消的點被取消
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);

		// TODO: implements the Consumer's work
		// consumer 負責從 worker_queue 中取出 item, 並丟到 output_queue 中
		Item* item = consumer->worker_queue->dequeue();

		// 2. 拿到工作後，禁止被取消，確保 Item 不會丟失
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);

		if (item == nullptr) {
			consumer->output_queue->enqueue(nullptr);
			break;
		}

		// 2. do the consuming transformation
		item->val = consumer->transformer->consumer_transform(item->opcode, item->val);
		// 3. 將 transform 後的 item 放入 output_queue
		consumer->output_queue->enqueue(item);
		// 4. 不讓 consumer 自行釋放 而透過 controller 統一釋放
		// delete consumer;
	}
	

	return nullptr;
}

#endif // CONSUMER_HPP
