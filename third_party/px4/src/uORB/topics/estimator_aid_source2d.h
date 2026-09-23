/// Minimal stand-in for the PX4 uORB topic the EKF writes its 2-axis aid status into.
#ifndef EFIS_ESTIMATOR_AID_SOURCE2D_H
#define EFIS_ESTIMATOR_AID_SOURCE2D_H

#include <cstdint>

struct estimator_aid_source2d_s
{
    uint64_t timestamp{};
    uint64_t timestamp_sample{};
    uint8_t estimator_instance{};
    uint32_t device_id{};
    uint64_t time_last_fuse{};
    double observation[2]{};
    float observation_variance[2]{};
    float innovation[2]{};
    float innovation_filtered[2]{};
    float innovation_variance[2]{};
    float test_ratio[2]{};
    float test_ratio_filtered[2]{};
    bool innovation_rejected{};
    bool fused{};
};

#endif
