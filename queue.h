// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template <class T>
class Queue
{
private:
	std::condition_variable cv;
	mutable std::mutex m;
	std::queue<T> q;

public:
	Queue(void)
	{
	}

	~Queue(void)
	{
	}

	void push(T t)
	{
		std::lock_guard<std::mutex> lock(m);
		q.push(t);
		cv.notify_one();
	}

	T pop(void)
	{
		std::unique_lock<std::mutex> lock(m);

		while(q.empty())
			cv.wait(lock);

		T val = q.front();
		q.pop();

		return val;
	}
};
