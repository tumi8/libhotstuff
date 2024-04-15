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

#ifndef HOTSTUFF_RETRANSMISSION_H
#define HOTSTUFF_RETRANSMISSION_H

#include "hotstuff/consensus.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/robust_fetchcontext.h"
#include "hotstuff/type.h"
#include "rtt_estimation.h"
#include "salticidae/event.h"
#include "salticidae/msg.h"
#include "salticidae/network.h"
#include "salticidae/stream.h"
#include "salticidae/util.h"
#include <memory>
#include <vector>
#include <optional>

namespace hotstuff {

struct MsgPingRTT {
    static const opcode_t opcode = 0x7;
    DataStream serialized;
    ReplicaID rid;
    MsgPingRTT(ReplicaID rid) { serialized << rid; }
    MsgPingRTT(DataStream&& s):
        serialized(std::move(s)) { serialized >> rid; }
};

struct MsgPongRTT {
    static const opcode_t opcode = 0x8;
    DataStream serialized;
    ReplicaID rid;
    MsgPongRTT(ReplicaID rid) { serialized << rid; }
    MsgPongRTT(DataStream&& s):
        serialized(std::move(s)) { serialized >> rid; }
};

struct Timing {
    TimerEvent timeout;
    ElapsedTime elapsed;
    /* double send_time; */
    bool first_send;
    bool active;

    Timing(const EventContext& ec, TimerEvent::callback_t&& callback):
        timeout(ec, std::move(callback)), elapsed(), first_send(false), active(false) {}
    Timing():
        timeout(), elapsed(), first_send(false), active(false) {}
};

enum RTTEstimator {
    TCPEstimator,
    VectorEstimator
};

struct RetransmissionConfig {
    // general values
    double initial_rtt;

    // values for tcp estimator
    double lower_bound;
    double upper_bound;
    double alpha;
    double beta;

    // values for vector estimator
    int saved_measurements;
    RTTEstimator rtt_estimator;
};

class HotStuffRetransmissionBase: public HotStuffBase {
    friend RobustBlockFetchContext;

    RetransmissionConfig config;

    std::unique_ptr<RTTEstimationBase> rtt_estimation;
    std::vector<Timing> vote_timings;

    // information needed for retransmitting
    Proposal currentProposal;

    std::optional<MsgVote> currentVote;
    uint256_t currentVoteHash;

    ElapsedTime proposal_processing_elapsed;
    ElapsedTime proposal_processing_elapsed_leader;
    std::unique_ptr<RTTEstimationBase> proposal_procesing_estimation;

    bool proposer_in_first_round;

    public:
    HotStuffRetransmissionBase(uint32_t blk_size, ReplicaID rid,
                               privkey_bt&& priv_key, NetAddr listen_addr,
                               pacemaker_bt pmaker,
                               RetransmissionConfig& retransmission_config,
                               EventContext ec, size_t nworker,
                               const Net::Config& netconfig,
                               std::unordered_map<uint32_t, std::set<uint16_t>>& disabled_votes);

    ~HotStuffRetransmissionBase() = default;

    void start(std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>>&& replicas,
               bool ec_loop = false);

    protected:
    // new message handlers
    inline void retransmission_propose_handler(MsgPropose&&, const Net::conn_t&);
    inline void retransmission_vote_handler(MsgVote&&, const Net::conn_t&);

    inline void retransmission_ping_handler(MsgPingRTT&&, const Net::conn_t&);
    inline void retransmission_pong_handler(MsgPongRTT&&, const Net::conn_t&);
    // inline void retransmission_pong(MsgPongRTT&&, const Net::conn_t&);

    // override proposal and vote messages
    void do_broadcast_proposal(const Proposal&) override;
    void do_vote(ReplicaID, const Vote&, bool dont_send = false) override;

    void vote_timeout_callback(ReplicaID rid);

    public:
    EventContext& get_event_context() { return ec; }
    Net& get_peer_network() { return pn; }

    private:
    void update_processing_estimation();
    void update_processing_estimation_leader();
    void reg_process_proposal_leader_start();
    void reset_retransmission_timeout(ReplicaID rid, bool first_send);
};

/** HotStuff protocol (templated by cryptographic implementation). */
/** rewritten for retransmission class */
template<typename PrivKeyType = PrivKeyDummy,
         typename PubKeyType = PubKeyDummy,
         typename PartCertType = PartCertDummy,
         typename QuorumCertType = QuorumCertDummy>
class HotStuffRetransmission: public HotStuffRetransmissionBase {
    using HotStuffRetransmissionBase::HotStuffRetransmissionBase;

    protected:
    part_cert_bt create_part_cert(const PrivKey& priv_key, const uint256_t& blk_hash) override {
        HOTSTUFF_LOG_DEBUG("create part cert with priv=%s, blk_hash=%s",
                           get_hex10(priv_key).c_str(), get_hex10(blk_hash).c_str());
        return new PartCertType(
            static_cast<const PrivKeyType&>(priv_key),
            blk_hash);
    }

    part_cert_bt parse_part_cert(DataStream& s) override {
        PartCert* pc = new PartCertType();
        s >> *pc;
        return pc;
    }

    quorum_cert_bt create_quorum_cert(const uint256_t& blk_hash) override {
        return new QuorumCertType(get_config(), blk_hash);
    }

    quorum_cert_bt parse_quorum_cert(DataStream& s) override {
        QuorumCert* qc = new QuorumCertType();
        s >> *qc;
        return qc;
    }

    public:
    HotStuffRetransmission(uint32_t blk_size,
                           ReplicaID rid, // this is actually the idx
                           const bytearray_t& raw_privkey,
                           NetAddr listen_addr,
                           pacemaker_bt pmaker,
                           RetransmissionConfig& retransmission_config,
                           EventContext ec = EventContext(),
                           size_t nworker = 4,
                           const Net::Config& netconfig = Net::Config(),
                           std::unordered_map<uint32_t, std::set<uint16_t>> disabled_votes = std::unordered_map<uint32_t, std::set<uint16_t>>()):
        HotStuffRetransmissionBase(blk_size,
                                   rid,
                                   new PrivKeyType(raw_privkey),
                                   listen_addr,
                                   std::move(pmaker),
                                   retransmission_config,
                                   ec,
                                   nworker,
                                   netconfig,
                                   disabled_votes) {}

    void start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>>& replicas, bool ec_loop = false) {
        std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> reps;
        for (auto& r : replicas)
            reps.push_back(
                std::make_tuple(
                    std::get<0>(r),
                    new PubKeyType(std::get<1>(r)),
                    uint256_t(std::get<2>(r))));
        HotStuffRetransmissionBase::start(std::move(reps), ec_loop);
    }
};

using HotStuffRetransmissionSecp256k1 = HotStuffRetransmission<PrivKeySecp256k1, PubKeySecp256k1,
                                                               PartCertSecp256k1, QuorumCertSecp256k1>;

} // namespace hotstuff

#endif // HOTSTUFF_RETRANSMISSION_H
