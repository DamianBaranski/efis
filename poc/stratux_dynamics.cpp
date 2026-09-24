/// \file stratux_dynamics.cpp
/// Stratux ground speed and vertical speed in metres. Not linked into the app.

        float ahrs_mag_heading = j["AHRSMagHeading"];
        float ahrs_pitch = (float)(j["AHRSPitch"])/180.0*M_PI;
        float ahrs_roll = (float)(j["AHRSRoll"])/180.0*M_PI;
        const auto metres = [](const nlohmann::json &node, const char *key) -> float {
            if (!node.contains(key) || !node[key].is_number())
            {
                return 0.0f;
            }
            return node[key].get<float>();
        };
        const float baro_vertical_speed = metres(j, "BaroVerticalSpeed");
        const float gps_height_above_ellipsoid = metres(j, "GPSHeightAboveEllipsoid");
        const float gps_latitude = metres(j, "GPSLatitude");
        const float gps_longitude = metres(j, "GPSLongitude");
        const float gps_vertical_speed = metres(j, "GPSVerticalSpeed");
        const float gps_ground_speed = metres(j, "GPSGroundSpeed");
        
        mLocationData.latitude = gps_latitude;
        mLocationData.longitude = gps_longitude;
        mLocationData.altitude = gps_height_above_ellipsoid;
        notify(DataType::LOCATION_DATA);

        bool notifyAttitude = false;
        if (mAttitudeData.pitch != ahrs_pitch)
        {
            mAttitudeData.pitch = ahrs_pitch;
            notifyAttitude = true;
        }
        if (mAttitudeData.roll != ahrs_roll)
        {
            mAttitudeData.roll = ahrs_roll;
            notifyAttitude = true;
        }
        const float heading = ahrs_mag_heading / 180.0f * static_cast<float>(M_PI);
        if (mAttitudeData.heading != heading)
        {
            mAttitudeData.heading = heading;
            notifyAttitude = true;
        }
        if (notifyAttitude)
        {
            notify(DataType::ATTITUDE_DATA);
        }

        (void)ahrs_gyro_heading;
        // Ground speed is knots. Both vertical speeds are feet per minute.
        const float vertical_fpm = gps_vertical_speed != 0.0f ? gps_vertical_speed : baro_vertical_speed;
        const float ground_mps = gps_ground_speed * 0.514444f;
        const float vertical_mps = vertical_fpm * 0.00508f;
        if (mDynamicsData.airspeed != ground_mps || mDynamicsData.vertical_speed != vertical_mps)
        {
            mDynamicsData.airspeed = ground_mps;
            mDynamicsData.vertical_speed = vertical_mps;
            mDynamicsData.slip_rad = 0.0f;
            notify(DataType::DYNAMICS_DATA);
        }
    }
    catch (const std::exception &e)
    {
