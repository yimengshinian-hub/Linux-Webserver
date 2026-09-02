#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H
#include<queue>
#include<unordered_map>
#include<time.h>
#include<algorithm>
#include<arpa/inet.h>
#include<functional>
#include<assert.h>
#include<chrono>
#include"log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;//定时器的到期时间点

struct TimerNode
{
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& t){
        return expires < t.expires;
    }
};
class HeapTimer{//最小堆计时器(父节点小于等于子节点)
public:
    HeapTimer(){heap_.reserve(64);}
    ~HeapTimer(){clear();}
    void adjust(int id,int newExpires);
    void add(int id,int timeOut,const TimeoutCallBack& cb);
    void doWork(int id);
    void clear();
    void tick();
    void pop();
    int GetNextTick();
private:
    void del_(size_t i);
    void siftup_(size_t i);//上滤
    bool siftdown_(size_t index,size_t n);//下滤
    void SwapNode_(size_t i,size_t j);//交换两个节点
    std::vector<TimerNode> heap_;
    std::unordered_map<int,size_t> ref_;
};
#endif // !HEAP_TIMER_H