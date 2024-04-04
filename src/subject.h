#ifndef OBSERVER_H
#define OBSERVER_H

#include "iobserver.h"
#include <vector>
#include <utility>
#include <iostream>
#include <algorithm>

/**
 * @brief A concrete implementation of the Subject interface.
 * 
 * @tparam T The type of data being observed.
 */
template<typename T>
class Subject: public ISubject<T>
{
public:
    /// Attaches an observer to the subject.
    ///
    /// @param observer Pointer to the observer to attach.
    /// @param type The type of data the observer is interested in.
    void attach(IObserver<T> *observer, const T& type) override;

    /// Detaches an observer from the subject.
    ///
    /// @param observer Pointer to the observer to detach.
    void detach(IObserver<T> *observer) override;

protected:
    /// Notifies all attached observers of changes in data.
    ///
    /// @param type The type of data that has changed.
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
