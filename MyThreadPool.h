#ifndef __MY_THREAD_POOL__
#define __MY_THREAD_POOL__

#include <mutex>
#include <vector>
#include <queue>
#include <memory>
#include <future>
#include <atomic>
#include <condition_variable>
#include <iostream>

class MyThreadPool {
private:
	using Task = std::packaged_task<void()>;
	std::mutex				 mutex_;
	std::condition_variable  cond_;
	std::atomic_bool		 stop_;
	std::atomic_int			 thread_count_;
	std::vector<std::thread> threads_;
	std::queue<Task>		 tasks_;
public:
	MyThreadPool(size_t size = std::thread::hardware_concurrency()) {
		if (size < 1) {
			thread_count_ = 1;
		}
		else {
			thread_count_ = size;
		}

		start();
	}

	~MyThreadPool() {
		{
			std::lock_guard lock(mutex_);
			stop_.store(true);
		}
		cond_.notify_all();

		for (auto& t : threads_) {
			if (t.joinable()) {
				std::cout << "thread " << t.get_id() << " joined" << std::endl;
				t.join();		
			}
		}
	}

	MyThreadPool(const MyThreadPool&) = delete;

	MyThreadPool& operator=(const MyThreadPool&) = delete;

	void start() {
		for (unsigned int i = 0; i < thread_count_; ++i) {
			threads_.emplace_back([this, i]() {
				while (true) {
					Task task;
					{
						std::unique_lock lock(mutex_);
						cond_.wait(lock, [this]() {
							return stop_.load() || !tasks_.empty();
						});

						if (stop_.load() && tasks_.empty()) {
							return;
						}

						task = std::move(tasks_.front());
						tasks_.pop();
					}
					task();
				}
			});
		}
	}

	template<class F, class... Args>
	auto commit(F&& f, Args&&... args) -> std::future<decltype(std::invoke(std::forward<F>(f), std::forward<Args>(args)...))> {
		using TaskType = decltype(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
		if (stop_.load()) {
			return std::future<TaskType>{ };
		}
		auto task = std::make_shared<std::packaged_task<TaskType()>>(
			[f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
				return std::invoke(f, std::move(args)...);
			});
		std::future<TaskType> res = task->get_future();
		{
			std::lock_guard lock(mutex_);
			tasks_.emplace([task]() {(*task)(); });
		}
		cond_.notify_one();
		return res;
	}
};

#endif // !__MY_THREAD_POOL__

