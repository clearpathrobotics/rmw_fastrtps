// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RMW_FASTRTPS_CPP__CUSTOM_SUBSCRIBER_INFO_HPP_
#define RMW_FASTRTPS_CPP__CUSTOM_SUBSCRIBER_INFO_HPP_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "fastrtps/subscriber/SampleInfo.h"
#include "fastrtps/subscriber/Subscriber.h"
#include "fastrtps/subscriber/SubscriberListener.h"

#include "rcutils/logging_macros.h"

class SubListener;

typedef struct CustomSubscriberInfo
{
  eprosima::fastrtps::Subscriber * subscriber_;
  SubListener * listener_;
  void * type_support_;
  const char * typesupport_identifier_;
} CustomSubscriberInfo;

class SubListener : public eprosima::fastrtps::SubscriberListener
{
public:
  explicit SubListener(CustomSubscriberInfo * info)
  : data_(0)
  , conditionMutex_(nullptr)
  , conditionVariable_(nullptr)
  , customSubscriberInfo_(info)
  {
    // Field is not used right now
    (void)info;
  }

  void
  onSubscriptionMatched(
    eprosima::fastrtps::Subscriber * sub, eprosima::fastrtps::MatchingInfo & info)
  {
    (void)sub;
    (void)info;
  }

  void
  onNewDataMessage(eprosima::fastrtps::Subscriber * sub)
  {
    std::lock_guard<std::mutex> lock(internalMutex_);

    if (conditionMutex_ != nullptr) {
      std::unique_lock<std::mutex> clock(*conditionMutex_);
      // the change to data_ needs to be mutually exclusive with rmw_wait()
      // which checks hasData() and decides if wait() needs to be called
      data_ = sub->getUnreadCount();
      clock.unlock();
      conditionVariable_->notify_one();
    } else {
      data_ = sub->getUnreadCount();
    }
  }

  void
  attachCondition(std::mutex * conditionMutex, std::condition_variable * conditionVariable)
  {
    std::lock_guard<std::mutex> lock(internalMutex_);
    conditionMutex_ = conditionMutex;
    conditionVariable_ = conditionVariable;
  }

  void
  detachCondition()
  {
    std::lock_guard<std::mutex> lock(internalMutex_);
    conditionMutex_ = nullptr;
    conditionVariable_ = nullptr;
  }

  bool
  hasData()
  {
    uint64_t real_cnt = (customSubscriberInfo_->subscriber_)? customSubscriberInfo_->subscriber_->getUnreadCount(): -1;
    uint64_t cache_cnt = data_;
    RCUTILS_LOG_WARN_EXPRESSION_NAMED((cache_cnt > real_cnt), "rmw_fastrtps_cpp", "rtps history byte count missmatch: data_ %d > actual %d", cache_cnt, real_cnt);
    return data_ > 0;
  }

  bool
  takeNextData(void* buffer, eprosima::fastrtps::SampleInfo_t* info)
  {
    bool taken = false;
    uint64_t data = data_;

    taken = customSubscriberInfo_->subscriber_->takeNextData(buffer, info);

    RCUTILS_LOG_WARN_EXPRESSION_NAMED(!taken, "rmw_fastrtps_cpp", "takeNextData failed");

    std::lock_guard<std::mutex> lock(internalMutex_);
    if (conditionMutex_ != nullptr) {
      std::unique_lock<std::mutex> clock(*conditionMutex_);
      data_ = taken? data_ - 1: data_ - data;
    } else {
      data_ = taken? data_ - 1: data_ - data;
    }
    return taken;
  }

private:
  std::mutex internalMutex_;
  std::atomic_size_t data_;
  std::mutex * conditionMutex_;
  std::condition_variable * conditionVariable_;
  CustomSubscriberInfo * customSubscriberInfo_;
};

#endif  // RMW_FASTRTPS_CPP__CUSTOM_SUBSCRIBER_INFO_HPP_
