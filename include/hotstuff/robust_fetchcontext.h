/**
 * Copyright 2023 Chair of Network Architectures and Services, Technical University of Munich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HOTSTUFF_ROBUST_FETCHCONTEXT_H
#define HOTSTUFF_ROBUST_FETCHCONTEXT_H

#include <unordered_set>

#include "salticidae/event.h"
#include "salticidae/network.h"
#include "salticidae/stream.h"

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "salticidae/util.h"

namespace hotstuff {

using promise::promise_t;
using TimerEvent = salticidae::TimerEvent;
using ElapsedTime = salticidae::ElapsedTime;
using PeerId = salticidae::PeerId;

class HotStuffBase;
class MsgReqBlock;
class RTTEstimationBase;

class RobustBlockFetchContext: public promise_t {
    TimerEvent timeout;
    ElapsedTime elapsed;
    HotStuffBase* hs;
    RTTEstimationBase& rtt_estimation;
    std::unique_ptr<promise_t> fetch_finish;

    std::unique_ptr<MsgReqBlock> fetch_msg;
    const uint256_t ent_hash;
    ReplicaID leaderID = -1;
    bool single_send;

    public:
    RobustBlockFetchContext(const RobustBlockFetchContext&) = delete;
    RobustBlockFetchContext& operator=(const RobustBlockFetchContext&) = delete;
    RobustBlockFetchContext(RobustBlockFetchContext&& other) noexcept;

    RobustBlockFetchContext(const uint256_t& ent_hash, HotStuffBase* hs);
    ~RobustBlockFetchContext() = default;

    void add_replica(const PeerId& replica, bool fetch_now = true);
    void reset_timeout(const ReplicaID& rid);

    private:
    void timeout_cb(TimerEvent&);
    void send(const ReplicaID& rid);
    void on_fullfilled(const block_t& blk);
};

} // namespace hotstuff

#endif // HOTSTUFF_ROBUST_FETCHCONTEXT_H
