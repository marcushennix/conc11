#pragma once

#include "FunctionTraits.h"
#include "TaskUtils.h"
#include "TimeIntervalCollector.h"
#include "Types.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace conc11
{

enum TaskStatus
{
	TsPending,
	TsScheduledOnce,
	TsScheduledPolling,
	TsDone,
	TsInvalid
};

class TaskBase /*abstract*/
{
public:

	virtual void operator()() = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual void setStatus(TaskStatus status) = 0;
	virtual bool isContinuation() const = 0;
	virtual const std::vector<std::shared_ptr<TaskBase>>& getDependencies() const = 0;
	virtual std::shared_ptr<TimeIntervalCollector> getTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) = 0;
	virtual void setTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) = 0;

	inline static unsigned int getInstanceCount()
	{
		return s_instanceCount;
	}

protected:

	static unsigned int s_instanceCount;
};

template<typename T>
class Task : public TaskBase, public std::enable_shared_from_this<Task<T>>
{
public:

	typedef T ReturnType;

	Task(const std::string& name = "", const float* color = nullptr, bool isContinuation = false)
		: m_name(name)
		, m_status(TsInvalid)
		, m_isContinuation(isContinuation)
	{
		s_instanceCount++;
		
		if (color)
		{
			m_debugColor[0] = color[0];
			m_debugColor[1] = color[1];
			m_debugColor[2] = color[2];
		}
		else
		{
			m_debugColor[0] = 1.0f;
			m_debugColor[1] = 1.0f;
			m_debugColor[2] = 1.0f;
		}

		reset();
	}

	virtual ~Task()
	{
		assert(m_status != TsPending || m_status != TsScheduledOnce || m_status != TsScheduledPolling);
		s_instanceCount--;
	}

	virtual void operator()() final
	{
		assert(m_function);
				
		{
			ScopedTimeInterval scope(m_collector, m_name, m_debugColor);
			
			m_function();
		}

		assert(m_status != TsInvalid);

		if (m_status == TsDone && !m_continuation.expired())
		{
			if (std::shared_ptr<TaskBase> c = m_continuation.lock())
			{
				(*c)();
			}
			else
			{
				assert(false);
			}
		}
	}

	virtual TaskStatus getStatus() const final
	{
		return m_status;
	}

	virtual void setStatus(TaskStatus status) final
	{
		if (m_status == TsDone && status != TsDone)
			reset();

		m_status = status;
	}

	virtual bool isContinuation() const final
	{
		return m_isContinuation;
	}

	virtual const std::vector<std::shared_ptr<TaskBase>>& getDependencies() const final
	{
		return m_dependencies;
	}

	virtual std::shared_ptr<TimeIntervalCollector> getTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) final
	{
		return m_collector;
	}

	virtual void setTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) final
	{
		m_collector = collector;
	}

	inline void reset()
	{
		m_promise = std::make_shared<std::promise<ReturnType>>();
		m_future = m_promise->get_future().share();
	}

	inline const float* getDebugColor() const
	{
		return m_debugColor;
	}

	inline void setDebugColor(const float color[3])
	{
		m_debugColor[0] = color[0];
		m_debugColor[1] = color[1];
		m_debugColor[2] = color[2];
	}

	inline const std::function<void()>& getFunction() const
	{
		return m_function;
	}

	inline void setFunction(const std::function<void()>& f)
	{
		m_function = f;
	}

	inline void moveFunction(std::function<void()>&& f)
	{
		m_function = std::forward<std::function<void()>>(f);
	}

	inline const std::weak_ptr<TaskBase>& getContinuation() const
	{
		return m_continuation;
	}

	inline void setContinuation(const std::shared_ptr<TaskBase>& c)
	{
		m_continuation = c;
	}

	inline const std::shared_ptr<std::promise<ReturnType>>& getPromise() const
	{
		return m_promise;
	}

	inline const std::shared_future<ReturnType>& getFuture() const
	{
		return m_future;
	}

	template<typename U>
	inline void addDependencies(const std::vector<std::shared_ptr<U>>& deps)
	{
		m_dependencies.insert(m_dependencies.end(), deps.begin(), deps.end());
	}

	template<typename U, typename... Args>
	inline void addDependencies(const std::shared_ptr<Task<U>>& d0, const std::shared_ptr<Task<Args>>&... dn)
	{
		m_dependencies.push_back(d0);
		addDependencies(dn...);
	}

	inline void addDependencies()
	{
	}

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, const std::string& name = "", const float* color = nullptr)
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto t = std::make_shared<Task<ThenReturnType>>(name, color, true);
		Task<ThenReturnType>& tref = *t;
		auto tf = std::function<void()>([this, &tref, f]
		{
			trySetFuncResult(*tref.getPromise(), f, m_future,
				std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
				std::is_void<typename FunctionTraits<Func>::ReturnType>(),
				std::is_assignable<ReturnType, typename FunctionTraits<Func>::template Arg<0>::Type>());

			tref.setStatus(TsDone);
		});

		t->moveFunction(std::move(tf));
		t->addDependencies(this->shared_from_this());

		m_continuation = t;

		return t;
	}

private:

	Task(const Task&);
	Task& operator=(const Task&);

	std::function<void()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
	std::weak_ptr<TaskBase> m_continuation;
	std::vector<std::shared_ptr<TaskBase>> m_dependencies;
	std::shared_ptr<TimeIntervalCollector> m_collector;
	std::string m_name;
	float m_debugColor[3];
	TaskStatus m_status;
	bool m_isContinuation;
};

} // namespace conc11
