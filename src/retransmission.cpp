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

#include "hotstuff/retransmission.h"

#include <iostream>
#include <memory>
#include <utility>

#include "hotstuff/consensus.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/liveness.h"
#include "hotstuff/robust_fetchcontext.h"
#include "hotstuff/rtt_estimation.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"

namespace hotstuff {

const opcode_t MsgPingRTT::opcode;
const opcode_t MsgPongRTT::opcode;

HotStuffRetransmissionBase::HotStuffRetransmissionBase(
    uint32_t blk_size, ReplicaID rid, privkey_bt&& priv_key, NetAddr listen_addr,
    pacemaker_bt pmaker,
    RetransmissionConfig& retransmission_config,
    EventContext ec, size_t nworker, const Net::Config& netconfig,
    std::unordered_map<uint32_t, std::set<uint16_t>>& disabled_votes):
    HotStuffBase(blk_size, rid, std::move(priv_key), listen_addr, std::move(pmaker),
                 std::move(ec), nworker, netconfig, disabled_votes),
    config(retransmission_config),
    rtt_estimation(),
    vote_timings() {
    pn.reg_handler(salticidae::generic_bind(
        &HotStuffRetransmissionBase::retransmission_propose_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(
        &HotStuffRetransmissionBase::retransmission_vote_handler, this, _1, _2));

    pn.reg_handler(salticidae::generic_bind(
        &HotStuffRetransmissionBase::retransmission_ping_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(
        &HotStuffRetransmissionBase::retransmission_pong_handler, this, _1, _2));
}

void HotStuffRetransmissionBase::start(std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>>&& replicas, bool ec_loop) {
    HotStuffBase::start(std::move(replicas), ec_loop);

    HOTSTUFF_LOG_INFO("Starting HotStuffRetransmissionBase");
    // callback for vote timing -- only for first leader
    if (this->get_id() == get_pace_maker()->get_proposer()) {
        reg_process_proposal_leader_start();
        proposer_in_first_round = true;
    }
    // timings
    vote_timings.reserve(get_config().nreplicas);
    for (size_t i = 0; i < get_config().nreplicas; ++i) {
        // seems to work. In other instances std::bind is used (when also directly passing a function)
        vote_timings.emplace_back(this->ec, [this, i](TimerEvent&) {
            vote_timeout_callback(i);
        });
    }
    // rtt_estimation, nreplicas + 1 so that we can use the last entry for the processing time estimation
    switch (config.rtt_estimator) {
        case TCPEstimator:
            rtt_estimation = std::make_unique<RTTEstimationTCP>(get_config().nreplicas,
                                                                config.alpha, config.beta,
                                                                config.upper_bound, config.lower_bound);
            break;
        case VectorEstimator:
            rtt_estimation = std::make_unique<RTTEstimationVector>(get_config().nreplicas,
                                                                   config.saved_measurements);
            break;
    }
    proposal_procesing_estimation = std::make_unique<RTTEstimationTCP>(1,
                                                                       config.alpha, 1.1,
                                                                       config.upper_bound, 0);
    // initial rtt values
    for (size_t i = 0; i < get_config().nreplicas; ++i)
        rtt_estimation->addMeasurement(config.initial_rtt, i);
    // use our rtt estimation ping
    std::unique_ptr<RTTEstimationBase> tmp;
    rtt_estimation.swap(tmp);
    rtt_estimation = std::make_unique<RTTEstimationWithPing>(std::move(tmp), 5, *this, pn, ec);
}

void HotStuffRetransmissionBase::retransmission_propose_handler(MsgPropose&& msg,
                                                                const Net::conn_t& conn) {
    if (conn->get_peer_id().is_null()) return;
    if (currentProposal.blk) { // stop timouts of last proposal
        currentProposal = Proposal();
        for (auto& t : vote_timings) {
            t.timeout.del();
            t.elapsed.stop();
            t.active = false;
        }
    }
    auto msg_copy = msg;
    msg_copy.postponed_parse(this);

    // check if I need to resend a previous vote
    if (currentVote && msg_copy.proposal.blk->get_hash() == currentVoteHash) {
        PeerId a = conn->get_peer_id();
        PeerId b = get_config().get_peer_id(msg_copy.proposal.proposer);
        pn.send_msg(*currentVote, conn->get_peer_id());
        HOTSTUFF_LOG_INFO("Resending vote for block %.10s", currentVoteHash.to_hex().c_str());
        return;
    }

    proposer_in_first_round = false;
    proposal_processing_elapsed.start();
    HotStuffBase::propose_handler(std::move(msg), conn);
}

void HotStuffRetransmissionBase::retransmission_vote_handler(MsgVote&& msg,
                                                             const Net::conn_t& conn) {
    auto msg_copy = msg;
    msg_copy.postponed_parse(this);
    auto rid = msg_copy.vote.voter;

    // measuring
    if (msg_copy.vote.blk_hash == currentProposal.blk->get_hash()) { // sanity check -- retransmission might falsly stop the timer
        auto& timing = vote_timings[rid];
        if (timing.active && timing.first_send) {
            timing.elapsed.stop();
            timing.active = false;
            double measuredTime = timing.elapsed.elapsed_sec - proposal_procesing_estimation->getEstimation(0);
            if (measuredTime <= 0) {
                HOTSTUFF_LOG_INFO("Measurement for replica %d non existent, when subtracting processing time: %f", rid, measuredTime);
            } else {
                HOTSTUFF_LOG_INFO("Measurement for replica %d of %f seconds.", rid, measuredTime);
                rtt_estimation->addMeasurement(measuredTime, rid);
            }
        }
        timing.timeout.del();
    }

    // back to HoStuff
    HotStuffBase::vote_handler(std::move(msg), conn);
}

void HotStuffRetransmissionBase::do_broadcast_proposal(const Proposal& prop) {
    // timing finish for self vote. Self vote should now be done
    update_processing_estimation_leader();
    HOTSTUFF_LOG_INFO("processing time is self vote measurement.");

    currentProposal = prop;
    for (ReplicaID rid = 0; rid < get_config().nreplicas; rid++) {
        if (rid == get_id()) continue;
        reset_retransmission_timeout(rid, true);
    }
    HotStuffBase::do_broadcast_proposal(prop);
}

void HotStuffRetransmissionBase::do_vote(ReplicaID last_proposer, const Vote& vote,
                                         bool dont_send) {
    // save vote and its hash into currentVote
    new (&currentVote) std::optional(MsgVote(vote));
    currentVoteHash = vote.blk_hash;

    HotStuffBase::do_vote(last_proposer, vote, dont_send);
    // here explicitly time AFTER HotStuffBase logic
    update_processing_estimation();
}

void HotStuffRetransmissionBase::vote_timeout_callback(ReplicaID rid) {
    HOTSTUFF_LOG_INFO("Vote timed out for Replica %d for block %.10s. Retransmitting.", rid, currentProposal.blk->get_hash().to_hex().c_str());
    pn.send_msg(MsgPropose(currentProposal), get_config().get_peer_id(rid));
    rtt_estimation->backoff(rid);
    reset_retransmission_timeout(rid, false);
}

//------------------------------------------------------------------------------------------

void HotStuffRetransmissionBase::reg_process_proposal_leader_start() {
    async_pre_process_proposal().then([this](const Proposal& prop) {
        if (!proposer_in_first_round) return;
        proposal_processing_elapsed_leader.start();
        reg_process_proposal_leader_start();
    });
}

void HotStuffRetransmissionBase::update_processing_estimation() {
    proposal_processing_elapsed.stop();
    HOTSTUFF_LOG_INFO("Process proposal time: %.8f", proposal_processing_elapsed.elapsed_sec);
    proposal_procesing_estimation->addMeasurement(proposal_processing_elapsed.elapsed_sec, 0);
}

void HotStuffRetransmissionBase::update_processing_estimation_leader() {
    if (!proposer_in_first_round) return;
    proposal_processing_elapsed_leader.stop();
    HOTSTUFF_LOG_INFO("Process proposal time leader: %.8f", proposal_processing_elapsed_leader.elapsed_sec);
    proposal_procesing_estimation->addMeasurement( // using the experimentally detected diff of 2.8 -> use 1.4 for more tolerance
        proposal_processing_elapsed_leader.elapsed_sec * 1.4, 0);
}

void HotStuffRetransmissionBase::reset_retransmission_timeout(ReplicaID rid, bool first_send) {
    auto& timing = vote_timings[rid];
    timing.first_send = first_send;
    if (first_send) {
        timing.active = true;
        timing.elapsed.start();
    }
    HOTSTUFF_LOG_INFO("Timeout for %d set to %.4f", rid, rtt_estimation->getTimeout(rid));
    timing.timeout.add(rtt_estimation->getTimeout(rid) + proposal_procesing_estimation->getTimeout(0));
}

//------------------------------------------------------------------------------------------
// ping pong methods
void HotStuffRetransmissionBase::retransmission_ping_handler(MsgPingRTT&& msg,
                                                             const Net::conn_t& conn) {
    assert(msg.rid == id);
    pn.send_msg(MsgPongRTT(id), conn->get_peer_id());
}
void HotStuffRetransmissionBase::retransmission_pong_handler(MsgPongRTT&& msg,
                                                             const Net::conn_t& conn) {
    static_cast<RTTEstimationWithPing&>(*rtt_estimation).addPingMeasurement(msg.rid);
}

} // namespace hotstuff
