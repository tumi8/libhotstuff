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

#include "hotstuff/robust_fetchcontext.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/liveness.h"
#include "hotstuff/retransmission.h"
#include "hotstuff/rtt_estimation.h"
#include "hotstuff/type.h"

#include <cassert>
#include <memory>

namespace hotstuff {

using salticidae::_1;
using salticidae::_2;
using salticidae::ElapsedTime;
using salticidae::PeerNetwork;

RobustBlockFetchContext::RobustBlockFetchContext(const uint256_t& ent_hash,
                                                 HotStuffBase* hs):
    promise_t([](promise_t) {}),
    hs(hs),
    rtt_estimation(*static_cast<HotStuffRetransmissionBase*>(hs)->rtt_estimation),
    fetch_finish(std::make_unique<promise_t>()),
    ent_hash(ent_hash),
    single_send(true) {
    // self promise resolves fetch_finish promise
    this->then([fetch_promise = fetch_finish.get()](const block_t& blk) { fetch_promise->resolve(blk); });
    fetch_finish->then([this](const block_t& blk) { on_fullfilled(blk); });

    fetch_msg = std::make_unique<MsgReqBlock>(std::vector<uint256_t>{ent_hash});
    timeout = TimerEvent(
        hs->ec, std::bind(&RobustBlockFetchContext::timeout_cb, this, _1));
}

RobustBlockFetchContext::RobustBlockFetchContext(
    RobustBlockFetchContext&& other) noexcept
    :
    promise_t(static_cast<const promise_t&>(other)),
    timeout(std::move(other.timeout)),
    hs(other.hs),
    rtt_estimation(other.rtt_estimation),
    fetch_finish(std::move(other.fetch_finish)),
    fetch_msg(std::move(other.fetch_msg)),
    ent_hash(other.ent_hash),
    leaderID(other.leaderID),
    single_send(other.single_send) {
    // the self promise already resolves the fetch_finished promise.
    // we now overwrite the promise at that memory location to instead call our own promise
    // with the correct this referece
    fetch_finish->~promise_t();
    new (fetch_finish.get()) promise_t();
    fetch_finish->then([this](const block_t& blk) {
        on_fullfilled(blk);
    });

    // also reset the timeout callback to have the right this reference
    timeout.set_callback(std::bind(&RobustBlockFetchContext::timeout_cb, this, _1));
}

void RobustBlockFetchContext::reset_timeout(const ReplicaID& rid) {
    double rto = rtt_estimation.getTimeout(rid);
    timeout.add(rto);
}

void RobustBlockFetchContext::timeout_cb(TimerEvent&) {
    HOTSTUFF_LOG_WARN("block fetching %.10s timeout",
                      salticidae::get_hex(ent_hash).c_str());
    single_send = false;
    rtt_estimation.backoff(leaderID);
    send(leaderID);
}

void RobustBlockFetchContext::send(const ReplicaID& rid) {
    const PeerId& replica = hs->get_config().get_peer_id(rid);
    hs->part_fetched_replica[replica]++;
    HOTSTUFF_LOG_INFO("Sending msg req block for %.10s to replica %d", ent_hash.to_hex().c_str(), rid);
    reset_timeout(rid);
    elapsed.start();
    hs->pn.send_msg(*fetch_msg, replica);
}

void RobustBlockFetchContext::add_replica(const PeerId& replica,
                                          bool fetch_now) {
    bool leader_change = leaderID != hs->get_pace_maker()->get_proposer();
    if (leader_change) {
        if(leaderID == (ReplicaID) -1) single_send = false;
        leaderID = hs->get_pace_maker()->get_proposer();
        if (fetch_now) {
            send(leaderID);
        }
    }
}

void RobustBlockFetchContext::on_fullfilled(const block_t&) {
    HOTSTUFF_LOG_INFO("Received MsgRespBlock for %.10s", ent_hash.to_hex().c_str());
    // only add measurement if there was no second sending
    if (!single_send) return;
    elapsed.stop();
    rtt_estimation.addMeasurement(0.1, leaderID);
}

} // namespace hotstuff
