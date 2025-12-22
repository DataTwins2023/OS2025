#include <fstream>
#include "thread.hpp"
#include "ts_queue.hpp"
#include "item.hpp"

#ifndef WRITER_HPP
#define WRITER_HPP

class Writer : public Thread {
public:
	// constructor
	Writer(int expected_lines, std::string output_file, TSQueue<Item*>* output_queue);

	// destructor
	~Writer();

	virtual void start() override;
private:
	// the expected lines to write,
	// the writer thread finished after output expected lines of item
	int expected_lines;

	std::ofstream ofs;
	TSQueue<Item*> *output_queue;

	// the method for pthread to create a writer thread
	static void* process(void* arg);
};

// Implementation start

Writer::Writer(int expected_lines, std::string output_file, TSQueue<Item*>* output_queue)
	: expected_lines(expected_lines), output_queue(output_queue) {
	ofs = std::ofstream(output_file);
}

Writer::~Writer() {
	ofs.close();
}

void Writer::start() {
	// TODO: starts a Writer thread
	pthread_create(&t, 0, Writer::process, (void*)this); 
}

void* Writer::process(void* arg) {
	// TODO: implements the Writer's work

	Writer* writer = (Writer*)arg;

	// write expected_lines 行 item 到 output file
	for (int i = 0; i < writer->expected_lines; i++) {
		// 1. 從 output_queue 中取出 item
		Item* item = writer->output_queue->dequeue();

		// 2. 將 item 寫入 output file
		
		writer->ofs << *item;
		// delete
		// writer 創建 item 接著由 writer 負責刪除
		delete item; // 寫入後釋放 item 記憶體
	}
	return nullptr;
}

#endif // WRITER_HPP
