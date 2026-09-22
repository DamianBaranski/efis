/// \file subject.h
/// Stores observers per channel and notifies only the matching ones.
#ifndef OBSERVER_H
#define OBSERVER_H

#include "iobserver.h"
#include <vector>
#include <utility>
#include <iostream>
#include <algorithm>

/// In-process publisher. One observer may subscribe to several channels.
/// \tparam T Channel identifier. EFIS uses DataType.
template<typename T>
class Subject: public ISubject<T>
{
public:
    /// Subscribes observer to one channel. A second attach for the same pair is ignored.
    /// \param observer Not owned.
    /// \param type Channel the observer wants.
    void attach(IObserver<T> *observer, const T& type) override;

    /// Drops every subscription for observer.
    /// \param observer Previously attached observer.
    void detach(IObserver<T> *observer) override;

protected:
    /// Calls update on every observer subscribed to type.
    /// \param type Channel that changed.
    void notify(const T& type) const override;

private:
    std::vector<std::pair<T, IObserver<T> *>> mObservers; ///< Vector to store pairs of data type and observer pointers.
};

template<typename T>
void Subject<T>::attach(IObserver<T> *observer, const T& type)
{
    auto it = std::find_if(mObservers.begin(), mObservers.end(),
                           [&](const std::pair<T, IObserver<T> *> &element)
                           {
                               return element.first  == type && element.second == observer;
                           });

    if (it == std::end(mObservers))
    {
        std::cout << "Added new observer" << std::endl;
        mObservers.push_back(std::pair(type, observer));
    }
}

template<typename T>
void Subject<T>::detach(IObserver<T> *observer)
{
    auto it = std::find_if(mObservers.begin(), mObservers.end(),
                           [&](const std::pair<T, IObserver<T> *> &element)
                           {
                               return element.second == observer;
                           });

    if (it != std::end(mObservers))
    {
        std::cout << "Removing observer" << std::endl;
        mObservers.erase(it);
    }
}

template<typename T>
void Subject<T>::notify(const T& type) const
{
    // Loop through all observers
    for (auto& observerPair : mObservers)
    {
        // Check if the observer's data type matches the specified type
        if (observerPair.first == type)
        {
            // If so, update the observer
            observerPair.second->update(type);
        }
    }
}

#endif
