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

#ifndef HOTSTUFF_RTT_ESTIMATION_H
#define HOTSTUFF_RTT_ESTIMATION_H

#include "hotstuff/crypto.h"
#include "hotstuff/type.h"
#include "hotstuff/hotstuff.h"
#include "salticidae/event.h"
#include "salticidae/network.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace hotstuff {

using ReplicaID = uint16_t;
using TimerEvent = salticidae::TimerEvent;
using Net = HotStuffBase::Net;
using opcode_t = uint8_t;

class RTTEstimationBase {
    public:
    RTTEstimationBase() = default;
    virtual ~RTTEstimationBase() = default;

    virtual void addMeasurement(double measuredTime, ReplicaID rid) = 0;
    virtual double getTimeout(ReplicaID rid) = 0;
    virtual double getEstimation(ReplicaID rid) = 0;
    virtual void backoff(ReplicaID rid) = 0;

    // not used / correctly implemented
    virtual void addReplica() = 0;
    virtual ReplicaID trackedReplicas() = 0;
};

class RTTEstimationTCP: public RTTEstimationBase {
    std::vector<double> srtts;
    double alpha, beta, upper_bound, lower_bound;

    public:
    RTTEstimationTCP(uint16_t replicas, double alpha, double beta,
                     double upper_bound, double lower_bound):
        RTTEstimationBase(),
        srtts(replicas, 0.1), alpha(alpha), beta(beta), upper_bound(upper_bound),
        lower_bound(lower_bound) {}
    ~RTTEstimationTCP() override = default;

    void addMeasurement(double measuredTime, ReplicaID rid) override;
    double getTimeout(ReplicaID rid) override;
    double getEstimation(ReplicaID rid) override { return srtts[rid]; }
    void backoff(ReplicaID rid) override;

    void addReplica() override;
    ReplicaID trackedReplicas() override { return srtts.size(); };
};

class RTTEstimationVector: public RTTEstimationBase {
    std::vector<std::vector<double>> measurements;
    std::vector<int> currentIndex;
    int savedMeasurements;

    public:
    RTTEstimationVector(uint16_t replicas, int savedMeasurements):
        RTTEstimationBase(), measurements(replicas, std::vector<double>(savedMeasurements)),
        currentIndex(replicas, 0), savedMeasurements(savedMeasurements) {}
    ~RTTEstimationVector() override = default;

    void addMeasurement(double measuredTime, ReplicaID rid) override;
    double getTimeout(ReplicaID rid) override;
    double getEstimation(ReplicaID rid) override;
    void backoff(ReplicaID rid) override {};

    void addReplica() override;
    ReplicaID trackedReplicas() override { return measurements.size(); };
};

class HotStuffRetransmissionBase;
struct Timing;
class RTTEstimationWithPing: public RTTEstimationBase {
    std::unique_ptr<RTTEstimationBase> rtt_estimator;
    std::vector<Timing> timings;
    HotStuffRetransmissionBase& hs;
    Net& pn;
    EventContext& ec;
    double measurement_timeout;

    public:
    RTTEstimationWithPing(std::unique_ptr<RTTEstimationBase> rtt_estimator,
                          double measurement_timeout, HotStuffRetransmissionBase& hs,
                          Net& pn, EventContext& ec);
    ~RTTEstimationWithPing() override = default;

    void addMeasurement(double measuredTime, ReplicaID rid) override;

    void addPingMeasurement(ReplicaID rid);
    double getTimeout(ReplicaID rid) override { return rtt_estimator->getTimeout(rid); }
    double getEstimation(ReplicaID rid) override { return rtt_estimator->getEstimation(rid); }
    void backoff(ReplicaID rid) override { rtt_estimator->backoff(rid); }

    void addReplica() override { rtt_estimator->addReplica(); }
    ReplicaID trackedReplicas() override { return rtt_estimator->trackedReplicas(); };

    private:
    void send_ping(ReplicaID rid);
    void reset_timeout(ReplicaID rid);
};

} // namespace hotstuff

#endif // HOTSTUFF_RTT_ESTIMATION_H
