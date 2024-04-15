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

#include "hotstuff/rtt_estimation.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/retransmission.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"
#include "salticidae/util.h"
#include <numeric>

#include <algorithm>
#include <iostream>
#include <memory>

namespace hotstuff {

void RTTEstimationTCP::addMeasurement(double measuredTime, ReplicaID rid) {
    srtts[rid] = (alpha * srtts[rid]) + ((1 - alpha) * measuredTime);
}

double RTTEstimationTCP::getTimeout(ReplicaID rid) {
    return std::min(upper_bound, std::max(lower_bound, beta * srtts[rid]));
}

void RTTEstimationTCP::backoff(ReplicaID rid) {
    srtts[rid] += lower_bound / 2.0;
}

void RTTEstimationTCP::addReplica() {
    srtts.resize(srtts.size() + 1);
}

//------------------------------------------------------

void RTTEstimationVector::addMeasurement(double measuredTime, ReplicaID rid) {
    measurements[rid][currentIndex[rid]++] = measuredTime;
    currentIndex[rid] %= savedMeasurements;
}

double RTTEstimationVector::getTimeout(ReplicaID rid) {
    auto begin = measurements[rid].begin();
    auto end = measurements[rid].end();
    double avg = std::accumulate(begin, end, 0.0) / savedMeasurements;
    // TODO ignore extrem cases?
    double min = *std::min_element(begin, end);
    double max = *std::max_element(begin, end);

    double delta = std::min(max - avg, avg - min);
    return max + delta;
}

double RTTEstimationVector::getEstimation(ReplicaID rid) {
    auto begin = measurements[rid].begin();
    auto end = measurements[rid].end();
    return std::accumulate(begin, end, 0.0) / savedMeasurements;
}

void RTTEstimationVector::addReplica() {
    measurements.resize(measurements.size() + 1, std::vector<double>(savedMeasurements));
}

//------------------------------------------------------

RTTEstimationWithPing::RTTEstimationWithPing(std::unique_ptr<RTTEstimationBase> rtt_estimator,
                                             double measurement_timeout, HotStuffRetransmissionBase& hs, Net& pn, EventContext& ec):
    RTTEstimationBase(),
    rtt_estimator(std::move(rtt_estimator)),
    hs(hs),
    pn(pn),
    ec(ec),
    measurement_timeout(measurement_timeout) {
    timings.reserve(this->rtt_estimator->trackedReplicas());
    for (int i = 0; i < this->rtt_estimator->trackedReplicas(); i++) {
        if (i == hs.get_id()) {
            // empty self timeout
            timings.emplace_back();
        } else {
            timings.emplace_back(hs.get_event_context(), [this, i](TimerEvent&) {
                send_ping(i);
                reset_timeout(i);
            });
            // set an initial delay of 2.5 seconds (see 2 sec delay of UDP/DTLS "connection" establishment
            timings[i].timeout.add(2.5);
        }
    }
}

void RTTEstimationWithPing::addMeasurement(double measuredTime, ReplicaID rid) {
    rtt_estimator->addMeasurement(measuredTime, rid);
    reset_timeout(rid);
}

void RTTEstimationWithPing::addPingMeasurement(ReplicaID rid) {
    ElapsedTime& elapsed = timings[rid].elapsed;
    elapsed.stop();
    HOTSTUFF_LOG_INFO("Ping time: %f", elapsed.elapsed_sec);
    double measuredTime = elapsed.elapsed_sec;
    addMeasurement(measuredTime, rid);
}

void RTTEstimationWithPing::send_ping(ReplicaID rid) {
    timings[rid].elapsed.start();
    pn.send_msg(MsgPingRTT(rid), hs.get_config().get_peer_id(rid));
}

void RTTEstimationWithPing::reset_timeout(ReplicaID rid) {
    timings[rid].timeout.add(salticidae::gen_rand_timeout(measurement_timeout));
}

} // namespace hotstuff
