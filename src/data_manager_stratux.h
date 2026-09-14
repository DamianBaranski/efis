/// \file data_manager_stratux.h
/// \brief Contains the declaration of the DataManagerStratux class.

#ifndef DATA_MANAGER_STRATUX_H
#define DATA_MANAGER_STRATUX_H

#include "idata_manager.h"
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>

/// \class DataManagerStratux
/// \brief Manages data retrieval and updates from a Stratux device.
class DataManagerStratux : public IDataManager
{
public:
    /// \brief Constructs a DataManagerStratux object.
    DataManagerStratux();

    /// \brief Destructor for the DataManagerStratux class.
    ~DataManagerStratux();

    /// \brief Retrieves the attitude data.
    /// \return The attitude data.
    const AttitudeData &getAttitudeData() const override;

    /// \brief Retrieves the dynamics data.
    /// \return The dynamics data.
    const DynamicsData &getDynamicsData() const override;

    /// \brief Retrieves the engine data.
    /// \return The engine data.
    const EngineData &getEngineData() const override;

    /// \brief Retrieves the location data.
    /// \return The location data.
    const LocationData &getLocationData() const override;

    /// \brief Starts the data retrieval thread.
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
