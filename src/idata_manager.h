/// \file idata_manager.h
/// \brief Contains the declaration of the IDataManager interface.
#ifndef IDATA_MANAGER_H
#define IDATA_MANAGER_H

#include "data_type.h"
#include "subject.h"

/// \class IDataManager
/// \brief The interface for managing various types of data.
///
/// This class defines the common interface for managing different types of data.
class IDataManager : public Subject<DataType> {
public:
    virtual ~IDataManager() = default;
    /// \brief Gets the attitude data.
    ///
    /// \return A constant reference to the attitude data.
    virtual const AttitudeData &getAttitudeData() const = 0;

    /// \brief Gets the dynamics data.
    ///
    /// \return A constant reference to the dynamics data.
    virtual const DynamicsData &getDynamicsData() const = 0;

    /// \brief Gets the engine data.
    ///
    /// \return A constant reference to the engine data.
    virtual const EngineData &getEngineData() const = 0;

    /// \brief Gets the location data.
    ///
    /// \return A constant reference to the location data.
    virtual const LocationData &getLocationData() const = 0;

    /// \brief Starts background updates, if the implementation has any.
    virtual void start() {}
};

#endif // IDATA_MANAGER_H
