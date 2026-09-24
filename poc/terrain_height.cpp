/// \file terrain_height.cpp
/// Elevation grid built from a loaded terrain mesh. Not linked into the app.

bool BucketContainer::groundMetres(double latitude, double longitude, float &ellipsoidM) const
{
    const float lat = static_cast<float>(latitude);
    const float lon = static_cast<float>(longitude);
    for (const auto &tile : mMap)
    {
        if (tile->contain(lat, lon))
        {
            return tile->groundMetres(latitude, longitude, ellipsoidM);
        }
    }
    return false;
}

bool Bucket::groundMetres(double latitude, double longitude, float &ellipsoidM) const
{
    if (mState.load(std::memory_order_acquire) < 2 || mGridN < 2 || mGrid.empty())
    {
        return false;
    }
    const double fx = (longitude - mGridLon0) / mGridDLon - 0.5;
    const double fy = (latitude - mGridLat0) / mGridDLat - 0.5;
    const int i0 = static_cast<int>(std::floor(fx));
    const int j0 = static_cast<int>(std::floor(fy));
    const float tx = static_cast<float>(fx - i0);
    const float ty = static_cast<float>(fy - j0);
    auto at = [&](int i, int j, float &height) {
        if (i < 0 || j < 0 || i >= mGridN || j >= mGridN)
        {
            return false;
        }
        height = mGrid[static_cast<size_t>(j * mGridN + i)];
        return height > -1.0e20f;
    };
    float h00 = 0.0f;
    float h10 = 0.0f;
    float h01 = 0.0f;
    float h11 = 0.0f;
    if (at(i0, j0, h00) && at(i0 + 1, j0, h10) && at(i0, j0 + 1, h01) && at(i0 + 1, j0 + 1, h11))
    {
        const float h0 = h00 + (h10 - h00) * tx;
        const float h1 = h01 + (h11 - h01) * tx;
        ellipsoidM = h0 + (h1 - h0) * ty;
        return true;
    }
    const int i = std::clamp(static_cast<int>(std::lround(fx)), 0, mGridN - 1);
    const int j = std::clamp(static_cast<int>(std::lround(fy)), 0, mGridN - 1);
    return at(i, j, ellipsoidM);
}

void Bucket::buildHeightGrid()
{
    double lat0 = 0.0;
    double lat1 = 0.0;
    double lon0 = 0.0;
    double lon1 = 0.0;
    tileBounds(lat0, lat1, lon0, lon1);
    if (lat1 <= lat0 || lon1 <= lon0)
    {
        return;
    }
    constexpr int kN = 64;
    mGridN = kN;
    mGridLat0 = lat0;
    mGridLon0 = lon0;
    mGridDLat = (lat1 - lat0) / kN;
    mGridDLon = (lon1 - lon0) / kN;
    mGrid.assign(static_cast<size_t>(kN * kN), -1.0e30f);

    auto heightOf = [&](const VertexTexture &vertex, double &lat, double &lon, float &alt) {
        const double x = mCenter.x + vertex.vertex.x;
        const double y = mCenter.y + vertex.vertex.y;
        const double z = mCenter.z + vertex.vertex.z;
        const auto ll = GeoCoordUtils::convertXYZToLatLon(x, y, z);
        lat = ll.latitude;
        lon = ll.longitude;
        const auto ell = GeoCoordUtils::convertLatLonToXYZ(lat, lon, 0.0);
        const double radius = std::sqrt(x * x + y * y + z * z);
        const double sea = std::sqrt(ell.x * ell.x + ell.y * ell.y + ell.z * ell.z);
        alt = static_cast<float>(radius - sea);
    };

    for (const Triangles &group : mMesh)
    {
        if (group.vertex.empty() || group.indices.size() < 3)
        {
            continue;
        }
        std::vector<double> lats(group.vertex.size());
        std::vector<double> lons(group.vertex.size());
        std::vector<float> alts(group.vertex.size());
        for (size_t i = 0; i < group.vertex.size(); ++i)
        {
            heightOf(group.vertex[i], lats[i], lons[i], alts[i]);
        }
        for (size_t t = 0; t + 2 < group.indices.size(); t += 3)
        {
            const unsigned int ia = group.indices[t];
            const unsigned int ib = group.indices[t + 1];
            const unsigned int ic = group.indices[t + 2];
            if (ia >= group.vertex.size() || ib >= group.vertex.size() || ic >= group.vertex.size())
            {
                continue;
            }
            const double latA = lats[ia];
            const double lonA = lons[ia];
            const double latB = lats[ib];
            const double lonB = lons[ib];
            const double latC = lats[ic];
            const double lonC = lons[ic];
            const float altA = alts[ia];
            const float altB = alts[ib];
            const float altC = alts[ic];
            const double minLat = std::min(latA, std::min(latB, latC));
            const double maxLat = std::max(latA, std::max(latB, latC));
            const double minLon = std::min(lonA, std::min(lonB, lonC));
            const double maxLon = std::max(lonA, std::max(lonB, lonC));
            int j0 = static_cast<int>(std::floor((minLat - lat0) / mGridDLat)) - 1;
            int j1 = static_cast<int>(std::floor((maxLat - lat0) / mGridDLat)) + 1;
            int i0 = static_cast<int>(std::floor((minLon - lon0) / mGridDLon)) - 1;
            int i1 = static_cast<int>(std::floor((maxLon - lon0) / mGridDLon)) + 1;
            j0 = std::max(0, j0);
            i0 = std::max(0, i0);
            j1 = std::min(kN - 1, j1);
            i1 = std::min(kN - 1, i1);
            const double den = (latB - latC) * (lonA - lonC) + (lonC - lonB) * (latA - latC);
            if (std::fabs(den) < 1.0e-14)
            {
                continue;
            }
            for (int j = j0; j <= j1; ++j)
            {
                const double lat = lat0 + (static_cast<double>(j) + 0.5) * mGridDLat;
                for (int i = i0; i <= i1; ++i)
                {
                    const double lon = lon0 + (static_cast<double>(i) + 0.5) * mGridDLon;
                    const float w0 = static_cast<float>(((latB - latC) * (lon - lonC) + (lonC - lonB) * (lat - latC)) / den);
                    const float w1 = static_cast<float>(((latC - latA) * (lon - lonC) + (lonA - lonC) * (lat - latC)) / den);
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f)
                    {
                        continue;
                    }
                    const float height = w0 * altA + w1 * altB + w2 * altC;
                    float &cell = mGrid[static_cast<size_t>(j * kN + i)];
                    if (height > cell)
                    {
                        cell = height;
                    }
                }
            }
        }
    }
}
