/// \file data_manager_stratux.h
/// Live situation from a Stratux receiver on this machine.

#ifndef DATA_MANAGER_STRATUX_H
#define DATA_MANAGER_STRATUX_H

#include "idata_manager.h"
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>

/// Polls http://127.0.0.1:5000/getSituation and publishes each sample.
/// Desktop only. Android always uses the simulator.
class DataManagerStratux : public IDataManager
{
public:
    /// Does not open the network. Call start() to begin polling.
    DataManagerStratux();

    /// Stops the poll thread before members are destroyed.
    ~DataManagerStratux();

    /// Pitch, roll, and heading in radians.
    const AttitudeData &getAttitudeData() const override;

    /// Airspeed and vertical speed in metres per second.
    const DynamicsData &getDynamicsData() const override;

    /// Engine indications. Stratux does not fill these.
    const EngineData &getEngineData() const override;

    /// WGS-84 position from GPS. Altitude is metres above the ellipsoid.
    const LocationData &getLocationData() const override;

    /// Starts the background HTTP poll.
    void start() override;

private:
    /// \brief Callback function for writing HTTP response data.
    /// \param contents Pointer to the received data.
    /// \param size Size of each data element.
    /// \param nmemb Number of data elements.
    /// \param buffer Pointer to the receiving buffer.
    /// \return The number of bytes written.
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *buffer);

    /// \brief Updates the data manager with new data.
    void update_data();

    /// \brief Function executed by the thread to continuously update data.
    void threadFunction();

    CURL *mCurl;                ///< Pointer to the libcurl handle.
    AttitudeData mAttitudeData; ///< Attitude data.
    DynamicsData mDynamicsData; ///< Dynamics data.
    EngineData mEngineData;     ///< Engine data.
    LocationData mLocationData; ///< Location data.
    std::thread mThread;        ///< Thread for data retrieval.
};

#endif // DATA_MANAGER_STRATUX_H
