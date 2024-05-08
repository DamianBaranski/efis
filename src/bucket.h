#include "btg_file.h"
#include "shader.h"

class Bucket
{
public:
    Bucket(float lat, float lon) : mLon(lon), mLat(lat)
    {
        std::string path = generateTilePath();
        mIndex = genIndex(lat, lon);
        std::string filename = "../resources/terrain/";
        filename += generateTilePath();
        filename += "/" + std::to_string(mIndex) + ".btg.gz";
        BtgFile btgFile;
        if(btgFile.load(filename)) { 
        mShader.setTriangles(btgFile.generateTriangles());
        mModelMat = glm::translate(glm::mat4(1.0), glm::vec3(-btgFile.getBoundingSphere().getCenterX(),
                                                             -btgFile.getBoundingSphere().getCenterY(),
                                                             -btgFile.getBoundingSphere().getCenterZ()));
        }
    }

    void render()
    {
        mShader.render();
    }

    bool contain(float lat, float lon) {
        long int index = genIndex(lat, lon);
        return mIndex == index;
    }

    double distanceTo(float lat, float lon) const
    {
        // Radius of the Earth in meters
        constexpr double R = 6371000.0;

        // Convert latitude and longitude from degrees to radians
        double lat1 = mLat * M_PI / 180.0;
        double lon1 = mLon * M_PI / 180.0;
        double lat2 = lat * M_PI / 180.0;
        double lon2 = lon * M_PI / 180.0;

        // Haversine formula
        double dLat = lat2 - lat1;
        double dLon = lon2 - lon1;
        double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(lat1) * cos(lat2) *
                   sin(dLon / 2) * sin(dLon / 2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        double distance = R * c;

        return distance;
    }

    void setMvpMatrix(glm::mat4 mvpMat)
    {
        mShader.setMvpMatrix(mvpMat * mModelMat);
    }

private:
    std::string generateTilePath()
    {
        int top_lon, top_lat, main_lon, main_lat;
        char hem, pole;

        top_lon = static_cast<int>(mLon / 10);
        main_lon = static_cast<int>(mLon);
        if ((mLon < 0) && (top_lon * 10 != mLon))
        {
            top_lon -= 1;
        }
        top_lon *= 10;
        if (top_lon >= 0)
        {
            hem = 'e';
        }
        else
        {
            hem = 'w';
            top_lon *= -1;
        }
        if (main_lon < 0)
        {
            main_lon *= -1;
        }

        top_lat = static_cast<int>(mLat / 10);
        main_lat = static_cast<int>(mLat);
        if ((mLat < 0) && (top_lat * 10 != mLat))
        {
            top_lat -= 1;
        }
        top_lat *= 10;
        if (top_lat >= 0)
        {
            pole = 'n';
        }
        else
        {
            pole = 's';
            top_lat *= -1;
        }
        if (main_lat < 0)
        {
            main_lat *= -1;
        }

        std::stringstream ss;
        ss << std::setw(3) << std::setfill('0') << top_lon;
        std::string top_lon_str = ss.str();
        ss.str(""); // Clear stringstream
        ss << std::setw(2) << std::setfill('0') << top_lat;
        std::string top_lat_str = ss.str();
        ss.str(""); // Clear stringstream
        ss << std::setw(3) << std::setfill('0') << main_lon;
        std::string main_lon_str = ss.str();
        ss.str(""); // Clear stringstream
        ss << std::setw(2) << std::setfill('0') << main_lat;
        std::string main_lat_str = ss.str();
        ss.str(""); // Clear stringstream

        return std::string(1, hem) + top_lon_str + pole + top_lat_str + "/" +
               std::string(1, hem) + main_lon_str + pole + main_lat_str;
    }

    long int genIndex(float lat, float lon)
    {
        // Calculate the span
        double span = getSpan(lat);
        int base_y = floor(lat);
        int y = trunc((lat-base_y) * 8);

        int base_x=floor(floor(lon / span) * span);
        int x=floor((lon - base_x) / span);

        long int index=(((long int)lon + 180) << 14) + (((long int)lat + 90) << 6) + (y << 3) + x;
        return index;
     }

    static double getSpan(double l)
    {
        if (l >= 89.0)
        {
            return 12.0;
        }
        else if (l >= 86.0)
        {
            return 4.0;
        }
        else if (l >= 83.0)
        {
            return 2.0;
        }
        else if (l >= 76.0)
        {
            return 1.0;
        }
        else if (l >= 62.0)
        {
            return 0.5;
        }
        else if (l >= 22.0)
        {
            return 0.25;
        }
        else if (l >= -22.0)
        {
            return 0.125;
        }
        else if (l >= -62.0)
        {
            return 0.25;
        }
        else if (l >= -76.0)
        {
            return 0.5;
        }
        else if (l >= -83.0)
        {
            return 1.0;
        }
        else if (l >= -86.0)
        {
            return 2.0;
        }
        else if (l >= -89.0)
        {
            return 4.0;
        }
        else
        {
            return 12.0;
        }
    }

    static constexpr float SG_EPSILON = 1e-6f;
    float mLon;
    float mLat;
    glm::mat4 mModelMat;
    Shader mShader;
    long int mIndex;
};