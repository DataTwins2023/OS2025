# Pthreads Report

## 組員貢獻度表格

| 貢獻項目 | 名字 | 系所 |
|---------|------|------|
| Implementation | 吳征彥、許恩嘉 | 資工碩二、資應碩二 |
| Experiment |  吳征彥、許恩嘉 | 資工碩二、資應碩二 |
| Report |  吳征彥、許恩嘉 | 資工碩二、資應碩二 |


# Experiment
### Base case
We use `tests/00.in` as the testing data for our following experiment  
original experiments parameters    
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental Result 0
<img src="images/base.png" width="25%">  

#### Discussion 0
一開始系統沒有consumer thread，producer thread會一直丟items進 worker_queue，等到 worker_queue 滿八成以上同時 controller 到它的 check period 的時候，  
會觸發 controller 做 consumer 數量的 scale up. 第一行一定是: `scaling up consumers from 0 to 1`.

### Different values of CONSUMER_CONTROLLER_CHECK_PERIOD
#### Experimental setting 1 
將 controller check 的頻率調快 **2** 個order (1000000 -> 10000)，其他參數維持不變。   
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 10000
```
#### Experimental result 1
<img src="images/period10000.png" width="25%">  

#### Discussion 1
相較於 experiment 0，由於 check period 更低，controller 更頻繁的去check worker_queue 的狀態，  
scaling 的操作更為頻繁。  
從印出的行為上來觀察，分成前後兩個階段:  
- 第一個階段為一開始 consumer 稀缺 -> woker_queue 負載高 -> controller 透過 scaling up 來加速消耗。
- 第二個階段為 items 快被拿完 -> worker_queue 的 items 稀缺 -> controller 透過 scaling down 來緩和消耗。
因為前期有較多的 consumers 整個寫檔案的任務也比較快的完成。   



#### Experimental setting 2
將 controller 的 check 頻率再調快**2**個 orders (check period = 100)    
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 100
```
#### Experimental result 2
<img src="images/period100.png" width="25%">  

#### Discussion 2  
行為與 experiment 1 差不多，scaling 的操作並沒有更多，
因為即使 check period 下降，造成的結果是前期 scaling up 的操作速率更快，  
但當 consumers 的數量大於 **4** (*4 為 producer 的數量*)時，worker_queue 的 items 便有減少的趨勢，  
worker_queue 的負載很快就掉到 80% 以下，scaling up 的操作便停止。
第二階段的行為與原因與 Experiment 1一樣，差別在於 scaling down 的操作速率更快。
### Different values of CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE and CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE
#### Experimental setting 3 
同時將 low threshold 調高，high threshold 調低 -> 平均來說更容易因為 worker_queue 的 items 量變化去 trigger controller.  
換句話說，如此設定目標為在將woker_queue的item數控制在半滿左右的狀態。
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 45
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 55
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 3
![image3](images/threshold45_55.png)
#### Discussion 3 
實驗結果顯示，相較於 `Experiment 0` controller 一開始確實更頻繁的去 scaling up。  



#### Experimental setting 4
同時將 low threshold 調低，high threshold 調高 -> 平均來說更不容易因為 worker_queue 的 items 量變化去 trigger controller.  
換句話說，controller會比較少干涉 controller 的數量只有在極端情況(*小於5%*或*大於95%*)才會去做scale consumer的操作。  
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 5
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 95
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 4
<img src="images/threshold5_95.png" width="25%">

#### Discussion 4 
實驗結果顯示，相較於上一個實驗設定(experiment 3) 整個過程中 controller 確實沒那麼頻繁的去做 scaling 的操作。
任務完成時間也相對較長。    
### Different values of WORKER_QUEUE_SIZE
#### Experimental setting 5
把 worker_queue 的 size 調降成原來的 1/10。只要 worker_queue 的負載超過 16 個 items，controller 就會做 scaling up，  
另一方面，只要負載低於 4 個，controller 便會做 Scaling down。
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 20
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 5
![image5](images/workerQsize20.png)
#### Discussion 5
從實驗結果顯示，對於大小只有 20 的 worker_queue 前期相對容易被填八成滿，controller 做連續的 scaling up。
沒有 scaling down 的操作，原因是因為整個任務在下一次的 check period 內就結束了，  
所以觀察不到 worker_queue items 剩下 4 個以下而 controller 去做 scaling down 的輸出。 


#### Experimental setting 6
將 worker_queue 增加 40 個位置的大小。只有 worker_queue 的負載在 192 以上時 controller 才會 scaling up。  
為了讓系統能夠順利運作 worker_queue 的 size 是有 upper bound 的如果 `input file` 的總行數為 *n* ， WORKER_QUEUE_SIZE <= *n\*80%*，  
如果 WORKER_QUEUE_SIZE 大於這個數字，即使全部 items 都已 enqueue 進 controller 也不會配置任何 consumer，整個系統會因為缺乏 consumer 而卡住。  
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 240
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 6
<img src="images/wokerQsize240.png" width="25%">  

#### Discussion 6
從實驗結果看來，整個過程只有一次 scaling up，即 items 超過 192 那一次，  
在第一次 scaling up 後剩餘在 input_queue 的 items 數也小於等於 4 個 (總共 200 個)。
整個系統在只有 1 個 consumer 的時候就在下一次check period 之前快速結束。

### WRITER_QUEUE_SIZE is very small
#### Experimental setting 7
將 writer_queue 的size變成原來的 1/1000。  
```cpp
#define READER_QUEUE_SIZE 200
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 7
<img src="images/writterQsize4.png" width="25%">  

#### Discussion 7 
從 scaling 的結果來看，相較於 experiment 0 沒有什麼太大的變化，因為 writer queue 的 size 只有 4 個，容易成為系統的 bottleneck，連帶影響 `worker_queue`，使它更容易滿，但因為系統設計上有動態調整的機制，所以整體速率維持沒有太大的變化。      

### READER_QUEUE_SIZE is very small
#### Experimental setting 8
將 reader queue size 調整成 5。並跑了多次實驗。
```cpp
#define READER_QUEUE_SIZE 5
#define WORKER_QUEUE_SIZE 200
#define WRITER_QUEUE_SIZE 4000
#define CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE 20
#define CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE 80
#define CONSUMER_CONTROLLER_CHECK_PERIOD 1000000
```
#### Experimental result 8
- run 1  
<img src="images/readerQsize5_1.png" width="25%"> 
 
- run 2     
<img src="images/readerQsize5_2.png" width="25%">  

#### Discussion 8  
從 scaling 的行為看來，並沒有跟 experiment 0 差太多，  
input_queue 小的話也會成為系統的 bottleneck，只是因為縮小幅度沒有像 experiment 7 一樣這麼嚴重，  
整體系統效能只有稍微影響。  

### Conclusion
1. 從八次實驗的觀察下來，最直接的觀察其實是縱使程式碼完全一模一樣，run多次，每次 threads 的運作行為也會不一樣，運行結果會受到 run time 時整個系統的狀態，以及 scheduler 的決定不一致，所造成結果是 nondeterministic 的。
2. 在一開始實作作業時，常常因為同步化沒有處理好產生 Deadlock，必須仔細的理解各個 threads 如何與他們的 **同步化資源** (像是*mutex_lock* 或 *condition variable*) 的互動，以及會有甚麼情況會造成 dealock，實際在實作作業的例子，是在實作 cleanup_handler 前，系統常因為卡在 cond_dequeue 的 consumer 被喚醒後帶著鎖被殺掉，而造成 deadlock，實作玩cleanup_handler後才避免了此問題發生。





# Implmentation

## Code Structure

### TSQueue
在`ts_queue.hpp`中利用 pthread lib 實作 threads safe queue，作為 buffer 空間的資料結構，  
目的是可以支援多個 threads concurrently 的 **共享**/**存取** 這個空間
1. cleanup_handler:  
   於 `Class TSQueue` 中實作 public member function `cleanup handler` 避免 threads 在 cond_wait 期間被 cancel 造成:  
   - signal 遺失
   - mutex 無法被正常 unlock
   -> 進而使系統 deadlock 的情況  
   ```cpp
	static void cleanup_handler(void* arg) {
        TSQueue<T>* queue = (TSQueue<T>*)arg;
        
		// 1. 補發 cond_enqueue 以及 cond_dequeue 的 signals
        pthread_cond_signal(&queue->cond_enqueue);
        pthread_cond_signal(&queue->cond_dequeue);
        
        // 2. 解鎖 Mutex
        pthread_mutex_unlock(&queue->mutex);
    }
    ```

2. constructor:  
   - 配置 **buffer** 空間，並以 **queue** 的結構管理，支援 `enqueue()`, `dequeue()`, `get_size()` 的 member functions  
   - 為了避免 **race condition** 以及 **維持 threads 之間對於 buffer 的同步化** 需要 `pthread lib` 的以下物件:  
     - `mutex lock:` ts_queue 自己的鎖
     - `condition variable:` 通知/控制 **producer threads** 以及 **consumer threads** 等待  
   ```cpp
    template <class T>
    TSQueue<T>::TSQueue(int buffer_size) : buffer_size(buffer_size) {
        // TODO: implements TSQueue constructor

        // 1. 在記憶體中配置 buffer 的空間
        buffer  = new T[buffer_size];
        // 2. initialize tsqueue 的初始狀態
        size = 0;
        head = 0;
        tail = 0;
        // 3. 初始化 mutex 以及 condition variables
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&cond_enqueue, nullptr); // 讓 producer waiting 的條件變數
        pthread_cond_init(&cond_dequeue, nullptr); // 讓 consumer waiting 的條件變數
    }
   ```  
3. destructor:  
   安全釋放 buffer 的記憶體空間以及同步化的資源(mutex lock, cond_enqueue, cond_dequeue)  
   ```cpp
    template <class T>
    TSQueue<T>::~TSQueue() {
        // TODO: implenents TSQueue destructor

        // 1. 釋放 buffer 的記憶體
        delete[] buffer;
        // 2. destory mutex鎖 以及 condition variables
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond_enqueue);
        pthread_cond_destroy(&cond_dequeue);
    }
    ```
4. enqueue:  
   其功能為從 `TSQueue` 的 tail 放入一個 item  
   其中處理同步化的 pthread工具 包含:  
   - mutex lock: 確保threads之間互斥存取，保護 TSQueue 中的共享變數 `buffer`, `tail`, `size`  
   - cleanup_handler: 處理當 thread 在 condition wait 時可能被 cancel 而帶著lock死去或丟失signal的情況  
   - condition wait: 處理當 buffer 滿時 `producer thread` 必須 wait on `cond_enqueue` 直到有 consumer dequeue 而 signal 它  
   - condition signal: 若有 consumer 因為 TSQueue 空而 wait on `cond_dequeue` 時去 signal 它  
   
   ```cpp
    template <class T>
    void TSQueue<T>::enqueue(T item) {
        // TODO: enqueues an element to the end of the queue

        pthread_mutex_lock(&mutex);

        // 預先註冊 cleanup_handler: 
        // 若在此區塊被 cancel 則會執行 cleanup_handler 釋放鎖以及補發 signal
        pthread_cleanup_push(TSQueue<T>::cleanup_handler, this);

        // 1. 當 buffer is full, 持續等待直到有 consumer dequeue
        while (size == buffer_size) {
            pthread_cond_wait(&cond_enqueue, &mutex);
        }
        
        pthread_cleanup_pop(0); // 若正常離開區塊: 移除 cleanup handler 登記

        // 2. 將 item 放入 buffer 的 tail
        buffer[tail] = item;
        tail = (tail + 1) % buffer_size; // update 尾端的 index
        size++; // update 新增一個 item 後的 size
        pthread_cond_signal(&cond_dequeue); // 嘗試 wake up consumer (因為 buffer 中有資料了, comsumer 若卡在 waiting condition 下可以被 waken up)
        pthread_mutex_unlock(&mutex);
    }
    ```
5. dequeue:
   其功能為從 `TSQueue` 的 head 拿出一個 item  
   其中處理同步化的 pthread工具 包含:  
   - mutex lock: 確保threads之間互斥存取，保護 TSQueue 中的共享變數 `buffer`, `head`, `size`  
   - cleanup_handler: 處理當 thread 在 condition wait 時可能被 cancel 而帶著lock死去或丟失signal的情況  
   - condition wait: 處理當 buffer 空時 `consumer thread` 必須 wait on `cond_dequeue` 直到有 producer enqueue 而 signal 它  
   - condition signal: 若有 produce 因為 TSQueue 滿而 wait on `cond_enqueue` 時去 signal 它  
   ```cpp
    template <class T>
    T TSQueue<T>::dequeue() {
        // TODO: dequeues the first element of the queue

        T item;
        pthread_mutex_lock(&mutex);

        // 註冊 cleanup_handler
        pthread_cleanup_push(TSQueue<T>::cleanup_handler, this);

        // 1. 當 buffer is empty, 則持續等待直到有 producer enqueue
        while (size == 0) {
            pthread_cond_wait(&cond_dequeue, &mutex);
        }
        pthread_cleanup_pop(0); // 若正常離開區塊: 移除 cleanup handler 登記
        
        // 2. 從 buffer 的 head 拿出 item
        item = buffer[head];
        head = (head + 1) % buffer_size; // update 頭端的 index
        size--; // update 減少一個 item 後的 size
        pthread_cond_signal(&cond_enqueue); // 嘗試 wake up producer (因為 buffer 中有空間了, producer 若卡在 waiting condition 下可以被 waken up)
        pthread_mutex_unlock(&mutex);

        return item;
    }
   ```
6. get_size:
   其功為取得`TSQueue`目前的 item 數  
   其中的同步化工具為:
   - mutex lock: 下面**問題探討**有說名其功用
   ```cpp
    template <class T>
    int TSQueue<T>::get_size() {
        // TODO: returns the size of the queue
        int current_size;
        pthread_mutex_lock(&mutex);
        current_size = size;
        pthread_mutex_unlock(&mutex);
        return current_size;
    }
   ```
   - **問題探討:**  
   在 get_size() 的實作中為什麼要加上 `mutex lock` 來保護 `size` 這個變數?  
   他明明只是**一條讀記憶體的指令(MOV)**，理論上不會產生 race condition，跟課堂中的例子(counter++)的情況不一樣。  
   - **解釋:**  
   在查詢相關資料後，在多核且多執行緒的環境下，每個 CPU 有自己的 local cache，如果一個 thread 在自己的 local cache 改變了 size 的值但並未更新到 main memory 其他 thread (e.g., comsumerController) 有可能讀到舊的值，而pthread 的 mutex lock 工具很強大，可以用來**同步記憶體**，所以在這邊的作用不是保證互斥，而是做記憶體階層的同步。   


### Reader
在`reader.hpp`中助教已實作 reader thread。  
1. start: (助教已實作)  
   
   ```cpp
    void Reader::start() {
        // 其中第三個 arg 是這類 thread 要執行的函式: process，助教已實作
        pthread_create(&t, 0, Reader::process, (void*)this);
    }
   ```
2. process: 助教已實作，skip
### Producer
1. start:
   Activate this `produce thread`  
   ```cpp
    void Producer::start() {
        // TODO: starts a Producer thread
        pthread_create(&t, 0, Producer::process, (void*)this);
    }
   ```
2. process:  
   `producer thread` 的功能是  
   -> 不斷從 `input_queue` 中取出 item  
   -> 按照 opcode 對該 item 做 transform  
   -> 把 item 放入 `woker_queue` 中  
   若取出的是 nullptr 則 回傳一個 nullptr  
   
   ```cpp
    void* Producer::process(void* arg) {
        // TODO: implements the Producer's work
        // producer 負責從 input_queue 中取出 item, 並丟到 worker_queue 中
        Producer* producer = (Producer*)arg;

        // 不斷從 input_queue 中拿 item 出來處理
        while(true) {
            // 1. 從 input_queue 取出 item
            Item* item = producer->input_queue->dequeue();

            if (item == nullptr) {
                // 若取出的 item 是 nullptr, 表示沒有更多 item 可以處理, 結束 producer thread
                break;
            }
            // 2. do the producing transformation
            item->val = producer->transformer->producer_transform(item->opcode, item->val);
            // 3. 將 transform 後的 item 放入 worker_queue
            producer->worker_queue->enqueue(item);
        }
        return nullptr;
    }
   ```
   - **問題探討:**  
   在迴圈中為什麼要用無窮 while(true) 迴圈的寫法，從 input_queue 中不斷取 item，直到取到 nullptr?  
   為何不是使用 `while(input_queue->get_size() > 0)` 先做檢查後才去取得item?  
   - **解釋:**  
   在 multi-thread programming 的環境下，會發生所謂的 **check-then-act** 的情況，當一個 thread check queue 有東西後，準備拿東西之前，就已經被contex switch，其他thread在原本的thread 還沒取 item 時，另一條 thread 可能優先把最後一個 item 取走(即 contex switch 發生在 check 與 act 之間)，**race conditoin** 就發生了。  
   為了避免上述情況，我們可以改為先取 item 之後再來判斷內容物狀態(是不是 nullptr)，即可化解上述 race condition。

### Writer
1. start:
   Activate this `writer thread`  
   ```cpp
    void Writer::start() {
        // TODO: starts a Writer thread
        // 第三個 arg 為此 thread 會去執行的 function
        pthread_create(&t, 0, Writer::process, (void*)this); 
    }
   ```
2. process:  
   `writer thread` 的作用是從 `output_queue` 總共取出 `expected_line` 個 `transform 過後的 items` 並寫到 output_file，  
   每個 `item` 一一對應 `expected_line` 行內容  
   為每個 item 最後一步處理，寫入檔案後釋放 item 的記憶體  
   ```cpp
    void* Writer::process(void* arg) {
        // TODO: implements the Writer's work

        Writer* writer = (Writer*)arg;

        // write expected_lines 行 item 到 output file
        for (int i = 0; i < writer->expected_lines; i++) {
            // 1. 從 output_queue 中取出 item
            Item* item = writer->output_queue->dequeue();

            // 2. 將 item 寫入 output file
            writer->ofs << *item;

            // 3. 寫入後釋放 item 記憶體
            delete item; 
        }
        return nullptr;
    }
   ```


### Consumer
1. start:  
   Activate this `consumer thread`
   ```cpp
    void Consumer::start() {
        // TODO: starts a Consumer thread
        pthread_create(&t, 0, Consumer::process, (void*)this);
    }
   ```
2. cancel:  
   題目要求 `consumer thread` 的數量隨 `worker_queue` 中的 `size` 動態變動的。  
   這邊先去設定`is_cancel`的狀態，再透過 `pthread_cancel` 做延遲 cancel。  
   `pthread_cancel(t)` 的作用並非立即取消 thread t，而是透過向該 thread 發送**取消請球**，再觀察它的**取消狀態(is_cancel)**以及**取消類型(type)**，  
   等到 **取消點** 像是 **pthread_cond_wait** 或 **pthread_testcancel()** 才會真正的依據其 **取消狀態** 做取消。
   ```cpp
    int Consumer::cancel() {
        // TODO: cancels the consumer thread
        is_cancel = true; // 宣告一個 cancel flag 作為取消標記
        return pthread_cancel(t);
    }
   ```
3. process:  
   `consumer` 的工作為負責從 worker_queue 中取出 item,經過 consumer_transform 後，丟到 output_queue 中。 
   在process的一開始透過 pthread 的`pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);`將取消類型設定為延遲取消，目的是讓thread能夠透過檢查狀態以及在安全的取消點被取消。   
   因為`consumer thread`可以被動態取消，若其取消狀態為 `false` (is_cancel == false)，  
   ->則進入 `while迴圈` 執行接下來的動作:    
   1. 透過 `pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);` 將 thread 的取消狀態設定成`ENABLE`，允許在**還沒拿到item時(可能等在cond_dequeue中)**被controller動態殺掉。
   2. 當consumer順利地取出item之後，它的取消狀態要透過`pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);`被改成禁止取消的狀態，避免握有 item 的被`consumer thread`被取消而丟失 item。   
   3. 接下來，透過`Item* item = consumer->worker_queue->dequeue();`從`worker_queue`中拿到 item  
   4. 對item做`consumer_transform`，過程中人要確保不被取消。  
   5. 回圈內的最後一步是，將 transformed 過後的 item 安全的 enqueue 到 `output_queue`，之後回到迴圈的開頭等待新工作。
   ```cpp
    void* Consumer::process(void* arg) {
        Consumer* consumer = (Consumer*)arg;

        // 設置 thread 的 cancel type 為 deferred cancelation:
        // ->不馬上取消 thread, 而是在 thread 執行到可以被取消的點時才取消
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);

        while (!consumer->is_cancel) {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);

            // TODO: implements the Consumer's work
            // 0. consumer 負責從 worker_queue 中取出 item, 並丟到 output_queue 中
            Item* item = consumer->worker_queue->dequeue();

            // 1. 拿到工作後，禁止被取消，確保 Item 不會丟失
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
            // 該consumer會回到迴圈一開始，等待新的工作。
            // delete consumer;
        }
        

        return nullptr;
    }
   ```

### ConsumerController
1. start:
   Activate this `ConsumerController thread`  
   ```cpp
    void ConsumerController::start() {
        // TODO: starts a ConsumerController thread
        pthread_create(&t, 0, ConsumerController::process, (void*)this);
    }
   ```
2. process:
   `ConsumerController`(以下簡稱controller)做的事是: 定期的去 check `woker_queue` 的 size，並根據 size 去決定要**新增consumer(scale up)**或**砍掉consumer(scale down)**  
   從程式碼的行為來說明:  
   1. 整個系統中只會有一個Controller，在進入`while(true)`迴圈後，會先透過`usleep(controller->check_period);`等待 check_period 的時間(定期檢查)  
   2. `int current_size = controller->worker_queue->get_size();`去 access 目前 worker_queue 的 size  
   3. 根據這個 size 去判斷要執行 **scale up** 或 **scale down** 的分支行為  
      1. 若 current_size > 上限(controller->high_threshold)時: 配置一個新的 `consumer thread` 並 啟動它，然後新增這個 consumer 到 controller 的 consumers list 中，方便後續管理。  
      2. 若 current_size < 下限(controller->low_threshold)時:  
         1. 從consumers list 中取得最後一個 consumer (first in first out)
         2. `target->cancel();`並送出一個cancel請求，欲cancel掉它
         3. `target->join();`等待這個`consumer thread`安全的被取消後
         4. `delete target;`才去釋放其對應的記憶體空間
         5. 上述的 scale down 完整操作完成後，最後才從 controller 的 consumers list 中移除這個 consumer
   4. 若`沒有要執行scale`或`執行完scale`的操作後，回到迴圈的一開始等待下一次的判斷與操作  
   ```cpp
    void* ConsumerController::process(void* arg) {
        // TODO: implements the ConsumerController's work
        ConsumerController* controller = (ConsumerController*)arg;

        // 透過 usleep 等待 check_period 後定期檢查 worker_queue 的 size
        while (true) {
            // 1. 等待 check_period 微秒
            usleep(controller->check_period);
            // 2. 取得 worker_queue 的 size
            int current_size = controller->worker_queue->get_size();
            // 3. 根據 current_size 決定scale up or scale down consumer

            if (current_size > controller->high_threshold) { // Scale up

                // scale up 前 consumers 的數量 
                int old_count = controller->consumers.size();
                // create 新的一個 comsumer
                Consumer* new_consumer = new Consumer(
                    controller->worker_queue,
                    controller->writer_queue,
                    controller->transformer
                );
                // start 這個新的 consumer thread
                new_consumer->start();
                // 新增這個 consumer 到 controller 的 consumers list 中
                controller->consumers.push_back(new_consumer);

                // scale up 後 consumers 的數量 (理論上應該為 old_count + 1)
                int new_count = controller->consumers.size();
                std::cout << "Scaling up consumers from " << old_count << " to " << new_count << std::endl;

            }else if (current_size < controller->low_threshold && controller->consumers.size() > 1) { // Scale down
                int old_count = controller->consumers.size();

                // 0. 取得最後一個 consumer
                Consumer* target = controller->consumers.back();

                // 1. cancel 這個 consumer thread
                target->cancel();
                // revision note:
                // 2. 等待 thread 結束
                target->join();
                // 3. 釋放 consumer 配置的記憶體
                delete target;

                // 從 controller 的 consumers list 中移除這個 consumer
                controller->consumers.pop_back();

                // scale down 後 consumers 的數量(理論上應該為 old_count - 1)
                int new_count = controller->consumers.size();
                std::cout << "Scaling down consumers from " << old_count << " to " << new_count << std::endl;
            
            }
        }
        return nullptr;
    }
   ```

### main
主程式將透過上述時做完成的執行續物件，模擬題目中描述的工作環境。
1. 初始化題目中的三個 thread safe queues: `input_queue`, `worker_queue`, `output_queue`。
2. 初始化 transformer 為後續 items 做 transform 的動作
3. 按題目要求初始化`1 reader thread`, `1 writer thread`, `4 producer threads`  
4. 初始化 controller 其中屬於 controller 的參數有:  
   1. `worker_queue`  
   2. `output_queue`
   3. `transformer`
   4. `CONSUMER_CONTROLLER_CHECK_PERIOD`
   5. `low_threshold = WORKER_QUEUE_SIZE * CONSUMER_CONTROLLER_LOW_THRESHOLD_PERCENTAGE / 100;`
   6. `high_threshold = WORKER_QUEUE_SIZE * CONSUMER_CONTROLLER_HIGH_THRESHOLD_PERCENTAGE / 100;`
5. 啟動`reader thread`, `writer thread`, `4 producer threads`, (起初不包含`consumer thread`)
6. 啟動`controller`，開始模擬工作，直到寫檔案的任務結束  
7. `main`必須要依序等待各個thread完成對應階段的工作才可以terminate，其中必須依序等待:  
   1. reader 讀完整個 input file 後
   2. producer 將 items 全部送完
   3. writer 寫完所有內容到 ouput file 後
8. 工作完成後，釋放所有thread的資源，安全的terminate  
```cpp
    int main(int argc, char** argv) {
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

        return 0;
    }
```


