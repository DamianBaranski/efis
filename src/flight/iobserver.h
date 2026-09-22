/// \file iobserver.h
/// Observer contract for one situation channel.
#ifndef IOBSERVER_H
#define IOBSERVER_H

/// Receives one channel when the situation source publishes it.
/// \tparam T Channel identifier. EFIS uses DataType.
template <typename T>
class IObserver {
public:
    virtual ~IObserver() {}

    /// Situation channel changed.
    /// \param type Which block to read.
    virtual void update(T type) = 0;
};

/// Publishes channel changes to the observers that asked for them.
/// \tparam T Channel identifier. EFIS uses DataType.
template <typename T>
class ISubject {
public:
    virtual ~ISubject() {}

    /// Subscribes observer to one channel.
    /// \param observer Not owned.
    /// \param type Channel the observer wants.
    virtual void attach(IObserver<T>* observer, const T& type) = 0;

    /// Drops every subscription for observer.
    /// \param observer Previously attached observer.
    virtual void detach(IObserver<T>* observer) = 0;

protected:
    /// Calls update on every observer subscribed to type.
    /// \param type Channel that changed.
    virtual void notify(const T& type) const = 0;
};

#endif
