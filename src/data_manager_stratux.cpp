#include "data_manager_stratux.h"
#include <chrono>
#include <thread>

DataManagerStratux::DataManagerStratux()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

size_t DataManagerStratux::WriteCallback(void *contents, size_t size, size_t nmemb, std::string *buffer)
{
    buffer->append((char *)contents, size * nmemb);
    return size * nmemb;
}

const AttitudeData &DataManagerStratux::getAttitudeData() const
{
    return mAttitudeData;
}

const DynamicsData &DataManagerStratux::getDynamicsData() const
{
    return mDynamicsData;
}

const EngineData &DataManagerStratux::getEngineData() const
{
    return mEngineData;
}

const LocationData &DataManagerStratux::getLocationData() const
{
    return mLocationData;
}

void DataManagerStratux::update_data()
{
    mCurl = curl_easy_init();
    if (!mCurl)
    {
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return;
    }
    // Set URL to fetch data from
    curl_easy_setopt(mCurl, CURLOPT_URL, "http://127.0.0.1:5000/getSituation");
    curl_easy_setopt(mCurl, CURLOPT_WRITEFUNCTION, DataManagerStratux::WriteCallback);
    // Set up variables to hold response data
    std::string response_data;
    curl_easy_setopt(mCurl, CURLOPT_WRITEDATA, &response_data);

    // Perform HTTP request
    CURLcode res = curl_easy_perform(mCurl);
    if (res != CURLE_OK)
    {
        std::cerr << "Failed to perform HTTP request: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(mCurl);
        return;
    }

    // Clean up libcurl
    curl_easy_cleanup(mCurl);

    // Parse JSON data
    try
    {
        nlohmann::json_abi_v3_11_2::json j = nlohmann::json_abi_v3_11_2::json::parse(response_data);

        // Access individual fields
        float ahrs_gyro_heading = j["AHRSGyroHeading"];
        float ahrs_mag_heading = j["AHRSMagHeading"];
        float ahrs_pitch = j["AHRSPitch"];
        float ahrs_roll = j["AHRSRoll"];
        float baro_vertical_speed = j["BaroVerticalSpeed"];
        float gps_height_above_ellipsoid = j["GPSHeightAboveEllipsoid"];
        float gps_latitude = j["GPSLatitude"];
        float gps_longitude = j["GPSLongitude"];
        float gps_vertical_speed = j["GPSVerticalSpeed"];

        mLocationData.latitude = gps_latitude;
        mLocationData.longitude = gps_longitude;
        mLocationData.altitude = gps_height_above_ellipsoid;
        notify(DataType::LOCATION_DATA);

        mAttitudeData.pitch = ahrs_pitch;
        mAttitudeData.roll = ahrs_roll;
        notify(DataType::ATTITUDE_DATA);

        //ToDo add data to members
        //ToDo notify when data was change
        (void) ahrs_gyro_heading;
        (void) ahrs_mag_heading;
        (void) baro_vertical_speed;
        (void) gps_vertical_speed;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        return;
    }
}

void DataManagerStratux::start() {
    mThread = std::thread([this](){threadFunction();});
}

void DataManagerStratux::threadFunction()
{
	while (true)
	{
		update_data();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
